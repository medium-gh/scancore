#include "App.h"
#include "core/QuietSites.h"
#include "KeepAwake.h"

#include "WebBridge.h"
#include "Images.h"
#include "Announcements.h"
#include "SiteNotifications.h"
#include "Downloads.h"
#include "CrashLog.h"
#include "Library.h"
#include "Live.h"
#include "Extensions.h"
#include "Notifications.h"
#include "Platform.h"
#include "Profiler.h"
#include "core/Cookies.h"
#include "core/Database.h"
#include "core/Http.h"
#include "core/ImageCache.h"
#include "extensions/ExtensionSource.h"
#include "extensions/LineHost.h"
#include "extensions/Manager.h"
#include "core/Task.h"
#include "core/Text.h"
#include "sources/Manifest.h"
#include "sources/comics/Registry.h"
#include "sources/novels/Registry.h"

#include <QJSValue>
#include <QUrlQuery>
#include <QNetworkInformation>
#include <QClipboard>
#include <QSaveFile>
#include <QImageReader>
#include <QBuffer>
#include <QDateTime>
#include <QHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkCookie>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#else
#include <QSystemTrayIcon>
#endif
#include <QVersionNumber>

namespace sc {

namespace {

const QVariantMap kDefaults{
    {"theme", "dark"},
    {"accent", "mono"},
    {"reducedMotion", false},
    // E-ink screens: black and white, no motion, a flash on comic page
    // turns (einkFlash), novels paged by taps.
    {"eink", false},
    {"einkFlash", true},
    {"readerMode", "strip"},
    {"readerWidth", 820},
    {"readerGap", 0},
    {"readerDirection", "ltr"},
    {"readerSpread", "single"},
    {"readerCrop", false},
    // true (always), "controls" (only with the reader's controls) or false.
    {"readerProgressBar", true},
    // Its width in pixels: 3, 6 or 10.
    {"readerProgressWidth", 3},
    // Its colours: pages not read yet, pages read, the page you are on.
    {"readerRailUnread", "#e4e4e7"},
    {"readerRailRead", "#8b8b94"},
    {"readerRailCurrent", "#000000"},
    // Its look ("segments", "line", "floating"), edge ("left", "right",
    // "bottom") and strength (0.3 see-through to 1 solid).
    {"readerProgressLook", "segments"},
    // Anime opened from AniList or MyAnimeList play from: "builtin" (the
    // built-in servers; extensions only from Watch from), "extensions" (the
    // ones in animeMatchSources, all when empty) or "ask".
    {"animeTrackerPlayer", "builtin"},
    {"animeMatchSources", QStringList()},
    {"readerProgressEdge", "left"},
    {"readerProgressOpacity", 0.85},
    // Readers open in full screen (their full screen button sets it).
    {"readerFullscreen", false},
    {"novelFont", "Literata"},
    {"novelSize", 19},
    {"novelLineHeight", 1.5},
    {"novelWidth", 720},
    {"novelTheme", "app"},
    {"novelJustify", false},
    // More of the novel reader's typography and look.
    {"novelWeight", 400},
    {"novelParagraphSpacing", 0.9},
    {"novelIndent", false},
    {"novelMargin", 32},
    {"novelDim", 0},
    {"novelCustomBg", "#1b1a17"},
    {"novelCustomText", "#e6dfd2"},
    {"novelAutoScrollSpeed", 40},
    // Reading aloud (Speech): voice id, speed and pitch multipliers, go on
    // to the next chapter, keep the spoken paragraph on screen.
    {"ttsVoice", ""},
    {"ttsRate", 1},
    {"ttsPitch", 1},
    {"ttsContinue", true},
    {"ttsFollow", true},
    {"autoNext", true},
    // The video player's volume (0-130) and speed, kept between episodes.
    {"playerVolume", 100},
    // The player's subtitles (size, outline, colour, a box behind them) and
    // Anime4K sharpening (off, fast, hq).
    {"subtitleScale", 1.0},
    {"subtitleOutline", 3},
    {"subtitleColor", "#ffffff"},
    {"subtitleBox", false},
    {"animeSharpen", "off"},
    // Android's video decoding: "copy" (MediaCodec, frames copied back),
    // "direct" (into OpenGL; faster, froze on some phones) or "off".
    {"playerHwdec", "copy"},
    {"playerSpeed", 1},
    // How the picture fills the player (fit, crop, stretch, original) and
    // the phone gestures in full screen (NativePlayer.qml).
    {"playerFit", "fit"},
    {"playerSwipes", true},
    {"playerDoubleTapSeek", true},
    {"playerHoldSpeed", true},
    {"autoSkip", false},
    // What plays first: a picture height (0 for the best) and the audio
    // (VideoPrefs.languages: "" for the source's choice, "ja", "en", …).
    {"animeQuality", 0},
    {"animeAudio", ""},
    {"libraryCheckHours", 2},
    // Which saved series the check looks at, and when it runs by itself.
    {"updateLists", QVariantList{"reading", "planned", "paused", "completed"}},
    {"updateSkipFinished", true},
    {"updateSkipUnread", false},
    {"updateSkipNotStarted", false},
    {"updateOnlyUnmetered", false},
    {"updateOnlyCharging", false},
    // New chapters of series on these lists download by themselves.
    {"autoDownloadLists", QVariantList{}},
    // A downloaded chapter is deleted once read.
    {"deleteAfterRead", false},
    // Comic reader: colour filter, wide pages, taps and volume keys (the
    // reader's own settings may keep them per series).
    {"readerBrightness", 1},
    {"readerWarmth", 0},
    {"readerGrey", false},
    {"readerInvert", false},
    {"readerSplitWide", false},
    // "sides", "lshape", "kindle", "edge" or "off"; flipped by
    // readerTapInvert ("none", "horizontal", "vertical", "both").
    {"readerTapZones", "sides"},
    {"readerTapInvert", "none"},
    // Tap zones in the vertical strip too (on touch screens).
    {"readerStripTaps", false},
    {"readerVolumeKeys", false},
    // Privacy: incognito (nothing recorded) and when the app lock asks
    // again (minutes away, or -1 for only at start; see Lock).
    {"incognito", false},
    {"lockAfter", 1},
    {"lockBiometric", true},
    {"notifyUpdates", true},
    {"notifyDigest", false},
    {"showNsfwSources", false},
    {"imageCacheMB", 1024},
    {"closeToTray", false},
    // Android: the one-time question about battery optimisation was asked.
    {"batteryAsked", false},
    {"notificationsAsked", false},
    // Announcements from scancore.onl in the notification area.
    {"announcements", true},
    {"checkAppUpdates", true},
    {"sidebarCollapsed", false},
    // The year whose Wrapped banner on Home was closed (December to 7 January).
    {"wrappedDismissed", 0},
    {"comicSource", "all"},
    // The library: which media, category ("all", "favourites" or one of
    // Router.libraryCategories) and view (grid, compact, list).
    {"libraryMedia", "comic"},
    {"libraryCategory", "all"},
    {"libraryView", "grid"},
    // A series' chapters: one list ("list") or ten a page ("pages").
    {"chapterList", "list"},
    // The screen stays on while a reader is open.
    {"readerKeepAwake", true},
    {"librarySort", "updates"},
    {"comickCatalog", true},
    // The tracker catalogue each page browses and searches: comick,
    // anilist, mal or none (novels: anilist, mal or none).
    {"catalogComic", "comick"},
    {"catalogNovel", "anilist"},
    // Anime: anilist or mal (the Anime page's shelves).
    {"catalogAnime", "anilist"},
    // Content language "All sources" searches; empty for every language.
    {"sourceLanguages", ""},
    {"onboarded", false},
    {"autoUpdate", true},
    // Extension updates signed with a trusted key install by themselves.
    {"autoUpdateExtensions", true},
    {"scrollSpeed", 1},
    // How long a fling glides (Motion.flickDeceleration).
    {"flingGlide", "default"},
    // Android: draw at the screen's fastest refresh rate (Platform).
    {"highRefresh", true},
    {"novelSource", "all"},
};

} // namespace

App::App(QObject *parent) : QObject(parent) {
    // Settings restored before the event loop runs are not changes.
    QTimer::singleShot(0, this, [this] {
        m_running = true;
        platform::setFastestRefresh(m_settings.value(QStringLiteral("highRefresh"), true).toBool());
    });
    // Qt's surface can come a moment after the window: ask again for it.
    QTimer::singleShot(2500, this, [this] { platform::setFastestRefresh(m_settings.value(QStringLiteral("highRefresh"), true).toBool()); });
    m_settings = kDefaults;
    for (auto it = kDefaults.begin(); it != kDefaults.end(); ++it)
        m_settings.insert(it.key(), Database::pref(QStringLiteral("setting.") + it.key(), it.value()));
    // Before 2.0 the only catalogue was Comick's, switched by comickCatalog.
    if (!Database::pref(QStringLiteral("setting.catalogComic")).isValid() && m_settings.value("comickCatalog") == false)
        m_settings.insert(QStringLiteral("catalogComic"), QStringLiteral("none"));
    // The network's kind (metered, cellular) for "only on Wi-Fi" checks.
    QNetworkInformation::loadDefaultBackend();
    m_libraryTimer.setSingleShot(false);
    connect(&m_libraryTimer, &QTimer::timeout, this, [] { Library::instance()->checkUpdates(true); });
    platform::setSiteCheckDone([this](const QString &url) { emit siteChecked(url); });
    connect(this, &App::siteChecked, this, [this](const QString &url) { answerReader(url, true); });
    setClearanceSeen([this](const QString &domain) { emit siteCleared(domain); });
    // Browser checks the desktop extension host asks SCANCORE to pass in a
    // page nobody sees (Android's host has its own web view).
    ext::LineHost::setAskHandler([this](const QString &method, const QJsonObject &params, ext::LineHost::Answer answer) {
        if (method == QLatin1String("readerBrowserCheck")) {
            QMetaObject::invokeMethod(
                this, [this, url = params.value(QStringLiteral("url")).toString(), answer = std::move(answer)] { askReader(url, answer); },
                Qt::QueuedConnection);
            return;
        }
        if (method != QLatin1String("solveBrowserCheck"))
            return answer(QJsonValue());
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [params, answer = std::move(answer)] {
                solveBrowserCheck(QUrl(params.value(QStringLiteral("url")).toString()), params.value(QStringLiteral("userAgent")).toString(),
                                  params.value(QStringLiteral("headers")).toObject(),
                                  [answer](const QJsonArray &cookies) { answer(QJsonObject{{"cookies", cookies}}); });
            },
            Qt::QueuedConnection);
    });
    platform::setWebPlayerPrepared([this] { emit webPlayerPrepared(); });
    platform::setVolumeKeyPressed([this](bool down) { emit volumeKey(down); });
#ifdef Q_OS_ANDROID
    // The reader may change it in the system's settings while away.
    m_batteryRestricted = platform::batteryRestricted();
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            return;
        const bool restricted = platform::batteryRestricted();
        if (restricted != m_batteryRestricted) {
            m_batteryRestricted = restricted;
            emit batteryChanged();
        }
    });
