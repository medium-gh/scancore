#include "Manager.h"

#include "Apk.h"
#include "Host.h"
#include "Index.h"
#include "Runtime.h"
#include "core/Database.h"
#include "core/Http.h"
#include "core/Parallel.h"
#include "core/Text.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVersionNumber>
#include <QSet>
#include <QUrlQuery>
#include <QWaitCondition>

namespace sc::ext {

namespace {

struct RepoState {
    Repository repo;
    QList<Package> packages;
    QString etag;
    QString modified;
    QString error;
};

struct Trust {
    qint64 versionCode = 0;
    QString signer;
};

struct State {
    bool loaded = false;
    QList<RepoState> repos;
    QHash<QString, Installed> installed;          // kind:pkg
    QHash<QString, Trust> trust;                  // kind:pkg
    QHash<QString, bool> enabled;                 // source key
    QHash<QString, QString> errors;               // source key
};

QReadWriteLock g_lock;
State g_state;
QString g_storage;
QMutex g_callbackMutex;
std::function<void()> g_changed;
// One install of a package at a time; different packages download in
// parallel (a new repository installs many at once).
QMutex g_busyMutex;
QWaitCondition g_busyFree;
QSet<QString> g_busy;

class BusyPackage {
public:
    explicit BusyPackage(QString key) : m_key(std::move(key)) {
        QMutexLocker lock(&g_busyMutex);
        while (g_busy.contains(m_key))
            g_busyFree.wait(&g_busyMutex);
        g_busy.insert(m_key);
    }
    ~BusyPackage() {
        QMutexLocker lock(&g_busyMutex);
        g_busy.remove(m_key);
        g_busyFree.wakeAll();
    }

private:
    QString m_key;
};

QString id(Kind kind, const QString &pkg) { return kindName(kind) + QLatin1Char(':') + pkg; }

QJsonArray packagesJson(const QList<Package> &packages) {
    QJsonArray out;
    for (const auto &p : packages)
        out.append(p.toJson());
    return out;
}

QList<Package> packagesFrom(const QByteArray &json) {
    QList<Package> out;
    for (const auto &value : QJsonDocument::fromJson(json).array())
        out.append(Package::fromJson(value.toObject()));
    return out;
}

QByteArray compact(const QJsonValue &value) {
    return value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                           : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
}

// Loads everything from the database once; callers hold the write lock.
void ensureLoaded() {
    if (g_state.loaded)
        return;
    State state;
    state.loaded = true;
    // Tools and tests without a library have no extensions.
    if (Database::path().isEmpty()) {
        g_state = state;
        return;
    }
    for (const auto &row : Database::query(QStringLiteral(
             "SELECT url, info, packages, etag, modified, error FROM ext_repos ORDER BY added_at"))) {
        const auto r = row.toMap();
        RepoState repo;
        repo.repo = Repository::fromJson(QJsonDocument::fromJson(r.value("info").toByteArray()).object());
        repo.repo.url = r.value("url").toString();
        repo.packages = packagesFrom(r.value("packages").toByteArray());
        repo.etag = r.value("etag").toString();
        repo.modified = r.value("modified").toString();
        repo.error = r.value("error").toString();
        state.repos.append(repo);
    }
    for (const auto &row : Database::query(QStringLiteral(
             "SELECT kind, pkg, package, repo_url, signers, path, installed_at FROM ext_installed"))) {
        const auto r = row.toMap();
        Installed i;
        i.package = Package::fromJson(QJsonDocument::fromJson(r.value("package").toByteArray()).object());
        i.repoUrl = r.value("repo_url").toString();
        i.signers = r.value("signers").toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        i.path = r.value("path").toString();
        i.installedAt = r.value("installed_at").toLongLong();
        state.installed.insert(id(i.package.kind, i.package.pkg), i);
    }
    for (const auto &row : Database::query(QStringLiteral("SELECT kind, pkg, version_code, signer FROM ext_trust"))) {
        const auto r = row.toMap();
        state.trust.insert(r.value("kind").toString() + QLatin1Char(':') + r.value("pkg").toString(),
                           {r.value("version_code").toLongLong(), r.value("signer").toString()});
    }
    for (const auto &row : Database::query(QStringLiteral("SELECT key, enabled, error FROM ext_sources"))) {
        const auto r = row.toMap();
        state.enabled.insert(r.value("key").toString(), r.value("enabled").toBool());
        if (!r.value("error").toString().isEmpty())
            state.errors.insert(r.value("key").toString(), r.value("error").toString());
    }
    state.loaded = true;
    g_state = state;
}

// Read access with the state loaded.
template <typename F>
auto reading(F &&fn) {
    {
        QReadLocker lock(&g_lock);
        if (g_state.loaded)
            return fn(std::as_const(g_state));
    }
    QWriteLocker lock(&g_lock);
    ensureLoaded();
    return fn(std::as_const(g_state));
}

template <typename F>
auto writing(F &&fn) {
    QWriteLocker lock(&g_lock);
    ensureLoaded();
    return fn(g_state);
}

void changed() {
    std::function<void()> callback;
    {
        QMutexLocker lock(&g_callbackMutex);
        callback = g_changed;
    }
    if (callback)
        callback();
}

// Whether `repoUrl` may give `installed` its updates: its own repository,
// or any while that one is no longer added.
bool updateSource(const State &state, const Installed &installed, const QString &repoUrl) {
    if (installed.repoUrl.isEmpty() || installed.repoUrl == repoUrl)
        return true;
    return std::none_of(state.repos.begin(), state.repos.end(),
                        [&](const RepoState &r) { return r.repo.url == installed.repoUrl; });
}

// Signed with a repository's key, or trusted by hand at this version and key.
bool isTrusted(const State &state, const Installed &i) {
    if (isUnsignedKind(i.package.kind))
        return std::any_of(state.repos.begin(), state.repos.end(),
                           [&](const RepoState &r) { return r.repo.kind == i.package.kind && r.repo.url == i.repoUrl; });
    if (i.signers.isEmpty())
        return false;
    for (const auto &r : state.repos)
        if (r.repo.kind == i.package.kind && !r.repo.signingKey.isEmpty() && i.signers.contains(r.repo.signingKey))
            return true;
    const auto t = state.trust.value(id(i.package.kind, i.package.pkg));
    return t.versionCode == i.package.versionCode && t.signer == i.signers.last();
}

bool allowedHost(const QUrl &url) {
    if (url.scheme() == QLatin1String("https"))
        return true;
    // Plain http only for development servers; extension code otherwise
    // comes over https.
    const auto host = url.host();
    return url.scheme() == QLatin1String("http") &&
           (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host.endsWith(QLatin1String(".test")));
}

// The files a repository folder may hold, most likely first: Mihon and
// Aniyomi indexes (and the repo.json beside them, or a store's index.pb),
// and LNReader plugin lists (published under .dist/ on their own branch).
const QStringList kIndexFiles{QStringLiteral("index.min.json"), QStringLiteral("repo.json"), QStringLiteral("index.json"),
                              QStringLiteral("index.pb"),       QStringLiteral(".dist/plugins.min.json"),
                              QStringLiteral("plugins.min.json"), QStringLiteral("plugins.json")};

// A file (its name has an extension: index.min.json, index.pb, a page)
// rather than a folder.
bool looksLikeFile(const QString &path) {
    const auto name = path.section(QLatin1Char('/'), -1);
    return name.contains(QLatin1Char('.')) && !name.endsWith(QLatin1Char('.'));
}

QUrl normalized(QString text) {
    text = text.trimmed();
    // "owner/repo": a GitHub repository.
    static const QRegularExpression shorthand(QStringLiteral(R"(^[A-Za-z0-9-]+/[A-Za-z0-9._-]+/?$)"));
    if (shorthand.match(text).hasMatch() && !text.section(QLatin1Char('/'), 0, 0).contains(QLatin1Char('.')))
        text.prepend(QStringLiteral("https://github.com/"));
    if (!text.contains(QLatin1String("://")))
        text.prepend(QStringLiteral("https://"));
    return QUrl(text);
}

// A GitHub repository's branches, the ones that hold repositories first:
// "repo" (Mihon and Aniyomi publish there), LNReader's "plugins/vX" (newest
// first), then the default and the rest. Empty when GitHub does not say
// (offline, or its API's hourly limit).
QStringList githubBranches(const QString &repo) {
    QStringList names;
    QString fallback;
    try {
        Request request;
        request.url = QUrl(QStringLiteral("https://api.github.com/repos/%1/branches?per_page=100").arg(repo));
        request.headers = {{"Accept", "application/vnd.github+json"}};
        request.timeoutMs = 15000;
        const auto response = Http::send(request);
        if (!response.ok())
            return {};
        for (const auto &value : QJsonDocument::fromJson(response.body).array())
            names.append(value.toObject().value("name").toString());
        request.url = QUrl(QStringLiteral("https://api.github.com/repos/%1").arg(repo));
        const auto info = Http::send(request);
        if (info.ok())
            fallback = QJsonDocument::fromJson(info.body).object().value("default_branch").toString();
    } catch (const Error &) {
        return {};
    }
    QStringList plugins;
    for (const auto &name : std::as_const(names))
        if (name.startsWith(QLatin1String("plugins/")))
            plugins.append(name);
    std::sort(plugins.begin(), plugins.end(), [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a.mid(9)) > QVersionNumber::fromString(b.mid(9));
    });
    QStringList out;
    if (names.contains(QLatin1String("repo")))
        out.append(QStringLiteral("repo"));
    out += plugins;
    if (!fallback.isEmpty())
        out.append(fallback);
    for (const auto *name : {"main", "master"})
        if (names.contains(QLatin1String(name)))
            out.append(QLatin1String(name));
    out.removeDuplicates();
    return out;
}

// The files to try for a link, most likely first. A link to a file is that
// file (a GitHub page of one becomes its raw address); a folder, a raw
// branch or a repository's page (GitHub, Codeberg/Forgejo/Gitea, GitLab,
// jsDelivr) becomes every index file on every branch that may hold one.
QList<QUrl> repositoryCandidates(const QString &text) {
    auto url = normalized(text);
    const auto host = url.host().toLower();
    auto parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const auto inFolder = [](const QString &base) {
        QList<QUrl> out;
        for (const auto &file : kIndexFiles)
            out.append(QUrl(base + file));
        return out;
    };
    const bool github = host == QLatin1String("github.com") || host == QLatin1String("www.github.com");
    if (github && parts.size() >= 2) {
        const auto repo = parts.at(0) + QLatin1Char('/') + QString(parts.at(1)).remove(QRegularExpression(QStringLiteral("\\.git$")));
        // github.com/<owner>/<repo>/(blob|raw)/<branch>/<file>: the file itself.
        if (parts.size() > 4 && (parts.at(2) == QLatin1String("blob") || parts.at(2) == QLatin1String("raw")) && looksLikeFile(url.path()))
            return {QUrl(QStringLiteral("https://raw.githubusercontent.com/%1/%2").arg(repo, parts.mid(3).join(QLatin1Char('/'))))};
        QStringList branches;
        QString folder;
        if (parts.size() > 3 && parts.at(2) == QLatin1String("tree")) {
            // A branch name may hold slashes (plugins/v3.0.0): the rest is
            // tried both as more of the branch and as a folder.
            for (int cut = parts.size(); cut > 3; --cut)
                branches.append(parts.mid(3, cut - 3).join(QLatin1Char('/')));
            folder = parts.mid(4).join(QLatin1Char('/'));
        }
        branches += githubBranches(repo);
        branches += {QStringLiteral("repo"), QStringLiteral("main"), QStringLiteral("master")};
        branches.removeDuplicates();
        QList<QUrl> out;
        for (const auto &branch : std::as_const(branches)) {
            out += inFolder(QStringLiteral("https://raw.githubusercontent.com/%1/%2/").arg(repo, branch));
            if (!folder.isEmpty() && !branch.contains(QLatin1Char('/')))
                out += inFolder(QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3/").arg(repo, branch, folder));
        }
        return out;
    }
    if (looksLikeFile(url.path()))
        return {url};
    // A folder: its index files.
    auto base = url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
    if (!base.endsWith(QLatin1Char('/')))
        base += QLatin1Char('/');
    QList<QUrl> out = inFolder(base);
    // raw.githubusercontent.com/<owner>/<repo>: its branches.
    if (host == QLatin1String("raw.githubusercontent.com") && parts.size() == 2) {
        out.clear();
        auto branches = githubBranches(parts.at(0) + QLatin1Char('/') + parts.at(1));
        branches += {QStringLiteral("repo"), QStringLiteral("main"), QStringLiteral("master")};
        branches.removeDuplicates();
        for (const auto &branch : std::as_const(branches))
            out += inFolder(base + branch + QLatin1Char('/'));
    } else if (parts.size() == 2) {
        // A repository's page elsewhere: Codeberg, Forgejo and Gitea serve
        // files at /raw/branch/<branch>/, GitLab at /-/raw/<branch>/.
        for (const auto *branch : {"main", "master", "repo"}) {
            out += inFolder(base + QStringLiteral("raw/branch/%1/").arg(QLatin1String(branch)));
            out += inFolder(base + QStringLiteral("-/raw/%1/").arg(QLatin1String(branch)));
        }
    }
    return out;
}

Response fetch(const QUrl &url, const QString &etag = {}, const QString &modified = {}) {
    if (!url.isValid() || !allowedHost(url))
        throw Error(QStringLiteral("Repositories must be https links."));
    Request request;
    request.url = url;
    request.timeoutMs = 30000;
    request.maxBytes = 64 * 1024 * 1024;
    request.headers = {{"Accept", "application/json, application/x-protobuf, */*"}};
    if (!etag.isEmpty())
        request.headers.append({"If-None-Match", etag.toUtf8()});
    if (!modified.isEmpty())
        request.headers.append({"If-Modified-Since", modified.toUtf8()});
    const auto response = Http::send(request);
    if (response.status == 304)
        return response;
    if (response.status == 404)
        throw Error(QStringLiteral("There is no repository at %1.").arg(url.toString()), 404);
    if (!response.ok())
        throw Error(QStringLiteral("%1 answered HTTP %2.").arg(url.host()).arg(response.status), response.status);
    return response;
}

// Echo's lists name no file or version: each extension's latest GitHub
// release does, as Echo's app reads it (its tag is the version, its first
// .eapk asset the file). Looked up at most once an hour per extension; one
// that cannot be looked up keeps what was known before.
bool resolveEcho(Package &p) {
    if (p.kind != Kind::Echo || p.versionCode > 0)
        return true;
    QUrl url(p.fileUrl);
    static const QRegularExpression github(QStringLiteral(R"(^/repos/([^/]+)/([^/]+))"));
    if (url.host() == QLatin1String("api.github.com")) {
        const auto m = github.match(url.path());
        if (!m.hasMatch())
            return false;
        url = QUrl(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(m.captured(1), m.captured(2)));
    } else if (!allowedHost(url) || !(url.host().endsWith(QLatin1String(".test")) || url.host() == QLatin1String("localhost") ||
                                      url.host() == QLatin1String("127.0.0.1"))) {
        // Echo's app follows GitHub only (a test server stands in for it).
        return false;
    }
    const auto key = QStringLiteral("echo-release|") + url.toString();
    auto body = Database::cacheGet(key);
    if (!body) {
        Request request;
        request.url = url;
        request.timeoutMs = 20000;
        request.headers = {{"Accept", "application/vnd.github+json"}};
        const auto response = Http::send(request);
        if (!response.ok())
            return false;
        body = response.body;
        Database::cachePut(key, *body, 3600);
    }
    const auto doc = QJsonDocument::fromJson(*body);
    // A list of releases (the newest first) or one.
    QJsonObject release = doc.object();
    for (const auto &value : doc.array())
        if (!value.toObject().value("draft").toBool() && !value.toObject().value("prerelease").toBool()) {
            release = value.toObject();
            break;
        }
    QString file;
    for (const auto &value : release.value("assets").toArray()) {
        const auto name = value.toObject().value("name").toString();
        if (name.endsWith(QLatin1String(".eapk")) || (file.isEmpty() && name.endsWith(QLatin1String(".apk"))))
            file = value.toObject().value("browser_download_url").toString();
        if (name.endsWith(QLatin1String(".eapk")))
            break;
    }
    const auto published = QDateTime::fromString(release.value("published_at").toString(), Qt::ISODate);
    if (file.isEmpty() || !published.isValid())
        return false;
    p.fileUrl = url.resolved(QUrl(file)).toString();
    p.versionName = release.value("tag_name").toString();
    p.versionCode = published.toSecsSinceEpoch();
    return true;
}

void resolveEcho(QList<Package> &packages, const QList<Package> &before = {}) {
    QList<std::function<Package()>> jobs;
    for (const auto &p : std::as_const(packages))
        jobs.append([p] {
            auto resolved = p;
            resolveEcho(resolved);
            return resolved;
        });
    const auto results = parallel<Package>(jobs);
    for (int i = 0; i < packages.size(); ++i) {
        if (results[i].ok())
            packages[i] = *results[i].value;
        if (packages[i].versionCode > 0)
            continue;
        for (const auto &old : before)
            if (old.pkg == packages[i].pkg && old.versionCode > 0)
                packages[i] = old;
    }
}

// Reads a repository: follows repo.json, index_v2 and extension lists until
// it has the repository's details and its extensions.
RepoState read(const QUrl &start, std::optional<Kind> hint) {
    RepoState state;
    std::optional<Repository> repo;
    std::optional<QList<Package>> packages;
    QUrl next = start;
    QString listEtag, listModified;
    for (int step = 0; step < 4 && next.isValid(); ++step) {
        const auto response = fetch(next);
        const auto doc = parseIndex(response.body, next, hint);
        if (doc.type == IndexDocument::Type::Unknown)
            throw Error(step == 0 ? QStringLiteral("That link is not an extension or plugin repository.")
                                  : QStringLiteral("The repository's files could not be read."));
        if (doc.repo && !repo)
            repo = doc.repo;
        if (doc.hasPackages && (!packages || doc.type == IndexDocument::Type::ExtensionList ||
                                doc.type == IndexDocument::Type::Store)) {
            packages = doc.packages;
            listEtag = QString::fromUtf8(response.header("ETag"));
            listModified = QString::fromUtf8(response.header("Last-Modified"));
        }
        // A legacy repository with a newer store (index_v2) is read as the
        // store.
        const bool better = doc.type == IndexDocument::Type::LegacyRepo && !doc.follow.endsWith(QLatin1String("index.min.json"));
        if (better) {
            repo.reset();
            packages.reset();
        } else if (repo && packages) {
            break;
        }
        next = doc.follow.isEmpty() ? QUrl() : QUrl(doc.follow);
    }
    if (!repo || !packages)
        throw Error(QStringLiteral("The repository's files could not be read."));
    if (!isUnsignedKind(repo->kind) && repo->signingKey.isEmpty())
        throw Error(QStringLiteral("The repository has no signing key, so its extensions cannot be checked."));
    // One kind per repository: the hint, else what most extensions are.
    // CloudStream's files say what they are, whatever link led there.
    Kind kind = repo->kind == Kind::CloudStream || repo->kind == Kind::Echo ? repo->kind : hint.value_or(repo->kind);
    if (!hint && !isUnsignedKind(repo->kind)) {
        int anime = 0;
        for (const auto &p : *packages)
            anime += p.kind == Kind::Aniyomi;
        kind = anime * 2 > packages->size() ? Kind::Aniyomi : Kind::Mihon;
    }
    repo->kind = kind;
    for (auto &p : *packages)
        p.kind = kind;
    if (kind == Kind::Echo)
        resolveEcho(*packages);
    state.repo = *repo;
    state.packages = *packages;
    state.etag = listEtag;
    state.modified = listModified;
    return state;
}

void storeRepo(const RepoState &r, bool fresh) {
    if (fresh)
        Database::exec(QStringLiteral("INSERT INTO ext_repos(url, kind, info, packages, etag, modified, refreshed_at, added_at) "
                                      "VALUES(?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(url) DO UPDATE SET kind = excluded.kind, "
                                      "info = excluded.info, packages = excluded.packages, etag = excluded.etag, "
                                      "modified = excluded.modified, refreshed_at = excluded.refreshed_at, error = NULL"),
                       {r.repo.url, kindName(r.repo.kind), compact(r.repo.toJson()), compact(packagesJson(r.packages)),
                        r.etag, r.modified, Database::now(), Database::now()});
    else
        Database::exec(QStringLiteral("UPDATE ext_repos SET packages = ?, etag = ?, modified = ?, refreshed_at = ?, error = ? WHERE url = ?"),
                       {compact(packagesJson(r.packages)), r.etag, r.modified, Database::now(),
                        r.error.isEmpty() ? QVariant() : QVariant(r.error), r.repo.url});
}

QString contentBase(const QString &lang) { return baseLanguage(lang); }

bool inLanguage(const QString &lang, const QString &language) {
    const auto base = contentBase(lang);
    return base == language || base == QLatin1String("all") || base == QLatin1String("multi");
}

// On computers a Mihon extension that also comes as a Java jar is installed
// from the jar: the host runs it as it is, while converting the APK's
// Dalvik code fails for builds minified as Keiyoushi's are since lib 1.6.
bool usesJar(const Package &p) {
#ifdef Q_OS_ANDROID
    Q_UNUSED(p)
    return false;
#else
    return p.kind == Kind::Mihon && !p.jarUrl.isEmpty();
#endif
}

QString libProblem(const Package &p) {
    switch (libSupport(p.kind, p.lib)) {
    case LibSupport::Supported: return {};
    case LibSupport::TooNew: return QStringLiteral("%1 needs a newer SCANCORE. Update SCANCORE to use it.").arg(p.name);
    case LibSupport::TooOld: return QStringLiteral("%1 is too old for SCANCORE. Its repository may have a newer version.").arg(p.name);
    case LibSupport::Unknown: return QStringLiteral("%1 uses an extension version SCANCORE does not know (%2).").arg(p.name, p.lib);
    }
    return {};
}

} // namespace

void Manager::setStorage(const QString &folder) { g_storage = folder; }
QString Manager::storage() { return g_storage; }

void Manager::setChangedCallback(std::function<void()> callback) {
    QMutexLocker lock(&g_callbackMutex);
    g_changed = std::move(callback);
}

void Manager::reload() {
    QWriteLocker lock(&g_lock);
    g_state = State();
}

// ---------------------------------------------------------------- repositories

// CloudStream's ways of naming a repository: cloudstreamrepo://<address>,
// https://cs.repo/<address>, and short codes (a word, or "!word"), which
// cutt.ly or py.md redirect to its repo.json. Echo's codes are words too,
// which v.gd redirects to an extension list: tried when CloudStream has no
// repository by that code.
static QString cloudStreamAddress(const QString &text) {
    auto t = text.trimmed();
    static const QRegularExpression prefix(QStringLiteral(R"(^(cloudstreamrepo://|https?://cs\.repo/\??))"), QRegularExpression::CaseInsensitiveOption);
    if (const auto m = prefix.match(t); m.hasMatch()) {
        t = t.mid(m.capturedLength());
        return t.contains(QLatin1String("://")) ? t : QStringLiteral("https://") + t;
    }
    static const QRegularExpression code(QStringLiteral(R"(^!?[A-Za-z0-9_-]+$)"));
    if (!code.match(t).hasMatch())
        return text;
    const auto follow = [](const QUrl &url) -> QString {
        Request request;
        request.url = url;
        request.method = "HEAD";
        request.timeoutMs = 15000;
        const auto response = Http::send(request);
        if (response.finalUrl.host() == url.host())
            return {};
        return response.finalUrl.toString();
    };
    auto target = follow(t.startsWith(QLatin1Char('!')) ? QUrl(QStringLiteral("https://py.md/") + t.mid(1)) : QUrl(QStringLiteral("https://cutt.ly/") + t));
    if (target.isEmpty() && !t.startsWith(QLatin1Char('!')))
        target = follow(QUrl(QStringLiteral("https://v.gd/") + t));
    if (target.isEmpty())
        throw Error(QStringLiteral("No repository has the code %1.").arg(t), 404);
    return target;
}

RepositoryPreview Manager::preview(const QString &original, std::optional<Kind> hint) {
    const auto text = cloudStreamAddress(original);
    const auto first = normalized(text);
    if (!first.isValid() || first.host().isEmpty())
        throw Error(QStringLiteral("That is not a link."));
    auto candidates = repositoryCandidates(text);
    // Which of them exist, asked all at once (a HEAD each); a server that
    // does not answer HEAD keeps its candidate.
    if (candidates.size() > 1) {
        QList<std::function<int()>> checks;
        for (const auto &candidate : std::as_const(candidates))
            checks.append([candidate] {
                Request request;
                request.url = candidate;
                request.method = "HEAD";
                request.timeoutMs = 15000;
                return Http::send(request).status;
            });
        const auto answers = parallel<int>(checks);
        QList<QUrl> existing;
        for (int i = 0; i < candidates.size(); ++i)
            if (!answers[i].ok() || (*answers[i].value != 404 && *answers[i].value != 410 && *answers[i].value != 400))
                existing.append(candidates[i]);
        if (existing.isEmpty())
            throw Error(QStringLiteral("No extension or plugin repository was found at that link. Paste the repository's page, "
                                       "or the link to its index.min.json, repo.json or plugins.min.json."),
                        404);
        candidates = existing;
    }
    RepoState state;
    QString lastError;
    bool found = false;
    for (int i = 0; i < candidates.size() && !found; ++i) {
        try {
            state = read(candidates.at(i), hint);
            found = true;
        } catch (const Error &e) {
            // Not a repository there: the next one.
            if (candidates.size() == 1)
                throw;
            lastError = e.message();
        }
    }
    if (!found)
        throw Error(lastError.isEmpty() ? QStringLiteral("That link is not an extension or plugin repository.") : lastError);
    // A plugin list names no repository: its GitHub owner and name do.
    if (isUnsignedKind(state.repo.kind) && (state.repo.name.isEmpty() || state.repo.name.contains(QLatin1Char('.')))) {
        const QUrl list(state.repo.url);
        const auto path = list.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const auto host = list.host().toLower();
        if ((host == QLatin1String("raw.githubusercontent.com") || host.endsWith(QLatin1String("github.com"))) && path.size() >= 2)
            state.repo.name = path.at(0) + QLatin1Char('/') + path.at(1);
        else if (host == QLatin1String("cdn.jsdelivr.net") && path.size() >= 3)
            state.repo.name = path.at(1) + QLatin1Char('/') + path.at(2).section(QLatin1Char('@'), 0, 0);
        state.repo.badge = state.repo.name;
    }
    RepositoryPreview preview;
    preview.repo = state.repo;
    preview.packages = state.packages;
    preview.autoInstall = autoInstallSelection(state.packages, contentLanguage(),
                                               Database::pref(QStringLiteral("setting.showNsfwSources"), false).toBool());
    preview.repo.listUrl = state.repo.listUrl.isEmpty() ? state.repo.url : state.repo.listUrl;
    return preview;
}

Repository Manager::add(const RepositoryPreview &preview) {
    RepoState state;
    state.repo = preview.repo;
    state.packages = preview.packages;
    storeRepo(state, true);
    writing([&](State &s) {
        s.repos.erase(std::remove_if(s.repos.begin(), s.repos.end(), [&](const RepoState &r) { return r.repo.url == state.repo.url; }),
                      s.repos.end());
        s.repos.append(state);
        return 0;
    });
    changed();
    return state.repo;
}

void Manager::removeRepository(const QString &url) {
    Database::exec(QStringLiteral("DELETE FROM ext_repos WHERE url = ?"), {url});
    writing([&](State &s) {
        s.repos.erase(std::remove_if(s.repos.begin(), s.repos.end(), [&](const RepoState &r) { return r.repo.url == url; }),
                      s.repos.end());
        return 0;
    });
    // Its extensions stay installed; those it vouched for need trust again.
    changed();
}

QList<Repository> Manager::repositories() {
    return reading([](const State &s) {
        QList<Repository> out;
        for (const auto &r : s.repos)
            out.append(r.repo);
        return out;
    });
}

QList<RepositoryStatus> Manager::repositoryStatus() {
    return reading([](const State &s) {
        QList<RepositoryStatus> out;
        for (const auto &r : s.repos)
            out.append({r.repo, int(r.packages.size()), r.error});
        return out;
    });
}

std::optional<RepositoryLink> Manager::parseLink(const QString &link) {
    const QUrl url(link.trimmed());
    const auto scheme = url.scheme().toLower();
    // The action is the host and path together ("add-repo", "repos/add").
    const auto action = (url.host() + url.path()).toLower().remove(QLatin1Char('/'));
    std::optional<Kind> kind;
    if (scheme == QLatin1String("tachiyomi") || scheme == QLatin1String("mihon"))
        kind = Kind::Mihon;
    else if (scheme == QLatin1String("aniyomi"))
        kind = Kind::Aniyomi;
    else if (scheme == QLatin1String("lnreader"))
        kind = Kind::LNReader;
    else if (scheme == QLatin1String("cloudstreamrepo"))
        // The whole link is the repository's address.
        return RepositoryLink{link.trimmed(), Kind::CloudStream};
    else if (scheme != QLatin1String("scancore"))
        return std::nullopt;
    const bool adds = action == QLatin1String("add-repo") || action == QLatin1String("addrepo") ||
                      action == QLatin1String("reposadd") || action == QLatin1String("repoadd") ||
                      action == QLatin1String("extension-store") || action == QLatin1String("add-extension-store");
    if (!adds)
        return std::nullopt;
    const auto target = QUrlQuery(url).queryItemValue(QStringLiteral("url"), QUrl::FullyDecoded).trimmed();
    if (target.isEmpty())
        return std::nullopt;
    if (scheme == QLatin1String("scancore"))
        kind = kindFrom(QUrlQuery(url).queryItemValue(QStringLiteral("kind")));
    return RepositoryLink{target, kind};
}

bool Manager::refresh(const QString &url) {
    auto current = reading([&](const State &s) -> std::optional<RepoState> {
        for (const auto &r : s.repos)
            if (r.repo.url == url)
                return r;
        return std::nullopt;
    });
    if (!current)
        throw Error(QStringLiteral("That repository is not added."));
    RepoState next = *current;
    try {
        const QUrl listUrl(current->repo.listUrl.isEmpty() ? current->repo.url : current->repo.listUrl);
        // Echo's list stays the same when its extensions get new releases.
        const bool echo = current->repo.kind == Kind::Echo;
        const auto response = fetch(listUrl, echo ? QString() : current->etag, echo ? QString() : current->modified);
        if (response.status == 304) {
            Database::exec(QStringLiteral("UPDATE ext_repos SET refreshed_at = ?, error = NULL WHERE url = ?"), {Database::now(), url});
            return false;
        }
        const auto doc = parseIndex(response.body, listUrl, current->repo.kind);
        if (!doc.hasPackages)
            throw Error(QStringLiteral("The repository's list could not be read."));
        // A store whose key changed is not taken at its word.
        if (doc.repo && !isUnsignedKind(current->repo.kind) && !doc.repo->signingKey.isEmpty() &&
            doc.repo->signingKey != current->repo.signingKey)
            throw Error(QStringLiteral("The repository's signing key changed. Remove it and add it again if you trust the new key."));
        next.packages = doc.packages;
        for (auto &p : next.packages)
            p.kind = current->repo.kind;
        if (echo)
            resolveEcho(next.packages, current->packages);
        next.etag = QString::fromUtf8(response.header("ETag"));
        next.modified = QString::fromUtf8(response.header("Last-Modified"));
        next.error.clear();
    } catch (const Error &e) {
        next.error = e.message();
        storeRepo(next, false);
        writing([&](State &s) {
            for (auto &r : s.repos)
                if (r.repo.url == url)
                    r.error = next.error;
            return 0;
        });
        throw;
    }
    storeRepo(next, false);
    writing([&](State &s) {
        for (auto &r : s.repos)
            if (r.repo.url == url)
                r = next;
        return 0;
    });
    changed();
    return true;
}

void Manager::refreshAll() {
    for (const auto &repo : repositories())
        try {
            refresh(repo.url);
        } catch (const Error &) {
            // Kept with the repository as its error.
        }
    // For "checked … ago" on the Extensions page.
    Database::setPref(QStringLiteral("extensions.checkedAt"), Database::now());
}

QList<Package> Manager::available(std::optional<Kind> kind, QStringList *repos) {
    return reading([&](const State &s) {
        QHash<QString, int> at;
        QList<Package> out;
        QStringList from;
        for (const auto &r : s.repos) {
            if (kind && r.repo.kind != *kind)
                continue;
            for (const auto &p : r.packages) {
                const auto key = id(p.kind, p.pkg);
                if (const auto i = at.value(key, -1); i >= 0) {
                    if (p.versionCode > out[i].versionCode) {
                        out[i] = p;
                        from[i] = r.repo.url;
                    }
                } else {
                    at.insert(key, int(out.size()));
                    out.append(p);
                    from.append(r.repo.url);
                }
            }
        }
        if (repos)
            *repos = from;
        return out;
    });
}

QString Manager::repositoryOf(Kind kind, const QString &pkg) {
    return reading([&](const State &s) {
        QString best;
        qint64 version = -1;
        for (const auto &r : s.repos)
            if (r.repo.kind == kind)
                for (const auto &p : r.packages)
                    if (p.pkg == pkg && p.versionCode > version) {
                        version = p.versionCode;
                        best = r.repo.url;
                    }
        return best;
    });
}

QList<Package> Manager::autoInstallSelection(const QList<Package> &packages, const QString &language, bool adult) {
    QList<Package> out;
    for (const auto &p : packages) {
        if (p.torrent || (!adult && p.warning == ContentWarning::Nsfw) || !libProblem(p).isEmpty())
            continue;
        const auto base = contentBase(p.lang);
        bool wanted = base == language;
        // Multi-language extensions when one of their sources is in it.
        if (!wanted && (base == QLatin1String("all") || base == QLatin1String("multi") || base.isEmpty()))
            wanted = std::any_of(p.sources.begin(), p.sources.end(),
                                 [&](const SourceInfo &s) { return contentBase(s.lang) == language; });
        if (wanted)
            out.append(p);
    }
    return out;
}

// ---------------------------------------------------------------- extensions

Installed Manager::install(Kind kind, const QString &pkg) {
    BusyPackage busy(id(kind, pkg));
    // An installed extension updates from its own repository (see
    // updateSource); a new one from whichever offers the newest version.
    QString repoUrl;
    std::optional<Package> listed;
    reading([&](const State &s) {
        const auto current = s.installed.value(id(kind, pkg));
        for (const auto &r : s.repos) {
            if (r.repo.kind != kind || !updateSource(s, current, r.repo.url))
                continue;
            for (const auto &p : r.packages)
                if (p.pkg == pkg && (!listed || p.versionCode > listed->versionCode)) {
                    listed = p;
                    repoUrl = r.repo.url;
                }
        }
        return 0;
    });
    if (!listed)
        throw Error(QStringLiteral("No repository lists that extension."));
    if (!resolveEcho(*listed))
        throw Error(QStringLiteral("The latest release of %1 could not be found. Try again later.").arg(listed->name));
    const auto &package = *listed;
    if (const auto problem = libProblem(package); !problem.isEmpty())
        throw Error(problem);
    if (package.torrent)
        throw Error(QStringLiteral("%1 plays torrents, which SCANCORE cannot.").arg(package.name));
    if (g_storage.isEmpty())
        throw Error(QStringLiteral("Extensions have nowhere to be stored."));

    // CloudStream plugins run on a runtime phones download separately.
    if (kind == Kind::CloudStream)
        Runtime::installCloudStream();
    else if (kind == Kind::Echo)
        Runtime::installEcho();

    const QUrl fileUrl(usesJar(package) ? package.jarUrl : package.fileUrl);
    if (!allowedHost(fileUrl))
        throw Error(QStringLiteral("%1 is not offered over https.").arg(package.name));
    Request request;
    request.url = fileUrl;
    request.timeoutMs = 60000;
    request.maxBytes = 64 * 1024 * 1024;
    const auto response = Http::send(request);
    if (!response.ok() || response.body.isEmpty())
        throw Error(QStringLiteral("%1 could not be downloaded (HTTP %2).").arg(package.name).arg(response.status), response.status);

    QStringList signers;
    if (isSignedFile(kind)) {
        signers = apkSigners(response.body);
        if (signers.isEmpty())
            throw Error(QStringLiteral("%1 is not signed, so SCANCORE will not install it.").arg(package.name));
        // An update must come from the key that signed what is installed.
        if (const auto old = installedPackage(kind, pkg); old && !old->signers.isEmpty())
            for (const auto &signer : old->signers)
                if (!signers.contains(signer))
                    throw Error(QStringLiteral("This version of %1 is signed with a different key than the one installed. "
                                               "Remove it first if you trust the new key.")
                                    .arg(package.name));
    }

    const auto folder = QStringLiteral("%1/%2/%3").arg(g_storage, kindName(kind), pkg);
    QDir().mkpath(folder);
    const auto path = QStringLiteral("%1/%2.%3").arg(folder).arg(package.versionCode).arg(kind == Kind::CloudStream ? "cs3" : kind == Kind::Echo ? "eapk" : isPluginKind(kind) ? "js" : usesJar(package) ? "jar" : "apk");
    {
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(response.body) != response.body.size() || !file.commit())
            throw Error(QStringLiteral("%1 could not be saved.").arg(package.name));
    }
    // Code is loaded from read-only files (Android 14 refuses writable ones).
    QFile::setPermissions(path, QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
    for (const auto &old : QDir(folder).entryInfoList(QDir::Files))
        if (old.absoluteFilePath() != QFileInfo(path).absoluteFilePath()) {
            QFile::setPermissions(old.absoluteFilePath(), QFile::ReadOwner | QFile::WriteOwner);
            QFile::remove(old.absoluteFilePath());
        }

    Installed installed;
    installed.package = package;
    installed.repoUrl = repoUrl;
    installed.signers = signers;
    installed.path = path;
    installed.installedAt = Database::now();
    const auto language = contentLanguage();
    const bool anyInLanguage = std::any_of(package.sources.begin(), package.sources.end(),
                                           [&](const SourceInfo &s) { return inLanguage(s.lang, language); });
    Database::transaction([&] {
        Database::query(QStringLiteral("INSERT INTO ext_installed(kind, pkg, package, repo_url, signers, path, installed_at) "
                                       "VALUES(?, ?, ?, ?, ?, ?, ?) ON CONFLICT(kind, pkg) DO UPDATE SET package = excluded.package, "
                                       "repo_url = excluded.repo_url, signers = excluded.signers, path = excluded.path, "
                                       "installed_at = excluded.installed_at"),
                        {kindName(kind), pkg, compact(package.toJson()), repoUrl, signers.join(QLatin1Char(',')), path,
                         installed.installedAt});
        // New sources start on when they are in the content language (all
        // of them when none is); a source's switch is kept across updates.
        for (const auto &s : package.sources)
            Database::query(QStringLiteral("INSERT OR IGNORE INTO ext_sources(key, enabled) VALUES(?, ?)"),
                            {sourceKey(kind, s.id), !anyInLanguage || inLanguage(s.lang, language)});
    });
    writing([&](State &s) {
        s.installed.insert(id(kind, pkg), installed);
        for (const auto &source : package.sources) {
            const auto key = sourceKey(kind, source.id);
            if (!s.enabled.contains(key))
                s.enabled.insert(key, !anyInLanguage || inLanguage(source.lang, language));
        }
        installed.trusted = isTrusted(s, installed);
        return 0;
    });
    changed();
    return installed;
}

void Manager::uninstall(Kind kind, const QString &pkg) {
    const auto old = installedPackage(kind, pkg);
    Database::exec(QStringLiteral("DELETE FROM ext_installed WHERE kind = ? AND pkg = ?"), {kindName(kind), pkg});
    Database::exec(QStringLiteral("DELETE FROM ext_trust WHERE kind = ? AND pkg = ?"), {kindName(kind), pkg});
    if (old) {
        const auto folder = QFileInfo(old->path).absolutePath();
        for (const auto &file : QDir(folder).entryInfoList(QDir::Files))
            QFile::setPermissions(file.absoluteFilePath(), QFile::ReadOwner | QFile::WriteOwner);
        QDir(folder).removeRecursively();
    }
    writing([&](State &s) {
        s.installed.remove(id(kind, pkg));
        s.trust.remove(id(kind, pkg));
        return 0;
    });
    changed();
}

QList<Installed> Manager::installed(std::optional<Kind> kind) {
    return reading([&](const State &s) {
        // The newest version the repositories offer of each installed
        // package, in one pass over them (hundreds installed, thousands
        // offered: Extensions and Sources ask often).
        // Updates come from the repository an extension was installed from
        // while it is still added: another repository listing the same
        // package cannot take it over.
        QHash<QString, const Package *> newest;
        for (const auto &i : s.installed)
            if (!kind || i.package.kind == *kind)
                newest.insert(id(i.package.kind, i.package.pkg), nullptr);
        for (const auto &r : s.repos) {
            if (kind && r.repo.kind != *kind)
                continue;
            for (const auto &p : r.packages) {
                const auto key = id(p.kind, p.pkg);
                const auto it = newest.find(key);
                if (it == newest.end() || !updateSource(s, s.installed.value(key), r.repo.url))
                    continue;
                if (!*it || p.versionCode > (*it)->versionCode)
                    *it = &p;
            }
        }
        QList<Installed> out;
        out.reserve(newest.size());
        for (auto i : s.installed) {
            if (kind && i.package.kind != *kind)
                continue;
            i.trusted = isTrusted(s, i);
            i.lib = libSupport(i.package.kind, i.package.lib);
            // An APK installed before its repository offered the jar is
            // replaced by it, at the same version.
            if (const auto *p = newest.value(id(i.package.kind, i.package.pkg));
                p && (p->versionCode > i.package.versionCode ||
                      (p->versionCode == i.package.versionCode && usesJar(*p) && i.path.endsWith(QLatin1String(".apk")))))
                i.update = *p;
            out.append(i);
        }
        std::sort(out.begin(), out.end(), [](const Installed &a, const Installed &b) {
            return a.package.name.compare(b.package.name, Qt::CaseInsensitive) < 0;
        });
        return out;
    });
}

std::optional<Installed> Manager::installedPackage(Kind kind, const QString &pkg) {
    return reading([&](const State &s) -> std::optional<Installed> {
        if (!s.installed.contains(id(kind, pkg)))
            return std::nullopt;
        auto i = s.installed.value(id(kind, pkg));
        i.trusted = isTrusted(s, i);
        i.lib = libSupport(i.package.kind, i.package.lib);
        return i;
    });
}

void Manager::trust(Kind kind, const QString &pkg) {
    const auto i = installedPackage(kind, pkg);
    if (!i || i->signers.isEmpty())
        return;
    Database::exec(QStringLiteral("INSERT INTO ext_trust(kind, pkg, version_code, signer) VALUES(?, ?, ?, ?) "
                                  "ON CONFLICT(kind, pkg) DO UPDATE SET version_code = excluded.version_code, signer = excluded.signer"),
                   {kindName(kind), pkg, i->package.versionCode, i->signers.last()});
    writing([&](State &s) {
        s.trust.insert(id(kind, pkg), {i->package.versionCode, i->signers.last()});
        return 0;
    });
    changed();
}

QList<Installed> Manager::updates() {
    QList<Installed> out;
    for (const auto &i : installed())
        if (i.update)
            out.append(i);
    return out;
}

QList<Installed> Manager::installTrustedUpdates() {
    QList<Installed> out;
    for (const auto &i : updates()) {
        if (!i.trusted)
            continue;
        try {
            auto next = install(i.package.kind, i.package.pkg);
            if (next.trusted)
                out.append(next);
        } catch (const Error &) {
            // Left for the Extensions page, which shows the update.
        }
    }
    return out;
}

// ---------------------------------------------------------------- sources

QList<SourceEntry> Manager::sources(std::optional<Kind> kind) {
    const auto all = installed(kind);
    return reading([&](const State &s) {
        QList<SourceEntry> out;
        for (const auto &i : all)
            for (const auto &source : i.package.sources) {
                SourceEntry e;
                e.kind = i.package.kind;
                e.id = source.id;
                e.key = sourceKey(e.kind, source.id);
                e.name = source.name;
                e.lang = source.lang;
                e.baseUrl = source.baseUrl;
                e.pkg = i.package.pkg;
                e.path = i.path;
                e.signer = i.signers.isEmpty() ? QString() : i.signers.last();
                e.enabled = s.enabled.value(e.key, true);
                e.nsfw = i.package.warning == ContentWarning::Nsfw;
                if (!i.trusted)
                    e.blocked = QStringLiteral("%1 is not trusted yet.").arg(i.package.name);
                else
                    e.blocked = libProblem(i.package);
                out.append(e);
            }
        return out;
    });
}

void Manager::setSourceEnabled(const QString &key, bool enabled) {
    Database::exec(QStringLiteral("INSERT INTO ext_sources(key, enabled) VALUES(?, ?) "
                                  "ON CONFLICT(key) DO UPDATE SET enabled = excluded.enabled"),
                   {key, enabled});
    writing([&](State &s) {
        s.enabled.insert(key, enabled);
        return 0;
    });
    changed();
}

void Manager::recordSourceResult(const QString &key, const QString &error) {
    // Written only when the health changes, not on every request.
    const bool same = reading([&](const State &s) { return s.errors.value(key) == error; });
    if (same)
        return;
    if (error.isEmpty())
        Database::exec(QStringLiteral("UPDATE ext_sources SET error = NULL, ok_at = ? WHERE key = ?"), {Database::now(), key});
    else
        Database::exec(QStringLiteral("INSERT INTO ext_sources(key, error, error_at) VALUES(?, ?, ?) "
                                      "ON CONFLICT(key) DO UPDATE SET error = excluded.error, error_at = excluded.error_at"),
                       {key, error, Database::now()});
    writing([&](State &s) {
        if (error.isEmpty())
            s.errors.remove(key);
        else
            s.errors.insert(key, error);
        return 0;
    });
}

QString Manager::sourceError(const QString &key) {
    return reading([&](const State &s) { return s.errors.value(key); });
}

QString Manager::preference(const QString &sourceKey, const QString &key) {
    return Database::scalar(QStringLiteral("SELECT value FROM ext_prefs WHERE source_key = ? AND key = ?"), {sourceKey, key}).toString();
}

void Manager::setPreference(const QString &sourceKey, const QString &key, const QString &value) {
    Database::exec(QStringLiteral("INSERT INTO ext_prefs(source_key, key, value) VALUES(?, ?, ?) "
                                  "ON CONFLICT(source_key, key) DO UPDATE SET value = excluded.value"),
                   {sourceKey, key, value});
}

} // namespace sc::ext
