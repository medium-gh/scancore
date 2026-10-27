#include "Anime.h"

#include "App.h"

#include <optional>
#include "Library.h"
#include "Platform.h"
#include "MpvPlayer.h"
#include "Resolve.h"
#include "core/Database.h"
#include "core/Html.h"
#include "core/Parallel.h"
#include "core/StreamRelay.h"
#include "core/Text.h"
#include "extensions/ExtensionSource.h"
#include "extensions/Host.h"
#include "sources/Manifest.h"
#include "sources/anime/Kitsu.h"
#include "sources/anime/Playback.h"
#include "sources/anime/Registry.h"

#include <QCryptographicHash>
#include <atomic>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QTimeZone>
#include <QUrlQuery>

namespace sc {

namespace {

const QUrl kAniList(QStringLiteral("https://graphql.anilist.co/"));

const char *kCardFields = R"(
  id idMal
  title { romaji english native userPreferred }
  format status(version: 2) episodes duration season seasonYear
  startDate { year }
  genres averageScore popularity isAdult bannerImage
  coverImage { extraLarge large medium color }
  nextAiringEpisode { airingAt timeUntilAiring episode }
)";

const char *kMediaFields = R"(
  id idMal
  title { romaji english native userPreferred }
  synonyms
  description(asHtml: false)
  format status(version: 2) episodes duration
  season seasonYear startDate { year month day }
  genres averageScore meanScore popularity isAdult
  bannerImage
  coverImage { extraLarge large medium color }
  trailer { id site thumbnail }
  nextAiringEpisode { airingAt timeUntilAiring episode }
  studios(isMain: true) { nodes { name } }
  relations { edges { relationType node { id type format title { romaji english } coverImage { large } } } }
)";

// Until when AniList is taken as down (Unix seconds): its calls then go
// straight to what was cached or to the fallback (Kitsu) instead of waiting
// on it each time.
std::atomic<qint64> g_anilistDownUntil{0};

void anilistFailed() { g_anilistDownUntil = QDateTime::currentSecsSinceEpoch() + 120; }

QJsonObject graphql(const QString &query, const QJsonObject &variables, const QString &cacheKey, qint64 ttl) {
    if (!cacheKey.isEmpty())
        if (auto cached = Database::cacheGet(cacheKey))
            return QJsonDocument::fromJson(*cached).object();
    if (QDateTime::currentSecsSinceEpoch() < g_anilistDownUntil) {
        if (!cacheKey.isEmpty())
            if (auto stale = Database::cacheGet(cacheKey, true))
                return QJsonDocument::fromJson(*stale).object();
        throw Error(QStringLiteral("AniList is not answering right now. Try again later."), 503);
    }
    Response response;
    try {
        response = Http::postJson(kAniList, QJsonDocument(QJsonObject{{"query", query}, {"variables", variables}}).toJson(),
                                  {{"Accept", "application/json"}});
    } catch (const Error &e) {
        if (e.status() != 499)
            anilistFailed();
        if (!cacheKey.isEmpty())
            if (auto stale = Database::cacheGet(cacheKey, true))
                return QJsonDocument::fromJson(*stale).object();
        throw;
    }
    if (response.status == 429) {
        if (!cacheKey.isEmpty())
            if (auto stale = Database::cacheGet(cacheKey, true))
                return QJsonDocument::fromJson(*stale).object();
        throw Error(QStringLiteral("AniList is busy. Try again in a minute."), 429);
    }
    const auto root = QJsonDocument::fromJson(response.body).object();
    const auto data = root.value("data").toObject();
    if (data.isEmpty()) {
        QString reason;
        for (const auto &e : root.value("errors").toArray()) {
            const auto error = e.toObject();
            if (error.value("status").toInt() == 404)
                throw Error(QStringLiteral("Anime not found."), 404);
            if (reason.isEmpty())
                reason = error.value("message").toString().trimmed();
        }
        // AniList is down or has switched its API off (it did so in October
        // 2026): what was fetched last time, however old, beats an error.
        anilistFailed();
        if (!cacheKey.isEmpty())
            if (auto stale = Database::cacheGet(cacheKey, true))
                return QJsonDocument::fromJson(*stale).object();
        throw Error(reason.isEmpty() || reason.size() > 200
                        ? QStringLiteral("AniList is not answering right now. Try again later.")
                        : QStringLiteral("AniList is unavailable right now: %1").arg(reason),
                    response.status >= 400 ? response.status : 502);
    }
    if (!cacheKey.isEmpty())
        Database::cachePut(cacheKey, QJsonDocument(data).toJson(QJsonDocument::Compact), ttl);
    return data;
}

// AniList's answer, else (AniList down, not "not found") the fallback's;
// when that fails too, AniList's reason is the one given.
template <typename T>
T orFallback(const std::function<T()> &anilist, const std::function<T()> &fallback) {
    try {
        return anilist();
    } catch (const Error &e) {
        if (e.status() == 404 || e.status() == 499)
            throw;
        try {
            return fallback();
        } catch (const Error &f) {
            if (f.status() == 499)
                throw;
            throw e;
        }
    }
}

QString title(const QJsonObject &media) {
    const auto t = media.value("title").toObject();
    for (const char *key : {"english", "romaji", "userPreferred", "native"})
        if (auto value = t.value(QLatin1String(key)).toString(); !value.isEmpty())
            return value;
    return QStringLiteral("Untitled");
}

QString formatLabel(const QString &format) {
    static const QHash<QString, QString> labels{{"TV", "TV"},           {"TV_SHORT", "TV Short"}, {"MOVIE", "Movie"},
                                                {"SPECIAL", "Special"}, {"OVA", "OVA"},           {"ONA", "ONA"},
                                                {"MUSIC", "Music"}};
    return labels.value(format, format);
}

QString statusLabel(const QString &status) {
    static const QHash<QString, QString> labels{{"FINISHED", "Finished"},
                                                {"RELEASING", "Airing"},
                                                {"NOT_YET_RELEASED", "Upcoming"},
                                                {"CANCELLED", "Cancelled"},
                                                {"HIATUS", "Hiatus"}};
    return labels.value(status, status);
}

QString decodeEntitiesKeepLines(const QString &text) {
    QStringList lines;
    for (const auto &line : text.split(QLatin1Char('\n')))
        lines.append(decodeEntities(line));
    return lines.join(QLatin1Char('\n')).replace(QRegularExpression(QStringLiteral("\n{3,}")), QStringLiteral("\n\n"));
}

QString plainDescription(const QString &text) {
    auto out = text;
    out.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption),
                QStringLiteral("\n"));
    out.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    out = decodeEntitiesKeepLines(out);
    return out.trimmed();
}


QVariantMap card(const QJsonObject &m) {
    const auto cover = m.value("coverImage").toObject();
    const auto next = m.value("nextAiringEpisode").toObject();
    QVariantMap map{{"anilistId", m.value("id").toInt()},
                    {"idMal", m.value("idMal").toInt()},
                    {"id", Library::idFor(QStringLiteral("anime"), QStringLiteral("anilist"),
                                          QString::number(m.value("id").toInt()))},
                    {"media", QStringLiteral("anime")},
                    {"title", title(m)},
                    {"romaji", m.value("title").toObject().value("romaji").toString()},
                    {"cover", cover.value("extraLarge").toString(cover.value("large").toString())},
                    {"coverSmall", cover.value("large").toString(cover.value("medium").toString())},
                    {"color", cover.value("color").toString()},
                    {"banner", m.value("bannerImage").toString()},
                    {"format", formatLabel(m.value("format").toString())},
                    {"status", statusLabel(m.value("status").toString())},
                    {"rawStatus", m.value("status").toString()},
                    {"episodes", m.value("episodes").toInt()},
                    {"duration", m.value("duration").toInt()},
                    {"score", m.value("averageScore").toInt()},
                    {"popularity", m.value("popularity").toInt()},
                    {"year", m.value("seasonYear").toInt() ? m.value("seasonYear").toInt()
                                                            : m.value("startDate").toObject().value("year").toInt()},
                    {"season", m.value("season").toString()},
                    {"genres", m.value("genres").toVariant()},
                    {"isAdult", m.value("isAdult").toBool()},
                    {"nextEpisode", next.value("episode").toInt()},
                    {"nextAiringAt", qint64(next.value("airingAt").toDouble())}};
    return map;
}