#endif
}

App *App::instance() {
    static auto *app = [] {
        auto *created = new App;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return app;
}

QString App::version() const { return QCoreApplication::applicationVersion(); }

bool App::tv() const { return platform::isTv(); }

QString App::platform() const {
    // Development hook: SCANCORE_LOOK_AS=android lays the interface out as
    // on a phone (screenshots of phone layouts on a computer); the phone's
    // own services stay absent.
    static const auto lookAs = qEnvironmentVariable("SCANCORE_LOOK_AS");
    if (!lookAs.isEmpty())
        return lookAs;
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString App::dataFolder() const { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
int App::manifestVersion() const { return Manifest::version(); }

bool App::webEngine() const {
#if defined(SCANCORE_HAVE_WEBENGINE) || defined(SCANCORE_HAVE_WEBVIEW)
    return true;
#else
    return false;
#endif
}

void App::prepareWeb() const { installWebBridge(); }

QVariantMap App::startAction() const {
#if defined(Q_OS_ANDROID) && defined(QT_DEBUG)
    // Android debug builds have no environment to pass: the same JSON in
    // files/start.json (adb shell run-as …), read once and removed.
    static const auto fromFile = [] {
        QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/start.json"));
        if (!file.open(QIODevice::ReadOnly))
            return QVariantMap();
        const auto map = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        file.remove();
        return map;
    }();
    if (!fromFile.isEmpty())
        return fromFile;
#endif
    return QJsonDocument::fromJson(qgetenv("SCANCORE_START")).object().toVariantMap();
}

QVariant App::plain(const QVariant &value) {
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

void App::pinnedFirst(QVariantList &sources) {
    const auto keys = instance()->m_settings.value(QStringLiteral("pinnedSources")).toStringList();
    if (keys.isEmpty())
        return;
    const QSet<QString> pinned(keys.begin(), keys.end());
    for (auto &value : sources) {
        auto row = value.toMap();
        row.insert(QStringLiteral("pinned"), pinned.contains(row.value(QStringLiteral("key")).toString()));
        value = row;
    }
    std::stable_partition(sources.begin(), sources.end(), [](const QVariant &v) { return v.toMap().value(QStringLiteral("pinned")).toBool(); });
}

void App::setSetting(const QString &key, const QVariant &input) {
    const auto value = plain(input);
    if (m_settings.value(key) == value)
        return;
    m_settings.insert(key, value);
    Database::setPref(QStringLiteral("setting.") + key, value);
    if (key == QLatin1String("libraryCheckHours"))
        scheduleLibraryCheck();
    if (key == QLatin1String("announcements")) {
        if (value.toBool())
            QMetaObject::invokeMethod(this, [] { Announcements::instance()->start(); }, Qt::QueuedConnection);
        else
            Announcements::instance()->stop();
    }
    // Built-in sources follow their language being switched on or off.
    if (key == QLatin1String("builtinLanguages")) {
        comics::Registry::rebuild();
        novels::Registry::rebuild();
        emit manifestChanged();
    }
    // Mihon's downloads in a folder added (or no longer there after one is
    // taken off the list).
    if (key == QLatin1String("highRefresh"))
        platform::setFastestRefresh(value.toBool());
    if (key == QLatin1String("localFolders") && m_running)
        Downloads::instance()->relinkMihon();
    if (key == QLatin1String("imageCacheMB")) {
        ImageCache::setBudget(std::max<qint64>(128, value.toLongLong()) * 1024 * 1024);
        // A smaller limit takes effect now, not at the next start.
        runInBackground([] { ImageCache::trim(); });
    }
    emit settingsChanged();
}

void App::rememberSetting(const QString &key, const QVariant &input) {
    const auto value = plain(input);
    if (m_settings.value(key) == value)
        return;
    m_settings.insert(key, value);
    Database::setPref(QStringLiteral("setting.") + key, value);
}

QString App::image(const QString &url, const QString &referer) const {
    if (url.isEmpty())
        return {};
    if (url.startsWith(QLatin1String("file:")) || url.startsWith(QLatin1String("qrc:")) ||
        url.startsWith(QLatin1String("image:")))
        return url;
    return RemoteImageProvider::encode(url, referer);
}

QString App::icon(const QString &name, const QString &color) const {
    return QStringLiteral("image://icon/%1?color=%2").arg(name, QString::fromUtf8(QUrl::toPercentEncoding(color)));
}

void App::openUrl(const QString &url) const {
    const QUrl parsed(url);
    if (parsed.scheme() == QLatin1String("https") || parsed.scheme() == QLatin1String("http"))
        QDesktopServices::openUrl(parsed);
}

void App::moveToBackground() const {
#ifdef Q_OS_ANDROID
    // What Back on an app's first screen does on Android 12 and later. Runs on
    // the Android UI thread without waiting for it, so it cannot deadlock with
    // Qt's thread.
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid())
            activity.callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true));
    });
