#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <functional>

namespace sc {

// Shares the embedded browser's cookies with the app's HTTP client, so a
// browser check passed in the verification window also lets SCANCORE's own
// requests through. The browser uses the client's user agent because
// Cloudflare ties clearance cookies to it. No-op without Qt WebEngine.
// Called by each embedded browser view as it is created (App.prepareWeb):
// the first call starts Chromium, about half a second, so not at start-up.
void installWebBridge();
// Called with the site's domain when the embedded browser's window receives
// a Cloudflare clearance (the reader passed the check there).
void setClearanceSeen(std::function<void(const QString &domain)> seen);
// The reader's check window is open (true) or closed: ad networks are
// blocked while one is. The hidden checks always block them.
void setSiteCheckOpen(bool open);
// The name a host is registered under ("manga.example.co.uk" ->
// "example.co.uk"), and whether `url` is on the same site as `home` (or a
// browser check's own host): a check window stays on its site.
QString registeredSite(QString host);
bool sameSite(const QUrl &home, const QUrl &url);

// Passes a browser check without the reader, for extensions (the host asks,
// see host/desktop/AppBrowserCheck.kt): `url` opens in a page nobody sees,
// in a private profile with the extension's user agent and headers. `done`
// gets the site's cookies (raw Set-Cookie form) once Cloudflare set a
// clearance, or nothing when the check wants the reader or 30 seconds pass.
// One check at a time, as each sets the profile's user agent. GUI thread.
// Without Qt WebEngine `done` gets nothing at once.
void solveBrowserCheck(const QUrl &url, const QString &userAgent, const QJsonObject &headers, std::function<void(const QJsonArray &cookies)> done);

} // namespace sc
