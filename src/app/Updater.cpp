#include "Updater.h"

#include "App.h"
#include "core/Ed25519.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

#include <cstdio>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/qnativeinterface.h>
#endif

namespace sc {

namespace {
const QUrl kLatest(QStringLiteral("https://api.github.com/repos/medium-gh/scancore/releases/latest"));
// Releases are signed with this key (the Release workflow's
// RELEASE_SIGNING_KEY): an installer someone swapped on GitHub, or one
// without a signature, is never run.
const QByteArray kReleaseKey = QByteArray::fromHex("79f296044adac851512ffa7657849d3386e8640990c56946bbd12f2667caf115");
} // namespace

Updater::Updater(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)) {
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    connect(&m_timer, &QTimer::timeout, this, [this] { check(true); });
}

Updater *Updater::instance() {
    static auto *updater = [] {
        auto *created = new Updater;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return updater;
}

QString Updater::assetSuffix() const {
#if defined(Q_OS_ANDROID)
    // The APK built for this phone's processor (32-bit phones: issue #19).
#if defined(__aarch64__)
    return QStringLiteral("-android-arm64.apk");
#elif defined(__arm__)
    return QStringLiteral("-android-armv7.apk");
#else
    return QStringLiteral("-android-x86_64.apk");
#endif
#elif defined(Q_OS_WIN)
    return QStringLiteral("-windows-x64-setup.exe");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("-macos-universal.dmg");
#elif defined(Q_OS_IOS)
    // Unsigned: signed and installed by the user's own sideloading tool.
    return QStringLiteral("-ios-unsigned.ipa");
#else
    return QStringLiteral("-linux-x86_64.AppImage");
#endif
}

bool Updater::canInstall() const {
    if (managedByFlatpak())
        return false;
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Only the AppImage can replace itself; the runtime sets $APPIMAGE.
    return !qEnvironmentVariableIsEmpty("APPIMAGE") && QFileInfo(qEnvironmentVariable("APPIMAGE")).isWritable();
#elif defined(Q_OS_WIN)
    // Portable zip builds have no installer to run.
    return QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/unins000.exe"));
#elif defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath().endsWith(QLatin1String(".app/Contents/MacOS"));
#elif defined(Q_OS_IOS)
    // An app cannot replace itself there: the new IPA is sideloaded.
    return false;
#else
    return true;
#endif
}

bool Updater::managedByFlatpak() const {
    return qEnvironmentVariableIsSet("FLATPAK_ID");
}

QString Updater::downloadPath() const {
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/updates");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/SCANCORE-") + m_version + assetSuffix();
}

void Updater::setState(const QString &state, const QString &error) {
    m_state = state;
    m_error = error;
    emit changed();
    // Development hook: SCANCORE_START {"installUpdate": true} installs as soon
    // as the download is ready.
    if (state == QLatin1String("ready") && App::instance()->startAction().value(QStringLiteral("installUpdate")).toBool())
        QTimer::singleShot(0, this, &Updater::install);
}

void Updater::startAutomatic() {
    // Flatpak updates are delivered by the user's Flatpak remote/software center.
    if (managedByFlatpak())
        return;
    if (App::instance()->settings().value(QStringLiteral("checkAppUpdates"), true).toBool()) {
        QTimer::singleShot(5000, this, [this] { check(true); });
        m_timer.start(6 * 3600 * 1000);
    }
}

