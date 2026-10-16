#include "app/Account.h"
#include "app/App.h"
#include "app/Images.h"
#include "app/Updater.h"
#include "app/WebBridge.h"
#include "core/Cookies.h"
#include "core/Database.h"
#include "core/Http.h"
#include "core/ImageCache.h"
#include "sources/Manifest.h"
#include "sources/comics/Registry.h"
#include "sources/novels/Registry.h"

#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#include <QtWebView/QtWebView>
#include <jni.h>
#else
#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>
#endif
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>

#ifdef SCANCORE_HAVE_WEBENGINE
#include <QtWebEngineQuick/QtWebEngineQuick>
#endif

using namespace sc;

#ifdef Q_OS_ANDROID
extern "C" JNIEXPORT jboolean JNICALL
Java_com_mediumdev_scancore_ScanCoreActivity_dispatchBack(JNIEnv *, jclass) {
    auto *appState = App::instance();
    return QMetaObject::invokeMethod(appState, [appState] { emit appState->androidBackRequested(); },
                                     Qt::QueuedConnection)
               ? JNI_TRUE : JNI_FALSE;
}
#endif

namespace {

void loadFonts() {
    for (const auto *font : {":/fonts/DMSans.ttf", ":/fonts/DMSans-Italic.ttf", ":/fonts/SpaceGrotesk.ttf",
                             ":/fonts/Literata.ttf", ":/fonts/Literata-Italic.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(font));
    QFont font(QStringLiteral("DM Sans"));
    font.setPixelSize(14);
    font.setHintingPreference(QFont::PreferNoHinting);
    QGuiApplication::setFont(font);
}

void openStorage() {
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(data);
    QDir().mkpath(cache);
    const auto db = data + QStringLiteral("/scancore.db");
    // A backup chosen in Settings is swapped in before anything opens the
    // database.
    if (QFile::exists(db + QStringLiteral(".restore"))) {
        QFile::remove(db + QStringLiteral("-wal"));
        QFile::remove(db + QStringLiteral("-shm"));
        QFile::remove(db + QStringLiteral(".previous"));
        QFile::rename(db, db + QStringLiteral(".previous"));
        QFile::rename(db + QStringLiteral(".restore"), db);
    }
    Database::open(db);
    CookieJar::shared().load(data + QStringLiteral("/cookies.txt"));
    ImageCache::init(cache + QStringLiteral("/images"), 1024LL * 1024 * 1024);
    Manifest::load(data + QStringLiteral("/manifest.json"));
    comics::Registry::rebuild();
    novels::Registry::rebuild();
}

} // namespace

int main(int argc, char *argv[]) {
#ifdef SCANCORE_HAVE_WEBENGINE
#ifdef Q_OS_LINUX
    // Chromium's GPU compositor shares textures with Qt Quick through GBM;
    // where GBM is unavailable it falls back to Vulkan, and some Mesa drivers
    // then crash on import. Compositing the player in software avoids that
    // and video still decodes on the GPU. Users can override the flags.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu-compositing");
    // A global MANGOHUD=1 injects MangoHud's Vulkan layer into Chromium's
    // in-process GPU thread, which then aborts with "free(): invalid
    // pointer" shortly after startup. Set DISABLE_MANGOHUD=0 to keep it.
    if (!qEnvironmentVariableIsSet("DISABLE_MANGOHUD"))
        qputenv("DISABLE_MANGOHUD", "1");
#endif
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QtWebEngineQuick::initialize();
#endif
#ifdef Q_OS_ANDROID
    QtWebView::initialize();
    using Application = QGuiApplication;
#else
    using Application = QApplication;
#endif
    Application app(argc, argv);
    Application::setOrganizationName(QStringLiteral("Medium Dev"));
    Application::setOrganizationDomain(QStringLiteral("mediumdev.com"));
    Application::setApplicationName(QStringLiteral("ScanCore"));
    Application::setApplicationDisplayName(QStringLiteral("SCANCORE"));
    Application::setApplicationVersion(QStringLiteral(SCANCORE_VERSION));
    // Development hook for testing updates.
    if (qEnvironmentVariableIsSet("SCANCORE_PRETEND_VERSION"))
        Application::setApplicationVersion(qEnvironmentVariable("SCANCORE_PRETEND_VERSION"));
    Application::setDesktopFileName(QStringLiteral("com.mediumdev.scancore"));
    Application::setWindowIcon(QIcon(QStringLiteral(":/branding/scancore.svg")));
#ifndef Q_OS_ANDROID
    QApplication::setQuitOnLastWindowClosed(false);
#endif

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    loadFonts();

    try {
        openStorage();
    } catch (const Error &e) {
        qCritical().noquote() << e.message();
        return 1;
    }

    installWebBridge();
    Account::instance();
    auto *appState = App::instance();
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("remote"), new RemoteImageProvider);
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);

#ifndef Q_OS_ANDROID
    QSystemTrayIcon tray(QApplication::windowIcon());
    QMenu trayMenu;
    trayMenu.addAction(QStringLiteral("Open SCANCORE"), appState, &App::activateRequested);
    trayMenu.addSeparator();
    trayMenu.addAction(QStringLiteral("Quit"), &app, &QApplication::quit);
    tray.setContextMenu(&trayMenu);
    tray.setToolTip(QStringLiteral("SCANCORE"));
    QObject::connect(&tray, &QSystemTrayIcon::activated, appState, [appState](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            emit appState->activateRequested();
    });
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray.show();
    appState->setTrayIcon(&tray);
#endif

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("ScanCore", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *root = engine.rootObjects().first();
    QObject::connect(appState, &App::androidBackRequested, root, [root] {
        QMetaObject::invokeMethod(root, "handleAndroidBack", Qt::DirectConnection);
    });
    // The window decides whether closing hides to the tray; once it is gone
    // there is nothing left to show.
    QObject::connect(engine.rootObjects().first(), &QObject::destroyed, &app, &Application::quit);

    appState->startBackgroundWork();
    Updater::instance()->startAutomatic();

    QObject::connect(&app, &Application::aboutToQuit, [] {
        CancelScope::cancelAll();
        CookieJar::shared().save();
    });
    return app.exec();
}
