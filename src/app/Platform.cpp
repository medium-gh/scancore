#include "Platform.h"

#ifdef Q_OS_ANDROID
#include "core/Cookies.h"
#include "core/Http.h"
#include "core/Task.h"
#include "extensions/ExtensionSource.h"
#include "extensions/LineHost.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QNetworkCookie>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStyleHints>
#include <QTouchEvent>
#include <QUrl>

#include <cmath>
#endif

namespace sc::platform {

namespace {

std::function<void(const QString &)> g_siteCheckDone;
std::function<void()> g_webPlayerPrepared;
std::function<void(bool)> g_volumeKey;

#ifdef Q_OS_ANDROID
constexpr auto kBrowserCheckClass = "com/mediumdev/scancore/BrowserCheck";
constexpr auto kPowerClass = "com/mediumdev/scancore/Power";
constexpr auto kNotifyClass = "com/mediumdev/scancore/Notify";
constexpr auto kWebPlayerClass = "com/mediumdev/scancore/WebPlayer";
constexpr auto kScreenClass = "com/mediumdev/scancore/Screen";
constexpr auto kVolumeKeysClass = "com/mediumdev/scancore/VolumeKeys";
constexpr auto kImagesClass = "com/mediumdev/scancore/Images";
constexpr auto kExternalPlayerClass = "com/mediumdev/scancore/ExternalPlayer";
constexpr auto kMulticastClass = "com/mediumdev/scancore/Multicast";
constexpr auto kShareClass = "com/mediumdev/scancore/Share";

void jniVolumeKey(JNIEnv *, jclass, jboolean down) {
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [down] {
            if (g_volumeKey)
                g_volumeKey(down);
        },
        Qt::QueuedConnection);
}

bool registerVolumeKeys() {
    static const bool registered = [] {
        const JNINativeMethod methods[] = {
            {"nativeKey", "(Z)V", reinterpret_cast<void *>(jniVolumeKey)},
        };
        QJniEnvironment env;
        return env.registerNativeMethods(kVolumeKeysClass, methods, 1);
    }();
    return registered;
}

QString javaString(JNIEnv *env, jstring s) {
    if (!s)
        return {};
    const jchar *chars = env->GetStringChars(s, nullptr);
    QString out(reinterpret_cast<const QChar *>(chars), env->GetStringLength(s));
    env->ReleaseStringChars(s, chars);
    return out;
}

// "a=1; b=2" from Android's cookie store, for the site's host.
void jniSiteCheckDone(JNIEnv *env, jclass, jstring jurl, jstring jcookies) {
    const auto url = javaString(env, jurl);
    const auto header = javaString(env, jcookies);
    const QUrl site(url);
    QList<QNetworkCookie> cookies;
    // The web view keeps the real expiry; a month here is enough to carry
    // a check's pass (they last hours to days).
    const auto expires = QDateTime::currentDateTimeUtc().addDays(30);
    for (const auto &part : header.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const auto eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        QNetworkCookie cookie(part.left(eq).trimmed().toUtf8(), part.mid(eq + 1).trimmed().toUtf8());
        cookie.setDomain(QLatin1Char('.') + site.host());
        cookie.setPath(QStringLiteral("/"));
        cookie.setExpirationDate(expires);
        cookies.append(cookie);
    }
    if (!cookies.isEmpty()) {
        CookieJar::shared().setCookiesFromUrl(cookies, site);
        CookieJar::shared().save();
    }
    // Pages try again once extensions have the cookies too.
    runInBackground([url] {
        try {
            ext::ExtensionSource::shareCookies(url);
        } catch (const Error &) {
            // The extension asks again if it still needs the check.
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [url] {
                if (g_siteCheckDone)
                    g_siteCheckDone(url);
            },
            Qt::QueuedConnection);
    });
}

void jniWebPlayerPrepared(JNIEnv *, jclass) {
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [] {
            if (g_webPlayerPrepared)
                g_webPlayerPrepared();
        },
        Qt::QueuedConnection);
}

bool registerWebPlayer() {
    static const bool registered = [] {
        const JNINativeMethod methods[] = {
            {"nativePrepared", "()V", reinterpret_cast<void *>(jniWebPlayerPrepared)},
        };
        QJniEnvironment env;
        return env.registerNativeMethods(kWebPlayerClass, methods, 1);
    }();
    return registered;
}

bool registerBrowserCheck() {
    static const bool registered = [] {
        const JNINativeMethod methods[] = {
            {"nativeDone", "(Ljava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void *>(jniSiteCheckDone)},
        };
        QJniEnvironment env;
        return env.registerNativeMethods(kBrowserCheckClass, methods, 1);
    }();
    return registered;
}
#endif

} // namespace