void Updater::check(bool quiet) {
    if (managedByFlatpak())
        return;
    if (m_state == QLatin1String("checking") || m_state == QLatin1String("downloading") ||
        m_state == QLatin1String("ready"))
        return;
    m_quiet = quiet;
    if (!quiet)
        setState(QStringLiteral("checking"));
    QNetworkRequest request(kLatest);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("SCANCORE/") + QCoreApplication::applicationVersion().toUtf8());
    auto *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setState(m_quiet ? QStringLiteral("idle") : QStringLiteral("error"),
                     QStringLiteral("Could not reach GitHub to check for updates."));
            return;
        }
        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        auto tag = release.value("tag_name").toString();
        if (tag.startsWith(QLatin1Char('v')))
            tag.remove(0, 1);
        const auto latest = QVersionNumber::fromString(tag);
        qsizetype suffix = 0;
        const auto running = QCoreApplication::applicationVersion();
        const auto current = QVersionNumber::fromString(running, &suffix);
        // A pre-release (2.0.0-beta.1) is older than its release; GitHub's
        // latest release is never a pre-release.
        const bool prerelease = suffix < running.size();
        // Names a file and goes into the macOS install script: version
        // characters only.
        static const QRegularExpression unsafe(QStringLiteral("[^0-9A-Za-z.+-]"));
        m_version = QString(tag).remove(unsafe);
        m_notes = release.value("body").toString();
        m_pageUrl = release.value("html_url").toString();
        m_assetUrl.clear();
        m_signatureUrl.clear();
        for (const auto &value : release.value("assets").toArray()) {
            const auto asset = value.toObject();
            const auto name = asset.value("name").toString();
            if (name.endsWith(assetSuffix())) {
                m_assetUrl = asset.value("browser_download_url").toString();
                m_size = qint64(asset.value("size").toDouble());
            } else if (name.endsWith(assetSuffix() + QStringLiteral(".sig"))) {
                m_signatureUrl = asset.value("browser_download_url").toString();
            }
        }
        if (latest.isNull() || latest < current || (latest == current && !prerelease)) {
            setState(m_quiet ? QStringLiteral("idle") : QStringLiteral("current"));
            return;
        }
        setState(QStringLiteral("available"));
        // Updates download in the background when automatic updates are on.
        if (canInstall() && !m_assetUrl.isEmpty() && App::instance()->settings().value(QStringLiteral("autoUpdate"), true).toBool())
            download();
    });
}

void Updater::download() {
    if (m_assetUrl.isEmpty() || m_state == QLatin1String("downloading"))
        return;
    const auto path = downloadPath();
    if (m_signatureUrl.isEmpty()) {
        setState(QStringLiteral("error"), QStringLiteral("This release is not signed, so SCANCORE will not install it. Download it from the release page."));
        return;
    }
    if (QFileInfo(path).size() == m_size && m_size > 0) {
        checkSignature(path, [this] {
            m_progress = 1;
            emit progressChanged();
            setState(QStringLiteral("ready"));
        });
        return;
    }
    m_file = new QFile(path + QStringLiteral(".part"), this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        setState(QStringLiteral("error"), QStringLiteral("Could not save the update."));
        return;
    }
    m_progress = 0;
    setState(QStringLiteral("downloading"));
    QNetworkRequest request{QUrl(m_assetUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("SCANCORE/") + QCoreApplication::applicationVersion().toUtf8());
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { m_file->write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_progress = total > 0 ? double(received) / double(total) : 0;
        emit progressChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, path] {
        m_reply->deleteLater();
        m_file->write(m_reply->readAll());
        m_file->close();
        const bool ok = m_reply->error() == QNetworkReply::NoError && (m_size <= 0 || m_file->size() == m_size);
        if (!ok) {
            m_file->remove();
            m_file->deleteLater();
            m_file = nullptr;
            setState(QStringLiteral("error"), QStringLiteral("The update download did not finish. It will be retried later."));
            return;
        }
        QFile::remove(path);
        m_file->rename(path);
        m_file->deleteLater();
        m_file = nullptr;
        checkSignature(path, [this] { setState(QStringLiteral("ready")); });
    });
}

void Updater::checkSignature(const QString &path, const std::function<void()> &ready) {
    QNetworkRequest request{QUrl(m_signatureUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("SCANCORE/") + QCoreApplication::applicationVersion().toUtf8());
    auto *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, ready] {
        reply->deleteLater();
        const auto signature = reply->readAll();
        QFile file(path);
        const bool good = reply->error() == QNetworkReply::NoError && file.open(QIODevice::ReadOnly) &&
                          ed25519::verify(signature.trimmed().size() == 88 ? QByteArray::fromBase64(signature.trimmed()) : signature,
                                          kReleaseKey, file);
        file.close();
        if (!good) {
            QFile::remove(path);
            setState(QStringLiteral("error"),
                     reply->error() == QNetworkReply::NoError
                         ? QStringLiteral("The update's signature does not match, so it was not installed.")
                         : QStringLiteral("Could not check the update's signature. It will be retried later."));
            return;
        }
        ready();
    });
}

