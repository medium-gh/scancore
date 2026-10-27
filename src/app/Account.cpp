#include "Account.h"

#include "Library.h"
#include "Live.h"
#include "Sync.h"
#include "core/Cookies.h"
#include "core/Database.h"
#include "sources/comics/Registry.h"
#include "sources/novels/Registry.h"

#include <algorithm>

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QRegularExpression>
#include <QTimer>

namespace sc {

namespace {

const QString kDefaultSite = QStringLiteral("https://scancore.onl");

// scancore.onl, or a SCANCORE website hosted elsewhere (Settings → Sync).
// SCANCORE_SITE points the app at a local copy of the website (development).
QString siteUrl() {
    if (qEnvironmentVariableIsSet("SCANCORE_SITE"))
        return qEnvironmentVariable("SCANCORE_SITE");
    const auto own = Database::pref(QStringLiteral("account.site")).toString();
    return own.isEmpty() ? kDefaultSite : own;
}

// How the website is named in messages: "scancore.onl", or its host.
QString siteName() { return QUrl(siteUrl()).host(); }

Headers apiHeaders() {
    return {{"Accept", "application/json"}, {"Content-Type", "application/json"}};
}

QString categoryFor(const QString &status) {
    if (status == QLatin1String("COMPLETED"))
        return QStringLiteral("completed");
    if (status == QLatin1String("DROPPED"))
        return QStringLiteral("dropped");
    if (status == QLatin1String("PLANNED"))
        return QStringLiteral("planned");
    return QStringLiteral("reading");
}

QString statusFor(const QString &category) {
    if (category == QLatin1String("completed"))
        return QStringLiteral("COMPLETED");
    if (category == QLatin1String("dropped"))
        return QStringLiteral("DROPPED");
    if (category == QLatin1String("planned"))
        return QStringLiteral("PLANNED");
    return QStringLiteral("IN_PROGRESS");
}

// Maps a website series to the local series id, or an empty map when the
// desktop app has no matching source.
QVariantMap localSeries(const QJsonObject &series) {
    const auto media = series.value("mediaType").toString();
    QVariantMap out{{"title", series.value("title").toString()},
                    {"cover", series.value("cover").toString()},
                    {"description", series.value("description").toString()}};
    for (const auto &value : series.value("sourceMappings").toArray()) {
        const auto mapping = value.toObject();
        const auto name = mapping.value("sourceName").toString();
        const auto url = mapping.value("canonicalUrl").toString();
        if (media == QLatin1String("ANIME") && name.startsWith(QLatin1String("anime:anilist"))) {
            const auto id = mapping.value("externalId").toString();
            out.insert("id", Library::idFor(QStringLiteral("anime"), QStringLiteral("anilist"), id));
            out.insert("media", QStringLiteral("anime"));
            out.insert("source", QStringLiteral("anilist"));
            out.insert("sourceUrl", QStringLiteral("https://anilist.co/anime/%1").arg(id));
            out.insert("extra", QVariantMap{{"anilistId", id.toInt()}});
            return out;
        }
        if (url.isEmpty())
            continue;
        if (media == QLatin1String("NOVEL")) {
            if (auto source = novels::Registry::forUrl(url)) {
                out.insert("id", Library::idFor(QStringLiteral("novel"), source->key(), url));
                out.insert("media", QStringLiteral("novel"));
                out.insert("source", source->key());
                out.insert("sourceUrl", url);
                return out;
            }
        } else if (auto source = comics::Registry::forUrl(url)) {
            out.insert("id", Library::idFor(QStringLiteral("comic"), source->key(), url));
            out.insert("media", QStringLiteral("comic"));
            out.insert("source", source->key());
            out.insert("sourceUrl", url);
            return out;
        }
    }
    return {};
}

QJsonObject getJson(const QString &path) {
    const auto response = Http::get(QUrl(siteUrl() + path), apiHeaders(), 20000);
    if (response.status == 401)
        throw Error(QStringLiteral("Your %1 session ended. Sign in again.").arg(siteName()), 401);
    if (!response.ok())
        throw Error(QStringLiteral("%1 responded with HTTP %2.").arg(siteName()).arg(response.status), response.status);
    return response.json().object();
}

qint64 isoSeconds(const QJsonValue &value) {
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs).toSecsSinceEpoch();
}

QString animeTitle(const QJsonObject &anime) {
    const auto title = anime.value("title").toObject();
    for (const auto *key : {"english", "romaji", "userPreferred", "native"}) {
        const auto text = title.value(QLatin1String(key)).toString();
        if (!text.isEmpty())
            return text;
    }
    return QStringLiteral("Anime");
}

// The website keeps watch progress per episode unit; this maps an AniList
// show's episode numbers to those units. Kept for a day, and fetched again
// when an episode is missing (a new one aired).
QHash<int, QString> episodeUnits(int anilistId, int episode) {
    const auto key = QStringLiteral("%1:units:%2").arg(siteName()).arg(anilistId);
    const auto parse = [](const QByteArray &body) {
        QHash<int, QString> units;
        for (const auto &value : QJsonDocument::fromJson(body).object().value("episodes").toArray()) {
            const auto row = value.toObject();
            units.insert(row.value("episode_number").toInt(), row.value("unitId").toString());
        }
        return units;
    };
    if (const auto cached = Database::cacheGet(key)) {
        auto units = parse(*cached);
        if (units.contains(episode))
            return units;
    }
    const auto json = getJson(QStringLiteral("/api/anime/media/%1/episodes").arg(anilistId));
    const auto body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    Database::cachePut(key, body, 86400);
    return parse(body);
}

// Local anime progress changed since the last push -> website, in one batch
// request. A website without the batch endpoint gets one request per
// episode, at most 30 a sync (its per-minute write limit).
int pushAnimeProgress(bool batchSupported, bool *more) {
    constexpr int kBatch = 100;
    constexpr int kSingleLimit = 30;
    const auto since = Database::pref(QStringLiteral("account.animePushedAt"), 0).toLongLong();
    const auto rows = Database::query(QStringLiteral(R"(
        SELECT series_id, unit_key, position, completed, updated_at FROM progress
        WHERE series_id LIKE 'anime:anilist:%' AND updated_at > ? ORDER BY updated_at ASC LIMIT ?
    )"),
                                      {since, kBatch});
    *more = rows.size() == kBatch;
    if (rows.isEmpty())
        return 0;

    // Each row with the item to send, or an empty item when the website has
    // no unit for it (skipped, not retried).
    struct Pending {
        qint64 at;
        QJsonObject item;
    };
    QList<Pending> pending;
    QHash<int, QHash<int, QString>> unitsByShow;
    // The newest episode of each show in this batch, so a cached episode list
    // that predates it is fetched again.
    QHash<int, int> newestEpisode;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const int id = row.value("series_id").toString().section(QLatin1Char(':'), 2).toInt();
        newestEpisode[id] = std::max(newestEpisode.value(id), row.value("unit_key").toInt());
    }
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const int anilistId = row.value("series_id").toString().section(QLatin1Char(':'), 2).toInt();
        const int episode = row.value("unit_key").toInt();
        const auto position = QJsonDocument::fromJson(row.value("position").toByteArray()).object();
        double seconds = position.value("seconds").toDouble();
        double duration = position.value("duration").toDouble();
        if (duration < 1 && row.value("completed").toBool())
            seconds = duration = 1; // marked watched without playing
        QString unit;
        if (anilistId > 0 && episode > 0 && duration >= 1) {
            if (!unitsByShow.contains(anilistId)) {
                try {
                    unitsByShow.insert(anilistId, episodeUnits(anilistId, newestEpisode.value(anilistId)));
                } catch (const Error &e) {
                    // A show the website does not know is skipped, not retried forever.
                    if (e.status() < 400 || e.status() >= 500 || e.status() == 401 || e.status() == 429)
                        throw;
                    unitsByShow.insert(anilistId, {});
                }
            }
            unit = unitsByShow.value(anilistId).value(episode);
        }
        QJsonObject item;
        if (!unit.isEmpty())
            item = QJsonObject{{"unitId", unit},
                               {"seconds", std::clamp(seconds, 0.0, 36000.0)},
                               {"duration", std::clamp(duration, 1.0, 36000.0)},
                               {"track", false}};
        pending.append({row.value("updated_at").toLongLong(), item});
    }

    const auto transient = [](int status) { return status == 429 || status >= 500 || status == 0; };
    QJsonArray items;
    for (const auto &p : pending)
        if (!p.item.isEmpty())
            items.append(p.item);
    if (items.isEmpty()) {
        Database::setPref(QStringLiteral("account.animePushedAt"), pending.last().at);
        return 0;
    }

    if (batchSupported) {
        const auto batch = Http::postJson(QUrl(siteUrl() + QStringLiteral("/api/anime/progress/batch")),
                                          QJsonDocument(QJsonObject{{"items", items}}).toJson(QJsonDocument::Compact),
                                          {{"Accept", "application/json"}});
        if (batch.status == 401)
            throw Error(QStringLiteral("Your %1 session ended. Sign in again.").arg(siteName()), 401);
        if (batch.ok()) {
            Database::setPref(QStringLiteral("account.animePushedAt"), pending.last().at);
            return QJsonDocument::fromJson(batch.body).object().value("saved").toInt();
        }
        if (transient(batch.status))
            return 0;
    }

    // No batch endpoint yet: one episode at a time.
    qint64 doneUpTo = since;
    int pushed = 0;
    int sent = 0;
    for (const auto &p : pending) {
        if (!p.item.isEmpty()) {
            if (sent == kSingleLimit) {
                *more = true;
                break;
            }
            ++sent;
            const auto response = Http::postJson(QUrl(siteUrl() + QStringLiteral("/api/anime/progress")),
                                                 QJsonDocument(p.item).toJson(QJsonDocument::Compact),
                                                 {{"Accept", "application/json"}});
            if (response.status == 401 || transient(response.status))
                break; // nothing skipped; an episode the website rejects is
            if (response.ok())
                ++pushed;
        }
        doneUpTo = p.at;
    }
    Database::setPref(QStringLiteral("account.animePushedAt"), doneUpTo);
    return pushed;
}

