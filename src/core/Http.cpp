#include "Http.h"

#include "Cookies.h"

#include <QEventLoop>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace sc {

namespace {

thread_local CancelFlag t_cancel;
std::atomic_bool g_shutdown = false;

std::mutex g_uaMutex;
QByteArray g_userAgent = Http::BrowserUA;

std::atomic_bool g_hasTestHandler = false;
std::mutex g_testMutex;
Http::TestHandler g_testHandler;

// Bounds concurrent requests per host and overall, separately for pages/API
// calls and for images, so a long chapter cannot starve search requests.
class Limiter {
public:
    Limiter(int perHost, int total) : m_perHost(perHost), m_total(total) {}

    bool acquire(const QString &host) {
        std::unique_lock lock(m_mutex);
        while (true) {
            if (CancelScope::cancelled())
                return false;
            if (m_active < m_total && m_hosts.value(host) < m_perHost) {
                ++m_active;
                m_hosts[host] += 1;
                return true;
            }
            m_cv.wait_for(lock, std::chrono::milliseconds(50));
        }
    }

    void release(const QString &host) {
        {
            std::lock_guard lock(m_mutex);
            --m_active;
            if (--m_hosts[host] <= 0)
                m_hosts.remove(host);
        }
        m_cv.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    QHash<QString, int> m_hosts;
    int m_active = 0;
    int m_perHost;
    int m_total;
};

Limiter &limiter(Lane lane) {
    static Limiter pages(4, 12);
    static Limiter images(6, 18);
    return lane == Lane::Images ? images : pages;
}

QNetworkAccessManager &manager() {
    thread_local std::unique_ptr<QNetworkAccessManager> nam;
    if (!nam) {
        nam = std::make_unique<QNetworkAccessManager>();
        // The jar lives on the main thread (created at startup), so managers
        // on worker threads use it without taking ownership.
        nam->setCookieJar(&CookieJar::shared());
        nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
        nam->setAutoDeleteReplies(false);
    }
    return *nam;
}

struct Slot {
    Lane lane;
    QString host;
    bool held = false;
    ~Slot() {
        if (held)
            limiter(lane).release(host);
    }
};

} // namespace

CancelScope::CancelScope(CancelFlag flag) : m_previous(t_cancel) { t_cancel = std::move(flag); }
CancelScope::~CancelScope() { t_cancel = m_previous; }
bool CancelScope::cancelled() { return g_shutdown.load() || (t_cancel && t_cancel->load()); }
void CancelScope::cancelAll() { g_shutdown.store(true); }
CancelFlag CancelScope::current() { return t_cancel; }

QJsonDocument Response::json() const {
    QJsonParseError error{};
    auto doc = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError) {
        if (isHtml() || body.trimmed().startsWith('<'))
            throw Error(isChallenge() ? QStringLiteral("The site asked for a browser check.")
                                      : QStringLiteral("The site returned a web page instead of data."),
                        status,
                        isChallenge() ? QStringLiteral("%1://%2/").arg(finalUrl.scheme(), finalUrl.host()) : QString());
        throw Error(QStringLiteral("The site returned data SCANCORE could not read."), status);
    }
    return doc;
}

QByteArray Response::header(const QByteArray &name) const {
    for (const auto &[key, value] : headers)
        if (key.compare(name, Qt::CaseInsensitive) == 0)
            return value;
    return {};
}