QVariantList cards(const QJsonArray &media) {
    QVariantList out;
    for (const auto &m : media)
        if (m.isObject())
            out.append(card(m.toObject()));
    return out;
}

int releasedCount(const QJsonObject &media) {
    const auto next = media.value("nextAiringEpisode").toObject();
    if (!next.isEmpty())
        return std::max(0, next.value("episode").toInt() - 1);
    if (media.value("status").toString() == QLatin1String("NOT_YET_RELEASED"))
        return 0;
    return media.value("episodes").toInt();
}

QJsonObject mediaObject(int id) {
    return orFallback<QJsonObject>(
        [id] {
            const auto data = graphql(QStringLiteral("query ($id: Int) { Media(id: $id, type: ANIME) { %1 } }")
                                          .arg(QString::fromLatin1(kMediaFields)),
                                      {{"id", id}}, QStringLiteral("anilist:media:%1").arg(id), 6 * 3600);
            const auto media = data.value("Media").toObject();
            if (media.isEmpty())
                throw Error(QStringLiteral("Anime not found."), 404);
            return media;
        },
        [id] { return anime::kitsu::media(id); });
}

QPair<QString, QString> currentSeason() {
    const auto now = QDateTime::currentDateTimeUtc().date();
    const int m = now.month();
    const auto season = m <= 3 ? "WINTER" : m <= 6 ? "SPRING" : m <= 9 ? "SUMMER" : "FALL";
    return {QString::fromLatin1(season), QString::number(now.year())};
}

} // namespace

bool Anime::webPlayerAvailable() const {
#if defined(SCANCORE_HAVE_WEBENGINE) || defined(SCANCORE_HAVE_WEBVIEW)
    return true;
#else
    return false;
#endif
}

bool Anime::nativePlayerAvailable() const { return MpvPlayer::supported(); }

QString Anime::playerEngine() const {
    // Development override: SCANCORE_PLAYER=webengine|system.
    if (const auto forced = qEnvironmentVariable("SCANCORE_PLAYER"); !forced.isEmpty())
        return forced;
#if defined(SCANCORE_HAVE_WEBVIEW) && defined(Q_OS_WIN)
    // The episodes are H.264, which the Qt WebEngine in the official Qt
    // packages cannot decode; WebView2 can. It ships with Windows 11 and is
    // installed with Edge on Windows 10, but may have been removed.
    static const bool webView2 = [] {
        for (const auto *root : {"HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node", "HKEY_LOCAL_MACHINE\\SOFTWARE",
                                 "HKEY_CURRENT_USER\\Software"}) {
            const QSettings key(QStringLiteral("%1\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}")
                                    .arg(QLatin1String(root)),
                                QSettings::NativeFormat);
            const auto version = key.value(QStringLiteral("pv")).toString();
            if (!version.isEmpty() && version != QLatin1String("0.0.0.0"))
                return true;
        }
        return false;
    }();
    if (webView2)
        return QStringLiteral("system");
#elif defined(SCANCORE_HAVE_WEBVIEW)
    return QStringLiteral("system");
#endif
#ifdef SCANCORE_HAVE_WEBENGINE
    return QStringLiteral("webengine");
#else
    return {};
#endif
}

QString Anime::seriesId(int anilistId) const {
    // None yet: a MyAnimeList title still being looked up, or one AniList
    // does not have. All of them would otherwise share "anime:anilist:0".
    if (anilistId <= 0)
        return {};
    return Library::idFor(QStringLiteral("anime"), QStringLiteral("anilist"), QString::number(anilistId));
}

QStringList Anime::genres() const {
    return {QStringLiteral("Action"),    QStringLiteral("Adventure"),    QStringLiteral("Comedy"),
            QStringLiteral("Drama"),     QStringLiteral("Ecchi"),        QStringLiteral("Fantasy"),
            QStringLiteral("Horror"),    QStringLiteral("Mahou Shoujo"), QStringLiteral("Mecha"),
            QStringLiteral("Music"),     QStringLiteral("Mystery"),      QStringLiteral("Psychological"),
            QStringLiteral("Romance"),   QStringLiteral("Sci-Fi"),       QStringLiteral("Slice of Life"),
            QStringLiteral("Sports"),    QStringLiteral("Supernatural"), QStringLiteral("Thriller")};
}

QVariantMap Anime::seriesFor(const QVariantMap &media) const {
    QVariantMap extra{{"anilistId", media.value("anilistId")}, {"banner", media.value("banner")},
                      {"color", media.value("color")},         {"format", media.value("format")},
                      {"episodes", media.value("episodes")},   {"score", media.value("score")},
                      {"year", media.value("year")}};
    return {{"id", seriesId(media.value("anilistId").toInt())},
            {"media", QStringLiteral("anime")},
            {"source", QStringLiteral("anilist")},
            {"sourceUrl", QStringLiteral("https://anilist.co/anime/%1").arg(media.value("anilistId").toInt())},
            {"title", media.value("title")},
            {"cover", media.value("cover")},
            {"description", media.value("description")},
            {"status", media.value("status")},
            {"genres", media.value("genres")},
            {"extra", extra}};
}

namespace {

// MyAnimeList's rankings as the home page's shelves, in the shape of
// AniList's cards.
QVariantMap malCard(const QJsonObject &n) {
    static const QHash<QString, QString> formats{{"tv", "TV"},        {"movie", "Movie"},  {"ova", "OVA"},   {"ona", "ONA"},
                                                 {"special", "Special"}, {"music", "Music"}, {"tv_special", "TV Special"}};
    static const QHash<QString, QString> statuses{{"finished_airing", "Finished"}, {"currently_airing", "Airing"},
                                                  {"not_yet_aired", "Upcoming"}};
    const auto alt = n.value("alternative_titles").toObject();
    const auto english = alt.value("en").toString();
    const auto picture = n.value("main_picture").toObject();
    QStringList genres;
    for (const auto &g : n.value("genres").toArray())
        genres.append(g.toObject().value("name").toString());
    return {{"anilistId", 0},
            {"idMal", n.value("id").toInt()},
            {"id", Library::idFor(QStringLiteral("anime"), QStringLiteral("mal"), QString::number(n.value("id").toInt()))},
            {"media", QStringLiteral("anime")},
            {"title", english.isEmpty() ? n.value("title").toString() : english},
            {"romaji", n.value("title").toString()},
            {"cover", picture.value("large").toString(picture.value("medium").toString())},
            {"coverSmall", picture.value("medium").toString()},
            {"format", formats.value(n.value("media_type").toString(), n.value("media_type").toString().toUpper())},
            {"status", statuses.value(n.value("status").toString())},
            {"episodes", n.value("num_episodes").toInt()},
            {"score", int(std::round(n.value("mean").toDouble() * 10))},
            {"year", n.value("start_season").toObject().value("year").toInt(n.value("start_date").toString().left(4).toInt())},
            {"genres", genres},
            {"catalog", QStringLiteral("mal")}};
}

QVariantList malList(const QString &path, QList<QPair<QString, QString>> params) {
    const auto id = Anime::malClientId();
    params.append({QStringLiteral("fields"), QStringLiteral("alternative_titles,media_type,num_episodes,start_season,start_date,status,mean,main_picture,genres")});
    params.append({QStringLiteral("limit"), QStringLiteral("30")});
    QUrl url(QStringLiteral("https://api.myanimelist.net/v2") + path);
    QUrlQuery query;
    for (const auto &[k, v] : params)
        query.addQueryItem(k, v);
    url.setQuery(query);
    const auto key = QStringLiteral("mal:anime:") + url.toString();
    QByteArray body;
    if (auto cached = Database::cacheGet(key))
        body = *cached;
    else {
        const auto response = Http::get(url, {{"X-MAL-CLIENT-ID", id.toUtf8()}, {"Accept", "application/json"}}, 12000);
        if (!response.ok()) {
            if (auto stale = Database::cacheGet(key, true))
                body = *stale;
            else
                throw Error(QStringLiteral("MyAnimeList's catalogue is unavailable right now (%1).").arg(response.status), response.status);
        } else {
            body = response.body;
            Database::cachePut(key, body, 3600);
        }
    }
    QVariantList out;
    for (const auto &row : QJsonDocument::fromJson(body).object().value("data").toArray())
        out.append(malCard(row.toObject().value("node").toObject()));
    return out;
}

} // namespace

