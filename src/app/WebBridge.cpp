#include "WebBridge.h"

#ifdef SCANCORE_HAVE_WEBENGINE
#include "core/Cookies.h"
#include "core/Http.h"

#include <QCoreApplication>
#include <QNetworkCookie>
#include <QPointer>
#include <QTimer>
#include <QWebEngineCookieStore>
#include <QWebEngineHttpRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineUrlRequestInterceptor>
#include <QtWebEngineQuick/QQuickWebEngineProfile>

#include <algorithm>
#include <atomic>
#include <deque>
#endif

namespace sc {

namespace {
std::function<void(const QString &)> g_clearanceSeen;

// Ad and pop-under networks scan sites use (the same list as Android's
// BrowserCheck.java): never loaded while a site's check is open.
bool isAdHost(QString host) {
    static const QStringList ads{
        "doubleclick.net", "googlesyndication.com", "googleadservices.com", "adservice.google.com",
        "popads.net", "popcash.net", "propellerads.com", "propellerclick.com", "adsterra.com", "adsterratech.com",
        "highperformanceformat.com", "profitablecpmrate.com", "profitablegatecpm.com", "effectiveratecpm.com",
        "exoclick.com", "exosrv.com", "juicyads.com", "trafficjunky.net", "hilltopads.net", "clickadu.com",
        "adcash.com", "monetag.com", "onclickads.net", "onclickalgo.com", "a-ads.com", "adskeeper.com",
        "mgid.com", "revcontent.com", "taboola.com", "outbrain.com", "pubfuture.com", "galaksion.com",
        "admaven.com", "ad-maven.com", "adtelligent.com", "bidgear.com", "richads.com", "evadav.com",
        "pushwhy.com", "rollerads.com", "dtscout.com", "dtscdn.com"};
    host = host.toLower();
    return std::any_of(ads.begin(), ads.end(), [&](const QString &ad) { return host == ad || host.endsWith(QLatin1Char('.') + ad); });
}
} // namespace

void setClearanceSeen(std::function<void(const QString &domain)> seen) { g_clearanceSeen = std::move(seen); }

QString registeredSite(QString host) {
    host = host.toLower();
    const auto labels = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const auto n = labels.size();
    if (n <= 2)
        return host;
    static const QStringList secondLevel{"co", "com", "net", "org", "gov", "ac", "edu", "ne", "or"};
    const int keep = labels.last().size() == 2 && secondLevel.contains(labels[n - 2]) ? 3 : 2;
    return labels.mid(n - keep).join(QLatin1Char('.'));
}

bool sameSite(const QUrl &a, const QUrl &b) {
    static const QStringList checks{"cloudflare.com", "ddos-guard.net", "hcaptcha.com", "recaptcha.net"};
    const auto site = registeredSite(b.host());
    return site == registeredSite(a.host()) || checks.contains(site);
}

#ifdef SCANCORE_HAVE_WEBENGINE
namespace {
std::atomic<int> g_checksOpen{0};

class AdBlocker : public QWebEngineUrlRequestInterceptor {
public:
    // `always`: the hidden checks' own profile; otherwise only while the
    // reader's check window is open (players share that profile).
    AdBlocker(bool always, QObject *parent) : QWebEngineUrlRequestInterceptor(parent), m_always(always) {}
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        if ((m_always || g_checksOpen.load() > 0) && isAdHost(info.requestUrl().host()))
            info.block(true);
    }

private:
    bool m_always;
};
} // namespace
#endif

void setSiteCheckOpen(bool open) {
#ifdef SCANCORE_HAVE_WEBENGINE
    if (open)
        ++g_checksOpen;
    else if (g_checksOpen.load() > 0)
        --g_checksOpen;
#else
    Q_UNUSED(open)
#endif
}

void installWebBridge() {
#ifdef SCANCORE_HAVE_WEBENGINE
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    auto *profile = QQuickWebEngineProfile::defaultProfile();
    if (!profile)
        return;
    profile->setHttpUserAgent(QString::fromUtf8(Http::userAgent()));
    profile->setUrlRequestInterceptor(new AdBlocker(false, profile));
    auto *store = profile->cookieStore();
    QObject::connect(store, &QWebEngineCookieStore::cookieAdded, &CookieJar::shared(), [](const QNetworkCookie &cookie) {
        // Only a clearance the jar did not have is news: the stored ones
        // come through here too when the browser starts.
        bool fresh = false;
        if (cookie.name() == "cf_clearance") {
            const auto domain = cookie.domain().startsWith(QLatin1Char('.')) ? cookie.domain().mid(1) : cookie.domain();
            const auto known = CookieJar::shared().cookiesForUrl(QUrl(QStringLiteral("https://") + domain + cookie.path()));
            fresh = std::none_of(known.begin(), known.end(), [&](const QNetworkCookie &c) { return c.name() == cookie.name() && c.value() == cookie.value(); });
        }
        CookieJar::shared().insertCookie(cookie);
        CookieJar::shared().save();
        if (fresh && g_clearanceSeen)
            g_clearanceSeen(cookie.domain().startsWith(QLatin1Char('.')) ? cookie.domain().mid(1) : cookie.domain());
    });
    QObject::connect(store, &QWebEngineCookieStore::cookieRemoved, &CookieJar::shared(),
                     [](const QNetworkCookie &cookie) { CookieJar::shared().deleteCookie(cookie); });
    store->loadAllCookies();
#endif
}

