#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

namespace sc {

// Thrown by sources and services. The message is shown to the user, so it
// should say what failed in plain words, never dump a response body.
class Error : public std::runtime_error {
public:
    explicit Error(const QString &message, int status = 0, const QString &verifyUrl = QString())
        : std::runtime_error(message.toStdString()), m_status(status), m_verifyUrl(verifyUrl) {}
    int status() const { return m_status; }
    QString message() const { return QString::fromStdString(what()); }
    // Set when a site answered with a browser check: the page to open so the
    // user can pass it.
    QString verifyUrl() const { return m_verifyUrl; }

private:
    int m_status;
    QString m_verifyUrl;
};

using CancelFlag = std::shared_ptr<std::atomic_bool>;

// Requests made on a worker thread observe that thread's cancel flag, so a
// Task that is cancelled (or garbage collected by QML) stops its downloads.
class CancelScope {
public:
    explicit CancelScope(CancelFlag flag);
    ~CancelScope();
    static bool cancelled();
    static CancelFlag current();
    // Cancels every request in flight and refuses new ones (app shutdown).
    static void cancelAll();

private:
    CancelFlag m_previous;
};

using Headers = QList<QPair<QByteArray, QByteArray>>;

enum class Lane { Pages, Images };

struct Request {
    QUrl url;
    QByteArray method = "GET";
    Headers headers;
    QByteArray body;
    int timeoutMs = 15000;
    qint64 maxBytes = 16 * 1024 * 1024;
    Lane lane = Lane::Pages;
    // Asked for every address the request goes to (the first, and each
    // redirect's): empty to go on, else why not. Set, redirects are followed
    // here rather than by Qt, so none slips past it.
    std::function<QString(const QUrl &)> guard;
};

struct Response {
    int status = 0;
    QByteArray body;
    QByteArray contentType;
    QUrl finalUrl;
    QList<QPair<QByteArray, QByteArray>> headers;
    // A response header by name (any case), or empty.
    QByteArray header(const QByteArray &name) const;

    bool ok() const { return status >= 200 && status < 300; }
    QString text() const { return QString::fromUtf8(body); }
    QJsonDocument json() const;
    bool isHtml() const { return contentType.startsWith("text/html"); }
    // Cloudflare and similar interstitial pages.
    bool isChallenge() const;
};

class Http {
public:
    // Whether an address is this device or its network (loopback, private,
    // link-local, carrier-grade NAT, unique-local, multicast, unspecified,
    // or IPv4 inside IPv6), rather than somewhere on the internet.
    static bool isLocalAddress(const QHostAddress &address);
    static constexpr const char *BrowserUA =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36";
    // Chrome's user agent for this system at Chromium `major`, as Chrome
    // itself sends it (reduced: x.0.0.0). Builds with Qt WebEngine use it with
    // the engine's own version (main.cpp): Cloudflare compares the user agent
    // with what the browser shows of itself (navigator.platform, client
    // hints), and a Windows Chrome 141 that is Chromium 140 on Linux gets a
    // check to click every time, whose clearance then works for nothing.
    static QByteArray chromeUserAgent(int major);
    // Accept header for images: only formats every build decodes (Qt and
    // qtimageformats). Image servers that choose the format from it would
    // otherwise send AVIF, which Qt cannot read.
    static constexpr const char *ImageAccept = "image/webp,image/jpeg,image/png,image/gif,*/*;q=0.5";

    // Blocking; must run on a worker thread (Task or image loader).
    static Response send(const Request &request);

    static Response get(const QUrl &url, const Headers &headers = {}, int timeoutMs = 15000);
    static Response postForm(const QUrl &url, const QList<QPair<QString, QString>> &form, const Headers &headers = {});
    static Response postJson(const QUrl &url, const QByteArray &json, const Headers &headers = {});

    // Fails with a readable Error when the status is not 2xx.
    static Response getOk(const QUrl &url, const Headers &headers = {}, int timeoutMs = 15000);
    static QJsonDocument getJson(const QUrl &url, const Headers &headers = {}, int timeoutMs = 15000);

    static void setUserAgent(const QByteArray &ua);
    static QByteArray userAgent();

    // Offline tests answer requests themselves (fixture sites and
    // repositories on *.test). A handler that returns nothing lets the
    // request go to the network; an empty handler removes it.
    using TestHandler = std::function<std::optional<Response>(const Request &)>;
    static void setTestHandler(TestHandler handler);
};

QUrl withQuery(QUrl url, std::initializer_list<QPair<QString, QString>> params);
bool hostMatches(const QString &host, const QString &allowed);

} // namespace sc