QString Anime::malClientId() {
#ifdef SCANCORE_MAL_CLIENT_ID
    return QStringLiteral(SCANCORE_MAL_CLIENT_ID);
#else
    return {};
#endif
}

QHash<int, QString> Anime::currentCovers(const QList<int> &ids) {
    QHash<int, QString> out;
    for (qsizetype from = 0; from < ids.size(); from += 50) {
        QJsonArray page;
        for (const int id : ids.mid(from, 50))
            page.append(id);
        QJsonArray media;
        try {
            // Not cached: the stored address is the stale one.
            media = graphql(QStringLiteral("query ($ids: [Int]) { Page(perPage: 50) { media(id_in: $ids, type: ANIME) "
                                           "{ id coverImage { extraLarge large } } } }"),
                            {{"ids", page}}, {}, 0)
                        .value("Page").toObject().value("media").toArray();
        } catch (const Error &e) {
            if (e.status() == 499)
                throw;
            // AniList's API down: each as its page gets it (Kitsu's), a
            // few at a time.
            for (const int id : ids.mid(from, 12)) {
                try {
                    media.append(mediaObject(id));
                } catch (const Error &) {
                }
            }
        }
        for (const auto &value : media) {
            const auto m = value.toObject();
            const auto cover = m.value("coverImage").toObject();
            out.insert(m.value("id").toInt(), cover.value("extraLarge").toString(cover.value("large").toString()));
        }
    }
    return out;
}

Task *Anime::anilistIdFor(int malId) {
    return Task::run([malId]() -> QVariant {
        const int id = orFallback<int>(
            [malId] {
                const auto data = graphql(QStringLiteral("query ($id: Int) { Media(idMal: $id, type: ANIME) { id } }"),
                                          {{"id", malId}}, QStringLiteral("anilist:idmal:%1").arg(malId), 30 * 86400);
                return data.value("Media").toObject().value("id").toInt();
            },
            [malId] {
                if (const int found = anime::kitsu::anilistIdForMal(malId))
                    return found;
                throw Error(QStringLiteral("AniList has no entry for this anime yet."), 404);
            });
        if (!id)
            throw Error(QStringLiteral("AniList has no entry for this anime yet."), 404);
        return id;
    });
}

Task *Anime::home() {
    const auto catalog = App::instance()->settings().value(QStringLiteral("catalogAnime")).toString();
    if (catalog == QLatin1String("mal") && !malClientId().isEmpty())
        return Task::run([]() -> QVariant {
            const auto [season, year] = currentSeason();
            // MyAnimeList's lists, Kitsu's like them when it is down.
            const auto mal = [](const QString &path, QList<QPair<QString, QString>> params, const QString &kitsuList) {
                return [=]() -> QVariant {
                    try {
                        return malList(path, params);
                    } catch (const Error &e) {
                        if (e.status() == 499)
                            throw;
                        try {
                            return cards(anime::kitsu::list(kitsuList));
                        } catch (const Error &) {
                            throw e;
                        }
                    }
                };
            };
            QList<std::function<QVariant()>> jobs{
                mal(QStringLiteral("/anime/ranking"), {{"ranking_type", "airing"}}, QStringLiteral("trending")),
                mal(QStringLiteral("/anime/ranking"), {{"ranking_type", "bypopularity"}}, QStringLiteral("popular")),
                mal(QStringLiteral("/anime/season/%1/%2").arg(year, season.toLower()), {{"sort", "anime_num_list_users"}},
                    QStringLiteral("season")),
                mal(QStringLiteral("/anime/ranking"), {{"ranking_type", "all"}}, QStringLiteral("top")),
                // The spotlight wants banners, which MyAnimeList has not:
                // AniList's, else Kitsu's.
                [] {
                    return QVariant(cards(orFallback<QJsonArray>(
                        [] {
                            const auto data = graphql(QStringLiteral("query { Page(page: 1, perPage: 12) { media(type: ANIME, "
                                                                     "isAdult: false, sort: [TRENDING_DESC]) { %1 } } }")
                                                          .arg(QString::fromLatin1(kCardFields)),
                                                      {}, QStringLiteral("anilist:spotlight"), 3600);
                            return data.value("Page").toObject().value("media").toArray();
                        },
                        [] { return anime::kitsu::list(QStringLiteral("trending")); })));
                },
            };
            const auto results = parallel(jobs);
            if (std::none_of(results.begin(), results.end(), [](auto &r) { return r.ok(); }))
                throw Error(results.first().error);
            auto value = [&](int i) { return results[i].ok() ? *results[i].value : QVariant(QVariantList()); };
            return QVariantMap{{"trending", value(0)}, {"popular", value(1)}, {"season", value(2)}, {"top", value(3)},
                               {"spotlight", value(4)},
                               {"seasonName", QStringLiteral("%1 %2").arg(season.left(1) + season.mid(1).toLower(), year)},
                               {"catalog", QStringLiteral("mal")}};
        });
    return Task::run([]() -> QVariant {
        const auto [season, year] = currentSeason();
        // Each list from AniList, else Kitsu's like it.
        const auto list = [](const QString &sort, const QString &extraDecl, const QString &extraArgs,
                             const QJsonObject &vars, const QString &kitsuList) {
            return [=]() -> QVariant {
                return cards(orFallback<QJsonArray>(
                    [&] {
                        QJsonObject variables = vars;
                        variables.insert("sort", QJsonArray::fromStringList(sort.split(',')));
                        variables.insert("perPage", 30);
                        const auto key = QStringLiteral("anilist:list:%1:%2")
                                             .arg(sort, QString::fromUtf8(QJsonDocument(vars).toJson(QJsonDocument::Compact)));
                        const auto data = graphql(QStringLiteral("query ($sort: [MediaSort], $perPage: Int%1) { Page(page: 1, "
                                                                 "perPage: $perPage) { media(type: ANIME, isAdult: false, sort: "
                                                                 "$sort%2) { %3 } } }")
                                                      .arg(extraDecl, extraArgs, QString::fromLatin1(kCardFields)),
                                                  variables, key, 3600);
                        return data.value("Page").toObject().value("media").toArray();
                    },
                    [&] { return anime::kitsu::list(kitsuList); }));
            };
        };
        QList<std::function<QVariant()>> jobs{
            list(QStringLiteral("TRENDING_DESC,POPULARITY_DESC"), {}, {}, {}, QStringLiteral("trending")),
            list(QStringLiteral("POPULARITY_DESC"), {}, {}, {}, QStringLiteral("popular")),
            list(QStringLiteral("POPULARITY_DESC,SCORE_DESC"),
                 QStringLiteral(", $season: MediaSeason, $seasonYear: Int"),
                 QStringLiteral(", season: $season, seasonYear: $seasonYear"),
                 {{"season", season}, {"seasonYear", year.toInt()}}, QStringLiteral("season")),
            list(QStringLiteral("SCORE_DESC"), {}, {}, {}, QStringLiteral("top")),
        };
        const auto results = parallel(jobs);
        if (std::none_of(results.begin(), results.end(), [](auto &r) { return r.ok(); }))
            throw Error(results.first().error);
        auto value = [&](int i) { return results[i].ok() ? *results[i].value : QVariant(QVariantList()); };
        return QVariantMap{{"trending", value(0)}, {"popular", value(1)}, {"season", value(2)}, {"top", value(3)},
                           {"seasonName", QStringLiteral("%1 %2").arg(season.left(1) + season.mid(1).toLower(), year)},
                           {"catalog", QStringLiteral("anilist")}};
    });
}