// Local comic reading positions saved since the last push -> the website's
// reading history: the latest chapter of each series, which is what the
// website keeps. The website keys these entries itself ("scancore-app").
int pushComicProgress(double serverStamp) {
    constexpr int kMaxEntries = 120;
    const auto since = Database::pref(QStringLiteral("account.comicPushedAt"), 0).toLongLong();
    const auto rows = Database::query(QStringLiteral(R"(
        SELECT p.series_id, p.unit_key, p.unit_title, p.unit_number, p.unit_url, p.position, p.percent, p.updated_at,
               s.title, s.source_url, s.cover,
               (SELECT idx FROM units u WHERE u.series_id = p.series_id AND u.unit_key = p.unit_key) AS idx
        FROM progress p JOIN series s ON s.id = p.series_id
        WHERE p.series_id LIKE 'comic:%' AND p.updated_at > ? ORDER BY p.updated_at DESC
    )"),
                                      {since});
    if (rows.isEmpty())
        return 0;
    const auto newest = rows.first().toMap().value("updated_at").toLongLong();
    const auto isWebUrl = [](const QString &value) {
        const QUrl url(value);
        return url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"));
    };
    const auto nowMs = QDateTime::currentMSecsSinceEpoch();

    QJsonArray history;
    QSet<QString> seen;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto seriesId = row.value("series_id").toString();
        if (seen.contains(seriesId))
            continue;
        seen.insert(seriesId);
        const auto title = row.value("title").toString().trimmed();
        const auto seriesUrl = row.value("source_url").toString();
        const auto unitUrl = row.value("unit_url").toString();
        const auto chapterUrl = isWebUrl(unitUrl) ? unitUrl : row.value("unit_key").toString();
        if (title.isEmpty() || !isWebUrl(seriesUrl) || !isWebUrl(chapterUrl))
            continue;
        const auto position = QJsonDocument::fromJson(row.value("position").toByteArray()).object();
        const int panel = std::max(0, position.value("panel").toInt());
        const double percent = row.value("percent").toDouble();
        const int total = percent > 0 ? std::max(panel + 1, int(std::lround((panel + 1) / percent))) : panel + 1;
        history.append(QJsonObject{
            {"key", "scancore-app"},
            {"client", "scancore-app"},
            {"comic", QJsonObject{{"title", title.left(1000)}, {"title_no", ""}, {"url", seriesUrl}, {"thumbnail", row.value("cover").toString()}}},
            {"chapter", QJsonObject{{"episode_no", row.value("unit_number").toString().left(50)},
                                    {"episode_url", chapterUrl},
                                    {"title", row.value("unit_title").toString()}}},
            {"updatedAt", double(std::min(nowMs, row.value("updated_at").toLongLong() * 1000))},
            {"panelIndex", panel},
            {"scrollY", 0},
            {"chapterIndex", std::max(0, row.value("idx").isNull() ? 0 : row.value("idx").toInt())},
            {"totalPanels", total},
        });
        if (history.size() == kMaxEntries)
            break;
    }
    if (history.isEmpty()) {
        Database::setPref(QStringLiteral("account.comicPushedAt"), newest);
        return 0;
    }

    // The website accepts a write only against its latest version; if
    // another device got in first, try again once with the new stamp.
    double stamp = serverStamp;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto body = QJsonDocument(QJsonObject{{"history", history},
                                                    {"clientStamp", stamp},
                                                    {"mutationId", QUuid::createUuid().toString(QUuid::WithoutBraces)}})
                              .toJson(QJsonDocument::Compact);
        const auto response =
            Http::postJson(QUrl(siteUrl() + QStringLiteral("/api/progress")), body, {{"Accept", "application/json"}});
        if (response.status == 401)
            throw Error(QStringLiteral("Your %1 session ended. Sign in again.").arg(siteName()), 401);
        if (response.ok()) {
            Database::setPref(QStringLiteral("account.comicPushedAt"), newest);
            return history.size();
        }
        if (response.status != 409)
            return 0;
        stamp = QJsonDocument::fromJson(response.body).object().value("serverStamp").toDouble();
    }
    return 0;
}