bool Response::isChallenge() const {
    if (!(status == 403 || status == 503 || status == 429 || isHtml()))
        return false;
    static const QRegularExpression title(
        QStringLiteral(R"(<title[^>]*>\s*(Just a moment|Attention Required|Access denied|DDoS-Guard))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto head = QString::fromUtf8(body.left(8192));
    if (title.match(head).hasMatch())
        return true;
    // Cloudflare adds its challenge-platform script to ordinary pages too, so
    // it only marks a check on a refused request.
    const bool refused = status == 403 || status == 503 || status == 429;
    return refused && (head.contains(QLatin1String("cf-mitigated")) || head.contains(QLatin1String("challenge-platform")));
}

QByteArray Http::chromeUserAgent(int major) {
#if defined(Q_OS_WIN)
    const char *platform = "Windows NT 10.0; Win64; x64";
#elif defined(Q_OS_MACOS)
    const char *platform = "Macintosh; Intel Mac OS X 10_15_7";
#else
    // What Chrome sends on every Linux, whatever the processor.
    const char *platform = "X11; Linux x86_64";
#endif
    return QByteArray("Mozilla/5.0 (") + platform + ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + QByteArray::number(major) +
           ".0.0.0 Safari/537.36";
}

void Http::setUserAgent(const QByteArray &ua) {
    std::lock_guard lock(g_uaMutex);
    g_userAgent = ua;
}

QByteArray Http::userAgent() {
    std::lock_guard lock(g_uaMutex);
    return g_userAgent;
}

void Http::setTestHandler(TestHandler handler) {
    std::lock_guard lock(g_testMutex);
    g_testHandler = std::move(handler);
    g_hasTestHandler = bool(g_testHandler);
}

namespace {

// A network failure worth one more try: the connection dropped or the
// network blinked, as opposed to a bad address or a refusal.
struct TransientError : Error {
    using Error::Error;
};

Response sendOnce(const Request &request) {
    if (request.url.scheme() != QLatin1String("https") && request.url.scheme() != QLatin1String("http"))
        throw Error(QStringLiteral("Unsupported address: %1").arg(request.url.toString()));
    if (CancelScope::cancelled())
        throw Error(QStringLiteral("Cancelled"), 499);
    if (g_hasTestHandler) {
        Http::TestHandler handler;
        {
            std::lock_guard lock(g_testMutex);
            handler = g_testHandler;
        }
        if (handler)
            if (auto answer = handler(request)) {
                if (answer->finalUrl.isEmpty())
                    answer->finalUrl = request.url;
                return *answer;
            }
    }

    Slot slot{request.lane, request.url.host().toLower()};
    slot.held = limiter(request.lane).acquire(slot.host);
    if (!slot.held)
        throw Error(QStringLiteral("Cancelled"), 499);

    QNetworkRequest req(request.url);
    req.setHeader(QNetworkRequest::UserAgentHeader, Http::userAgent());
    // Covers come from extensions and plugins too: one pointing at SCANCORE's
    // own site gets no session.
    if (request.lane == Lane::Images && CookieJar::isProtected(request.url)) {
        req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    }
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    // A guarded request's redirects come back here, each to be asked first.
    if (request.guard)
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(request.timeoutMs);
    for (const auto &[name, value] : request.headers) {
        if (name.compare("User-Agent", Qt::CaseInsensitive) == 0)
            req.setHeader(QNetworkRequest::UserAgentHeader, value);
        else
            req.setRawHeader(name, value);
    }

    auto &nam = manager();
    QNetworkReply *reply = nullptr;
    if (request.method == "GET")
        reply = nam.get(req);
    else if (request.method == "POST")
        reply = nam.post(req, request.body);
    else
        reply = nam.sendCustomRequest(req, request.method, request.body);
    std::unique_ptr<QNetworkReply> owner(reply);

    QEventLoop loop;
    bool tooLarge = false;
    bool cancelled = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 received, qint64) {
        if (received > request.maxBytes) {
            tooLarge = true;
            reply->abort();
        }
    });
    QTimer poll;
    poll.setInterval(40);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (CancelScope::cancelled()) {
            cancelled = true;
            reply->abort();
        }
    });
    poll.start();
    QTimer deadline;
    deadline.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
    });
    deadline.start(request.timeoutMs + 5000);
    if (!reply->isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);

    if (cancelled)
        throw Error(QStringLiteral("Cancelled"), 499);
    if (tooLarge)
        throw Error(QStringLiteral("The response from %1 was too large.").arg(slot.host), 502);

    Response response;
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower();
    response.finalUrl = reply->url();
    response.headers = reply->rawHeaderPairs();
    response.body = reply->readAll();

    if (response.status == 0) {
        const auto error = reply->error();
        if (timedOut || error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError)
            throw Error(QStringLiteral("%1 took too long to respond.").arg(slot.host), 504);
        if (error == QNetworkReply::HostNotFoundError)
            throw Error(QStringLiteral("Could not find %1. Check your connection.").arg(slot.host), 502);
        if (error == QNetworkReply::SslHandshakeFailedError)
            throw Error(QStringLiteral("Secure connection to %1 failed.").arg(slot.host), 502);
        const auto message = QStringLiteral("Could not reach %1 (%2).").arg(slot.host, reply->errorString());
        if (error == QNetworkReply::RemoteHostClosedError || error == QNetworkReply::TemporaryNetworkFailureError ||
            error == QNetworkReply::NetworkSessionFailedError || error == QNetworkReply::UnknownNetworkError ||
            error == QNetworkReply::ProxyConnectionClosedError)
            throw TransientError(message, 502);
        throw Error(message, 502);
    }
    return response;
}