#ifdef SCANCORE_HAVE_WEBENGINE
namespace {

struct Check {
    QUrl url;
    QString userAgent;
    QJsonObject headers;
    std::function<void(const QJsonArray &)> done;
};

// Cloudflare's challenge page posts its progress to the window; a check
// that wants a tap says "interactiveBegin". Run in the page's own world, as
// it has to see the page's messages; it only reports them.
const char *const kListener = R"(
addEventListener("message", (e) => {
    const d = e.data;
    if (d && d.source === "cloudflare-challenge") console.info("scancore-check:" + d.event);
});
)";

// Whether the page is a challenge at all (else there is nothing to pass).
const char *const kIsChallenge = R"(
!!(document.querySelector("#challenge-form, #challenge-running, #challenge-stage, [name=cf-turnstile-response], script[src*='challenge-platform']")
   || /just a moment/i.test(document.title))
)";

bool cookieFor(const QNetworkCookie &cookie, const QString &host) {
    const auto domain = cookie.domain().startsWith(QLatin1Char('.')) ? cookie.domain().mid(1) : cookie.domain();
    return host == domain || host.endsWith(QLatin1Char('.') + domain);
}

class CheckPage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
    std::function<void(const QString &)> onEvent;
    QUrl home;

protected:
    // The check stays on its site: an ad's redirect goes nowhere.
    bool acceptNavigationRequest(const QUrl &url, NavigationType, bool isMainFrame) override {
        return !isMainFrame || home.isEmpty() || sameSite(home, url);
    }
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString &message, int, const QString &) override {
        if (message.startsWith(QLatin1String("scancore-check:")) && onEvent)
            onEvent(message.mid(int(qstrlen("scancore-check:"))));
    }
};

class Checks : public QObject {
public:
    Checks() : QObject(QCoreApplication::instance()) {
        // Off the record: no clearance left from an earlier check is sent.
        m_profile = new QWebEngineProfile(this);
        m_profile->setUrlRequestInterceptor(new AdBlocker(true, m_profile));
        QWebEngineScript listener;
        listener.setName(QStringLiteral("scancore-check"));
        listener.setSourceCode(QString::fromUtf8(kListener));
        listener.setInjectionPoint(QWebEngineScript::DocumentCreation);
        listener.setWorldId(QWebEngineScript::MainWorld);
        listener.setRunsOnSubFrames(false);
        m_profile->scripts()->insert(listener);
        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this] { finish(false); });
        connect(m_profile->cookieStore(), &QWebEngineCookieStore::cookieAdded, this, [this](const QNetworkCookie &cookie) {
            // SCANCORE's own requests to the site pass too.
            CookieJar::shared().insertCookie(cookie);
            CookieJar::shared().save();
            if (m_page && cookie.name() == "cf_clearance" && cookieFor(cookie, m_current.url.host()))
                finish(true);
        });
    }

    void add(Check check) {
        m_queue.push_back(std::move(check));
        if (!m_page)
            next();
    }

private:
    void next() {
        if (m_queue.empty())
            return;
        m_current = std::move(m_queue.front());
        m_queue.pop_front();
        m_profile->setHttpUserAgent(m_current.userAgent.isEmpty() ? QString::fromUtf8(Http::userAgent()) : m_current.userAgent);
        auto *page = new CheckPage(m_profile, this);
        m_page = page;
        page->home = m_current.url;
        page->onEvent = [this](const QString &event) {
            if (event == QLatin1String("interactiveBegin") || event == QLatin1String("fail"))
                finish(false);
        };
        connect(page, &QWebEnginePage::loadFinished, this, [this, page](bool) {
            page->runJavaScript(QString::fromUtf8(kIsChallenge), QWebEngineScript::ApplicationWorld, [this, page](const QVariant &challenge) {
                // A page without a check: give a clearance on its way a moment.
                if (m_page == page && !challenge.toBool())
                    QTimer::singleShot(1500, this, [this, page] {
                        if (m_page == page)
                            finish(false);
                    });
            });
        });
        QWebEngineHttpRequest request(m_current.url);
        for (auto it = m_current.headers.begin(); it != m_current.headers.end(); ++it)
            request.setHeader(it.key().toUtf8(), it.value().toString().toUtf8());
        page->load(request);
        m_timeout.start(30000);
    }

    void finish(bool passed) {
        if (!m_page)
            return;
        m_timeout.stop();
        m_page->deleteLater();
        m_page = nullptr;
        QJsonArray cookies;
        if (passed)
            for (const auto &cookie : CookieJar::shared().cookiesForUrl(m_current.url))
                cookies.append(QString::fromUtf8(cookie.toRawForm()));
        auto done = std::move(m_current.done);
        m_current = {};
        if (done)
            done(cookies);
        next();
    }

    QWebEngineProfile *m_profile = nullptr;
    QPointer<QWebEnginePage> m_page;
    std::deque<Check> m_queue;
    Check m_current;
    QTimer m_timeout;
};

} // namespace
#endif

void solveBrowserCheck(const QUrl &url, const QString &userAgent, const QJsonObject &headers, std::function<void(const QJsonArray &cookies)> done) {
#ifdef SCANCORE_HAVE_WEBENGINE
    static Checks *checks = nullptr;
    if (!checks)
        checks = new Checks();
    checks->add({url, userAgent, headers, std::move(done)});
#else
    Q_UNUSED(url)
    Q_UNUSED(userAgent)
    Q_UNUSED(headers)
    done({});
#endif
}

} // namespace sc