Task *Anime::search(const QVariantMap &filters) {
    return Task::run([filters]() -> QVariant {
        const auto q = filters.value("q").toString().simplified();
        auto sort = filters.value("sort").toString();
        if (sort.isEmpty() || (sort == QLatin1String("SEARCH_MATCH") && q.isEmpty()))
            sort = q.isEmpty() ? QStringLiteral("POPULARITY_DESC") : QStringLiteral("SEARCH_MATCH");
        QJsonObject vars{{"page", std::max(1, filters.value("page").toInt())},
                         {"perPage", 40},
                         {"sort", QJsonArray{sort}},
                         {"isAdult", filters.value("adult").toBool()}};
        if (!q.isEmpty())
            vars.insert("search", q);
        if (const auto g = filters.value("genres").toStringList(); !g.isEmpty())
            vars.insert("genres", QJsonArray::fromStringList(g));
        if (const auto y = filters.value("yearFrom").toInt(); y > 0)
            vars.insert("yearGreater", y * 10000);
        if (const auto y = filters.value("yearTo").toInt(); y > 0)
            vars.insert("yearLesser", (y + 1) * 10000);
        if (const auto s = filters.value("status").toString(); !s.isEmpty())
            vars.insert("status", s);
        if (const auto f = filters.value("format").toString(); !f.isEmpty())
            vars.insert("format", f);
        QJsonObject data;
        try {
            data = graphql(
            QStringLiteral("query ($search: String, $page: Int, $perPage: Int, $genres: [String], $yearGreater: "
                           "FuzzyDateInt, $yearLesser: FuzzyDateInt, $status: MediaStatus, $format: MediaFormat, $sort: "
                           "[MediaSort], $isAdult: Boolean) { Page(page: $page, perPage: $perPage) { pageInfo { "
                           "hasNextPage } media(search: $search, type: ANIME, sort: $sort, genre_in: $genres, "
                           "startDate_greater: $yearGreater, startDate_lesser: $yearLesser, status: $status, format: "
                           "$format, isAdult: $isAdult) { %1 } } }")
                .arg(QString::fromLatin1(kCardFields)),
            vars, QStringLiteral("anilist:search:") + QJsonDocument(vars).toJson(QJsonDocument::Compact), 3600);
        } catch (const Error &e) {
            if (e.status() == 499)
                throw;
            // AniList is down: Kitsu's search, its matches in AniList's shape.
            try {
                const auto found = anime::kitsu::search(filters);
                return QVariantMap{{"results", cards(found.value("media").toArray())},
                                   {"hasNextPage", found.value("hasNextPage").toBool()}};
            } catch (const Error &) {
                throw e;
            }
        }
        const auto page = data.value("Page").toObject();
        return QVariantMap{{"results", cards(page.value("media").toArray())},
                           {"hasNextPage", page.value("pageInfo").toObject().value("hasNextPage").toBool()}};
    });
}

Task *Anime::media(int anilistId) {
    return Task::run([anilistId]() -> QVariant {
        const auto m = mediaObject(anilistId);
        auto map = card(m);
        map.insert("description", plainDescription(m.value("description").toString()));
        // Every name it is known by, for finding it on anime sources.
        QStringList titles{map.value("title").toString()};
        const auto names = m.value("title").toObject();
        for (const char *key : {"english", "romaji", "userPreferred", "native"})
            titles.append(names.value(QLatin1String(key)).toString());
        for (const auto &synonym : m.value("synonyms").toArray())
            titles.append(synonym.toString());
        titles.removeAll(QString());
        titles.removeDuplicates();
        map.insert("titles", titles);
        QStringList studios;
        for (const auto &s : m.value("studios").toObject().value("nodes").toArray())
            studios.append(s.toObject().value("name").toString());
        map.insert("studios", studios);
        map.insert("released", releasedCount(m));
        const auto trailer = m.value("trailer").toObject();
        if (trailer.value("site").toString() == QLatin1String("youtube"))
            map.insert("trailer", QStringLiteral("https://www.youtube.com/watch?v=%1").arg(trailer.value("id").toString()));
        QVariantList relations;
        for (const auto &e : m.value("relations").toObject().value("edges").toArray()) {
            const auto edge = e.toObject();
            const auto node = edge.value("node").toObject();
            if (node.value("type").toString() != QLatin1String("ANIME"))
                continue;
            relations.append(QVariantMap{{"anilistId", node.value("id").toInt()},
                                         {"relation", edge.value("relationType").toString().replace('_', ' ').toLower()},
                                         {"title", title(node)},
                                         {"format", formatLabel(node.value("format").toString())},
                                         {"cover", node.value("coverImage").toObject().value("large").toString()}});
        }
        map.insert("relations", relations);
        return map;
    });
}

Task *Anime::episodes(int anilistId) {
    return Task::run([anilistId]() -> QVariant {
        const auto m = mediaObject(anilistId);
        QHash<int, QVariantMap> known;
        const auto cacheKey = QStringLiteral("anizip:%1").arg(anilistId);
        QJsonObject zip;
        if (auto cached = Database::cacheGet(cacheKey))
            zip = QJsonDocument::fromJson(*cached).object();
        else {
            try {
                const auto response =
                    Http::get(QUrl(QStringLiteral("https://api.ani.zip/mappings?anilist_id=%1").arg(anilistId)), {}, 10000);
                if (response.ok()) {
                    zip = QJsonDocument::fromJson(response.body).object();
                    Database::cachePut(cacheKey, response.body, 12 * 3600);
                }
            } catch (const Error &) {
                if (auto stale = Database::cacheGet(cacheKey, true))
                    zip = QJsonDocument::fromJson(*stale).object();
            }
        }
        const auto today = QDateTime::currentDateTimeUtc();
        int highest = 0;
        const auto eps = zip.value("episodes").toObject();
        static const QRegularExpression digits(QStringLiteral("^\\d+$"));
        for (auto it = eps.begin(); it != eps.end(); ++it) {
            if (!digits.match(it.key()).hasMatch())
                continue;
            const auto e = it.value().toObject();
            const auto air = e.value("airDate").toString(e.value("airdate").toString());
            if (!air.isEmpty() && QDateTime::fromString(air, Qt::ISODate) > today)
                continue;
            const int number = it.key().toInt();
            const auto titles = e.value("title").toObject();
            auto name = titles.value("en").toString();
            if (name.isEmpty())
                name = titles.value("x-jat").toString();
            known.insert(number, QVariantMap{{"title", name},
                                             {"image", e.value("image").toString()},
                                             {"overview", e.value("overview").toString(e.value("summary").toString())},
                                             {"airDate", air},
                                             {"runtime", e.value("runtime").toInt(e.value("length").toInt())}});
            highest = std::max(highest, number);
        }
        const int count = std::min(5000, std::max(releasedCount(m), highest));
        QVariantList out;
        for (int n = 1; n <= count; ++n) {
            auto row = known.value(n);
            if (row.value("title").toString().isEmpty())
                row.insert("title", QStringLiteral("Episode %1").arg(n));
            row.insert("number", n);
            row.insert("key", QString::number(n));
            out.append(row);
        }
        return out;
    });
}

