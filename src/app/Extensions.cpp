#include "Extensions.h"

#include "App.h"
#include "core/Http.h"
#include "extensions/ExtensionSource.h"
#ifdef SCANCORE_HAVE_ANDROID_HOST
#include "extensions/JniHost.h"
#endif
#include "extensions/Manager.h"
#include "extensions/ProcessHost.h"
#include "core/Database.h"
#include "core/Parallel.h"
#include "core/Text.h"
#include "extensions/Runtime.h"
#include "sources/anime/Registry.h"
#include "sources/comics/Registry.h"
#include "sources/novels/LnCrawl.h"
#include "sources/novels/Registry.h"

#include <QTimer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QCoreApplication>
#include <QJsonArray>
#include <QLocale>
#include <QMutex>
#include <QRegularExpression>

namespace sc {

namespace {

constexpr const char *kUvVersion = "0.13.0";
constexpr const char *kPythonVersion = "3.13";
constexpr const char *kCrawlerVersion = "lightnovel-crawler==4.14.0";

QString uvAsset() {
#if defined(Q_OS_WIN)
    return QStringLiteral("uv-x86_64-pc-windows-msvc.zip");
#elif defined(Q_OS_MACOS)
    return QSysInfo::currentCpuArchitecture() == QLatin1String("arm64") ? QStringLiteral("uv-aarch64-apple-darwin.tar.gz")
                                                                         : QStringLiteral("uv-x86_64-apple-darwin.tar.gz");
#else
    return QStringLiteral("uv-x86_64-unknown-linux-gnu.tar.gz");
#endif
}

QString runTool(const QString &program, const QStringList &args, const QProcessEnvironment &env, int timeoutMs) {
    QProcess process;
    process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    if (!process.waitForStarted(15000))
        throw Error(QStringLiteral("Could not start %1.").arg(QFileInfo(program).fileName()));
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(250)) {
        if (CancelScope::cancelled() || timer.elapsed() > timeoutMs) {
            process.kill();
            throw Error(CancelScope::cancelled() ? QStringLiteral("Cancelled") : QStringLiteral("The installation took too long."),
                        CancelScope::cancelled() ? 499 : 504);
        }
    }
    const auto output = QString::fromUtf8(process.readAll()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw Error(QStringLiteral("Installation step failed: %1").arg(output.section('\n', -1)));
    return output;
}

QString findFile(const QString &dir, const QString &name) {
    QDirIterator it(dir, {name}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

} // namespace

#ifndef SCANCORE_HOST_DEV_LIB
#define SCANCORE_HOST_DEV_LIB ""
#endif

namespace {

// The extension host's start-up: the installed runtime, else (development
// builds) the host built from host/.
ext::ProcessHost::Launch hostLaunch() {
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    auto launch = ext::ProcessHost::find(ext::Runtime::dir(), QStringLiteral(SCANCORE_HOST_DEV_LIB));
    launch.root = data + QStringLiteral("/extensions/host");
    launch.userAgent = QString::fromUtf8(Http::userAgent());
    launch.appVersion = QCoreApplication::applicationVersion();
    return launch;
}

std::shared_ptr<ext::ProcessHost> processHost() { return std::dynamic_pointer_cast<ext::ProcessHost>(ext::host(ext::Kind::Mihon)); }

} // namespace

Extensions::Extensions(QObject *parent) : QObject(parent) {}

Extensions *Extensions::instance() {
    static auto *extensions = [] {
        auto *created = new Extensions;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return extensions;
}

void Extensions::startHost() {
    // One host runs comic (Mihon) and anime (Aniyomi, CloudStream) extensions.
#if defined(SCANCORE_HAVE_ANDROID_HOST)
    // Built into the APK: nothing to download.
    const std::shared_ptr<ext::Host> host =
        ext::JniHost::create({QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/extensions/host"),
                              QString::fromUtf8(Http::userAgent()), QCoreApplication::applicationVersion()});
    ext::setHost(ext::Kind::Mihon, host);
    ext::setHost(ext::Kind::Aniyomi, host);
    ext::setHost(ext::Kind::CloudStream, host);
    ext::setHost(ext::Kind::Echo, host);
    ext::ExtensionSource::installImageWriter();
#elif !defined(Q_OS_ANDROID)
    ext::Runtime::setDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/extensions/runtime"));
    const auto host = std::make_shared<ext::ProcessHost>(hostLaunch());
    ext::setHost(ext::Kind::Mihon, host);
    ext::setHost(ext::Kind::Aniyomi, host);
    ext::setHost(ext::Kind::CloudStream, host);
    ext::setHost(ext::Kind::Echo, host);
    ext::ExtensionSource::installImageWriter();
    // A runtime from an older SCANCORE runs an older host (issue #14: lib
    // values it could not read, no anime): the user chose to have one, so
    // its replacement downloads by itself.
    if (ext::Runtime::outdated()) {
        auto *update = installRuntime();
        connect(update, &Task::changed, update, [update] {
            if (!update->loading())
                qInfo() << "Extensions runtime update:" << (update->ok() ? QStringLiteral("installed") : update->error());
        });
    }
#endif
    // Development hook: SCANCORE_START {"extensions": {"repo": url,
    // "install": [package or "lnreader:<id>", …]}} adds a repository and
    // installs from it, for smoke tests against a local test repository.
    const auto start = App::instance()->startAction().value(QStringLiteral("extensions")).toMap();
    if (start.isEmpty())
        return;
    auto *task = Task::run([start]() -> QVariant {
        ext::Manager::add(ext::Manager::preview(start.value(QStringLiteral("repo")).toString()));
        // "pkg" (a Mihon extension) or "<kind>:pkg" ("lnreader:fixture").
        for (const auto &pkg : start.value(QStringLiteral("install")).toStringList()) {
            const auto kind = pkg.contains(QLatin1Char(':')) ? ext::kindFrom(pkg.section(QLatin1Char(':'), 0, 0)) : std::nullopt;
            ext::Manager::install(kind.value_or(ext::Kind::Mihon), kind ? pkg.section(QLatin1Char(':'), 1) : pkg);
        }
        return true;
    });
    connect(task, &Task::changed, task, [task] {
        if (!task->loading())
            qInfo() << "Start extensions:" << (task->ok() ? QStringLiteral("installed") : task->error());
    });
}

void Extensions::stopHost() {
    if (auto host = processHost())
        host->stop();
    ext::setHost(ext::Kind::Mihon, nullptr);
    ext::setHost(ext::Kind::Aniyomi, nullptr);
    ext::setHost(ext::Kind::CloudStream, nullptr);
    ext::setHost(ext::Kind::Echo, nullptr);
}

bool Extensions::runtimeAvailable() const { return !ext::Runtime::platform().isEmpty(); }

bool Extensions::runtimeInstalled() const {
    auto host = ext::host(ext::Kind::Mihon);
    return host && host->available();
}

Task *Extensions::installRuntime() {
    m_busy = true;
    m_busyWith = QStringLiteral("runtime");
    m_status = QStringLiteral("Starting…");
    emit changed();
    auto *task = Task::run([this]() -> QVariant {
        // The running host holds the old runtime's files: it keeps working
        // while the new one downloads and stops just before the swap.
        ext::Runtime::install([this](const QString &status) { setStatus(status); }, [] {
            if (auto host = processHost())
                host->stop();
        });
        if (auto host = processHost())
            host->setLaunch(hostLaunch());
        return true;
    });
    connect(task, &Task::changed, this, [this, task] {
        if (task->loading())
            return;
        m_busy = false;
        m_status = task->ok() ? QString() : task->error();
        emit changed();
    });
    return task;
}

void Extensions::removeRuntime() {
    if (auto host = processHost())
        host->stop();
    ext::Runtime::remove();
    if (auto host = processHost())
        host->setLaunch(hostLaunch());
    emit changed();
}

bool Extensions::novelPackAvailable() const {
#ifdef SCANCORE_DISABLE_NOVEL_PACK
    return false;
#else
    return true;
#endif
}

bool Extensions::novelPackInstalled() const {
    return novelPackAvailable() && novels::LnCrawlRuntime::installed();
}

void Extensions::setStatus(const QString &status) {
    QMetaObject::invokeMethod(this, [this, status] {
        m_status = status;
        emit changed();
    });
}

Task *Extensions::installNovelPack() {
    if (!novelPackAvailable())
        return Task::run([]() -> QVariant { throw Error(QStringLiteral("The extended novel pack is disabled in this build.")); });
    m_busy = true;
    m_busyWith = QStringLiteral("novels");
    m_status = QStringLiteral("Starting…");
    emit changed();
    auto *task = Task::run([this]() -> QVariant {
        using novels::LnCrawlRuntime;
        const auto root = LnCrawlRuntime::root();
        QDir(root).removeRecursively();
        QDir().mkpath(root);

        setStatus(QStringLiteral("Downloading the installer…"));
        Request request;
        request.url = QUrl(QStringLiteral("https://github.com/astral-sh/uv/releases/download/%1/%2")
                               .arg(QString::fromLatin1(kUvVersion), uvAsset()));
        request.timeoutMs = 120000;
        request.maxBytes = 80 * 1024 * 1024;
        const auto archive = Http::send(request);
        if (!archive.ok())
            throw Error(QStringLiteral("Could not download the installer (HTTP %1).").arg(archive.status));
        const auto archivePath = root + QLatin1Char('/') + uvAsset();
        QSaveFile out(archivePath);
        if (!out.open(QIODevice::WriteOnly) || out.write(archive.body) != archive.body.size() || !out.commit())
            throw Error(QStringLiteral("Could not write to the app data folder."));
        const auto uvDir = root + QStringLiteral("/uv");
        QDir().mkpath(uvDir);
        runTool(QStringLiteral("tar"), {QStringLiteral("-xf"), archivePath, QStringLiteral("-C"), uvDir},
                QProcessEnvironment::systemEnvironment(), 120000);
#ifdef Q_OS_WIN
        const auto uv = findFile(uvDir, QStringLiteral("uv.exe"));
#else
        const auto uv = findFile(uvDir, QStringLiteral("uv"));
#endif
        if (uv.isEmpty())
            throw Error(QStringLiteral("The installer archive was not as expected."));
        QFile::setPermissions(uv, QFile::permissions(uv) | QFileDevice::ExeOwner);

        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("UV_PYTHON_INSTALL_DIR"), root + QStringLiteral("/python"));
        env.insert(QStringLiteral("UV_CACHE_DIR"), root + QStringLiteral("/cache"));
        env.insert(QStringLiteral("UV_NO_CONFIG"), QStringLiteral("1"));
        env.insert(QStringLiteral("UV_PYTHON_PREFERENCE"), QStringLiteral("only-managed"));

        setStatus(QStringLiteral("Installing Python…"));
        runTool(uv, {QStringLiteral("python"), QStringLiteral("install"), QString::fromLatin1(kPythonVersion)}, env, 600000);
        const auto python =
            runTool(uv, {QStringLiteral("python"), QStringLiteral("find"), QString::fromLatin1(kPythonVersion)}, env, 60000)
                .section('\n', -1)
                .trimmed();
        if (python.isEmpty() || !QFile::exists(python))
            throw Error(QStringLiteral("Python was not installed correctly."));

        setStatus(QStringLiteral("Installing the novel sources…"));
        runTool(uv,
                {QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("--python"), python, QStringLiteral("--target"),
                 LnCrawlRuntime::vendor(), QString::fromLatin1(kCrawlerVersion)},
                env, 900000);

        QFile::remove(LnCrawlRuntime::bridge());
        if (!QFile::copy(QStringLiteral(":/lncrawl/bridge.py"), LnCrawlRuntime::bridge()))
            throw Error(QStringLiteral("Could not write the source bridge."));
        QFile::setPermissions(LnCrawlRuntime::bridge(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        setStatus(QStringLiteral("Checking the installation…"));
        auto check = QProcessEnvironment::systemEnvironment();
        check.insert(QStringLiteral("PYTHONPATH"), LnCrawlRuntime::vendor());
        runTool(python, {QStringLiteral("-s"), QStringLiteral("-c"), QStringLiteral("import lncrawl.core, scraper")}, check, 120000);

        QSaveFile marker(root + QStringLiteral("/python-path.txt"));
        if (!marker.open(QIODevice::WriteOnly) || marker.write(python.toUtf8()) < 0 || !marker.commit())
            throw Error(QStringLiteral("Could not write to the app data folder."));
        QDir(root + QStringLiteral("/cache")).removeRecursively();
        QDir(uvDir).removeRecursively();
        QFile::remove(archivePath);
        novels::Registry::rebuild();
        return QVariantMap{{"sources", novels::Registry::all().size()}};
    });
    connect(task, &Task::changed, this, [this, task] {
        if (task->loading())
            return;
        m_busy = false;
        m_status = task->ok() ? QString() : task->error();
        emit changed();
    });
    return task;
}

void Extensions::removeNovelPack() {
    if (!novelPackAvailable())
        return;
    QDir(novels::LnCrawlRuntime::root()).removeRecursively();
    novels::Registry::rebuild();
    emit changed();
}

// ---------------------------------------------------------------- the Extensions page

namespace {

QMutex g_previewMutex;
QHash<QString, ext::RepositoryPreview> g_previews;

std::optional<ext::Kind> kindOf(const QString &name) { return name.isEmpty() ? std::nullopt : ext::kindFrom(name); }

ext::Kind requireKind(const QString &name) {
    const auto kind = ext::kindFrom(name);
    if (!kind)
        throw Error(QStringLiteral("Unknown kind of extension."));
    return *kind;
}

// Language names as the extension lists show them: "Português (Brasil)",
// "Multi" for extensions with sources in several languages.
QString extensionLanguage(const QString &code) {
    const auto lower = code.toLower();
    if (lower.isEmpty() || lower == QLatin1String("all") || lower == QLatin1String("multi"))
        return QStringLiteral("Multi");
    QString name = languageName(code);
    if (name == code) {
        const QLocale locale(code);
        if (locale.language() != QLocale::C)
            name = locale.nativeLanguageName();
    }
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    if (code.contains(QLatin1Char('-')) && name != code) {
        const QLocale locale(QString(code).replace(QLatin1Char('-'), QLatin1Char('_')));
        const auto region = code.section(QLatin1Char('-'), 1);
        const auto territory = region == QLatin1String("419") ? QStringLiteral("Latinoamérica")
                               : locale.territory() != QLocale::AnyTerritory ? locale.nativeTerritoryName()
                                                                            : region;
        name += QStringLiteral(" (%1)").arg(territory);
    }
    return name;
}

QString warningOf(const ext::Package &p) { return p.warning == ext::ContentWarning::Nsfw ? QStringLiteral("nsfw") : QString(); }

QString libProblemText(const ext::Package &p) {
    switch (ext::libSupport(p.kind, p.lib)) {
    case ext::LibSupport::Supported:
        return p.torrent ? QStringLiteral("Needs a torrent client") : QString();
    case ext::LibSupport::TooNew: return QStringLiteral("Needs a newer SCANCORE");
    case ext::LibSupport::TooOld: return QStringLiteral("Too old for SCANCORE");
    case ext::LibSupport::Unknown: return QStringLiteral("Unknown extension version");
    }
    return {};
}

QVariantMap packageMap(const ext::Package &p) {
    return QVariantMap{{"id", ext::kindName(p.kind) + QLatin1Char(':') + p.pkg},
                       {"kind", ext::kindName(p.kind)},
                       {"media", ext::kindMedia(p.kind)},
                       {"pkg", p.pkg},
                       {"name", p.name},
                       {"version", p.versionName},
                       {"lang", p.lang},
                       {"languageName", extensionLanguage(p.lang)},
                       {"icon", p.iconUrl},
                       {"nsfw", p.warning == ext::ContentWarning::Nsfw},
                       {"problem", libProblemText(p)}};
}

QString domainOf(const QString &url) {
    auto host = QUrl(url).host();
    if (host.startsWith(QLatin1String("www.")))
        host = host.mid(4);
    return host;
}

} // namespace

void Extensions::notifyExtensionsChanged() {
    ++m_revision;
    emit extensionsChanged();
}

QString Extensions::hostLog() const {
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/extensions/host.log");
    return QFileInfo(path).size() > 0 ? QUrl::fromLocalFile(path).toString() : QString();
}

Task *Extensions::diagnostics() {
    const auto logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/extensions/host.log");
    const bool installed = runtimeInstalled();
    const bool available = runtimeAvailable();
    return Task::run([logPath, installed, available]() -> QVariant {
        QVariantMap out{{"hostVersion", QString::fromLatin1(ext::Runtime::HostVersion)},
                        {"runtimeAvailable", available},
                        {"runtimeInstalled", installed},
                        {"system", QSysInfo::prettyProductName() + QStringLiteral(" · ") + QSysInfo::currentCpuArchitecture()},
                        {"qt", QString::fromLatin1(qVersion())}};
        if (installed) {
            try {
                const auto host = ext::requireHost(ext::Kind::Mihon);
                QElapsedTimer clock;
                clock.start();
                out.insert(QStringLiteral("host"), host->call(QStringLiteral("hello"), {}).toObject().toVariantMap());
                out.insert(QStringLiteral("hostAnswerMs"), clock.elapsed());
                out.insert(QStringLiteral("networkUse"), host->call(QStringLiteral("networkUse"), {}).toObject().toVariantMap());
            } catch (const Error &e) {
                out.insert(QStringLiteral("hostError"), e.message());
            } catch (const std::exception &e) {
                out.insert(QStringLiteral("hostError"), QString::fromUtf8(e.what()));
            }
        }
        // The last lines of the host's log (it can be long).
        QFile log(logPath);
        if (log.open(QIODevice::ReadOnly)) {
            if (log.size() > 64 * 1024)
                log.seek(log.size() - 64 * 1024);
            auto lines = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'));
            if (lines.size() > 120)
                lines = lines.mid(lines.size() - 120);
            out.insert(QStringLiteral("log"), lines.join(QLatin1Char('\n')).trimmed());
        }
        return out;
    });
}

QStringList Extensions::installing() const { return QStringList(m_installing.begin(), m_installing.end()); }

int Extensions::updateCount() const {
    // Read by many bindings at once: counted once per change.
    if (m_updateCountAt != m_revision) {
        m_updateCount = int(ext::Manager::updates().size());
        m_updateCountAt = m_revision;
    }
    return m_updateCount;
}

qint64 Extensions::checkedAt() const { return Database::pref(QStringLiteral("extensions.checkedAt"), 0).toLongLong(); }

void Extensions::setInstalling(const QString &id, bool on) {
    QMetaObject::invokeMethod(this, [this, id, on] {
        if (on)
            m_installing.insert(id);
        else
            m_installing.remove(id);
        emit extensionsChanged();
    });
}

QVariantList Extensions::repositories() const {
    QVariantList out;
    for (const auto &r : ext::Manager::repositoryStatus())
        out.append(QVariantMap{{"url", r.repo.url},
                               {"name", r.repo.name.isEmpty() ? domainOf(r.repo.url) : r.repo.name},
                               {"kind", ext::kindName(r.repo.kind)},
                               {"media", ext::kindMedia(r.repo.kind)},
                               {"website", r.repo.website},
                               {"domain", domainOf(r.repo.url)},
                               {"count", r.count},
                               {"error", r.error}});
    return out;
}

QVariantMap Extensions::repositoryLink(const QString &text) const {
    const auto trimmed = text.trimmed();
    if (const auto link = ext::Manager::parseLink(trimmed))
        return QVariantMap{{"url", link->url}, {"kind", link->kind ? ext::kindName(*link->kind) : QString()}};
    // "owner/repo": a GitHub repository.
    static const QRegularExpression shorthand(QStringLiteral(R"(^[A-Za-z0-9-]+/[A-Za-z0-9._-]+/?$)"));
    if (shorthand.match(trimmed).hasMatch() && !trimmed.section(QLatin1Char('/'), 0, 0).contains(QLatin1Char('.')))
        return QVariantMap{{"url", QStringLiteral("https://github.com/") + trimmed}, {"kind", QString()}};
    // A CloudStream repository code (a word, or "!word").
    static const QRegularExpression code(QStringLiteral(R"(^!?[A-Za-z0-9_-]+$)"));
    if (code.match(trimmed).hasMatch())
        return QVariantMap{{"url", trimmed}, {"kind", QStringLiteral("cloudstream")}};
    // A plain address: an index, a repo.json, a store or a plugin list.
    const QUrl url = QUrl::fromUserInput(trimmed);
    if (trimmed.contains(QLatin1Char(' ')) || !url.isValid() || url.host().isEmpty() || !url.host().contains(QLatin1Char('.')) ||
        (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")))
        return {};
    return QVariantMap{{"url", trimmed}, {"kind", QString()}};
}

Task *Extensions::previewRepository(const QString &link, const QString &kind) {
    return Task::run([link, kind]() -> QVariant {
        auto target = link.trimmed();
        auto hint = kindOf(kind);
        if (const auto parsed = ext::Manager::parseLink(target)) {
            target = parsed->url;
            if (!hint)
                hint = parsed->kind;
        }
        const auto preview = ext::Manager::preview(target, hint);
        {
            QMutexLocker lock(&g_previewMutex);
            g_previews.insert(preview.repo.url, preview);
        }
        // Languages by how many extensions each has, content language first.
        const auto language = contentLanguage();
        QHash<QString, int> counts;
        for (const auto &p : preview.packages)
            counts[baseLanguage(p.lang)] += 1;
        QStringList codes = counts.keys();
        std::sort(codes.begin(), codes.end(), [&](const QString &a, const QString &b) {
            if ((a == language) != (b == language))
                return a == language;
            return counts.value(a) > counts.value(b);
        });
        QVariantList languages;
        for (const auto &code : codes)
            languages.append(QVariantMap{{"value", code}, {"label", extensionLanguage(code)}, {"count", counts.value(code)}});
        QStringList autoNames;
        for (const auto &p : preview.autoInstall)
            autoNames.append(p.name);
        return QVariantMap{{"url", preview.repo.url},
                           {"name", preview.repo.name.isEmpty() ? domainOf(preview.repo.url) : preview.repo.name},
                           {"domain", domainOf(preview.repo.url)},
                           {"website", preview.repo.website},
                           {"kind", ext::kindName(preview.repo.kind)},
                           {"media", ext::kindMedia(preview.repo.kind)},
                           {"count", preview.packages.size()},
                           {"languages", languages},
                           {"autoInstall", autoNames},
                           {"language", language},
                           {"languageName", extensionLanguage(language)},
                           {"added", std::ranges::any_of(ext::Manager::repositories(),
                                                         [&](const ext::Repository &r) { return r.url == preview.repo.url; })}};
    });
}

Task *Extensions::addRepository(const QString &url, bool install) {
    return Task::run([this, url, install]() -> QVariant {
        std::optional<ext::RepositoryPreview> preview;
        {
            QMutexLocker lock(&g_previewMutex);
            if (g_previews.contains(url))
                preview = g_previews.take(url);
        }
        if (!preview)
            preview = ext::Manager::preview(url);
        const auto repo = ext::Manager::add(*preview);
        QList<ext::Package> wanted;
        if (install)
            for (const auto &p : preview->autoInstall)
                if (!ext::Manager::installedPackage(p.kind, p.pkg))
                    wanted.append(p);
        if (!wanted.isEmpty())
            QMetaObject::invokeMethod(this, [this, wanted] { installInBackground(wanted); });
        return QVariantMap{{"name", repo.name.isEmpty() ? domainOf(repo.url) : repo.name},
                           {"media", ext::kindMedia(repo.kind)},
                           {"queued", int(wanted.size())}};
    });
}

// Installs a few at a time on worker threads; the page shows how far it got.
void Extensions::installInBackground(const QList<ext::Package> &packages) {
    if (m_queueBatches++ == 0) {
        m_stopQueue = false;
        m_queueTotal = 0;
        m_queueDone = 0;
    }
    m_queueTotal += int(packages.size());
    emit queueChanged();
    runInBackground([this, packages] {
        QList<std::function<bool()>> jobs;
        for (const auto &p : packages)
            jobs.append([this, p] {
                if (m_stopQueue)
                    return false;
                const auto id = ext::kindName(p.kind) + QLatin1Char(':') + p.pkg;
                setInstalling(id, true);
                try {
                    ext::Manager::install(p.kind, p.pkg);
                } catch (...) {
                    setInstalling(id, false);
                    ++m_queueDone;
                    queueProgress();
                    throw;
                }
                setInstalling(id, false);
                ++m_queueDone;
                queueProgress();
                return true;
            });
        int installed = 0;
        int failed = 0;
        QString firstError;
        for (const auto &r : parallel(jobs)) {
            if (!r.ok()) {
                ++failed;
                if (firstError.isEmpty())
                    firstError = r.error;
            } else if (*r.value) {
                ++installed;
            }
        }
        QMetaObject::invokeMethod(this, [this, installed, failed, firstError] {
            if (--m_queueBatches == 0) {
                m_queueTotal = 0;
                m_queueDone = 0;
                emit queueChanged();
            }
            emit installsFinished(installed, failed, firstError);
        });
    });
}

// At most a few times a second, however fast installs finish.
void Extensions::queueProgress() {
    if (m_progressPending.exchange(true))
        return;
    QMetaObject::invokeMethod(this, [this] {
        QTimer::singleShot(250, this, [this] {
            m_progressPending = false;
            emit queueChanged();
        });
    });
}

void Extensions::stopInstalls() { m_stopQueue = true; }

void Extensions::removeRepository(const QString &url) { ext::Manager::removeRepository(url); }

Task *Extensions::refreshRepositories() {
    return Task::run([this]() -> QVariant {
        ext::Manager::refreshAll();
        QMetaObject::invokeMethod(this, [this] { notifyExtensionsChanged(); });
        return int(ext::Manager::updates().size());
    });
}

namespace {

QStringList pinnedKeys() { return App::instance()->settings().value(QStringLiteral("pinnedSources")).toStringList(); }

void savePinned(QStringList keys) {
    keys.removeDuplicates();
    App::instance()->setSetting(QStringLiteral("pinnedSources"), keys);
}

QVariantMap sourceMap(const ext::SourceEntry &s, const QString &language) {
    return QVariantMap{{"key", s.key},
                       {"pinned", pinnedKeys().contains(s.key)},
                       {"name", s.name},
                       {"lang", s.lang},
                       {"languageName", extensionLanguage(s.lang)},
                       {"preferred", baseLanguage(s.lang) == language},
                       {"baseUrl", s.baseUrl},
                       {"enabled", s.enabled},
                       {"blocked", s.blocked},
                       {"error", ext::Manager::sourceError(s.key)}};
}

} // namespace

// Every installed extension with its sources (`brief`: their count, how
// many fail, and their names and sites as one text for searching, not the
// list: 1,000 sources as JavaScript objects held Extensions' first frame
// back on phones).
QVariantList Extensions::installedRows(const QString &kind, bool brief, const QString &query) const {
    const auto language = contentLanguage();
    const auto q = query.trimmed().toLower();
    // The sources by their extension, in one pass.
    QHash<QString, QList<ext::SourceEntry>> byExtension;
    for (const auto &s : ext::Manager::sources(kindOf(kind)))
        byExtension[ext::kindName(s.kind) + QLatin1Char(':') + s.pkg].append(s);
    QVariantList out;
    // Pinned: any of its sources is.
    const auto pinnedList = pinnedKeys();
    const QSet<QString> pinned(pinnedList.begin(), pinnedList.end());
    QSet<QString> pinnedExtensions;
    for (auto it = byExtension.cbegin(); it != byExtension.cend(); ++it)
        for (const auto &s : it.value())
            if (pinned.contains(s.key))
                pinnedExtensions.insert(it.key());
    const auto idOf = [](const ext::Installed &i) { return ext::kindName(i.package.kind) + QLatin1Char(':') + i.package.pkg; };
    auto list = ext::Manager::installed(kindOf(kind));
    std::sort(list.begin(), list.end(), [&](const ext::Installed &a, const ext::Installed &b) {
        // Updates and extensions waiting for trust first, then pinned ones.
        const bool aNeeds = a.update || !a.trusted, bNeeds = b.update || !b.trusted;
        if (aNeeds != bNeeds)
            return aNeeds;
        const bool aPinned = pinnedExtensions.contains(idOf(a)), bPinned = pinnedExtensions.contains(idOf(b));
        if (aPinned != bPinned)
            return aPinned;
        return QString::localeAwareCompare(a.package.name, b.package.name) < 0;
    });
    for (const auto &i : list) {
        auto map = packageMap(i.package);
        map.insert(QStringLiteral("trusted"), i.trusted);
        map.insert(QStringLiteral("update"), i.update ? i.update->versionName : QString());
        map.insert(QStringLiteral("repo"), i.repoUrl);
        map.insert(QStringLiteral("signer"), i.signers.isEmpty() ? QString() : i.signers.last().left(16));
        if (map.value(QStringLiteral("problem")).toString().isEmpty() && i.lib != ext::LibSupport::Supported)
            map.insert(QStringLiteral("problem"), libProblemText(i.package));
        const auto own = byExtension.value(map.value(QStringLiteral("id")).toString());
        map.insert(QStringLiteral("pinned"), pinnedExtensions.contains(map.value(QStringLiteral("id")).toString()));
        if (brief) {
            int broken = 0;
            QStringList text;
            for (const auto &s : own) {
                broken += ext::Manager::sourceError(s.key).isEmpty() ? 0 : 1;
                text << s.name << s.baseUrl;
            }
            const auto sourceText = text.join(QLatin1Char(' ')).toLower();
            if (!q.isEmpty() && !i.package.name.toLower().contains(q) && !sourceText.contains(q))
                continue;
            map.insert(QStringLiteral("sourceCount"), int(own.size()));
            map.insert(QStringLiteral("brokenCount"), broken);
            map.insert(QStringLiteral("sourceText"), sourceText);
        } else {
            QVariantList sources;
            for (const auto &s : own)
                sources.append(sourceMap(s, language));
            map.insert(QStringLiteral("sources"), sources);
        }
        out.append(map);
    }
    return out;
}

QVariantList Extensions::installed(const QString &kind) const { return installedRows(kind, false); }
QVariantList Extensions::installedBrief(const QString &kind, const QString &query) const { return installedRows(kind, true, query); }

QVariantList Extensions::installedSources(const QString &id) const {
    const auto language = contentLanguage();
    QVariantList out;
    for (const auto &s : ext::Manager::sources(kindOf(id.section(QLatin1Char(':'), 0, 0))))
        if (ext::kindName(s.kind) + QLatin1Char(':') + s.pkg == id)
            out.append(sourceMap(s, language));
    return out;
}

int Extensions::installedCount(const QString &kind) const { return int(ext::Manager::installed(kindOf(kind)).size()); }

QVariantList Extensions::available(const QString &kind) const {
    const auto language = contentLanguage();
    const bool adult = Database::pref(QStringLiteral("setting.showNsfwSources"), false).toBool();
    QSet<QString> have;
    for (const auto &i : ext::Manager::installed(kindOf(kind)))
        have.insert(ext::kindName(i.package.kind) + QLatin1Char(':') + i.package.pkg);
    struct Row {
        bool preferred;
        QString language;
        QString name;
        QVariantMap map;
    };
    QList<Row> rows;
    QStringList repos;
    const auto packages = ext::Manager::available(kindOf(kind), &repos);
    for (int i = 0; i < packages.size(); ++i) {
        const auto &p = packages[i];
        auto map = packageMap(p);
        map.insert(QStringLiteral("repo"), repos.value(i));
        if (have.contains(map.value(QStringLiteral("id")).toString()) || (!adult && p.warning == ext::ContentWarning::Nsfw))
            continue;
        const auto base = baseLanguage(p.lang);
        rows.append({base == language, map.value(QStringLiteral("languageName")).toString(), p.name, map});
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.preferred != b.preferred)
            return a.preferred;
        if (a.language != b.language)
            return QString::localeAwareCompare(a.language, b.language) < 0;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    QVariantList out;
    out.reserve(rows.size());
    for (auto &r : rows) {
        r.map.insert(QStringLiteral("preferred"), r.preferred);
        out.append(r.map);
    }
    return out;
}

QVariantList Extensions::availableRows(const QVariantMap &filter) const {
    const auto q = filter.value(QStringLiteral("query")).toString().trimmed().toLower();
    const auto language = filter.value(QStringLiteral("language")).toString();
    const auto media = filter.value(QStringLiteral("media")).toString();
    const auto repo = filter.value(QStringLiteral("repo")).toString();
    QVariantList out;
    QString last;
    for (const auto &row : available()) {
        auto map = row.toMap();
        const auto rowLanguage = map.value(QStringLiteral("languageName")).toString();
        if ((!language.isEmpty() && rowLanguage != language) || (!media.isEmpty() && map.value(QStringLiteral("media")).toString() != media) ||
            (!repo.isEmpty() && map.value(QStringLiteral("repo")).toString() != repo) ||
            (!q.isEmpty() && !map.value(QStringLiteral("name")).toString().toLower().contains(q)))
            continue;
        map.insert(QStringLiteral("heading"), out.isEmpty() || rowLanguage != last);
        last = rowLanguage;
        out.append(map);
    }
    return out;
}

QVariantMap Extensions::availableSummary() const {
    const auto rows = available();
    QStringList languages;
    for (const auto &row : rows)
        if (const auto name = row.toMap().value(QStringLiteral("languageName")).toString(); !languages.contains(name))
            languages.append(name);
    return {{QStringLiteral("count"), int(rows.size())}, {QStringLiteral("languages"), languages}};
}

Task *Extensions::install(const QString &kind, const QString &pkg) {
    return Task::run([this, kind, pkg]() -> QVariant {
        const auto k = requireKind(kind);
        const auto id = kind + QLatin1Char(':') + pkg;
        setInstalling(id, true);
        try {
            const auto installed = ext::Manager::install(k, pkg);
            setInstalling(id, false);
            return QVariantMap{{"name", installed.package.name}, {"trusted", installed.trusted}};
        } catch (...) {
            setInstalling(id, false);
            throw;
        }
    });
}

void Extensions::uninstall(const QString &kind, const QString &pkg) {
    if (const auto k = ext::kindFrom(kind))
        ext::Manager::uninstall(*k, pkg);
}

void Extensions::trust(const QString &kind, const QString &pkg) {
    if (const auto k = ext::kindFrom(kind))
        ext::Manager::trust(*k, pkg);
}

Task *Extensions::updateAll() {
    return Task::run([this]() -> QVariant {
        QList<std::function<bool()>> jobs;
        for (const auto &i : ext::Manager::updates())
            jobs.append([this, i] {
                const auto id = ext::kindName(i.package.kind) + QLatin1Char(':') + i.package.pkg;
                setInstalling(id, true);
                try {
                    ext::Manager::install(i.package.kind, i.package.pkg);
                } catch (...) {
                    setInstalling(id, false);
                    throw;
                }
                setInstalling(id, false);
                return true;
            });
        const auto results = parallel(jobs);
        int installed = 0;
        QString error;
        for (const auto &r : results)
            if (r.ok())
                ++installed;
            else if (error.isEmpty())
                error = r.error;
        if (installed == 0 && !error.isEmpty())
            throw Error(error);
        return QVariantMap{{"installed", installed}};
    });
}

void Extensions::setSourceEnabled(const QString &key, bool enabled) { ext::Manager::setSourceEnabled(key, enabled); }

void Extensions::setPinned(const QString &key, bool pinned) {
    auto keys = pinnedKeys();
    keys.removeAll(key);
    if (pinned)
        keys.append(key);
    savePinned(keys);
    notifyExtensionsChanged();
}

void Extensions::setExtensionPinned(const QString &id, bool pinned) {
    auto keys = pinnedKeys();
    for (const auto &s : ext::Manager::sources(kindOf(id.section(QLatin1Char(':'), 0, 0))))
        if (ext::kindName(s.kind) + QLatin1Char(':') + s.pkg == id) {
            keys.removeAll(s.key);
            if (pinned)
                keys.append(s.key);
        }
    savePinned(keys);
    notifyExtensionsChanged();
}

Task *Extensions::sourceSettings(const QString &key) {
    return Task::run([key]() -> QVariant {
        if (auto novel = novels::Registry::byKey(key))
            return novel->preferences().toVariantList();
        if (auto show = anime::Registry::byKey(key))
            return show->preferences().toVariantList();
        auto source = comics::Registry::byKey(key);
        if (!source)
            throw Error(QStringLiteral("That source is not installed."));
        return source->preferences().toVariantList();
    });
}

Task *Extensions::setSourceSetting(const QString &key, const QString &setting, const QVariant &input) {
    // A multiselect's choices are a JS array (#21).
    const auto value = App::plain(input);
    return Task::run([key, setting, value]() -> QVariant {
        if (auto novel = novels::Registry::byKey(key)) {
            novel->setPreference(setting, QJsonValue::fromVariant(value));
            return novel->preferences().toVariantList();
        }
        if (auto show = anime::Registry::byKey(key)) {
            show->setPreference(setting, QJsonValue::fromVariant(value));
            return show->preferences().toVariantList();
        }
        auto source = comics::Registry::byKey(key);
        if (!source)
            throw Error(QStringLiteral("That source is not installed."));
        source->setPreference(setting, QJsonValue::fromVariant(value));
        // The source may list other settings now (one switch shows more).
        return source->preferences().toVariantList();
    });
}

} // namespace sc