// Website anime progress -> local, for shows that changed there since the
// last pull. Each episode keeps whichever side saved it last.
int pullAnimeProgress() {
    const auto since = Database::pref(QStringLiteral("account.animePulledAt"), 0).toLongLong();
    qint64 newest = since;
    qint64 newestWritten = 0;
    int pulled = 0;
    for (const auto &value : getJson(QStringLiteral("/api/anime/progress")).value("progress").toArray()) {
        const auto latest = value.toObject();
        const auto updated = isoSeconds(latest.value("updatedAt"));
        const int anilistId = latest.value("anime_id").toInt();
        if (updated <= since || anilistId <= 0)
            continue;
        newest = std::max(newest, updated);
        const auto id = Library::idFor(QStringLiteral("anime"), QStringLiteral("anilist"), QString::number(anilistId));
        if (!Database::scalar(QStringLiteral("SELECT 1 FROM series WHERE id = ?"), {id}).isValid()) {
            const auto anime = latest.value("anime").toObject();
            const auto cover = anime.value("coverImage").toObject();
            Library::upsertSeries({{"id", id},
                                   {"media", "anime"},
                                   {"source", "anilist"},
                                   {"sourceUrl", QStringLiteral("https://anilist.co/anime/%1").arg(anilistId)},
                                   {"title", animeTitle(anime)},
                                   {"cover", cover.value("extraLarge").toString(cover.value("large").toString())},
                                   {"extra", QVariantMap{{"anilistId", anilistId}}}});
        }
        const auto seriesId = latest.value("seriesId").toString();
        const auto episodes =
            getJson(QStringLiteral("/api/anime/progress/series?seriesId=") + QString::fromLatin1(QUrl::toPercentEncoding(seriesId))).value("progress").toArray();
        for (const auto &item : episodes) {
            const auto episode = item.toObject();
            const int number = episode.value("episode").toInt();
            const double duration = episode.value("duration").toDouble();
            const double seconds = episode.value("seconds").toDouble();
            const bool completed = episode.value("completed").toBool();
            const auto at = isoSeconds(episode.value("updatedAt"));
            if (number <= 0)
                continue;
            const double percent = completed ? 1.0 : (duration > 0 ? std::min(1.0, seconds / duration) : 0.0);
            const auto position =
                QJsonDocument(QJsonObject{{"type", "video"}, {"seconds", seconds}, {"duration", duration}}).toJson(QJsonDocument::Compact);
            const auto before = Database::scalar(QStringLiteral("SELECT updated_at FROM progress WHERE series_id = ? AND unit_key = ?"),
                                                 {id, QString::number(number)});
            if (before.isValid() && before.toLongLong() >= at)
                continue;
            Database::exec(QStringLiteral(R"(
                INSERT INTO progress(series_id, unit_key, unit_title, unit_number, position, percent, completed, updated_at)
                VALUES(?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(series_id, unit_key) DO UPDATE SET position = excluded.position, percent = excluded.percent,
                    completed = MAX(progress.completed, excluded.completed), updated_at = excluded.updated_at
            )"),
                           {id, QString::number(number), QStringLiteral("Episode %1").arg(number), QString::number(number),
                            position, percent, completed ? 1 : 0, at});
            newestWritten = std::max(newestWritten, at);
            ++pulled;
        }
    }
    Database::setPref(QStringLiteral("account.animePulledAt"), newest);
    // What just came from the website must not be pushed straight back.
    if (newestWritten > Database::pref(QStringLiteral("account.animePushedAt"), 0).toLongLong())
        Database::setPref(QStringLiteral("account.animePushedAt"), newestWritten);
    return pulled;
}

} // namespace