Task *Anime::recommendations(int anilistId) {
    return Task::run([anilistId]() -> QVariant {
        QJsonObject data;
        try {
            data = graphql(QStringLiteral("query ($id: Int) { Media(id: $id, type: ANIME) { recommendations(sort: "
                                                 "[RATING_DESC, ID_DESC], page: 1, perPage: 20) { nodes { "
                                                 "mediaRecommendation { %1 } } } } }")
                                      .arg(QString::fromLatin1(kCardFields)),
                                  {{"id", anilistId}}, QStringLiteral("anilist:recs:%1").arg(anilistId), 86400);
        } catch (const Error &e) {
            // Only AniList has them; without it the page shows none.
            if (e.status() == 499)
                throw;
            return QVariantList();
        }
        QJsonArray media;
        for (const auto &n : data.value("Media").toObject().value("recommendations").toObject().value("nodes").toArray())
            if (const auto r = n.toObject().value("mediaRecommendation"); r.isObject())
                media.append(r);
        return cards(media);
    });
}

Task *Anime::schedule(qint64 start, qint64 end) {
    return Task::run([start, end]() -> QVariant {
        QVariantList out;
        // Episodes of one anime out at the same moment (a whole season at
        // once) are one entry: airingEpisode to airingEpisodeLast.
        QHash<QString, qsizetype> slot;
        const auto add = [&](const QJsonArray &rows) {
            for (const auto &row : rows) {
                const auto r = row.toObject();
                const auto m = r.value("media").toObject();
                if (m.isEmpty() || m.value("isAdult").toBool())
                    continue;
                const auto at = qint64(r.value("airingAt").toDouble());
                const auto episode = r.value("episode").toInt();
                const auto key = QString::number(m.value("id").toInt()) + QLatin1Char('@') + QString::number(at);
                if (const auto it = slot.constFind(key); it != slot.cend()) {
                    auto merged = out[*it].toMap();
                    merged.insert("airingEpisode", std::min(merged.value("airingEpisode").toInt(), episode));
                    merged.insert("airingEpisodeLast", std::max(merged.value("airingEpisodeLast").toInt(), episode));
                    out[*it] = merged;
                    continue;
                }
                auto c = card(m);
                c.insert("airingAt", at);
                c.insert("airingEpisode", episode);
                slot.insert(key, out.size());
                out.append(c);
            }
        };
        for (int page = 1; page <= 8; ++page) {
            QJsonObject data;
            try {
                data = graphql(QStringLiteral("query ($page: Int, $start: Int, $end: Int) { Page(page: $page, "
                                              "perPage: 50) { pageInfo { hasNextPage } airingSchedules(airingAt_greater: "
                                              "$start, airingAt_lesser: $end, sort: TIME) { id airingAt episode media "
                                              "{ %1 } } } }")
                                   .arg(QString::fromLatin1(kCardFields)),
                               {{"page", page}, {"start", double(start)}, {"end", double(end)}},
                               QStringLiteral("anilist:schedule:%1:%2:%3").arg(start).arg(end).arg(page), 3600);
            } catch (const Error &e) {
                if (page > 1 || e.status() == 499)
                    throw;
                // AniList is down: when Kitsu says airing shows come next
                // (episode numbers unknown).
                try {
                    add(anime::kitsu::airing(start, end));
                } catch (const Error &) {
                    throw e;
                }
                return out;
            }
            const auto p = data.value("Page").toObject();
            add(p.value("airingSchedules").toArray());
            if (!p.value("pageInfo").toObject().value("hasNextPage").toBool())
                break;
        }
        return out;
    });
}

bool Anime::isEmbedAllowed(const QString &raw) const {
    const QUrl url(raw);
    if (url.scheme() != QLatin1String("https"))
        return false;
    const auto hosts = Manifest::current().value("anime").toObject().value("reanime").toObject().value("embedHosts").toArray();
    for (const auto &h : hosts)
        if (hostMatches(url.host(), h.toString()))
            return true;
    return false;
}

Task *Anime::streams(int anilistId, int episode) {
    return Task::run([this, anilistId, episode]() -> QVariant {
        // The built-in player is part of the bundled pack.
        if (!Manifest::bundledEnabled())
            return QVariantMap{{"streams", QVariantList()}, {"message", Manifest::bundledReason()}};
        const auto base = Manifest::current()
                              .value("anime")
                              .toObject()
                              .value("reanime")
                              .toObject()
                              .value("base")
                              .toString(QStringLiteral("https://reanime.to"));
        const auto response =
            Http::get(QUrl(QStringLiteral("%1/api/flix/%2/%3").arg(base).arg(anilistId).arg(episode)),
                      {{"Referer", (base + QLatin1Char('/')).toUtf8()}, {"Accept", "application/json"}}, 12000);
        if (response.status == 404)
            return QVariantMap{{"streams", QVariantList()}, {"message", QStringLiteral("This episode is not available yet.")}};
        if (!response.ok())
            throw Error(QStringLiteral("The player server responded with HTTP %1.").arg(response.status), response.status);
        QVariantList streams;
        QSet<QString> seen;
        for (const auto &s : response.json().object().value("servers").toArray()) {
            const auto server = s.toObject();
            QUrl link(server.value("dataLink").toString());
            if (link.scheme() != QLatin1String("https") || seen.contains(link.toString()) || !isEmbedAllowed(link.toString()))
                continue;
            seen.insert(link.toString());
            QUrlQuery query(link);
            query.removeAllQueryItems(QStringLiteral("autoPlay"));
            query.addQueryItem(QStringLiteral("autoPlay"), QStringLiteral("true"));
            link.setQuery(query);
            const auto name = server.value("serverName").toString();
            streams.append(QVariantMap{{"url", link.toString()},
                                       {"quality", name.isEmpty() ? QStringLiteral("Server %1").arg(streams.size() + 1) : name},
                                       {"type", QStringLiteral("embed")}});
        }
        return QVariantMap{{"streams", streams},
                           {"message", streams.isEmpty() ? QStringLiteral("No servers have this episode yet.") : QString()}};
    });
}

Task *Anime::skipTimes(int anilistId, int episode, int duration) {
    return Task::run([anilistId, episode, duration]() -> QVariant {
        const auto m = mediaObject(anilistId);
        const int mal = m.value("idMal").toInt();
        if (!mal)
            return QVariantList();
        const int length = std::max(1, int(std::round(duration / 10.0)) * 10);
        const auto key = QStringLiteral("aniskip:%1:%2:%3").arg(mal).arg(episode).arg(length);
        QByteArray body;
        if (auto cached = Database::cacheGet(key))
            body = *cached;
        else {
            QUrl url(QStringLiteral("https://api.aniskip.com/v2/skip-times/%1/%2").arg(mal).arg(episode));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("episodeLength"), QString::number(length));
            for (const char *t : {"op", "ed", "mixed-op", "mixed-ed", "recap"})
                q.addQueryItem(QStringLiteral("types"), QString::fromLatin1(t));
            url.setQuery(q);
            const auto response = Http::get(url, {{"Accept", "application/json"}}, 8000);
            body = response.status == 404 ? QByteArray("{}") : response.body;
            Database::cachePut(key, body, 86400);
        }
        QVariantList out;
        const auto root = QJsonDocument::fromJson(body).object();
        if (!root.value("found").toBool())
            return out;
        for (const auto &r : root.value("results").toArray()) {
            const auto row = r.toObject();
            const auto interval = row.value("interval").toObject();
            out.append(QVariantMap{{"type", row.value("skipType").toString()},
                                   {"start", interval.value("startTime").toDouble()},
                                   {"end", interval.value("endTime").toDouble()}});
        }
        return out;
    });
}