void setSiteCheckDone(std::function<void(const QString &)> done) { g_siteCheckDone = std::move(done); }

void setVolumeKeyPressed(std::function<void(bool)> pressed) { g_volumeKey = std::move(pressed); }

QImage decodeImage(const QByteArray &data) {
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    jbyteArray bytes = env->NewByteArray(jsize(data.size()));
    if (!bytes)
        return {};
    env->SetByteArrayRegion(bytes, 0, jsize(data.size()), reinterpret_cast<const jbyte *>(data.constData()));
    // [width, height, then ARGB pixels] or null.
    const auto answer = QJniObject::callStaticObjectMethod(kImagesClass, "decode", "([B)[I", bytes);
    env->DeleteLocalRef(bytes);
    const auto result = answer.object<jintArray>();
    if (env.checkAndClearExceptions() || !result)
        return {};
    const jsize length = env->GetArrayLength(result);
    if (length < 2)
        return {};
    jint size[2];
    env->GetIntArrayRegion(result, 0, 2, size);
    QImage image;
    if (size[0] > 0 && size[1] > 0 && qint64(size[0]) * size[1] == length - 2) {
        // Android's ARGB ints are QImage's ARGB32 in memory.
        image = QImage(size[0], size[1], QImage::Format_ARGB32);
        if (!image.isNull())
            for (int y = 0; y < size[1]; ++y)
                env->GetIntArrayRegion(result, 2 + y * size[0], size[0], reinterpret_cast<jint *>(image.scanLine(y)));
    }
    return image;
#else
    Q_UNUSED(data)
    return {};
#endif
}

void setVolumeKeys(bool on) {
#ifdef Q_OS_ANDROID
    if (!registerVolumeKeys())
        return;
    QJniObject::callStaticMethod<void>(kVolumeKeysClass, "setCapture", "(Landroid/app/Activity;Z)V",
                                       QNativeInterface::QAndroidApplication::context().object(), jboolean(on));
#else
    Q_UNUSED(on)
#endif
}

bool openSiteCheck(const QString &url, const QString &title, const QString &doneLabel) {
#ifdef Q_OS_ANDROID
    if (!registerBrowserCheck())
        return false;
    // AniList's sign-in only hands back a token in its address, and nothing
    // uses its cookies afterwards, so it keeps the web view's own user agent
    // (empty: BrowserCheck leaves it as it is). SCANCORE's desktop Chrome
    // agent on a TV box's older web view did not match what the page saw,
    // and Cloudflare refused the check.
    const QUrl address(url);
    const bool signInOnly = address.host() == QLatin1String("anilist.co") && address.path().startsWith(QLatin1String("/api/v2/oauth/"));
    QJniObject::callStaticMethod<void>(
        kBrowserCheckClass, "start", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        QNativeInterface::QAndroidApplication::context().object(), QJniObject::fromString(url).object<jstring>(),
        QJniObject::fromString(signInOnly ? QString() : ext::ExtensionSource::checkAgent(url)).object<jstring>(),
        QJniObject::fromString(title).object<jstring>(), QJniObject::fromString(doneLabel).object<jstring>());
    return true;
#else
    Q_UNUSED(url)
    Q_UNUSED(title)
    Q_UNUSED(doneLabel)
    return false;
#endif
}

bool batteryRestricted() {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kPowerClass, "restricted", "(Landroid/content/Context;)Z",
                                                  QNativeInterface::QAndroidApplication::context().object());
#else
    return false;
#endif
}

bool charging() {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kPowerClass, "charging", "(Landroid/content/Context;)Z",
                                                  QNativeInterface::QAndroidApplication::context().object());
#else
    return true;
#endif
}

void askBatteryUnrestricted() {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kPowerClass, "ask", "(Landroid/content/Context;)V",
                                       QNativeInterface::QAndroidApplication::context().object());
#endif
}

bool shareText(const QString &text, const QString &subject) {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kShareClass, "text", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)Z",
                                                  QNativeInterface::QAndroidApplication::context().object(),
                                                  QJniObject::fromString(text).object<jstring>(), QJniObject::fromString(subject).object<jstring>());
#else
    Q_UNUSED(text)
    Q_UNUSED(subject)
    return false;
#endif
}

bool shareFile(const QString &path, const QString &mime, const QString &subject) {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kShareClass, "file",
                                                  "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
                                                  QNativeInterface::QAndroidApplication::context().object(),
                                                  QJniObject::fromString(path).object<jstring>(), QJniObject::fromString(mime).object<jstring>(),
                                                  QJniObject::fromString(subject).object<jstring>());
#else
    Q_UNUSED(path)
    Q_UNUSED(mime)
    Q_UNUSED(subject)
    return false;