bool waitBeforeRetry() {
    for (int i = 0; i < 8; ++i) {
        if (CancelScope::cancelled())
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !CancelScope::cancelled();
}

// GETs are retried once after a dropped connection or a passing gateway
// error, which otherwise surface as a failed page that works on Try again.
Response sendRetrying(const Request &request) {
    const bool idempotent = request.method == "GET";
    try {
        auto response = sendOnce(request);
        const bool gateway = response.status == 502 || response.status == 503 || response.status == 504;
        if (!idempotent || !gateway || response.isChallenge() || !waitBeforeRetry())
            return response;
    } catch (const TransientError &) {
        if (!idempotent || !waitBeforeRetry())
            throw;
    }
    return sendOnce(request);
}

QHostAddress subnet(const char *text, int &length) {
    const auto [address, bits] = QHostAddress::parseSubnet(QString::fromLatin1(text));
    length = bits;
    return address;
}

} // namespace

bool Http::isLocalAddress(const QHostAddress &given) {
    QHostAddress address = given;
    bool v4 = false;
    // IPv4 written inside IPv6 (::ffff:127.0.0.1) is that IPv4 address.
    if (const quint32 ipv4 = address.toIPv4Address(&v4); v4)
        address = QHostAddress(ipv4);
    if (address.isNull() || address.isLoopback() || address.isLinkLocal() || address.isMulticast() ||
        address.isBroadcast() || address.isUniqueLocalUnicast() || address.isSiteLocal())
        return true;
    static const char *const kNetworks[] = {
        "0.0.0.0/8",     "10.0.0.0/8",     "100.64.0.0/10", "127.0.0.0/8", "169.254.0.0/16", "172.16.0.0/12",
        "192.0.0.0/24",  "192.168.0.0/16", "198.18.0.0/15", "224.0.0.0/4", "240.0.0.0/4",
        "::/128",        "::1/128",        "fc00::/7",      "fe80::/10",
        // NAT64, 6to4 and Teredo carry IPv4 addresses (any of the above).
        "64:ff9b::/96",  "2002::/16",      "2001::/32",
    };
    for (const auto *network : kNetworks) {
        int length = 0;
        const auto base = subnet(network, length);
        if (address.protocol() == base.protocol() && address.isInSubnet(base, length))
            return true;
    }
    return false;
}