#endif
}

bool App::openSiteCheck(const QString &url, const QString &title, const QString &doneLabel) {
    // Not SCANCORE's own site or the sync server: a check window there would
    // hand the account's session to whoever asked for it.
    if (CookieJar::isProtected(QUrl(url)))
        return true;
    return platform::openSiteCheck(url, title, doneLabel);
}

bool App::protectedSite(const QString &url) const { return CookieJar::isProtected(QUrl(url)); }

void App::cancelSiteCheck(const QString &url) { answerReader(url, false); }


// One window per site: requests that meet the same check wait on it together.
// Not while SCANCORE is in the background, and not longer than 90 seconds.
void App::askReader(const QString &url, std::function<void(const QJsonValue &)> answer) {
    if (qGuiApp->applicationState() != Qt::ApplicationActive || CookieJar::isProtected(QUrl(url)))
        return answer(QJsonObject{{"passed", false}});
    // A search across many sources: not now (the host does not count it as
    // the reader declining).
    if (isQuietSite(url))
        return answer(QJsonObject{{"passed", false}, {"quiet", true}});
    const auto host = QUrl(url).host();
    const bool open = m_readerChecks.contains(host);
    m_readerChecks[host].append(std::move(answer));
    if (open)
        return;
    emit browserCheckWanted(url);
    QTimer::singleShot(90000, this, [this, url] { answerReader(url, false); });
}