#endif
}

bool copyFile(const QString &path, const QString &mime) {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kShareClass, "copy", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)Z",
                                                  QNativeInterface::QAndroidApplication::context().object(),
                                                  QJniObject::fromString(path).object<jstring>(), QJniObject::fromString(mime).object<jstring>());
#else
    Q_UNUSED(path)
    Q_UNUSED(mime)
    return false;
#endif
}

void holdMulticast(bool on) {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kMulticastClass, "hold", "(Landroid/content/Context;Z)V",
                                       QNativeInterface::QAndroidApplication::context().object(), jboolean(on));
#else
    Q_UNUSED(on)
#endif
}

bool openExternalPlayer(const QString &url, const QString &title, const QStringList &headers, int positionMs,
                        const QStringList &subtitles, const QStringList &subtitleNames) {
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    const auto strings = [&](const QStringList &list) {
        auto *array = env->NewObjectArray(jsize(list.size()), env.findClass("java/lang/String"), nullptr);
        for (int i = 0; i < list.size(); ++i)
            env->SetObjectArrayElement(array, i, QJniObject::fromString(list[i]).object<jstring>());
        return array;
    };
    auto *headerArray = strings(headers);
    auto *subtitleArray = strings(subtitles);
    auto *nameArray = strings(subtitleNames);
    const bool ok = QJniObject::callStaticMethod<jboolean>(
        kExternalPlayerClass, "open",
        "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;I[Ljava/lang/String;[Ljava/lang/String;)Z",
        QNativeInterface::QAndroidApplication::context().object(), QJniObject::fromString(url).object<jstring>(),
        QJniObject::fromString(title).object<jstring>(), headerArray, jint(positionMs), subtitleArray, nameArray);
    env->DeleteLocalRef(headerArray);
    env->DeleteLocalRef(subtitleArray);
    env->DeleteLocalRef(nameArray);
    return ok;
#else
    Q_UNUSED(url)
    Q_UNUSED(title)
    Q_UNUSED(headers)
    Q_UNUSED(positionMs)
    Q_UNUSED(subtitles)
    Q_UNUSED(subtitleNames)
    return false;
#endif
}

bool notify(const QString &title, const QString &body, const QString &tag) {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        kNotifyClass, "show", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        QNativeInterface::QAndroidApplication::context().object(), QJniObject::fromString(title).object<jstring>(),
        QJniObject::fromString(body).object<jstring>(), QJniObject::fromString(tag).object<jstring>(),
        QJniObject::fromString(QCoreApplication::translate("Notifications", "Updates and announcements")).object<jstring>());
    return true;
#else
    Q_UNUSED(title)
    Q_UNUSED(body)
    Q_UNUSED(tag)
    return false;
#endif
}

void askNotificationPermission() {
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject::callStaticMethod<void>(kNotifyClass, "ask", "(Landroid/content/Context;)V",
                                           QNativeInterface::QAndroidApplication::context().object());
    });
#endif
}

bool notificationsAllowed() {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kNotifyClass, "allowed", "(Landroid/content/Context;)Z",
                                                  QNativeInterface::QAndroidApplication::context().object());
#else
    return true;
#endif
}

void setWebPlayerPrepared(std::function<void()> prepared) { g_webPlayerPrepared = std::move(prepared); }

void prepareWebPlayer() {
#ifdef Q_OS_ANDROID
    if (!registerWebPlayer()) {
        if (g_webPlayerPrepared)
            g_webPlayerPrepared();
        return;
    }
    QJniObject::callStaticMethod<void>(kWebPlayerClass, "prepare", "(Landroid/app/Activity;)V",
                                       QNativeInterface::QAndroidApplication::context().object());
#endif
}

bool closeWebPlayerMenu() {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kWebPlayerClass, "closeMenu", "(Landroid/app/Activity;)Z",
                                                  QNativeInterface::QAndroidApplication::context().object());
#else
    return false;
#endif
}

void setOrientation(const QString &mode) {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kScreenClass, "orient", "(Landroid/app/Activity;Ljava/lang/String;)V",
                                       QNativeInterface::QAndroidApplication::context().object(),
                                       QJniObject::fromString(mode).object<jstring>());
#else
    Q_UNUSED(mode)
#endif
}

double brightness() {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jfloat>(kScreenClass, "brightness", "(Landroid/app/Activity;)F",
                                                QNativeInterface::QAndroidApplication::context().object());
#else
    return -1;
#endif
}

void setBrightness(double level) {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kScreenClass, "setBrightness", "(Landroid/app/Activity;F)V",
                                       QNativeInterface::QAndroidApplication::context().object(), jfloat(level));