Account::Account(QObject *parent) : QObject(parent) {
    m_user = Database::pref(QStringLiteral("account.user")).toMap();
    if (signedIn() && active())
        QTimer::singleShot(6000, this, [this] { sync(); });
    m_soon.setSingleShot(true);
    connect(&m_soon, &QTimer::timeout, this, [this] {
        if (signedIn() && active() && !m_syncing)
            sync();
    });
    // Mirror saves, reads and watches shortly after they happen.
    connect(Library::instance(), &Library::changed, this, &Account::scheduleSync);
    // A series removed here is removed from the website at the next sync,
    // rather than brought back by it.
    connect(Library::instance(), &Library::removed, this, [this](const QString &id) {
        if (!signedIn())
            return;
        auto removed = Database::pref(QStringLiteral("account.libraryRemoved")).toStringList();
        if (!removed.contains(id)) {
            removed.append(id);
            Database::setPref(QStringLiteral("account.libraryRemoved"), removed);
        }
    });
    connect(Library::instance(), &Library::saved, this, [](const QString &id) {
        auto removed = Database::pref(QStringLiteral("account.libraryRemoved")).toStringList();
        if (removed.removeAll(id) > 0)
            Database::setPref(QStringLiteral("account.libraryRemoved"), removed);
    });
}

bool Account::active() const { return Database::pref(QStringLiteral("sync.kind")).toString().isEmpty(); }

QString Account::site() const { return siteUrl(); }

bool Account::customSite() const { return siteUrl() != kDefaultSite; }

QString Account::setSite(const QString &address) {
    if (signedIn())
        return QStringLiteral("Sign out first.");
    auto text = address.trimmed();
    while (text.endsWith(QLatin1Char('/')))
        text.chop(1);
    if (!text.isEmpty() && !text.contains(QLatin1String("://")))
        text.prepend(QStringLiteral("https://"));
    const QUrl url(text);
    if (!text.isEmpty() && (!url.isValid() || url.host().isEmpty() ||
                            (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))))
        return QStringLiteral("Enter the server's address, like http://192.168.1.20:8787 or https://sync.example.com.");
    Database::setPref(QStringLiteral("account.site"), text == kDefaultSite ? QString() : text);
    CookieJar::refreshProtected();
    emit changed();
    return {};
}

void Account::scheduleSync() {
    // Progress that live sync just brought in is not a change to send.
    if (!signedIn() || !active() || Live::applying())
        return;
    if (m_syncing) {
        m_dirty = true;
        return;
    }
    if (m_soon.isActive())
        return;
    // The players and readers record progress every few seconds, and a sync
    // is several requests to a site that limits them per minute.
    constexpr qint64 kQuiet = 4000;
    constexpr qint64 kMinGap = 60000;
    const qint64 sinceLast = QDateTime::currentMSecsSinceEpoch() - m_lastSyncEnded;
    m_soon.start(int(std::clamp(kMinGap - sinceLast, kQuiet, kMinGap)));
}

QString Account::siteBase() { return siteUrl(); }