void App::answerReader(const QString &url, bool passed) {
    const auto host = QUrl(url).host();
    const auto waiting = m_readerChecks.take(host);
    if (waiting.isEmpty())
        return;
    QJsonArray cookies;
    if (!CookieJar::isProtected(QUrl(url)))
        for (const auto &cookie : CookieJar::shared().cookiesForUrl(QUrl(QStringLiteral("https://%1/").arg(host))))
            cookies.append(QString::fromUtf8(cookie.toRawForm()));
    const QJsonObject result{{"passed", passed}, {"cookies", cookies}};
    for (const auto &answer : waiting)
        answer(result);
}

QString App::siteAgent(const QString &url) const { return ext::ExtensionSource::checkAgent(url); }

QString App::defaultAgent() const { return QString::fromUtf8(Http::userAgent()); }

void App::setSiteCheckOpen(bool open) const { sc::setSiteCheckOpen(open); }

QString App::chapterNumber(const QString &number, const QString &title) const { return sc::chapterNumber(number, title); }

bool App::sameSite(const QString &home, const QString &url) const { return sc::sameSite(QUrl(home), QUrl(url)); }

void App::finishSiteCheck(const QString &url) {
    QPointer<App> self(this);
    runInBackground([self, url] {
        try {
            ext::ExtensionSource::shareCookies(url);
        } catch (const Error &) {
            // The extension asks again if it still needs the check.
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, url] {
                if (self)
                    emit self->siteChecked(url);
            },
            Qt::QueuedConnection);
    });
}

