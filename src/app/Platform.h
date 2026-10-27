#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

namespace sc::platform {

// Android only (everything here answers false or does nothing elsewhere).

// Opens `url` in a browser check screen of Android's web view, with
// SCANCORE's user agent. When the reader leaves it, the site's cookies go
// into SCANCORE's cookie jar and to extensions that read the site, and
// `done(url)` runs on the GUI thread. False when there is no such screen.
bool openSiteCheck(const QString &url, const QString &title, const QString &doneLabel);
void setSiteCheckDone(std::function<void(const QString &url)> done);

// Battery optimisation stops the app soon after it leaves the screen
// (library update checks, downloads, reading aloud).
bool batteryRestricted();
// The system's question to let SCANCORE run unrestricted.
void askBatteryUnrestricted();
// Whether the device is charging (Android); true where SCANCORE cannot
// tell, so nothing waits for it.
bool charging();

// A system notification (Android); false where there is none here, so the
// caller can use the tray instead. `tag` replaces an earlier one with it.
bool notify(const QString &title, const QString &body, const QString &tag);
// Android 13 and later: ask once to allow notifications.
void askNotificationPermission();
// Whether the system shows SCANCORE's notifications (Android 13+ asks).
bool notificationsAllowed();

// The anime player's web view, just created: lets its video start with sound
// and the page go full screen (see WebPlayer.java). `prepared` runs on the GUI
// thread once that is done (or no web view turned up).
void prepareWebPlayer();
void setWebPlayerPrepared(std::function<void()> prepared);
// Back while the player's own menu (in its page) is open: closes that menu
// and answers true; false when none is open.
bool closeWebPlayerMenu();
// Volume keys go to `pressed` (true for volume down) instead of changing the
// volume, while on (the comic reader's page turning).
void setVolumeKeys(bool on);
void setVolumeKeyPressed(std::function<void(bool down)> pressed);

// The system's share sheet for a text (Android); false elsewhere.
bool shareText(const QString &text, const QString &subject);
// Android: the share sheet for a file in the app's storage.
bool shareFile(const QString &path, const QString &mime, const QString &subject);
// Android: a file SCANCORE made onto the clipboard, for other apps to paste.
bool copyFile(const QString &path, const QString &mime);

// Lets multicast packets in on Wi-Fi (Android drops them otherwise), while
// looking for cast devices.
void holdMulticast(bool on);

// Offers a video to the other player apps (the system's chooser), with its
// headers ("Name: value"), title, start and subtitles for those that read
// them. False where there is none (desktop) or it failed.
bool openExternalPlayer(const QString &url, const QString &title, const QStringList &headers, int positionMs,
                        const QStringList &subtitles, const QStringList &subtitleNames);

// Decodes an image Qt cannot (AVIF) with Android's own decoder (Android 12
// and later); a null image elsewhere or when it fails. Any thread.
QImage decodeImage(const QByteArray &data);

// The player's full screen on phones: "landscape", "portrait" (until the
// phone is held upright) or "" (the system's rotation setting).
void setOrientation(const QString &mode);
// The player's swipes (Android; elsewhere brightness() is -1 and nothing
// changes): the window's brightness 0-1 (below 0: the system's again) and
// the media volume 0-1.
double brightness();
void setBrightness(double level);
double mediaVolume();
void setMediaVolume(double level);
// Light status/navigation bar icons for a dark screen, dark ones for a light one.
void setBarIcons(bool darkScreen);
// A television (Android TV): a D-pad, no touch screen.
bool isTv();
// Android: the status and navigation bars shown or hidden (readers).
void setSystemBars(bool shown);
// Android: the screen's fastest refresh rate for this window, or the system's choice.
void setFastestRefresh(bool on);

} // namespace sc::platform
