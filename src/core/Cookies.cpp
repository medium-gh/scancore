#include "Cookies.h"

#include "Database.h"

#include <QDateTime>
#include <QFile>
#include <QNetworkCookie>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>

namespace sc {

QObject *CookieJar::owner() {
    static QObject object;
    return &object;
}

CookieJar &CookieJar::shared() {
    static CookieJar *jar = [] {
        auto *created = new CookieJar(owner());
        return created;
    }();
    return *jar;
}

QList<QNetworkCookie> CookieJar::cookiesForUrl(const QUrl &url) const {
    std::lock_guard lock(m_mutex);
    return QNetworkCookieJar::cookiesForUrl(url);
}

bool CookieJar::setCookiesFromUrl(const QList<QNetworkCookie> &cookies, const QUrl &url) {
    std::lock_guard lock(m_mutex);
    return QNetworkCookieJar::setCookiesFromUrl(cookies, url);
}

bool CookieJar::insertCookie(const QNetworkCookie &cookie) {
    std::lock_guard lock(m_mutex);
    return QNetworkCookieJar::insertCookie(cookie);
}

bool CookieJar::deleteCookie(const QNetworkCookie &cookie) {
    std::lock_guard lock(m_mutex);
    return QNetworkCookieJar::deleteCookie(cookie);
}

namespace {

std::mutex g_protectedMutex;
QStringList g_protected;
bool g_protectedRead = false;

QStringList readProtected() {
    QStringList hosts{QStringLiteral("scancore.onl")};
    for (const auto &address : {qEnvironmentVariable("SCANCORE_SITE"), Database::pref(QStringLiteral("account.site")).toString()})
        if (const auto host = QUrl(address).host().toLower(); !host.isEmpty() && !hosts.contains(host))
            hosts.append(host);
    return hosts;
}

} // namespace

bool CookieJar::isProtected(const QUrl &url) {
    const auto host = url.host().toLower();
    if (host.isEmpty())
        return false;
    std::lock_guard lock(g_protectedMutex);
    if (!g_protectedRead) {
        g_protected = readProtected();
        g_protectedRead = true;
    }
    for (const auto &own : std::as_const(g_protected))
        if (host == own || host.endsWith(QLatin1Char('.') + own))
            return true;
    return false;
}

void CookieJar::refreshProtected() {
    const auto hosts = readProtected();
    std::lock_guard lock(g_protectedMutex);
    g_protected = hosts;
    g_protectedRead = true;
}

void CookieJar::load(const QString &path) {
    std::lock_guard lock(m_mutex);
    m_path = path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QList<QNetworkCookie> cookies;
    const auto now = QDateTime::currentDateTimeUtc();
    while (!file.atEnd()) {
        const auto line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;
        for (const auto &cookie : QNetworkCookie::parseCookies(line))
            if (!cookie.isSessionCookie() && cookie.expirationDate() > now)
                cookies.append(cookie);
    }
    setAllCookies(cookies);
}

void CookieJar::save() const {
    std::lock_guard lock(m_mutex);
    if (m_path.isEmpty())
        return;
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    // Sign-ins live here (SCANCORE's own site among them): this user's alone.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    for (const auto &cookie : allCookies())
        if (!cookie.isSessionCookie())
            file.write(cookie.toRawForm(QNetworkCookie::Full) + '\n');
    if (file.commit())
        QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

} // namespace sc