void App::askBatteryUnrestricted() const { platform::askBatteryUnrestricted(); }

void App::prepareWebPlayer() const { platform::prepareWebPlayer(); }
bool App::closeWebPlayerMenu() const { return platform::closeWebPlayerMenu(); }

void App::setOrientation(const QString &mode) const { platform::setOrientation(mode); }

void App::setBarIcons(bool darkScreen) const { platform::setBarIcons(darkScreen); }
void App::setSystemBars(bool shown) const { platform::setSystemBars(shown); }
void App::keepAwake(const QString &holder, bool on) const { keepAwake::hold(holder, on); }

void App::setVolumeKeys(bool on) const { platform::setVolumeKeys(on); }

bool App::profiling() const { return Profiler::enabled(); }

void App::profilePlace(const QString &place) const { Profiler::instance()->setPlace(place); }
void App::startFrameTimes() const { Profiler::instance()->startFrameTimes(); }
QVariantMap App::stopFrameTimes() const { return Profiler::instance()->stopFrameTimes(); }

void App::openFolder(const QString &path) const {
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

double App::screenBrightness() const { return platform::brightness(); }
void App::setScreenBrightness(double level) const { platform::setBrightness(level); }
double App::mediaVolume() const { return platform::mediaVolume(); }
void App::setMediaVolume(double level) const { platform::setMediaVolume(level); }

void App::copy(const QString &text) const { QGuiApplication::clipboard()->setText(text); }

void App::notify(const QString &title, const QString &body) {
#ifndef Q_OS_ANDROID
    if (m_tray && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(title, body, QSystemTrayIcon::Information, 6000);
#else
    Q_UNUSED(title)
    Q_UNUSED(body)
#endif
}

QStringList App::chapterParts(const QStringList &titles) const {
    QStringList out;
    out.reserve(titles.size());
    for (const auto &title : titles)
        out.append(chapterPart(title));
    return out;
}

QString App::relativeTime(const QVariant &when) const {
    // Chapter lists ask for every row they show as they scroll: answers are
    // kept for a minute, as parsing a date and reading the local time zone
    // each time made rows slow to appear on phones.
    static QHash<QString, QString> cache;
    static qint64 cachedAt = 0;
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (now - cachedAt > 60 || cache.size() > 4000) {
        cache.clear();
        cachedAt = now;
    }
    const auto key = when.toString();
    if (const auto hit = cache.constFind(key); hit != cache.cend())
        return hit.value();
    QDateTime time;
    if (when.typeId() == QMetaType::QString)
        time = QDateTime::fromString(key, Qt::ISODate);
    else if (when.toLongLong() > 0)
        time = QDateTime::fromSecsSinceEpoch(when.toLongLong());
    const auto text = time.isValid() ? relativeText(time, now) : QString();
    cache.insert(key, text);
    return text;
}

QString App::relativeText(const QDateTime &time, qint64 now) {
    const auto secs = now - time.toSecsSinceEpoch();
    if (secs < 0) {
        const auto ahead = -secs;
        if (ahead < 3600)
            return QStringLiteral("in %1m").arg(std::max<qint64>(1, ahead / 60));
        if (ahead < 86400)
            return QStringLiteral("in %1h").arg(ahead / 3600);
        return QStringLiteral("in %1d").arg(ahead / 86400);
    }
    if (secs < 60)
        return QStringLiteral("just now");
    if (secs < 3600)
        return QStringLiteral("%1m ago").arg(secs / 60);
    if (secs < 86400)
        return QStringLiteral("%1h ago").arg(secs / 3600);
    if (secs < 7 * 86400)
        return QStringLiteral("%1d ago").arg(secs / 86400);
    return QLocale().toString(time.date(), QLocale::ShortFormat);
}

QString App::formatBytes(qint64 bytes) const { return QLocale().formattedDataSize(bytes); }

bool App::saveWindowImage(QObject *window, const QString &path) const {
    auto *quick = qobject_cast<QQuickWindow *>(window);
    return quick && quick->grabWindow().save(path);
}

QVariantList App::licences() const {
    QFile list(QStringLiteral(":/licenses/components.json"));
    QVariantList out;
    if (list.open(QIODevice::ReadOnly))
        out = QJsonDocument::fromJson(list.readAll()).toVariant().toList();
    // licenses/<library>/<file>, copied from the libmpv build.
    QDirIterator it(QStringLiteral(":/licenses/mpv"), QDir::Files, QDirIterator::Subdirectories);
    QVariantList player;
    while (it.hasNext()) {
        const auto path = it.next();
        const auto relative = path.mid(QStringLiteral(":/licenses/").size());
        player.append(QVariantMap{{"name", relative.section(QLatin1Char('/'), 1, 1)},
                                  {"use", QStringLiteral("Video player library")},
                                  {"licence", relative.section(QLatin1Char('/'), -1)},
                                  {"file", relative}});
    }
    std::sort(player.begin(), player.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("file").toString() < b.toMap().value("file").toString();
    });
    return out + player;
}

QString App::licenceText(const QString &file) const {
    // Only the app's own licence files.
    if (file.contains(QLatin1String("..")))
        return {};
    QFile text(QStringLiteral(":/licenses/") + file);
    return text.open(QIODevice::ReadOnly) ? QString::fromUtf8(text.readAll()) : QString();
}

QVariantList App::builtinLanguages() const {
    QHash<QString, int> counts;
    const auto manifest = Manifest::current();
    for (const auto *media : {"comics", "novels"})
        for (const auto &value : manifest.value(QLatin1String(media)).toArray()) {
            const auto o = value.toObject();
            if (Manifest::supports(o) && o.value(QStringLiteral("enabled")).toBool(true))
                ++counts[baseLanguage(o.value(QStringLiteral("language")).toString(QStringLiteral("en")))];
        }
    const auto on = sc::builtinLanguages();
    QVariantList out;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        out.append(QVariantMap{{"value", it.key()}, {"label", languageName(it.key())}, {"count", it.value()}, {"on", on.contains(QStringLiteral("*")) || on.contains(it.key())}});
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        if (x.value("on").toBool() != y.value("on").toBool())
            return x.value("on").toBool();
        return x.value("count").toInt() > y.value("count").toInt();
    });
    return out;
}