namespace {

// Redirects of a guarded request, followed by hand so the guard sees each
// address; as Qt's own policy, never from https down to http.
Response sendGuarded(const Request &request) {
    if (const auto refused = request.guard(request.url); !refused.isEmpty())
        throw Error(refused, 403);
    Request hop = request;
    for (int redirects = 0;; ++redirects) {
        auto response = sendRetrying(hop);
        const int status = response.status;
        const auto location = response.header("Location");
        if ((status != 301 && status != 302 && status != 303 && status != 307 && status != 308) || location.isEmpty())
            return response;
        if (redirects == 10)
            throw Error(QStringLiteral("%1 redirected too many times.").arg(request.url.host()), 502);
        const auto next = hop.url.resolved(QUrl::fromEncoded(location));
        if (hop.url.scheme() == QLatin1String("https") && next.scheme() != QLatin1String("https"))
            throw Error(QStringLiteral("%1 redirected to an insecure address.").arg(hop.url.host()), 502);
        if (const auto refused = request.guard(next); !refused.isEmpty())
            throw Error(refused, 403);
        // As browsers: 303 (and 301/302 after a POST) go on as a GET.
        if (status == 303 || ((status == 301 || status == 302) && hop.method == "POST")) {
            hop.method = "GET";
            hop.body.clear();
            hop.headers.removeIf([](const auto &header) { return header.first.compare("Content-Type", Qt::CaseInsensitive) == 0; });
        }
        hop.url = next;
    }
}

} // namespace

Response Http::send(const Request &request) {
    return request.guard ? sendGuarded(request) : sendRetrying(request);
}

Response Http::get(const QUrl &url, const Headers &headers, int timeoutMs) {
    Request request;
    request.url = url;
    request.headers = headers;
    request.timeoutMs = timeoutMs;
    return send(request);
}

Response Http::postForm(const QUrl &url, const QList<QPair<QString, QString>> &form, const Headers &headers) {
    Request request;
    request.url = url;
    request.method = "POST";
    request.headers = headers;
    request.headers.append(QPair<QByteArray, QByteArray>("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8"));
    QByteArrayList pairs;
    for (const auto &[key, value] : form)
        pairs.append(QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value));
    request.body = pairs.join('&');
    return send(request);
}

Response Http::postJson(const QUrl &url, const QByteArray &json, const Headers &headers) {
    Request request;
    request.url = url;
    request.method = "POST";
    request.headers = headers;
    request.headers.append(QPair<QByteArray, QByteArray>("Content-Type", "application/json"));
    request.body = json;
    return send(request);
}

Response Http::getOk(const QUrl &url, const Headers &headers, int timeoutMs) {
    auto response = get(url, headers, timeoutMs);
    if (response.isChallenge())
        throw Error(QStringLiteral("%1 asked for a browser check. Open it once in SCANCORE to pass it.").arg(url.host()),
                    403, QStringLiteral("%1://%2/").arg(url.scheme(), url.host()));
    if (!response.ok())
        throw Error(QStringLiteral("%1 responded with HTTP %2.").arg(url.host()).arg(response.status),
                    response.status);
    return response;
}

QJsonDocument Http::getJson(const QUrl &url, const Headers &headers, int timeoutMs) {
    Headers all = headers;
    bool hasAccept = false;
    for (const auto &h : headers)
        hasAccept |= h.first.compare("Accept", Qt::CaseInsensitive) == 0;
    if (!hasAccept)
        all.append(QPair<QByteArray, QByteArray>("Accept", "application/json, text/plain, */*"));
    const auto response = getOk(url, all, timeoutMs);
    // A body cut off in transit is not JSON but is not a web page either;
    // fetching it again usually works.
    QJsonParseError error{};
    auto doc = QJsonDocument::fromJson(response.body, &error);
    if (error.error == QJsonParseError::NoError)
        return doc;
    if (response.isHtml() || response.body.trimmed().startsWith('<') || !waitBeforeRetry())
        return response.json();
    return getOk(url, all, timeoutMs).json();
}

QUrl withQuery(QUrl url, std::initializer_list<QPair<QString, QString>> params) {
    QUrlQuery query(url);
    for (const auto &[key, value] : params)
        query.addQueryItem(key, QString::fromUtf8(QUrl::toPercentEncoding(value)));
    url.setQuery(query);
    return url;
}

bool hostMatches(const QString &host, const QString &allowed) {
    const auto h = host.toLower();
    return h == allowed || h.endsWith(QLatin1Char('.') + allowed);
}

} // namespace sc