void Updater::install() {
    const auto path = downloadPath();
    if (!QFileInfo::exists(path) || m_state != QLatin1String("ready")) {
        download();
        return;
    }
#if defined(Q_OS_ANDROID)
    installAndroid(path);
#elif defined(Q_OS_WIN)
    // The installer closes this app, upgrades it and starts it again.
    if (QProcess::startDetached(path, {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"), QStringLiteral("/NORESTART"),
                                       QStringLiteral("/CLOSEAPPLICATIONS")}))
        QCoreApplication::quit();
    else
        setState(QStringLiteral("error"), QStringLiteral("Could not start the installer."));
#elif defined(Q_OS_MACOS)
    // Replace the bundle once this process has exited, then reopen it.
    auto bundle = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../..")).absolutePath();
    const auto script = QStringLiteral(
        "while kill -0 %1 2>/dev/null; do sleep 0.5; done; "
        "mnt=$(hdiutil attach -nobrowse -noverify -noautoopen \"%2\" | tail -1 | awk -F'\\t' '{print $NF}'); "
        "app=$(ls -d \"$mnt\"/*.app | head -1); "
        "[ -n \"$app\" ] && rm -rf \"%3\" && ditto \"$app\" \"%3\"; "
        "hdiutil detach \"$mnt\" -quiet; open \"%3\"")
                            .arg(QCoreApplication::applicationPid())
                            .arg(path, bundle);
    if (QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script}))
        QCoreApplication::quit();
    else
        setState(QStringLiteral("error"), QStringLiteral("Could not start the update."));
#elif defined(Q_OS_IOS)
    setState(QStringLiteral("error"), QStringLiteral("Sideload the new IPA to update."));
#else
    // AppImage: swap the file in place (the running copy stays mounted) and restart.
    const auto target = qEnvironmentVariable("APPIMAGE");
    const auto staged = target + QStringLiteral(".new");
    QFile::remove(staged);
    if (!QFile::copy(path, staged)) {
        setState(QStringLiteral("error"), QStringLiteral("Could not write next to %1.").arg(target));
        return;
    }
    QFile::setPermissions(staged, QFile::permissions(staged) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    if (::rename(staged.toLocal8Bit().constData(), target.toLocal8Bit().constData()) != 0) {
        QFile::remove(staged);
        setState(QStringLiteral("error"), QStringLiteral("Could not replace %1.").arg(target));
        return;
    }
    QFile::remove(path);
    QProcess::startDetached(target, {});
    QCoreApplication::quit();
#endif
}

void Updater::installAndroid(const QString &apk) {
#ifdef Q_OS_ANDROID
    // Hand the APK to the system installer through Qt's FileProvider.
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto authority = context.callObjectMethod<jstring>("getPackageName").toString() + QStringLiteral(".qtprovider");
    QJniObject file("java/io/File", "(Ljava/lang/String;)V", QJniObject::fromString(apk).object<jstring>());
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider", "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;", context.object(),
        QJniObject::fromString(authority).object<jstring>(), file.object());
    if (!uri.isValid()) {
        setState(QStringLiteral("error"), QStringLiteral("Could not open the installer."));
        return;
    }
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      QJniObject::fromString(QStringLiteral("android.intent.action.VIEW")).object<jstring>());
    intent.callObjectMethod("setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;", uri.object(),
                            QJniObject::fromString(QStringLiteral("application/vnd.android.package-archive")).object<jstring>());
    // FLAG_GRANT_READ_URI_PERMISSION | FLAG_ACTIVITY_NEW_TASK
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(0x00000001 | 0x10000000));
    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
#else
    Q_UNUSED(apk)
#endif
}

void Updater::dismiss() {
    if (m_state != QLatin1String("downloading"))
        setState(QStringLiteral("idle"));
}

} // namespace sc