void App::stackAfter(QObject *item, QObject *sibling) const {
    auto *a = qobject_cast<QQuickItem *>(item);
    auto *b = qobject_cast<QQuickItem *>(sibling);
    if (a && b && a != b && a->parentItem() == b->parentItem())
        a->stackAfter(b);
}

void App::stackBefore(QObject *item, QObject *sibling) const {
    auto *a = qobject_cast<QQuickItem *>(item);
    auto *b = qobject_cast<QQuickItem *>(sibling);
    if (a && b && a != b && a->parentItem() == b->parentItem())
        a->stackBefore(b);
}

Task *App::checkForUpdate() {
    return Task::run([this]() -> QVariant {
        const auto doc = Http::getJson(QUrl(QStringLiteral("https://api.github.com/repos/medium-gh/scancore/releases/latest")),
                                       {{"Accept", "application/vnd.github+json"}});
        const auto release = doc.object();
        auto tag = release.value("tag_name").toString();
        if (tag.startsWith(QLatin1Char('v')))
            tag.remove(0, 1);
        const auto latest = QVersionNumber::fromString(tag);
        qsizetype suffix = 0;
        const auto running = QCoreApplication::applicationVersion();
        const auto current = QVersionNumber::fromString(running, &suffix);
        // A pre-release (2.0.0-beta.1) is older than its release.
        const bool available = !latest.isNull() && (latest > current || (latest == current && suffix < running.size()));
        const auto url = release.value("html_url").toString();
        if (available)
            QMetaObject::invokeMethod(this, [this, tag, url] { emit updateAvailable(tag, url); });
        return QVariantMap{{"available", available},
                           {"version", tag},
                           {"url", url},
                           {"notes", release.value("body").toString()}};
    });
}