Account *Account::instance() {
    static auto *account = [] {
        auto *created = new Account;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return account;
}

QString Account::lastSync() const {
    const auto at = Database::pref(QStringLiteral("account.lastSync")).toLongLong();
    return at > 0 ? QString::number(at) : QString();
}

void Account::setUser(const QVariantMap &user) {
    m_user = user;
    Database::setPref(QStringLiteral("account.user"), user);
    emit changed();
}

namespace {
// POSTs to one of the site's /api/auth routes (no session needed); throws
// the site's own message when it refuses.
QJsonObject postAuth(const QString &path, const QJsonObject &body) {
    const auto response = Http::postJson(QUrl(siteUrl() + path), QJsonDocument(body).toJson(QJsonDocument::Compact),
                                         {{"Accept", "application/json"}});
    const auto json = QJsonDocument::fromJson(response.body).object();
    if (!response.ok()) {
        auto message = json.value("error").toString();
        if (message.isEmpty())
            message = json.value("problem").toObject().value("message").toString();
        if (response.status == 429)
            message = QStringLiteral("Too many attempts. Wait a few minutes and try again.");
        throw Error(message.isEmpty() ? QStringLiteral("%1 responded with HTTP %2.").arg(siteName()).arg(response.status)
                                      : message,
                    response.status);
    }
    return json;
}
} // namespace

Task *Account::authenticate(const QString &path, const QJsonObject &body) {
    auto *task = Task::run([path, body]() -> QVariant {
        const auto json = postAuth(path, body);
        CookieJar::shared().save();
        return json.toVariantMap();
    });
    connect(task, &Task::finished, this, [this](const QVariant &result) {
        const auto user = result.toMap().value(QStringLiteral("user")).toMap();
        // Keep only what the app shows.
        setUser({{"username", user.value("username")}, {"email", user.value("email")}, {"avatar", user.value("avatarUrl")}});
        sync();
    });
    return task;
}

Task *Account::signIn(const QString &email, const QString &password) {
    return authenticate(QStringLiteral("/api/auth/login"), {{"email", email.trimmed()}, {"password", password}});
}

Task *Account::signUp(const QString &email, const QString &username, const QString &password, const QString &accessCode) {
    QJsonObject body{{"email", email.trimmed()}, {"username", username.trimmed()}, {"password", password}};
    if (!accessCode.trimmed().isEmpty())
        body.insert(QStringLiteral("accessCode"), accessCode.trimmed().toUpper());
    return authenticate(QStringLiteral("/api/auth/signup"), body);
}

Task *Account::requestPasswordReset(const QString &email) {
    const QJsonObject body{{"action", "request"}, {"email", email.trimmed()}, {"purpose", "PASSWORD_RESET"}};
    return Task::run([body]() -> QVariant { return postAuth(QStringLiteral("/api/auth/recovery"), body).toVariantMap(); });
}

Task *Account::exchangeResetCode(const QString &email, const QString &code) {
    const QJsonObject body{{"action", "code"}, {"email", email.trimmed()}, {"code", code.trimmed()}};
    return Task::run([body]() -> QVariant {
        return postAuth(QStringLiteral("/api/auth/recovery"), body).value(QStringLiteral("token")).toString();
    });
}

Task *Account::completePasswordReset(const QString &token, const QString &password) {
    const QJsonObject body{{"action", "complete"}, {"token", token}, {"purpose", "PASSWORD_RESET"}, {"password", password}};
    return Task::run([body]() -> QVariant { return postAuth(QStringLiteral("/api/auth/recovery"), body).toVariantMap(); });
}

Task *Account::signupOptions() {
    // recovery: forgotten passwords can be reset by email on the site
    // (scancore.onl says so; a sync server has no mail, its owner resets).
    const bool official = !customSite();
    return Task::run([official]() -> QVariant {
        QVariantMap options{{"open", true}, {"accessCode", false}, {"codeLength", 6}, {"help", QString()},
                            {"recovery", official}};
        const auto response = Http::get(QUrl(siteUrl() + QStringLiteral("/api/capabilities")), apiHeaders(), 10000);
        if (!response.ok())
            return options;
        const auto capabilities = response.json().object();
        // Sites from before 2.16.3 don't say; keep the default for them.
        if (capabilities.value("recovery").isBool())
            options.insert(QStringLiteral("recovery"), capabilities.value("recovery").toBool());
        // scancore.onl sends {open, accessCode, accessCodeLength, help}; a
        // SCANCORE sync server sends true or false.
        const auto signup = capabilities.value("signup");
        if (signup.isBool())
            options.insert(QStringLiteral("open"), signup.toBool());
        else if (signup.isObject()) {
            const auto rules = signup.toObject();
            options.insert(QStringLiteral("open"), rules.value("open").toBool(true));
            options.insert(QStringLiteral("accessCode"), rules.value("accessCode").toBool());
            options.insert(QStringLiteral("codeLength"), std::clamp(rules.value("accessCodeLength").toInt(6), 4, 12));
            options.insert(QStringLiteral("help"), rules.value("help").toString());
        }
        return options;
    });
}

QVariantMap Account::call(const QByteArray &method, const QString &path, const QVariantMap &body) {
    if (!path.startsWith(QLatin1String("/api/")))
        throw Error(QStringLiteral("Not a site address."));
    Request request;
    request.url = QUrl(siteUrl() + path);
    request.method = method;
    request.headers = apiHeaders();
    request.timeoutMs = 20000;
    if (method != "GET" && method != "DELETE")
        request.body = QJsonDocument(QJsonObject::fromVariantMap(body)).toJson(QJsonDocument::Compact);
    const auto response = Http::send(request);
    const auto json = response.json().object();
    if (!response.ok()) {
        auto message = json.value("error").toString();
        if (message.isEmpty())
            message = json.value("problem").toObject().value("message").toString();
        if (response.status == 401)
            message = QStringLiteral("Your %1 session ended. Sign in again.").arg(siteName());
        if (response.status == 429)
            message = QStringLiteral("Too many attempts. Wait a few minutes and try again.");
        throw Error(message.isEmpty() ? QStringLiteral("%1 responded with HTTP %2.").arg(siteName()).arg(response.status)
                                      : message,
                    response.status);
    }
    return json.toVariantMap();
}

Task *Account::request(const QString &method, const QString &path, const QVariantMap &body) {
    auto *task = Task::run([method = method.toUpper().toUtf8(), path, body]() -> QVariant { return call(method, path, body); });
    // Signed in here but not on the site (a password reset or sign-out
    // elsewhere ended the session): show it signed out instead of letting
    // every post fail.
    connect(task, &Task::changed, this, [this, task] {
        if (!task->loading() && !task->ok() && task->status() == 401 && signedIn())
            setUser({});
    });
    return task;
}

QVariantList Account::passwordChecks(const QString &password) const {
    const auto check = [](const QString &label, bool passed) { return QVariantMap{{"label", label}, {"passed", passed}}; };
    return {check(QStringLiteral("At least 8 characters"), password.size() >= 8 && password.size() <= 128),
            check(QStringLiteral("A lowercase letter"), password.contains(QRegularExpression(QStringLiteral("[a-z]")))),
            check(QStringLiteral("An uppercase letter"), password.contains(QRegularExpression(QStringLiteral("[A-Z]")))),
            check(QStringLiteral("A number"), password.contains(QRegularExpression(QStringLiteral("\\d")))),
            check(QStringLiteral("A symbol"), password.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9]"))))};
}

QString Account::usernameProblem(const QString &username) const {
    const auto name = username.trimmed();
    if (name.isEmpty())
        return {};
    if (name.size() < 3)
        return QStringLiteral("At least 3 characters");
    if (name.size() > 32)
        return QStringLiteral("At most 32 characters");
    if (!QRegularExpression(QStringLiteral("^[a-zA-Z0-9_]+$")).match(name).hasMatch())
        return QStringLiteral("Letters, numbers and underscores only");
    return {};
}

void Account::signOut() {
    // The session's cookies go now and the site is told with them in the
    // background: deleting them only once it answered also deleted the next
    // sign-in's when it came first (posts then failed as signed out).
    const QUrl site(siteUrl());
    QStringList session;
    for (const auto &cookie : CookieJar::shared().cookiesForUrl(site)) {
        session << QString::fromUtf8(cookie.name() + '=' + cookie.value());
        CookieJar::shared().deleteCookie(cookie);
    }
    CookieJar::shared().save();
    if (!session.isEmpty())
        runInBackground([site, cookie = session.join(QStringLiteral("; ")).toUtf8()] {
            try {
                Http::postJson(QUrl(site.toString() + QStringLiteral("/api/auth/logout")), "{}",
                               {{"Accept", "application/json"}, {"Cookie", cookie}});
            } catch (const Error &) {
            }
        });
    setUser({});
    Database::setPref(QStringLiteral("account.lastSync"), 0);
    // A different account starts from scratch, uploading this device's history.
    Database::setPref(QStringLiteral("account.animePushedAt"), 0);
    Database::setPref(QStringLiteral("account.animePulledAt"), 0);
    Database::setPref(QStringLiteral("account.comicPushedAt"), 0);
    Database::setPref(QStringLiteral("account.libraryPushedAt"), 0);
    Database::setPref(QStringLiteral("account.libraryRemoved"), QStringList());
    m_soon.stop();
    m_dirty = false;
}

QVariantMap Account::importAll(const QJsonObject &session) {
    // A SCANCORE sync server (scancore-web) keeps only the devices' snapshots,
    // which hold the library, removals, progress and activity whole; the
    // website's own library and history calls do not exist there.
    bool devicesOnly = false;
    try {
        devicesOnly = getJson(QStringLiteral("/api/capabilities")).value("appSync").toObject().value("devicesOnly").toBool();
    } catch (const Error &e) {
        if (e.status() == 401)
            throw;
    }
    if (devicesOnly) {
        auto store = Sync::serverStore(siteUrl());
        const auto merged = Sync::exchange(*store, QStringLiteral("account.devicesSeen")).value("merged").toInt();
        return {{"libraryImported", 0}, {"historyImported", merged}, {"pushed", 0}, {"morePending", false}};
    }

    int libraryImported = 0;
    int historyImported = 0;
    int pushed = 0;
    QSet<QString> remoteIds;
    // Removed here since the last sync, and the website's ids for them.
    const auto removedList = Database::pref(QStringLiteral("account.libraryRemoved")).toStringList();
    const QSet<QString> removed(removedList.begin(), removedList.end());
    QHash<QString, QString> removedRemote;

    // Website library -> local library.
    QString cursor;
    for (int page = 0; page < 20; ++page) {
        const auto json =
            getJson(QStringLiteral("/api/library?limit=500") + (cursor.isEmpty() ? QString() : QStringLiteral("&cursor=") + cursor));
        for (const auto &value : json.value("entries").toArray()) {
            const auto entry = value.toObject();
            auto series = localSeries(entry.value("series").toObject());
            if (series.isEmpty())
                continue;
            const auto id = series.value("id").toString();
            remoteIds.insert(id);
            if (removed.contains(id)) {
                removedRemote.insert(id, entry.value("series").toObject().value("id").toString());
                continue;
            }
            if (!Library::instance()->isSaved(id)) {
                Library::upsertSeries(series);
                Database::exec(QStringLiteral("INSERT OR IGNORE INTO library(series_id, category, added_at, changed_at) "
                                              "VALUES(?, ?, ?, ?)"),
                               {id, categoryFor(entry.value("status").toString()), Database::now(), Database::now()});
                ++libraryImported;
            }
        }
        cursor = json.value("nextCursor").toString();
        if (cursor.isEmpty())
            break;
    }

    // Local removals -> website. Kept for the next sync when the website
    // cannot be reached; dropped when it no longer has the series, or the
    // series was saved here again.
    QSet<QString> removalsDone;
    for (const auto &id : removed) {
        const auto remote = removedRemote.value(id);
        if (remote.isEmpty() || Library::instance()->isSaved(id)) {
            removalsDone.insert(id);
            continue;
        }
        Request request;
        request.method = "DELETE";
        request.url = QUrl(siteUrl() + QStringLiteral("/api/library?seriesId=") + QString::fromLatin1(QUrl::toPercentEncoding(remote)));
        request.headers = {{"Accept", "application/json"}};
        try {
            const auto response = Http::send(request);
            if (response.status == 401)
                throw Error(QStringLiteral("Your %1 session ended. Sign in again.").arg(siteName()), 401);
            if (response.ok() || response.status == 404)
                removalsDone.insert(id);
        } catch (const Error &e) {
            if (e.status() == 401)
                throw;
        }
    }
    if (!removalsDone.isEmpty()) {
        // Re-read: a removal made while this sync ran must not be lost.
        auto pending = Database::pref(QStringLiteral("account.libraryRemoved")).toStringList();
        pending.removeIf([&](const QString &id) { return removalsDone.contains(id); });
        Database::setPref(QStringLiteral("account.libraryRemoved"), pending);
    }

    // What this website accepts from the app; an older one gets neither.
    bool comicHistory = false;
    bool animeBatch = false;
    bool appDevices = false;
    try {
        const auto appSync = getJson(QStringLiteral("/api/capabilities")).value("appSync").toObject();
        comicHistory = appSync.value("comicHistory").toBool();
        animeBatch = appSync.value("animeBatch").toBool();
        appDevices = appSync.value("devices").toBool();
    } catch (const Error &) {
    }

    // Local comic positions -> website first, so the pull below cannot
    // replace something newer that has not gone up yet.
    int comicsPushed = 0;
    try {
        if (comicHistory)
            comicsPushed = pushComicProgress(session.value("serverStamp").toDouble());
    } catch (const Error &e) {
        if (e.status() == 401)
            throw;
        qWarning().noquote() << "Comic progress sync:" << e.message();
    }

    // Website comic reading history -> local progress, when newer.
    qint64 newestPulledComic = 0;
    for (const auto &value : session.value("history").toArray()) {
        const auto entry = value.toObject();
        const auto comic = entry.value("comic").toObject();
        const auto chapter = entry.value("chapter").toObject();
        const auto url = comic.value("url").toString();
        const auto chapterUrl = chapter.value("episode_url").toString();
        auto source = comics::Registry::forUrl(url);
        if (!source || chapterUrl.isEmpty())
            continue;
        const auto id = Library::idFor(QStringLiteral("comic"), source->key(), url);
        const auto updated = qint64(entry.value("updatedAt").toDouble() / 1000);
        const auto local = Database::scalar(QStringLiteral("SELECT MAX(updated_at) FROM progress WHERE series_id = ?"), {id});
        if (local.isValid() && local.toLongLong() >= updated)
            continue;
        Library::upsertSeries({{"id", id},
                               {"media", "comic"},
                               {"source", source->key()},
                               {"sourceUrl", url},
                               {"title", comic.value("title").toString()},
                               {"cover", comic.value("thumbnail").toString()}});
        // The website may name the chapter by a different URL; use this app's
        // chapter when the URL or, failing that, the number matches.
        const auto episodeNo = chapter.value("episode_no").toVariant().toString();
        auto unitKey = Database::scalar(
            QStringLiteral("SELECT unit_key FROM units WHERE series_id = ? AND (unit_key = ? OR url = ?) LIMIT 1"),
            {id, chapterUrl, chapterUrl});
        if (!unitKey.isValid() && !episodeNo.isEmpty())
            unitKey = Database::scalar(QStringLiteral("SELECT unit_key FROM units WHERE series_id = ? AND number = ? LIMIT 1"),
                                       {id, episodeNo});
        const auto key = unitKey.isValid() ? unitKey.toString() : chapterUrl;
        const int total = std::max(1, entry.value("totalPanels").toInt(1));
        const int panel = entry.value("panelIndex").toInt();
        const double percent = std::min(1.0, double(panel + 1) / total);
        const auto position = QJsonDocument(QJsonObject{{"type", "strip"}, {"panel", panel}}).toJson(QJsonDocument::Compact);
        Database::exec(QStringLiteral(R"(
            INSERT INTO progress(series_id, unit_key, unit_title, unit_number, unit_url, position, percent, completed, updated_at)
            VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(series_id, unit_key) DO UPDATE SET position = excluded.position, percent = excluded.percent,
                completed = MAX(progress.completed, excluded.completed), updated_at = excluded.updated_at
        )"),
                       {id, key, chapter.value("title").toString(), episodeNo, chapterUrl, position, percent,
                        percent >= 0.98 ? 1 : 0, updated > 0 ? updated : Database::now()});
        newestPulledComic = std::max(newestPulledComic, updated);
        ++historyImported;
    }
    // What just came from the website must not be pushed straight back.
    if (newestPulledComic > Database::pref(QStringLiteral("account.comicPushedAt"), 0).toLongLong())
        Database::setPref(QStringLiteral("account.comicPushedAt"), newestPulledComic);

    // Local library -> website, for series the website already knows. Each
    // save is offered once: a series the website does not know would
    // otherwise be sent again at every sync.
    const auto librarySince = Database::pref(QStringLiteral("account.libraryPushedAt"), 0).toLongLong();
    qint64 libraryNewest = librarySince;
    bool libraryRetry = false;
    const auto transientStatus = [](int status) { return status == 0 || status == 429 || status >= 500; };
    QStringList comicUrls;
    const auto local = Database::query(QStringLiteral("SELECT s.id, s.media, s.source_url, l.category, l.added_at "
                                                      "FROM library l JOIN series s ON s.id = l.series_id WHERE l.added_at > ?"),
                                       {librarySince});
    for (const auto &value : local) {
        const auto row = value.toMap();
        libraryNewest = std::max(libraryNewest, row.value("added_at").toLongLong());
        const auto id = row.value("id").toString();
        if (remoteIds.contains(id))
            continue;
        const auto url = row.value("source_url").toString();
        if (row.value("media").toString() == QLatin1String("comic")) {
            comicUrls.append(url);
            continue;
        }
        const auto body = QJsonDocument(QJsonObject{{"sourceUrl", url}, {"status", statusFor(row.value("category").toString())}})
                              .toJson(QJsonDocument::Compact);
        try {
            const auto response = Http::postJson(QUrl(siteUrl() + QStringLiteral("/api/library")), body, {{"Accept", "application/json"}});
            if (response.ok())
                ++pushed;
            libraryRetry |= transientStatus(response.status);
        } catch (const Error &) {
            libraryRetry = true;
        }
    }
    for (int i = 0; i < comicUrls.size(); i += 120) {
        QJsonArray urls;
        for (const auto &url : comicUrls.mid(i, 120))
            urls.append(url);
        try {
            const auto response = Http::postJson(QUrl(siteUrl() + QStringLiteral("/api/library/import")),
                                                 QJsonDocument(QJsonObject{{"urls", urls}}).toJson(QJsonDocument::Compact),
                                                 {{"Accept", "application/json"}});
            if (response.ok())
                pushed += QJsonDocument::fromJson(response.body).object().value("imported").toInt();
            libraryRetry |= transientStatus(response.status);
        } catch (const Error &) {
            libraryRetry = true;
        }
    }
    if (!libraryRetry && libraryNewest > librarySince)
        Database::setPref(QStringLiteral("account.libraryPushedAt"), libraryNewest);
    // Anime watch progress, both ways: local changes first, so a pull cannot
    // overwrite something newer that has not gone up yet.
    int animePushed = 0;
    int animePulled = 0;
    bool morePending = false;
    try {
        animePushed = pushAnimeProgress(animeBatch, &morePending);
        animePulled = pullAnimeProgress();
    } catch (const Error &e) {
        if (e.status() == 401)
            throw;
        qWarning().noquote() << "Anime progress sync:" << e.message();
    }
    // Device snapshots, on websites that keep them: everything the website's
    // own library does not know (novels, most comic sites, activity).
    int devicesMerged = 0;
    if (appDevices) {
        try {
            auto store = Sync::serverStore(siteUrl());
            devicesMerged = Sync::exchange(*store, QStringLiteral("account.devicesSeen")).value("merged").toInt();
        } catch (const Error &e) {
            if (e.status() == 401)
                throw;
            qWarning().noquote() << "Device sync:" << e.message();
        }
    }
    return {{"libraryImported", libraryImported},
            {"historyImported", historyImported + animePulled + devicesMerged},
            {"pushed", pushed + animePushed + comicsPushed},
            {"morePending", morePending}};
}

Task *Account::sync() {
    if (m_syncing && m_task)
        return m_task;
    m_soon.stop();
    m_dirty = false;
    m_syncing = true;
    emit changed();
    auto *task = Task::run([]() -> QVariant {
        const auto session = getJson(QStringLiteral("/api/auth/session"));
        if (session.value("user").isNull())
            throw Error(QStringLiteral("Your %1 session ended. Sign in again.").arg(siteName()), 401);
        auto result = importAll(session);
        // The profile as the website has it now (a new photo or name).
        result.insert(QStringLiteral("user"), session.value("user").toObject().toVariantMap());
        return result;
    });
    // Kept alive here rather than by QML: a page that starts a sync and
    // drops the task would otherwise let the garbage collector cancel it
    // halfway, and "Syncing…" would never end.
    QQmlEngine::setObjectOwnership(task, QQmlEngine::CppOwnership);
    m_task = task;
    connect(task, &Task::changed, this, [this, task] {
        if (task->loading())
            return;
        // Notify while still marked as syncing so the library-change hook
        // does not start another sync.
        if (task->ok()) {
            Database::setPref(QStringLiteral("account.lastSync"), Database::now());
            const auto user = task->result().toMap().value(QStringLiteral("user")).toMap();
            const QVariantMap fresh{{"username", user.value("username")}, {"email", user.value("email")}, {"avatar", user.value("avatarUrl")}};
            if (!user.isEmpty() && signedIn() && fresh != m_user)
                setUser(fresh);
            // What the sync brought in is not a change to send back; without
            // this every sync scheduled another a minute later.
            const bool dirty = m_dirty;
            Library::instance()->notifyChanged();
            m_dirty = dirty;
            // The website limits writes per minute; send the rest shortly.
            if (task->result().toMap().value(QStringLiteral("morePending")).toBool())
                QTimer::singleShot(65000, this, [this] {
                    if (signedIn() && !m_syncing)
                        sync();
                });
        } else if (task->status() == 401) {
            setUser({});
        }
        m_syncing = false;
        m_lastSyncEnded = QDateTime::currentMSecsSinceEpoch();
        emit changed();
        task->deleteLater();
        if (m_dirty) {
            m_dirty = false;
            scheduleSync();
        }
    });
    return task;
}

} // namespace sc