// ---------------------------------------------------------------- extensions

namespace {

std::shared_ptr<ext::AnimeSource> requireSource(const QString &key) {
    auto source = anime::Registry::byKey(key);
    if (!source)
        throw Error(QStringLiteral("That anime source is not installed."), 404);
    if (!source->enabled())
        throw Error(source->blocked().isEmpty() ? QStringLiteral("That source is switched off.") : source->blocked());
    return source;
}

QVariantMap itemMap(const ext::AnimeSource &source, const ext::AnimeItem &item) {
    return {{"id", Library::idFor(QStringLiteral("anime"), source.key(), item.url)},
            {"media", QStringLiteral("anime")},
            {"source", source.key()},
            {"sourceUrl", item.url},
            {"url", item.url},
            {"sourceLabel", source.label()},
            {"title", item.title},
            {"cover", item.cover},
            {"referer", source.imageReferer()},
            {"seasons", item.seasons},
            {"season", item.season}};
}

// An anime's episodes on a source, oldest first; kept half an hour so the
// next episode starts without asking again.
QJsonArray cachedEpisodes(ext::AnimeSource &source, const QString &url, const QString &title = {}) {
    const auto key = QStringLiteral("anime-episodes:") + url;
    if (auto hit = Database::cacheGet(key))
        return QJsonDocument::fromJson(*hit).array();
    const auto episodes = source.episodes(url, title);
    if (!episodes.isEmpty())
        Database::cachePut(key, QJsonDocument(episodes).toJson(QJsonDocument::Compact), 1800);
    return episodes;
}

QVariantList episodeUnits(const QJsonArray &episodes) {
    QVariantList out;
    for (const auto &value : episodes) {
        const auto e = value.toObject();
        const double number = e.value("number").toDouble(-1);
        const auto name = e.value("name").toString().trimmed();
        QVariantMap unit{{"key", e.value("url").toString()},
                         {"url", e.value("url").toString()},
                         {"number", number >= 0 ? QVariant(number) : QVariant(out.size() + 1)},
                         {"title", name.isEmpty() ? QStringLiteral("Episode %1").arg(out.size() + 1) : name},
                         {"filler", e.value("filler").toBool()},
                         {"overview", e.value("summary").toString()},
                         {"image", e.value("preview").toString()}};
        if (const auto uploaded = qint64(e.value("uploaded").toDouble()); uploaded > 0)
            unit.insert("released", QDateTime::fromMSecsSinceEpoch(uploaded, QTimeZone::UTC).toString(Qt::ISODate));
        out.append(unit);
    }
    return out;
}

QString providerKey(int anilistId) { return QStringLiteral("anime.provider:%1").arg(anilistId); }

} // namespace

QVariantList Anime::fetchUnits(const QString &url) {
    const auto wrapped = ext::unwrapUrl(url);
    return episodeUnits(requireSource(wrapped ? wrapped->key : QString())->episodes(url, {}));
}

QVariantList Anime::sources() const {
    QVariantList out;
    for (const auto &source : anime::Registry::all())
        out.append(QVariantMap{{"key", source->key()},
                               {"label", source->label()},
                               {"enabled", source->enabled()},
                               {"disabledReason", source->blocked()},
                               {"nsfw", source->nsfw()},
                               {"language", source->config().language},
                               {"languageName", languageName(source->config().language)},
                               {"base", source->baseUrl()},
                               {"canBrowse", true},
                               {"browseLatest", true},
                               {"extension", !source->isServer()}});
    App::pinnedFirst(out);
    return out;
}

QString Anime::sourceLabel(const QString &key) const {
    const auto source = anime::Registry::byKey(key);
    return source ? source->label() : key;
}

Task *Anime::browse(const QString &key, const QString &listing, int page, const QString &query, const QVariantList &filters) {
    return Task::run([key, listing, page, query, filters]() -> QVariant {
        auto source = requireSource(key);
        // Popular and latest pages change slowly; a search is asked afresh.
        const bool cached = listing != QLatin1String("search");
        const auto cacheKey = QStringLiteral("browse:%1:%2:%3").arg(key, listing).arg(page);
        if (cached)
            if (auto hit = Database::cacheGet(cacheKey))
                return QJsonDocument::fromJson(*hit).toVariant();
        const auto result = source->browse(listing, page, query, QJsonArray::fromVariantList(filters));
        QVariantList items;
        for (const auto &item : result.items)
            items.append(itemMap(*source, item));
        const QVariantMap out{{"items", items}, {"hasNextPage", result.hasNextPage}};
        if (cached && !items.isEmpty())
            Database::cachePut(cacheKey, QJsonDocument::fromVariant(out).toJson(QJsonDocument::Compact), 900);
        return out;
    });
}

Task *Anime::filters(const QString &key) {
    return Task::run([key]() -> QVariant { return requireSource(key)->filters().toVariantList(); });
}

Task *Anime::sourceAnime(const QString &url, const QVariantMap &summary) {
    return Task::run([url, summary]() -> QVariant {
        const auto wrapped = ext::unwrapUrl(url);
        auto source = requireSource(wrapped ? wrapped->key : QString());
        const bool seasons = summary.value("seasons").toBool();
        const auto title = summary.value("title").toString();
        // Details and episodes (or seasons) at once.
        QList<std::function<QVariant()>> jobs{
            [source, url, title, seasons]() -> QVariant { return source->details(url, title, seasons).toVariantMap(); },
            [source, url, title, seasons]() -> QVariant {
                if (!seasons) {
                    // Kept for the player, which looks episodes up again.
                    const auto episodes = source->episodes(url, title);
                    if (!episodes.isEmpty())
                        Database::cachePut(QStringLiteral("anime-episodes:") + url,
                                           QJsonDocument(episodes).toJson(QJsonDocument::Compact), 1800);
                    return episodeUnits(episodes);
                }
                QVariantList list;
                for (const auto &item : source->seasons(url, title))
                    list.append(itemMap(*source, item));
                return list;
            }};
        const auto results = parallel(jobs);
        if (!results[1].ok())
            throw Error(results[1].error, results[1].status);
        const auto details = results[0].ok() ? results[0].value->toMap() : QVariantMap();
        // The source may only now say the anime has seasons.
        const bool split = seasons || details.value("fetchType").toString() == QLatin1String("seasons");
        QVariantList units = seasons ? QVariantList() : results[1].value->toList();
        QVariantList seasonList = seasons ? results[1].value->toList() : QVariantList();
        if (split && !seasons)
            for (const auto &item : source->seasons(url, title))
                seasonList.append(itemMap(*source, item));

        QVariantMap series = summary;
        series.insert("id", Library::idFor(QStringLiteral("anime"), source->key(), url));
        series.insert("media", QStringLiteral("anime"));
        series.insert("source", source->key());
        series.insert("sourceLabel", source->label());
        series.insert("sourceUrl", url);
        for (const char *key : {"title", "cover", "description"})
            if (const auto value = details.value(QLatin1String(key)).toString(); !value.isEmpty())
                series.insert(QLatin1String(key), value);
        if (const auto genres = details.value("genres").toStringList(); !genres.isEmpty())
            series.insert("genres", genres);
        if (const auto status = ext::ExtensionSource::statusName(details.value("status").toInt()); !status.isEmpty())
            series.insert("status", status);
        auto extra = series.value("extra").toMap();
        extra.insert("background", details.value("background"));
        extra.insert("author", details.value("author"));
        extra.insert("seasons", split);
        series.insert("extra", extra);
        return QVariantMap{{"series", series}, {"episodes", units}, {"seasons", seasonList}};
    });
}

