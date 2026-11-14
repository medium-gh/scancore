#include "Runtime.h"

#include "core/Http.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSysInfo>
#include <QtCore/private/qzipreader_p.h>

namespace sc::ext {

namespace {

QMutex g_mutex;
QString g_dir;
QString g_base = QStringLiteral("https://github.com/medium-gh/scancore/releases/download");

QString assetName() {
    return QStringLiteral("SCANCORE-extensions-runtime-%1-%2.zip").arg(QLatin1String(Runtime::HostVersion), Runtime::platform());
}

QString hostVersionIn(const QString &dir) {
    QFile file(dir + QStringLiteral("/scancore-runtime.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("host")).toString();
}

void say(const std::function<void(const QString &)> &status, const QString &text) {
    if (status)
        status(text);
}

} // namespace

void Runtime::setDir(const QString &dir) {
    QMutexLocker lock(&g_mutex);
    g_dir = dir;
}

QString Runtime::dir() {
    QMutexLocker lock(&g_mutex);
    return g_dir;
}

void Runtime::setDownloadBase(const QString &base) {
    QMutexLocker lock(&g_mutex);
    g_base = base;
}

QString Runtime::platform() {
    // Android carries its host in the APK; iOS cannot run one.
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return {};
#else
    const auto cpu = QSysInfo::currentCpuArchitecture();
    const QString arch = cpu == QLatin1String("x86_64") ? QStringLiteral("x64") : cpu == QLatin1String("arm64") ? QStringLiteral("arm64") : QString();
    if (arch.isEmpty())
        return {};
#if defined(Q_OS_WIN)
    return arch == QLatin1String("x64") ? QStringLiteral("windows-x64") : QString();
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos-") + arch;
#else
    return arch == QLatin1String("x64") ? QStringLiteral("linux-x64") : QString();
#endif
#endif
}

bool Runtime::installed() { return !dir().isEmpty() && hostVersionIn(dir()) == QLatin1String(HostVersion); }

bool Runtime::outdated() {
    if (dir().isEmpty())
        return false;
    const auto version = hostVersionIn(dir());
    return !version.isEmpty() && version != QLatin1String(HostVersion);
}

QString Runtime::assetUrl() {
    QString base;
    {
        QMutexLocker lock(&g_mutex);
        base = g_base;
    }
    return QStringLiteral("%1/extensions-runtime-%2/%3").arg(base, QLatin1String(HostVersion), assetName());
}

void Runtime::install(const std::function<void(const QString &)> &status, const std::function<void()> &beforeSwap) {
    if (platform().isEmpty())
        throw Error(QStringLiteral("There is no Extensions runtime for this computer."));
    const auto target = dir();
    if (target.isEmpty())
        throw Error(QStringLiteral("The Extensions runtime has nowhere to go."));

    say(status, QStringLiteral("Checking the download…"));
    const auto checksum = Http::get(QUrl(assetUrl() + QStringLiteral(".sha256")), {}, 30000);
    if (!checksum.ok())
        throw Error(QStringLiteral("The Extensions runtime is not available to download yet (HTTP %1).").arg(checksum.status), checksum.status);
    const auto expected = checksum.body.trimmed().split(' ').value(0).toLower();

    say(status, QStringLiteral("Downloading the Extensions runtime…"));
    Request request;
    request.url = QUrl(assetUrl());
    request.timeoutMs = 600000;
    request.maxBytes = 400LL * 1024 * 1024;
    const auto archive = Http::send(request);
    if (!archive.ok())
        throw Error(QStringLiteral("The Extensions runtime could not be downloaded (HTTP %1).").arg(archive.status), archive.status);
    if (QCryptographicHash::hash(archive.body, QCryptographicHash::Sha256).toHex() != expected)
        throw Error(QStringLiteral("The download was damaged. Try again."));

    say(status, QStringLiteral("Unpacking…"));
    const auto staging = target + QStringLiteral(".new");
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);
    {
        QByteArray copy = archive.body;
        QBuffer buffer(&copy);
        buffer.open(QIODevice::ReadOnly);
        QZipReader zip(&buffer);
        if (!zip.isReadable())
            throw Error(QStringLiteral("The download was damaged. Try again."));
        // The zip holds one folder, scancore-extensions-runtime/.
        for (const auto &entry : zip.fileInfoList()) {
            if (CancelScope::cancelled())
                throw Error(QStringLiteral("Cancelled"), 499);
            const auto relative = entry.filePath.section(QLatin1Char('/'), 1);
            if (relative.isEmpty() || relative.contains(QLatin1String("..")))
                continue;
            const auto path = staging + QLatin1Char('/') + relative;
            if (entry.isDir) {
                QDir().mkpath(path);
                continue;
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(zip.fileData(entry.filePath)) < 0)
                throw Error(QStringLiteral("Could not write to the app data folder."));
            out.close();
            // Programs stay runnable.
            if (relative.startsWith(QLatin1String("bin/")) || relative.endsWith(QLatin1String("jspawnhelper")))
                out.setPermissions(out.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
        }
    }
    if (hostVersionIn(staging) != QLatin1String(HostVersion)) {
        QDir(staging).removeRecursively();
        throw Error(QStringLiteral("The download is not the runtime this SCANCORE needs."));
    }

    if (beforeSwap)
        beforeSwap();
    // Swap: the old runtime goes only once the new one is in place.
    const auto old = target + QStringLiteral(".old");
    QDir(old).removeRecursively();
    if (QFileInfo::exists(target) && !QDir().rename(target, old))
        throw Error(QStringLiteral("The old Extensions runtime is in use. Close SCANCORE's other windows and try again."));
    if (!QDir().rename(staging, target)) {
        QDir().rename(old, target);
        throw Error(QStringLiteral("The Extensions runtime could not be installed."));
    }
    QDir(old).removeRecursively();
    say(status, QStringLiteral("Installed"));
}

QString Runtime::pluginRuntimePath(const QString &name) {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/extensions/%1-runtime/%1-runtime-%2.jar").arg(name, QLatin1String(HostVersion));
}

QString Runtime::cloudStreamPath() { return pluginRuntimePath(QStringLiteral("cloudstream")); }
QString Runtime::echoPath() { return pluginRuntimePath(QStringLiteral("echo")); }

void Runtime::installPluginRuntime(const QString &name, const QString &label) {
#if defined(Q_OS_ANDROID)
    const auto path = pluginRuntimePath(name);
    if (QFileInfo::exists(path))
        return;
    QString base;
    {
        QMutexLocker lock(&g_mutex);
        base = g_base;
    }
    const auto url = QStringLiteral("%1/extensions-runtime-%2/SCANCORE-%3-runtime-%2-android.jar").arg(base, QLatin1String(HostVersion), name);
    const auto checksum = Http::get(QUrl(url + QStringLiteral(".sha256")), {}, 30000);
    if (!checksum.ok())
        throw Error(QStringLiteral("%1 support is not available to download yet (HTTP %2).").arg(label).arg(checksum.status), checksum.status);
    Request request;
    request.url = QUrl(url);
    request.timeoutMs = 300000;
    request.maxBytes = 64LL * 1024 * 1024;
    const auto jar = Http::send(request);
    if (!jar.ok())
        throw Error(QStringLiteral("%1 support could not be downloaded (HTTP %2).").arg(label).arg(jar.status), jar.status);
    if (QCryptographicHash::hash(jar.body, QCryptographicHash::Sha256).toHex() != checksum.body.trimmed().split(' ').value(0).toLower())
        throw Error(QStringLiteral("The download was damaged. Try again."));
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Older versions' runtimes go.
    for (const auto &old : QDir(QFileInfo(path).absolutePath()).entryInfoList(QDir::Files)) {
        QFile::setPermissions(old.absoluteFilePath(), QFile::ReadOwner | QFile::WriteOwner);
        QFile::remove(old.absoluteFilePath());
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(jar.body) != jar.body.size() || !file.commit())
        throw Error(QStringLiteral("Could not write to the app data folder."));
    // Code is loaded from read-only files (Android 14 refuses writable ones).
    QFile::setPermissions(path, QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
#else
    Q_UNUSED(name)
    Q_UNUSED(label)
#endif
}

void Runtime::installCloudStream() { installPluginRuntime(QStringLiteral("cloudstream"), QStringLiteral("CloudStream")); }
void Runtime::installEcho() { installPluginRuntime(QStringLiteral("echo"), QStringLiteral("Echo")); }

void Runtime::remove() {
    if (!dir().isEmpty())
        QDir(dir()).removeRecursively();
}

} // namespace sc::ext