#else
    Q_UNUSED(level)
#endif
}

double mediaVolume() {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jfloat>(kScreenClass, "mediaVolume", "(Landroid/app/Activity;)F",
                                                QNativeInterface::QAndroidApplication::context().object());
#else
    return -1;
#endif
}

void setMediaVolume(double level) {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kScreenClass, "setMediaVolume", "(Landroid/app/Activity;F)V",
                                       QNativeInterface::QAndroidApplication::context().object(), jfloat(level));
#else
    Q_UNUSED(level)
#endif
}

void setFastestRefresh(bool on) {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kScreenClass, "fastestRefresh", "(Landroid/app/Activity;Z)V",
                                       QNativeInterface::QAndroidApplication::context().object(), jboolean(on));
#else
    Q_UNUSED(on)
#endif
}

#ifdef Q_OS_ANDROID
namespace {

// The list (Flickable, ListView, GridView…) around `item` that scrolls
// along the finger's way.
QQuickItem *scrollerFor(QQuickItem *item, bool vertical) {
    for (auto *p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
        if (!p->inherits("QQuickFlickable") || !p->property("interactive").toBool())
            continue;
        // Flickable.AutoFlickDirection 0, HorizontalFlick 1, VerticalFlick 2,
        // HorizontalAndVerticalFlick 3.
        const int direction = p->property("flickableDirection").toInt();
        const bool along = vertical ? (direction == 2 || direction == 3 || (direction == 0 && p->property("contentHeight").toReal() > p->height()))
                                    : (direction == 1 || direction == 3 || (direction == 0 && p->property("contentWidth").toReal() > p->width()));
        if (along)
            return p;
    }
    return nullptr;
}

class TouchScroll : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if ((event->type() != QEvent::TouchUpdate && event->type() != QEvent::TouchEnd) || !qobject_cast<QQuickWindow *>(watched))
            return false;
        auto *touch = static_cast<QTouchEvent *>(event);
        if (touch->pointCount() != 1)
            return false;
        const auto &point = touch->point(0);
        const auto moved = point.globalPosition() - point.globalPressPosition();
        const int slop = QGuiApplication::styleHints()->startDragDistance();
        if (std::abs(moved.x()) <= slop && std::abs(moved.y()) <= slop)
            return false;
        // A control or row holding the touch (not a list, a slider being
        // dragged or anything else that keeps it): the list it is in takes
        // it, and the control lets go without acting.
        auto *holder = qobject_cast<QQuickItem *>(touch->exclusiveGrabber(point));
        if (!holder || holder->inherits("QQuickFlickable") || holder->keepTouchGrab() || holder->keepMouseGrab())
            return false;
        if (auto *list = scrollerFor(holder, std::abs(moved.y()) >= std::abs(moved.x())))
            touch->setExclusiveGrabber(point, list);
        return false;
    }
};

} // namespace
#endif

void setUpTouch() {
#ifdef Q_OS_ANDROID
    const auto configuration = QJniObject::callStaticObjectMethod(
        "android/view/ViewConfiguration", "get", "(Landroid/content/Context;)Landroid/view/ViewConfiguration;",
        QNativeInterface::QAndroidApplication::context().object());
    const auto *screen = QGuiApplication::primaryScreen();
    if (configuration.isValid() && screen && screen->devicePixelRatio() > 0) {
        const int slop = configuration.callMethod<jint>("getScaledTouchSlop");
        if (const int distance = qRound(slop / screen->devicePixelRatio()); distance > 0)
            QGuiApplication::styleHints()->setStartDragDistance(distance);
    }
    qApp->installEventFilter(new TouchScroll(qApp));
#endif
}

bool isTv() {
#ifdef Q_OS_ANDROID
    static const bool tv = QJniObject::callStaticMethod<jboolean>(kScreenClass, "isTv", "(Landroid/content/Context;)Z",
                                                                  QNativeInterface::QAndroidApplication::context().object());
    return tv;
#else
    // SCANCORE_TV=1 tries the TV's remote navigation on a computer.
    return qEnvironmentVariableIsSet("SCANCORE_TV");
#endif
}

void setSystemBars(bool shown) {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kScreenClass, "systemBars", "(Landroid/app/Activity;Z)V",
                                       QNativeInterface::QAndroidApplication::context().object(), jboolean(shown));
#else
    Q_UNUSED(shown)
#endif
}

void setBarIcons(bool darkScreen) {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kScreenClass, "barIcons", "(Landroid/app/Activity;Z)V",
                                       QNativeInterface::QAndroidApplication::context().object(),
                                       jboolean(darkScreen));
#else
    Q_UNUSED(darkScreen)
#endif
}

} // namespace sc::platform