Task *Anime::watchFrom(const QVariantMap &media, const QStringList &only) {
    return Task::run([media, only]() -> QVariant {
        const auto language = Database::pref(QStringLiteral("setting.sourceLanguages")).toString();
        QList<std::shared_ptr<ext::AnimeSource>> sources;
        QStringList keys;
        for (const auto &source : anime::Registry::all())
            if (source->enabled() && (only.isEmpty() ? language.isEmpty() || source->config().language == QLatin1String("*") ||
                                                           baseLanguage(source->config().language) == language
                                                     : only.contains(source->key()))) {
                sources.append(source);
                keys.append(source->key());
            }
        if (sources.isEmpty())
            return QVariantMap{{"matches", QVariantList()}, {"contentLanguage", contentLanguage()}};
        const auto id = media.value("anilistId").toInt();
        const auto sourcesHash = QCryptographicHash::hash(keys.join(QLatin1Char(',')).toUtf8(), QCryptographicHash::Md5).toHex().left(12);
        const auto cacheKey = QStringLiteral("resolve:anime:%1:%2:%3:%4").arg(QString::fromLatin1(sourcesHash), language, contentLanguage()).arg(id);
        if (id)
            if (auto cached = Database::cacheGet(cacheKey))
                return QJsonDocument::fromJson(*cached).toVariant();
        auto names = media.value("titles").toStringList();
        names.prepend(media.value("title").toString());
        names.append(media.value("romaji").toString());
        auto matches = resolveAcross<std::shared_ptr<ext::AnimeSource>>(sources, names);
        for (auto &match : matches) {
            auto map = match.toMap();
            if (const auto source = anime::Registry::byKey(map.value("source").toString()))
                map.insert(QStringLiteral("referer"), source->imageReferer());
            match = map;
        }
        const QVariantMap result{{"matches", matches}, {"contentLanguage", contentLanguage()}};
        if (id && !matches.isEmpty())
            Database::cachePut(cacheKey, QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact), 86400);
        return result;
    });
}

QVariantMap Anime::provider(int anilistId) const {
    auto chosen = Database::pref(providerKey(anilistId)).toMap();
    // A source since uninstalled: asked again.
    if (chosen.value("builtIn").toBool() || anime::Registry::byKey(chosen.value("source").toString()))
        return chosen;
    return {};
}

void Anime::setProvider(int anilistId, const QVariantMap &match) {
    if (match.value("source").toString().isEmpty()) {
        Database::setPref(providerKey(anilistId), QVariantMap{{"builtIn", true}});
        return;
    }
    Database::setPref(providerKey(anilistId), QVariantMap{{"source", match.value("source")},
                                                          {"url", match.value("url")},
                                                          {"title", match.value("title")},
                                                          {"sourceLabel", match.value("sourceLabel")}});
}

Task *Anime::videos(const QString &key, const QString &episodeUrl, const QString &animeUrl, double number) {
    return Task::run([key, episodeUrl, animeUrl, number]() -> QVariant {
        auto source = requireSource(key);
        auto url = episodeUrl;
        if (url.isEmpty()) {
            url = anime::episodeNumbered(cachedEpisodes(*source, animeUrl), number);
            if (url.isEmpty())
                return QVariantMap{{"hosters", QVariantList()},
                                   {"best", QVariant()},
                                   {"message", QStringLiteral("Episode %1 is not on %2 yet.").arg(number).arg(source->label())}};
        }
        QVariantList hosters;
        QList<int> waiting;
        QList<std::function<QJsonArray()>> jobs;
        for (const auto &value : source->hosters(url)) {
            const auto h = value.toObject();
            QVariantMap hoster{{"id", h.value("id").toString()}, {"name", h.value("name").toString()}, {"lazy", h.value("lazy").toBool()}};
            if (h.contains("videos")) {
                hoster.insert("state", QStringLiteral("ready"));
                hoster.insert("videos", anime::videoList(h.value("videos").toArray()));
            } else if (h.value("lazy").toBool()) {
                hoster.insert("state", QStringLiteral("idle"));
            } else {
                hoster.insert("state", QStringLiteral("loading"));
                waiting.append(int(hosters.size()));
                jobs.append([source, id = h.value("id").toString()] { return source->hosterVideos(id); });
            }
            hosters.append(hoster);
        }
        QVariantMap out{{"episodeUrl", url}, {"hosters", hosters}, {"best", anime::bestVideo(hosters)}};
        Task::report(out);
        // Every hoster at once; each shows up as it answers.
        parallelEach<QJsonArray>(jobs, 25000, [&](int i, const Outcome<QJsonArray> &result) {
            auto hoster = hosters[waiting[i]].toMap();
            if (result.ok()) {
                hoster.insert("state", QStringLiteral("ready"));
                hoster.insert("videos", anime::videoList(*result.value));
            } else {
                hoster.insert("state", QStringLiteral("failed"));
                hoster.insert("error", result.error);
            }
            hosters[waiting[i]] = hoster;
            out.insert("hosters", hosters);
            out.insert("best", anime::bestVideo(hosters));
            Task::report(out);
        });
        if (out.value("best").isNull())
            out.insert("message", hosters.isEmpty() ? QStringLiteral("%1 has no videos for this episode.").arg(source->label())
                                                    : QStringLiteral("None of the servers had a video that plays here."));
        return out;
    });
}

namespace {

struct Player {
    QString id;
    QString name;
    QString program;
};

// Desktop players found once: on the PATH, or where their installers put them.
QList<Player> desktopPlayers() {
    static const QList<Player> found = [] {
        QList<Player> out;
        const auto first = [](const QStringList &names, const QStringList &paths) {
            for (const auto &name : names)
                if (const auto path = QStandardPaths::findExecutable(name); !path.isEmpty())
                    return path;
            for (const auto &path : paths)
                if (QFileInfo(path).isExecutable())
                    return path;
            return QString();
        };
        const auto programFiles = qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files"));
        if (const auto mpv = first({QStringLiteral("mpv")}, {QStringLiteral("/opt/homebrew/bin/mpv"), QStringLiteral("/usr/local/bin/mpv"),
                                                            QStringLiteral("/Applications/mpv.app/Contents/MacOS/mpv")});
            !mpv.isEmpty())
            out.append({QStringLiteral("mpv"), QStringLiteral("mpv"), mpv});
        if (const auto iina = first({}, {QStringLiteral("/Applications/IINA.app/Contents/MacOS/iina-cli")}); !iina.isEmpty())
            out.append({QStringLiteral("iina"), QStringLiteral("IINA"), iina});
        if (const auto vlc = first({QStringLiteral("vlc")}, {programFiles + QStringLiteral("/VideoLAN/VLC/vlc.exe"),
                                                            QStringLiteral("C:/Program Files (x86)/VideoLAN/VLC/vlc.exe"),
                                                            QStringLiteral("/Applications/VLC.app/Contents/MacOS/VLC")});
            !vlc.isEmpty())
            out.append({QStringLiteral("vlc"), QStringLiteral("VLC"), vlc});
        return out;
    }();
    return found;
}

} // namespace

bool Anime::relayedExternally(const QVariantMap &video) const {
    return video::StreamRelay::wanted(QUrl(video.value(QStringLiteral("url")).toString())) && qgetenv("SCANCORE_STREAM_RELAY") != "0";
}

QVariantList Anime::externalPlayers() const {
    QVariantList out;
#ifdef Q_OS_ANDROID
    out.append(QVariantMap{{"id", "android"}, {"name", tr("Another app")}});
#else
    for (const auto &player : desktopPlayers())
        out.append(QVariantMap{{"id", player.id}, {"name", player.name}});
#endif
    return out;
}