Task *App::refreshSources() {
    return Task::run([this]() -> QVariant {
        const bool changed = Manifest::refresh();
        if (changed) {
            comics::Registry::rebuild();
            novels::Registry::rebuild();
            QMetaObject::invokeMethod(this, [this] { emit manifestChanged(); });
        }
        return QVariantMap{{"changed", changed}, {"version", Manifest::version()}};
    });
}

Task *App::cacheSize() {
    return Task::run([]() -> QVariant { return ImageCache::sizeOnDisk(); });
}

void App::clearCache() {
    runInBackground([] {
        ImageCache::clear();
        Database::exec(QStringLiteral("DELETE FROM cache"));
    });
}

Task *App::exportBackup(const QUrl &file) {
    return Task::run([file]() -> QVariant {
        const auto path = file.toLocalFile();
        if (path.isEmpty())
            throw Error(QStringLiteral("Choose a file to save the backup to."));
        QFile::remove(path);
        // VACUUM INTO writes a consistent copy while the app keeps running.
        auto escaped = path;
        escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
        Database::exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped));
        return path;
    });
}

Task *App::importBackup(const QUrl &file) {
    return Task::run([file]() -> QVariant {
        const auto path = file.toLocalFile();
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly) || in.read(16) != QByteArray("SQLite format 3\0", 16))
            throw Error(QStringLiteral("That file is not a SCANCORE backup."));
        in.close();
        const auto pending = Database::path() + QStringLiteral(".restore");
        QFile::remove(pending);
        if (!QFile::copy(path, pending))
            throw Error(QStringLiteral("Could not copy the backup."));
        return QStringLiteral("Restart SCANCORE to finish restoring the backup.");
    });
}

void App::scheduleLibraryCheck() {
    const int hours = m_settings.value(QStringLiteral("libraryCheckHours")).toInt();
    if (hours <= 0) {
        m_libraryTimer.stop();
        return;
    }
    m_libraryTimer.start(hours * 3600 * 1000);
}

void App::startBackgroundWork() {
    // New chapters reach the notification area (and the system's
    // notifications) from Library::checkUpdates itself; a new SCANCORE
    // from here.
    connect(this, &App::updateAvailable, this, [](const QString &version, const QString &url) {
        Notifications::post({QStringLiteral("app"), QStringLiteral("app:") + version, tr("SCANCORE %1 is available").arg(version),
                             tr("See what is new and update."), QVariantMap{{"url", url}}, true});
    });
    // Announcements from scancore.onl, live while online.
    QTimer::singleShot(6000, this, [] { Announcements::instance()->start(); });
    QTimer::singleShot(8000, this, [] { SiteNotifications::instance()->start(); });
    // Live progress with the account's other open apps.
    QTimer::singleShot(5000, this, [] { Live::instance()->update(); });
    scheduleLibraryCheck();
    QTimer::singleShot(4000, this, [this] {
        refreshSources();
        if (m_settings.value(QStringLiteral("libraryCheckHours")).toInt() > 0)
            Library::instance()->checkUpdates(true);
        runInBackground([] {
            ImageCache::trim();
            Database::cachePrune();
        });
    });
    // Repositories are read again twice a day (a conditional request each,
    // so usually a few bytes), and trusted updates installed.
    const auto checkExtensions = [this] {
        const bool install = m_settings.value(QStringLiteral("autoUpdateExtensions")).toBool();
        runInBackground([install] {
            ext::Manager::refreshAll();
            // Updated by themselves: said, so it is not a surprise.
            const auto updated = install ? ext::Manager::installTrustedUpdates() : QList<ext::Installed>();
            if (!updated.isEmpty()) {
                QStringList names;
                for (const auto &i : updated)
                    names.append(i.package.name);
                Notifications::post({QStringLiteral("extensions"), QStringLiteral("extensions:updated"),
                                     updated.size() == 1 ? tr("%1 was updated").arg(names.first())
                                                         : tr("%1 extensions were updated").arg(updated.size()),
                                     names.join(QStringLiteral(", ")), QVariantMap{{"page", "extensions"}}, false});
            }
            QMetaObject::invokeMethod(Extensions::instance(), [] { Extensions::instance()->notifyExtensionsChanged(); });
            // What is still waiting (new signing keys, or automatic
            // updates off).
            const int waiting = int(ext::Manager::updates().size());
            if (waiting > 0)
                Notifications::post({QStringLiteral("extensions"), QStringLiteral("extensions:updates"),
                                     waiting == 1 ? tr("An extension update is ready") : tr("%1 extension updates are ready").arg(waiting),
                                     tr("Open Extensions to update them."), QVariantMap{{"page", "extensions"}}, false});
        });
    };
    QTimer::singleShot(20000, this, checkExtensions);
    auto *extensionTimer = new QTimer(this);
    extensionTimer->setInterval(12 * 3600 * 1000);
    connect(extensionTimer, &QTimer::timeout, this, checkExtensions);
    extensionTimer->start();
}

} // namespace sc