bool Anime::openExternally(const QString &id, const QVariantMap &video, const QString &title, double startAt) const {
    auto url = video.value(QStringLiteral("url")).toString();
    if (url.isEmpty())
        return false;
    // Streams go through SCANCORE's relay as in its own player, so other
    // players get them undressed (disguised HLS) and with their headers;
    // the relay lives as long as SCANCORE does (relayedExternally).
    if (relayedExternally(video)) {
        Headers relayHeaders;
        const auto wanted = video.value(QStringLiteral("headers")).toMap();
        for (auto h = wanted.begin(); h != wanted.end(); ++h)
            relayHeaders.append({h.key().toUtf8(), h.value().toString().toUtf8()});
        const auto relayed = video::StreamRelay::wrap(QUrl(url), relayHeaders);
        if (relayed.host() == QLatin1String("127.0.0.1"))
            url = relayed.toString(QUrl::FullyEncoded);
    }
    QString agent = QString::fromUtf8(Http::userAgent());
    QString referer;
    QStringList others;
    const auto headers = video.value(QStringLiteral("headers")).toMap();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (it.key().compare(QLatin1String("user-agent"), Qt::CaseInsensitive) == 0)
            agent = it.value().toString();
        else if (it.key().compare(QLatin1String("referer"), Qt::CaseInsensitive) == 0)
            referer = it.value().toString();
        else
            others.append(it.key() + QStringLiteral(": ") + it.value().toString());
    }
    const auto tracks = [&](const char *kind) {
        QStringList out;
        for (const auto &value : video.value(QLatin1String(kind)).toList())
            if (const auto track = value.toMap().value(QStringLiteral("url")).toString(); !track.isEmpty())
                out.append(track);
        return out;
    };
#ifdef Q_OS_ANDROID
    Q_UNUSED(id)
    // Other apps cannot read SCANCORE's own files.
    if (QUrl(url).isLocalFile())
        return false;
    QStringList flat{QStringLiteral("User-Agent"), agent};
    if (!referer.isEmpty())
        flat << QStringLiteral("Referer") << referer;
    for (const auto &header : std::as_const(others))
        flat << header.section(QStringLiteral(": "), 0, 0) << header.section(QStringLiteral(": "), 1);
    QStringList names;
    for (const auto &value : video.value(QStringLiteral("subtitles")).toList())
        names.append(value.toMap().value(QStringLiteral("lang")).toString());
    return platform::openExternalPlayer(url, title, flat, int(startAt * 1000), tracks("subtitles"), names);
#else
    const auto players = desktopPlayers();
    const auto it = std::find_if(players.begin(), players.end(), [&](const Player &p) { return p.id == id; });
    if (it == players.end())
        return false;
    QStringList args;
    if (it->id == QLatin1String("vlc")) {
        // VLC takes the address's user agent and referrer, nothing more.
        args << url << QStringLiteral(":http-user-agent=") + agent;
        if (!referer.isEmpty())
            args << QStringLiteral(":http-referrer=") + referer;
        args << QStringLiteral("--meta-title=") + title;
        if (startAt > 1)
            args << QStringLiteral("--start-time=%1").arg(int(startAt));
    } else {
        // mpv, and IINA (mpv underneath) with its options prefixed.
        const QString prefix = it->id == QLatin1String("iina") ? QStringLiteral("--mpv-") : QStringLiteral("--");
        args << url << prefix + QStringLiteral("force-media-title=") + title << prefix + QStringLiteral("user-agent=") + agent;
        if (!referer.isEmpty())
            args << prefix + QStringLiteral("referrer=") + referer;
        for (const auto &header : std::as_const(others))
            args << prefix + QStringLiteral("http-header-fields-append=") + header;
        if (startAt > 1)
            args << prefix + QStringLiteral("start=%1").arg(int(startAt));
        for (const auto &track : tracks("subtitles"))
            args << prefix + QStringLiteral("sub-files-append=") + track;
        for (const auto &track : tracks("audio"))
            args << prefix + QStringLiteral("audio-files-append=") + track;
        // How to read it (a downloaded HLS episode), as SCANCORE's player does.
        const auto options = video.value(QStringLiteral("mpvArgs")).toMap();
        for (const auto *name : {"demuxer", "demuxer-lavf-format", "demuxer-lavf-o", "stream-lavf-o"})
            if (options.contains(QLatin1String(name)))
                args << prefix + QLatin1String(name) + QLatin1Char('=') + options.value(QLatin1String(name)).toString();
    }
    return QProcess::startDetached(it->program, args);
#endif
}

QVariantMap Anime::videoForDownload(const QString &key, const QString &episodeUrl, const QString &animeUrl, double number) {
    auto source = requireSource(key);
    auto url = episodeUrl;
    if (url.isEmpty())
        url = anime::episodeNumbered(cachedEpisodes(*source, animeUrl), number);
    if (url.isEmpty())
        throw Error(QStringLiteral("Episode %1 is not on %2 any more.").arg(number).arg(source->label()));
    QVariantList hosters;
    QList<int> waiting;
    QList<std::function<QJsonArray()>> jobs;
    for (const auto &value : source->hosters(url)) {
        const auto h = value.toObject();
        QVariantMap hoster{{"id", h.value("id").toString()}, {"name", h.value("name").toString()}};
        if (h.contains("videos")) {
            hoster.insert("state", QStringLiteral("ready"));
            hoster.insert("videos", anime::videoList(h.value("videos").toArray()));
        } else if (!h.value("lazy").toBool()) {
            waiting.append(int(hosters.size()));
            jobs.append([source, id = h.value("id").toString()] { return source->hosterVideos(id); });
        }
        hosters.append(hoster);
    }
    const auto answers = parallel<QJsonArray>(jobs);
    for (int i = 0; i < answers.size(); ++i) {
        if (!answers[i].ok())
            continue;
        auto hoster = hosters[waiting[i]].toMap();
        hoster.insert("state", QStringLiteral("ready"));
        hoster.insert("videos", anime::videoList(*answers[i].value));
        hosters[waiting[i]] = hoster;
    }
    // The server picked last time in the player, else the source's best.
    QVariantMap video;
    const auto wanted = App::instance()->settings().value(QStringLiteral("animeHoster")).toString();
    for (const auto &value : std::as_const(hosters)) {
        const auto hoster = value.toMap();
        if (wanted.isEmpty() || hoster.value("name").toString() != wanted)
            continue;
        for (const auto &v : hoster.value("videos").toList()) {
            const auto m = v.toMap();
            if (m.value("localServer").toBool())
                continue;
            if (video.isEmpty() || m.value("preferred").toBool())
                video = m;
            if (m.value("preferred").toBool())
                break;
        }
    }
    if (video.isEmpty()) {
        const auto best = anime::bestVideo(hosters).toMap();
        if (best.isEmpty())
            throw Error(hosters.isEmpty() ? QStringLiteral("%1 has no videos for this episode.").arg(source->label())
                                          : QStringLiteral("None of the servers had a video that can be downloaded."));
        video = hosters[best.value("hoster").toInt()].toMap().value("videos").toList()[best.value("video").toInt()].toMap();
    }
    if (!video.value("initialized", true).toBool() || video.value("url").toString().isEmpty())
        video = anime::videoMap(source->resolveVideo(video.value("id").toString()));
    if (video.value("url").toString().isEmpty())
        throw Error(QStringLiteral("The server gave no address for this episode's video."));
    return video;
}

Task *Anime::hosterVideos(const QString &key, const QString &hosterId) {
    return Task::run([key, hosterId]() -> QVariant { return anime::videoList(requireSource(key)->hosterVideos(hosterId)); });
}

Task *Anime::resolveVideo(const QString &key, const QString &videoId) {
    return Task::run([key, videoId]() -> QVariant {
        auto video = anime::videoMap(requireSource(key)->resolveVideo(videoId));
        if (video.value("url").toString().isEmpty())
            throw Error(QStringLiteral("This video is not available."), 404);
        return video;
    });
}

} // namespace sc