namespace sc {

QVariantList App::crashReports() const { return crashlog::reports(); }
QString App::crashReport(const QString &file) const { return crashlog::read(file); }
QStringList App::unseenCrashReports() const { return crashlog::unseen(); }
void App::markCrashReportSeen(const QString &file) { crashlog::markSeen(file); }
void App::removeCrashReport(const QString &file) { crashlog::remove(file); }
QString App::diagnostics() const { return crashlog::diagnostics(); }
bool App::shareText(const QString &text, const QString &subject) const { return platform::shareText(text, subject); }

Task *App::pageFile(const QString &url, const QString &referer) const {
    return Task::run([url, referer]() -> QVariant {
        auto bytes = ImageCache::fetch(url, referer);
        if (bytes.isEmpty())
            throw Error(tr("The page did not load."));
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        auto format = QString::fromLatin1(QImageReader::imageFormat(&buffer)).toLower();
        static const QHash<QString, QString> mimes{{"jpeg", "image/jpeg"}, {"jpg", "image/jpeg"}, {"png", "image/png"},
                                                   {"webp", "image/webp"}, {"gif", "image/gif"}};
        if (!mimes.contains(format)) {
            QBuffer png;
            png.open(QIODevice::WriteOnly);
            if (!decodeImage(bytes).save(&png, "PNG"))
                throw Error(tr("This page's picture could not be read."));
            bytes = png.data();
            format = QStringLiteral("png");
        }
        const auto suffix = format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format;
        // Under the app's files, which Android's share sheet and clipboard
        // can hand to other apps. Pages shared earlier than an hour ago go.
        const QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/shared"));
        dir.mkpath(QStringLiteral("."));
        const auto now = QDateTime::currentDateTime();
        for (const auto &old : dir.entryInfoList({QStringLiteral("page-*")}, QDir::Files))
            if (old.lastModified().secsTo(now) > 3600)
                QFile::remove(old.absoluteFilePath());
        const auto path = dir.filePath(QStringLiteral("page-%1.%2").arg(now.toMSecsSinceEpoch()).arg(suffix));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw Error(tr("The page could not be saved for copying."));
        return QVariantMap{{"path", path}, {"suffix", suffix}, {"mime", mimes.value(format)}};
    });
}

bool App::copyImageFile(const QString &path, const QString &mime) const {
#ifdef Q_OS_ANDROID
    return platform::copyFile(path, mime);
#else
    Q_UNUSED(mime)
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    try {
        const auto image = decodeImage(file.readAll());
        QGuiApplication::clipboard()->setImage(image);
        return true;
    } catch (const Error &) {
        return false;
    }
#endif
}

bool App::saveFileTo(const QString &path, const QUrl &target) const {
    QFile in(path);
    // A content:// address on Android, a path elsewhere.
    QFile out(target.isLocalFile() ? target.toLocalFile() : target.toString());
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const auto bytes = in.readAll();
    return out.write(bytes) == bytes.size();
}

bool App::shareFile(const QString &path, const QString &mime, const QString &subject) const {
    return platform::shareFile(path, mime, subject);
}

QString App::reportIssueUrl(const QString &text) const {
    // GitHub takes links of about 8 KB: the report's start (what happened
    // and the backtrace); the rest is pasted.
    const auto first = text.section(QLatin1Char('\n'), 0, 0);
    const auto what = text.section(QStringLiteral("What: "), 1).section(QLatin1Char('\n'), 0, 0);
    const auto title = first.contains(QLatin1String("crash"))  ? QStringLiteral("Crash: ") + what
                       : first.contains(QLatin1String("hang")) ? QStringLiteral("Hang: ") + what
                                                                : QStringLiteral("Problem report");
    auto body = QStringLiteral("What I was doing:\n\n\n```\n%1\n```").arg(text.left(5000));
    QUrl url(QStringLiteral("https://github.com/medium-gh/scancore/issues/new"));
    url.setQuery(QUrlQuery{{QStringLiteral("title"), title}, {QStringLiteral("body"), body}});
    return url.toString(QUrl::FullyEncoded);
}

} // namespace sc
