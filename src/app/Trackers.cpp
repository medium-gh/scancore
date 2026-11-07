#include "Trackers.h"
#include "sources/servers/Servers.h"
#include "TrackerCommon.h"

#include "Notifications.h"

#include "App.h"
#include "core/Database.h"
#include "core/Text.h"

#include <algorithm>

#include <QCoreApplication>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QUuid>
#include <QWaitCondition>

#ifndef SCANCORE_ANILIST_CLIENT_ID
#define SCANCORE_ANILIST_CLIENT_ID ""
#endif
#ifndef SCANCORE_MAL_CLIENT_ID
#define SCANCORE_MAL_CLIENT_ID ""
#endif
#ifndef SCANCORE_COMICK_CLIENT_ID
#define SCANCORE_COMICK_CLIENT_ID ""
#endif

namespace sc {

namespace tracking {

const QString kAniListId = QStringLiteral(SCANCORE_ANILIST_CLIENT_ID);
const QString kMalId = QStringLiteral(SCANCORE_MAL_CLIENT_ID);
const QString kComickId = QStringLiteral(SCANCORE_COMICK_CLIENT_ID);
// Sign-in pages come back to SCANCORE on this port.
constexpr quint16 kRedirectPort = 52817;
const QString kMalRedirect = QStringLiteral("http://127.0.0.1:52817/mal");
const QString kComickRedirect = QStringLiteral("http://127.0.0.1:52817/comick");
const QString kComickAuth = QStringLiteral("https://comick.dev/api/auth/oauth2");
const QString kComickApi = QStringLiteral("https://api.comick.dev/integrations/v1");

QMutex g_linksMutex;

// A sign-in page coming back to the local address.
struct Pending {
    QMutex mutex;
    QWaitCondition arrived;
    QString code;
    QString error;
};

// The code from the sign-in page; up to five minutes for the user.
QString waitForCode(const std::shared_ptr<Pending> &pending, const QString &service) {
    QMutexLocker lock(&pending->mutex);
    for (int i = 0; i < 600 && pending->code.isEmpty() && pending->error.isEmpty(); ++i) {
        if (CancelScope::cancelled())
            throw Error(QStringLiteral("Cancelled"), 499);
        pending->arrived.wait(&pending->mutex, 500);
    }
    if (pending->error == QLatin1String("Cancelled"))
        throw Error(QStringLiteral("Cancelled"), 499);
    if (!pending->error.isEmpty() || pending->code.isEmpty())
        throw Error(QStringLiteral("%1 sign-in did not finish.").arg(service));
    return pending->code;
}

QString randomText(int length) {
    static const QString alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~");
    QString out;
    for (int i = 0; i < length; ++i)
        out += alphabet.at(QRandomGenerator::system()->bounded(alphabet.size()));
    return out;
}

QJsonObject anilistQuery(const QString &query, const QJsonObject &variables, const QString &token = {}) {
    Headers headers{{"Accept", "application/json"}};
    if (!token.isEmpty())
        headers.append({"Authorization", "Bearer " + token.toUtf8()});
    const auto response = Http::postJson(QUrl(QStringLiteral("https://graphql.anilist.co")),
                                         QJsonDocument(QJsonObject{{"query", query}, {"variables", variables}}).toJson(QJsonDocument::Compact),
                                         headers);
    const auto json = response.json().object();
    if (response.status == 401 ||
        (response.status == 400 &&
         json.value("errors").toArray().first().toObject().value("message").toString().contains(QLatin1String("Invalid token"))))
        throw Error(QStringLiteral("AniList signed SCANCORE out. Connect again in Settings → Connections."), 401);
    if (!response.ok()) {
        const auto message = json.value("errors").toArray().first().toObject().value("message").toString();
        throw Error(message.isEmpty() ? QStringLiteral("AniList answered %1.").arg(response.status) : QStringLiteral("AniList: ") + message,
                    response.status);
    }
    return json.value("data").toObject();
}

// MyAnimeList's token, refreshed when it is about to run out.
QString malToken() {
    auto account = Database::pref(QStringLiteral("tracker.mal")).toMap();
    if (account.isEmpty())
        return {};
    if (account.value("expires").toLongLong() > Database::now() + 120)
        return account.value("access").toString();
    const auto response = Http::postForm(QUrl(QStringLiteral("https://myanimelist.net/v1/oauth2/token")),
                                         {{"client_id", kMalId},
                                          {"grant_type", "refresh_token"},
                                          {"refresh_token", account.value("refresh").toString()}});
    if (!response.ok())
        throw Error(QStringLiteral("MyAnimeList signed SCANCORE out. Connect again in Settings → Connections."), 401);
    const auto json = response.json().object();
    account.insert("access", json.value("access_token").toString());
    account.insert("refresh", json.value("refresh_token").toString());
    account.insert("expires", Database::now() + json.value("expires_in").toInteger(3600));
    Database::setPref(QStringLiteral("tracker.mal"), account);
    return account.value("access").toString();
}

Response malCall(const QByteArray &method, const QString &path, const QList<QPair<QString, QString>> &form = {}) {
    Request request;
    request.method = method;
    request.url = QUrl(QStringLiteral("https://api.myanimelist.net/v2") + path);
    request.headers = {{"Authorization", "Bearer " + malToken().toUtf8()}, {"Accept", "application/json"}};
    if (!form.isEmpty()) {
        QUrlQuery query;
        for (const auto &[key, value] : form)
            query.addQueryItem(key, value);
        request.body = query.toString(QUrl::FullyEncoded).toUtf8();
        request.headers.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/x-www-form-urlencoded")));
    }
    const auto response = Http::send(request);
    if (response.status == 401)
        throw Error(QStringLiteral("MyAnimeList signed SCANCORE out. Connect again in Settings → Connections."), 401);
    return response;
}

// Comick's access token (15 minutes), refreshed with the rotating refresh
// token when it has run out.
QMutex g_comickMutex;
QString comickToken() {
    QMutexLocker lock(&g_comickMutex);
    auto account = Database::pref(QStringLiteral("tracker.comick")).toMap();
    if (account.isEmpty())
        return {};
    if (account.value("expires").toLongLong() > Database::now() + 60)
        return account.value("access").toString();
    const auto signedOut = QStringLiteral("Comick signed SCANCORE out. Connect again in Settings → Connections.");
    if (account.value("refresh").toString().isEmpty())
        throw Error(signedOut, 401);
    const auto response = Http::postForm(QUrl(kComickAuth + QStringLiteral("/token")),
                                         {{"grant_type", "refresh_token"},
                                          {"client_id", kComickId},
                                          {"refresh_token", account.value("refresh").toString()},
                                          {"resource", kComickApi}},
                                         {{"Accept", "application/json"}});
    if (!response.ok())
        throw Error(signedOut, 401);
    const auto json = response.json().object();
    account.insert("access", json.value("access_token").toString());
    if (json.contains("refresh_token"))
        account.insert("refresh", json.value("refresh_token").toString());
    account.insert("expires", json.contains("expires_at") ? json.value("expires_at").toInteger()
                                                          : Database::now() + json.value("expires_in").toInteger(900));
    Database::setPref(QStringLiteral("tracker.comick"), account);
    return account.value("access").toString();
}

Response comickCall(const QByteArray &method, const QString &path, const QJsonObject &body = {}) {
    Request request;
    request.method = method;
    request.url = QUrl(kComickApi + path);
    request.headers = {{"Authorization", "Bearer " + comickToken().toUtf8()}, {"Accept", "application/json"}};
    if (!body.isEmpty()) {
        request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
        request.headers.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
    }
    const auto response = Http::send(request);
    if (response.status == 401)
        throw Error(QStringLiteral("Comick signed SCANCORE out. Connect again in Settings → Connections."), 401);
    if (response.status == 429)
        throw Error(QStringLiteral("Comick is busy. Try again in a minute."), 429);
    return response;
}

QVariantMap allLinks() { return Database::pref(QStringLiteral("tracker.links")).toMap(); }

// Ids are numbers on AniList and MyAnimeList, and short text on Comick.
bool hasId(const QVariant &id) { return !id.toString().isEmpty() && id.toString() != QLatin1String("0"); }

void saveLink(const QString &seriesId, const QString &service, const QVariant &id, const QString &title) {
    QMutexLocker lock(&g_linksMutex);
    auto links = allLinks();
    auto entry = links.value(seriesId).toMap();
    if (hasId(id))
        entry.insert(service, QVariantMap{{"id", id}, {"title", title}});
    else
        entry.remove(service);
    if (entry.isEmpty())
        links.remove(seriesId);
    else
        links.insert(seriesId, entry);
    Database::setPref(QStringLiteral("tracker.links"), links);
}

void moveLinkEntries(const QString &pref, const QString &fromId, const QString &toId, bool keepOld) {
    auto all = Database::pref(pref).toMap();
    const auto entry = all.value(fromId).toMap();
    if (entry.isEmpty())
        return;
    auto target = all.value(toId).toMap();
    for (auto it = entry.begin(); it != entry.end(); ++it)
        if (!target.contains(it.key()))
            target.insert(it.key(), it.value());
    all.insert(toId, target);
    if (!keepOld)
        all.remove(fromId);
    Database::setPref(pref, all);
}

// Services that could not find a series by its titles, so the series page
// can ask the reader to pick the entry: {seriesId: {service: when}}.
QVariantMap allMisses() { return Database::pref(QStringLiteral("tracker.misses")).toMap(); }

void saveMiss(const QString &seriesId, const QString &service, bool missed) {
    QMutexLocker lock(&g_linksMutex);
    auto misses = allMisses();
    auto entry = misses.value(seriesId).toMap();
    if (missed == entry.contains(service))
        return;
    if (missed)
        entry.insert(service, Database::now());
    else
        entry.remove(service);
    if (entry.isEmpty())
        misses.remove(seriesId);
    else
        misses.insert(seriesId, entry);
    Database::setPref(QStringLiteral("tracker.misses"), misses);
}

// A search that found nothing is not repeated for a day (every finished
// chapter would ask again); linking by hand clears it.
bool missedRecently(const QString &seriesId, const QString &service) {
    const auto when = allMisses().value(seriesId).toMap().value(service);
    return when.isValid() && Database::now() - when.toLongLong() < 86400;
}

// AniList titles: [{id, idMal, title, titles: [...], detail, cover, total}].
QVariantList anilistSearch(const QString &media, const QString &title) {
    // Novels are AniList "manga" in the NOVEL format; a search for one would
    // otherwise fill with its comic. (A null format finds nothing, so the
    // filter is only written when used.)
    const auto filter = media == QLatin1String("novel") ? QStringLiteral(", format: NOVEL")
                        : media == QLatin1String("comic") ? QStringLiteral(", format_not: NOVEL")
                                                          : QString();
    const auto data = anilistQuery(QStringLiteral(R"(
        query ($search: String, $type: MediaType) {
          Page(perPage: 10) { media(search: $search, type: $type%1, sort: SEARCH_MATCH) {
            id idMal format episodes chapters startDate { year }
            title { romaji english native } synonyms coverImage { medium } } }
        })").arg(filter),
                                   {{"search", title}, {"type", media == QLatin1String("anime") ? "ANIME" : "MANGA"}});
    QVariantList out;
    for (const auto &value : data.value("Page").toObject().value("media").toArray()) {
        const auto m = value.toObject();
        const auto format = m.value("format").toString();
        // Novels are AniList "manga" in the NOVEL format; comics are the rest.
        if (media == QLatin1String("novel") ? format != QLatin1String("NOVEL")
                                            : media == QLatin1String("comic") && format == QLatin1String("NOVEL"))
            continue;
        const auto t = m.value("title").toObject();
        QStringList titles{t.value("english").toString(), t.value("romaji").toString(), t.value("native").toString()};
        for (const auto &s : m.value("synonyms").toArray())
            titles.append(s.toString());
        titles.removeAll(QString());
        out.append(QVariantMap{{"id", m.value("id").toInt()},
                               {"idMal", m.value("idMal").toInt()},
                               {"oneShot", format == QLatin1String("ONE_SHOT")},
                               {"title", titles.value(0)},
                               {"titles", titles},
                               {"detail", QStringList{format, m.value("startDate").toObject().value("year").isNull()
                                                                  ? QString()
                                                                  : QString::number(m.value("startDate").toObject().value("year").toInt())}
                                              .join(QStringLiteral(" · "))},
                               {"cover", m.value("coverImage").toObject().value("medium").toString()},
                               {"total", m.value(media == QLatin1String("anime") ? "episodes" : "chapters").toInt()}});
    }
    return out;
}

QVariantList malSearch(const QString &media, const QString &title) {
    const bool anime = media == QLatin1String("anime");
    const auto response = malCall("GET", QStringLiteral("/%1?q=%2&limit=10&fields=alternative_titles,media_type,%3,start_date,main_picture")
                                             .arg(anime ? QStringLiteral("anime") : QStringLiteral("manga"),
                                                  QString::fromLatin1(QUrl::toPercentEncoding(title.left(60))),
                                                  anime ? QStringLiteral("num_episodes") : QStringLiteral("num_chapters")));
    if (!response.ok())
        throw Error(QStringLiteral("MyAnimeList answered %1.").arg(response.status), response.status);
    QVariantList out;
    for (const auto &value : response.json().object().value("data").toArray()) {
        const auto node = value.toObject().value("node").toObject();
        const auto type = node.value("media_type").toString();
        const bool novel = type == QLatin1String("light_novel") || type == QLatin1String("novel");
        if (!anime && (media == QLatin1String("novel")) != novel)
            continue;
        const auto alt = node.value("alternative_titles").toObject();
        QStringList titles{node.value("title").toString(), alt.value("en").toString(), alt.value("ja").toString()};
        for (const auto &s : alt.value("synonyms").toArray())
            titles.append(s.toString());
        titles.removeAll(QString());
        out.append(QVariantMap{{"id", node.value("id").toInt()},
                               {"oneShot", type == QLatin1String("one_shot")},
                               {"title", titles.value(0)},
                               {"titles", titles},
                               {"detail", QStringList{type.toUpper().replace(QLatin1Char('_'), QLatin1Char(' ')),
                                                      node.value("start_date").toString().left(4)}
                                              .join(QStringLiteral(" · "))},
                               {"cover", node.value("main_picture").toObject().value("medium").toString()},
                               {"total", node.value(anime ? "num_episodes" : "num_chapters").toInt()}});
    }
    return out;
}

// Comick titles: [{id (its HID), title, titles, detail, cover, total, anilist}].
QVariantList comickSearch(const QString &media, const QString &title) {
    QUrlQuery query{{"q", title.left(100)}, {"media_type", media == QLatin1String("anime") ? "anime" : "manga"}, {"limit", "20"}};
    const auto response = comickCall("GET", QStringLiteral("/titles?") + query.toString(QUrl::FullyEncoded));
    if (!response.ok())
        throw Error(QStringLiteral("Comick answered %1.").arg(response.status), response.status);
    static const QHash<QString, QString> kinds{{"kr", "Manhwa"}, {"cn", "Manhua"}, {"jp", "Manga"}};
    QVariantList out;
    for (const auto &value : response.json().object().value("data").toArray()) {
        const auto t = value.toObject();
        QStringList titles{t.value("title").toString()};
        for (const auto &alt : t.value("titles").toArray())
            titles.append(alt.toObject().value("title").toString());
        titles.removeAll(QString());
        titles.removeDuplicates();
        const auto year = t.value("year").isDouble() ? QString::number(t.value("year").toInt()) : QString();
        out.append(QVariantMap{{"id", t.value("hid").toString()},
                               {"title", titles.value(0)},
                               {"titles", titles},
                               {"detail", QStringList{media == QLatin1String("anime") ? QStringLiteral("Anime")
                                                                                       : kinds.value(t.value("country").toString(), QStringLiteral("Comic")),
                                                      year}
                                              .join(QStringLiteral(" · "))},
                               {"cover", t.value("cover_url").toString()},
                               {"total", t.value("total_units").toInt()},
                               {"anilist", t.value("external_ids").toObject().value("anilist").toString().toInt()}});
    }
    return out;
}

// The one candidate whose title is the series' title, or none. A one-shot
// (often the pilot, named like the series: Komi's "Komi-san wa Komyushou
// desu." beside "Komi-san wa, Komyushou desu.") only when no series has
// the title.
QVariantMap exactMatch(const QVariantList &candidates, const QString &title) {
    const auto wanted = titleKey(title);
    if (wanted.isEmpty())
        return {};
    QVariantMap oneShot;
    for (const auto &value : candidates) {
        const auto candidate = value.toMap();
        const auto titles = candidate.value("titles").toStringList();
        if (std::none_of(titles.begin(), titles.end(), [&](const QString &t) { return titleKey(t) == wanted; }))
            continue;
        if (!candidate.value("oneShot").toBool())
            return candidate;
        if (oneShot.isEmpty())
            oneShot = candidate;
    }
    return oneShot;
}

// What the series' source stored about it: a Comick catalogue entry
// ("hid", "catalogTitle") and AniList and MyAnimeList ids ("anilist",
// "mal") from sites that link them, such as MangaDex.
QJsonObject seriesExtra(const QString &seriesId) {
    return QJsonDocument::fromJson(
               Database::scalar(QStringLiteral("SELECT extra FROM series WHERE id = ?"), {seriesId}).toString().toUtf8())
        .object();
}

// The names to look a series up by: its source's title, then the
// catalogue's (when it was opened from one) and one of the catalogue's
// other names; three at most, each searched only if the ones before found
// nothing.
QStringList lookupTitles(const QString &seriesId, const QString &title) {
    const auto extra = seriesExtra(seriesId);
    QStringList names{title, extra.value("catalogTitle").toString()};
    const auto others = extra.value("titles").toArray();
    if (!others.isEmpty())
        names.append(others.first().toString());
    QStringList out;
    QSet<QString> seen;
    for (const auto &name : names) {
        const auto key = titleKey(name);
        if (key.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        out.append(name);
    }
    return out.mid(0, 3);
}

// The first candidate that has one of the names, searching name by name.
QVariantMap matchByTitles(const std::function<QVariantList(const QString &)> &search, const QStringList &names) {
    for (const auto &name : names) {
        const auto candidates = search(name);
        for (const auto &wanted : names)
            if (auto match = exactMatch(candidates, wanted); !match.isEmpty())
                return match;
    }
    return {};
}

int anilistIdOf(const QString &seriesId) {
    int id = allLinks().value(seriesId).toMap().value("anilist").toMap().value("id").toInt();
    if (id <= 0 && seriesId.startsWith(QLatin1String("anime:anilist:")))
        id = seriesId.section(QLatin1Char(':'), 2).toInt();
    if (id <= 0)
        id = seriesExtra(seriesId).value("anilist").toInt();
    return std::max(id, 0);
}

// How far the series is: the highest chapter or episode number finished.
int progressOf(const QString &seriesId) {
    int best = 0;
    int count = 0;
    for (const auto &value : Database::query(QStringLiteral("SELECT unit_key, unit_number FROM progress WHERE series_id = ? AND completed = 1"),
                                             {seriesId})) {
        const auto row = value.toMap();
        ++count;
        bool ok = false;
        auto number = row.value("unit_number").toString().toDouble(&ok);
        if (!ok)
            number = row.value("unit_key").toString().toDouble(&ok);
        if (ok && number < 100000)
            best = std::max(best, int(number));
    }
    return best > 0 ? best : count;
}

void updateAniList(const QString &seriesId, const QString &media, const QString &title, int progress) {
    const auto account = Database::pref(QStringLiteral("tracker.anilist")).toMap();
    if (account.isEmpty())
        return;
    const auto token = account.value("token").toString();
    int id = allLinks().value(seriesId).toMap().value("anilist").toMap().value("id").toInt();
    // An AniList anime's own id ("anime:anilist:<id>"); an extension's
    // anime has none and is matched by title.
    if (id <= 0 && seriesId.startsWith(QLatin1String("anime:anilist:")))
        id = seriesId.section(QLatin1Char(':'), 2).toInt();
    if (id <= 0 && media == QLatin1String("comic")) {
        id = seriesExtra(seriesId).value("anilist").toInt();
        if (id > 0)
            saveLink(seriesId, QStringLiteral("anilist"), id, title);
    }
    if (id <= 0) {
        if (missedRecently(seriesId, QStringLiteral("anilist")))
            return;
        const auto match = matchByTitles([&](const QString &t) { return anilistSearch(media, t); }, lookupTitles(seriesId, title));
        saveMiss(seriesId, QStringLiteral("anilist"), match.isEmpty());
        if (match.isEmpty())
            return;
        id = match.value("id").toInt();
        saveLink(seriesId, QStringLiteral("anilist"), id, match.value("title").toString());
    }
    const auto current = anilistQuery(QStringLiteral("query ($id: Int) { Media(id: $id) { episodes chapters mediaListEntry { progress status } } }"),
                                      {{"id", id}}, token)
                             .value("Media")
                             .toObject();
    const auto entry = current.value("mediaListEntry").toObject();
    if (entry.value("progress").toInt() >= progress)
        return;
    const int total = current.value(media == QLatin1String("anime") ? "episodes" : "chapters").toInt();
    const auto status = total > 0 && progress >= total ? QStringLiteral("COMPLETED") : QStringLiteral("CURRENT");
    anilistQuery(QStringLiteral("mutation ($id: Int, $progress: Int, $status: MediaListStatus) {"
                                " SaveMediaListEntry(mediaId: $id, progress: $progress, status: $status) { id } }"),
                 {{"id", id}, {"progress", progress}, {"status", status}}, token);
}

void updateMal(const QString &seriesId, const QString &media, const QString &title, int progress) {
    if (Database::pref(QStringLiteral("tracker.mal")).toMap().isEmpty())
        return;
    const bool anime = media == QLatin1String("anime");
    const auto links = allLinks().value(seriesId).toMap();
    int id = links.value("mal").toMap().value("id").toInt();
    const auto extra = media == QLatin1String("comic") ? seriesExtra(seriesId) : QJsonObject();
    if (id <= 0)
        id = extra.value("mal").toInt();
    if (id <= 0) {
        // Through AniList, which knows MyAnimeList's ids.
        int anilistId = links.value("anilist").toMap().value("id").toInt();
        if (anilistId <= 0 && seriesId.startsWith(QLatin1String("anime:anilist:")))
            anilistId = seriesId.section(QLatin1Char(':'), 2).toInt();
        if (anilistId <= 0)
            anilistId = extra.value("anilist").toInt();
        if (anilistId > 0)
            id = anilistQuery(QStringLiteral("query ($id: Int) { Media(id: $id) { idMal } }"), {{"id", anilistId}})
                     .value("Media").toObject().value("idMal").toInt();
        if (id <= 0) {
            if (missedRecently(seriesId, QStringLiteral("mal")))
                return;
            const auto match = matchByTitles([&](const QString &t) { return malSearch(media, t); }, lookupTitles(seriesId, title));
            saveMiss(seriesId, QStringLiteral("mal"), match.isEmpty());
            if (match.isEmpty())
                return;
            id = match.value("id").toInt();
        }
    }
    if (!links.contains(QStringLiteral("mal")))
        saveLink(seriesId, QStringLiteral("mal"), id, title);
    const auto kind = anime ? QStringLiteral("anime") : QStringLiteral("manga");
    const auto info = malCall("GET", QStringLiteral("/%1/%2?fields=my_list_status,%3").arg(kind).arg(id).arg(anime ? "num_episodes" : "num_chapters"));
    if (!info.ok())
        return;
    const auto json = info.json().object();
    const auto mine = json.value("my_list_status").toObject();
    if (mine.value(anime ? "num_episodes_watched" : "num_chapters_read").toInt() >= progress)
        return;
    const int total = json.value(anime ? "num_episodes" : "num_chapters").toInt();
    const bool done = total > 0 && progress >= total;
    malCall("PATCH", QStringLiteral("/%1/%2/my_list_status").arg(kind).arg(id),
            {{anime ? "num_watched_episodes" : "num_chapters_read", QString::number(progress)},
             {"status", done ? QStringLiteral("completed") : anime ? QStringLiteral("watching") : QStringLiteral("reading")}});
}

void updateComick(const QString &seriesId, const QString &media, const QString &title, int progress) {
    // Comick lists manga and anime.
    if (Database::pref(QStringLiteral("tracker.comick")).toMap().isEmpty() || media == QLatin1String("novel"))
        return;
    const auto links = allLinks().value(seriesId).toMap();
    auto hid = links.value("comick").toMap().value("id").toString();
    // A comic opened from Comick's catalogue already knows its entry there.
    const auto extra = media == QLatin1String("comic") ? seriesExtra(seriesId) : QJsonObject();
    if (hid.isEmpty() && media == QLatin1String("comic")) {
        hid = extra.value("hid").toString();
        if (!hid.isEmpty())
            saveLink(seriesId, QStringLiteral("comick"), hid, extra.value("catalogTitle").toString());
    }
    if (hid.isEmpty()) {
        int anilistId = links.value("anilist").toMap().value("id").toInt();
        if (anilistId <= 0 && seriesId.startsWith(QLatin1String("anime:anilist:")))
            anilistId = seriesId.section(QLatin1Char(':'), 2).toInt();
        if (anilistId <= 0)
            anilistId = extra.value("anilist").toInt();
        if (missedRecently(seriesId, QStringLiteral("comick")))
            return;
        const auto candidates = comickSearch(media, title);
        QVariantMap match;
        // By AniList entry when both know it, else by title.
        for (const auto &value : candidates)
            if (anilistId > 0 && value.toMap().value("anilist").toInt() == anilistId)
                match = value.toMap();
        const auto names = lookupTitles(seriesId, title);
        for (int i = 0; match.isEmpty() && i < names.size(); ++i)
            match = exactMatch(candidates, names[i]);
        if (match.isEmpty() && names.size() > 1)
            match = matchByTitles([&](const QString &t) { return comickSearch(media, t); }, names.mid(1));
        saveMiss(seriesId, QStringLiteral("comick"), match.isEmpty());
        if (match.isEmpty())
            return;
        hid = match.value("id").toString();
        saveLink(seriesId, QStringLiteral("comick"), hid, match.value("title").toString());
    }
    const auto path = QStringLiteral("/me/library/") + QString::fromLatin1(QUrl::toPercentEncoding(hid));
    const auto current = comickCall("GET", path);
    int status = 1;
    if (current.ok()) {
        const auto entry = current.json().object().value("data").toObject();
        if (entry.value("progress").toObject().value("number").toString().toDouble() >= progress)
            return;
        const int total = entry.value("total_units").toInt();
        status = total > 0 && progress >= total ? 2 : entry.value("status").toInt() == 2 ? 2 : 1;
    } else if (current.status != 404) {
        return;
    }
    comickCall("PUT", path, QJsonObject{{"status", status}, {"progress", QJsonObject{{"number", QString::number(progress)}}}});
}

} // namespace tracking

using namespace tracking;

Trackers::Trackers(QObject *parent) : QObject(parent) {}

Trackers *Trackers::instance() {
    static auto *trackers = [] {
        auto *created = new Trackers;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return trackers;
}

bool Trackers::anilistAvailable() const { return !kAniListId.isEmpty(); }
bool Trackers::malAvailable() const { return !kMalId.isEmpty(); }
bool Trackers::comickAvailable() const { return !kComickId.isEmpty(); }

QVariantMap Trackers::anilist() const {
    const auto account = Database::pref(QStringLiteral("tracker.anilist")).toMap();
    return account.isEmpty() ? QVariantMap()
                             : QVariantMap{{"name", account.value("name")}, {"avatar", account.value("avatar")}, {"expired", account.value("expired").toBool()}};
}

QVariantMap Trackers::mal() const {
    const auto account = Database::pref(QStringLiteral("tracker.mal")).toMap();
    return account.isEmpty() ? QVariantMap()
                             : QVariantMap{{"name", account.value("name")}, {"avatar", account.value("avatar")}, {"expired", account.value("expired").toBool()}};
}

QVariantMap Trackers::comick() const {
    const auto account = Database::pref(QStringLiteral("tracker.comick")).toMap();
    return account.isEmpty() ? QVariantMap()
                             : QVariantMap{{"name", account.value("name")}, {"avatar", account.value("avatar")}, {"expired", account.value("expired").toBool()}};
}

QVariantMap Trackers::account(const QString &service) const {
    const auto a = Database::pref(QStringLiteral("tracker.") + service).toMap();
    return a.isEmpty() || a.value("name").toString().isEmpty() && !a.value("expired").toBool()
               ? QVariantMap()
               : QVariantMap{{"name", a.value("name")}, {"avatar", a.value("avatar")}, {"expired", a.value("expired").toBool()}};
}

QVariantList Trackers::services() const {
    struct Builtin {
        const char *key;
        const char *label;
        QStringList media;
        const char *auth;
        const char *site;
        bool available;
    };
    const QList<Builtin> builtins{
        {"anilist", "AniList", {"comic", "novel", "anime"}, "token", "https://anilist.co/", anilistAvailable()},
        {"mal", "MyAnimeList", {"comic", "novel", "anime"}, "oauth", "https://myanimelist.net/", malAvailable()},
        {"comick", "Comick", {"comic", "anime"}, "oauth", "https://comick.dev/", comickAvailable()},
    };
    const QHash<QString, QPair<const char *, const char *>> more{
        {"kitsu", {"password", "https://kitsu.app/"}},
        {"mangaupdates", {"password", "https://www.mangaupdates.com/"}},
        {"shikimori", {"oauth", "https://shikimori.io/"}},
        {"bangumi", {"token", "https://bgm.tv/"}},
        {"simkl", {"pin", "https://simkl.com/"}},
    };
    QVariantList out;
    auto add = [&](const QString &key, const QString &label, const QStringList &media, const QString &auth, const QString &site) {
        const auto a = account(key);
        out.append(QVariantMap{{"key", key}, {"label", label}, {"media", media}, {"auth", auth}, {"site", site},
                               {"name", a.value("name")}, {"avatar", a.value("avatar")}, {"expired", a.value("expired").toBool()},
                               {"connected", !a.value("name").toString().isEmpty() && !a.value("expired").toBool()}});
    };
    for (const auto &b : builtins)
        if (b.available)
            add(QString::fromLatin1(b.key), QString::fromLatin1(b.label), b.media, QString::fromLatin1(b.auth), QString::fromLatin1(b.site));
    for (const auto &s : tracking::services())
        if (s.available())
            add(s.key, s.label, s.media, QString::fromLatin1(more.value(s.key).first), QString::fromLatin1(more.value(s.key).second));
    // The reader's Komga and Kavita servers: connected while they are in
    // Settings → Sources → Your servers.
    for (const auto &s : tracking::allServices())
        if (s.own)
            out.append(QVariantMap{{"key", s.key}, {"label", s.label}, {"media", s.media}, {"auth", "server"}, {"site", ""},
                                   {"name", s.label}, {"avatar", QString()}, {"expired", false}, {"connected", true}});
    return out;
}

bool Trackers::anyConnected() const {
    for (const auto &value : services())
        if (value.toMap().value("connected").toBool())
            return true;
    return false;
}

Task *Trackers::signIn(const QString &service) {
    if (service == QLatin1String("mal"))
        return malSignIn();
    if (service == QLatin1String("comick"))
        return comickSignIn();
    if (service == QLatin1String("simkl")) {
        auto *task = Task::run([]() -> QVariant {
            const auto pin = simklPin();
            QMetaObject::invokeMethod(Trackers::instance(), [pin] {
                Trackers::instance()->m_pin = pin;
                QDesktopServices::openUrl(QUrl(pin.value("url").toString()));
                emit Trackers::instance()->changed();
            });
            return simklWait(pin.value("code").toString(), pin.value("seconds").toInt(), pin.value("interval").toInt());
        });
        connect(task, &Task::changed, this, [this, task] {
            if (task->loading())
                return;
            m_pin.clear();
            emit changed();
        });
        return task;
    }
    if (service == QLatin1String("shikimori")) {
        QString problem;
        const auto pending = std::static_pointer_cast<Pending>(listen(QStringLiteral("/shikimori"), &problem));
        if (!pending)
            return Task::run([problem]() -> QVariant { throw Error(problem); });
        const auto redirect = QStringLiteral("http://127.0.0.1:%1/shikimori").arg(kRedirectPort);
        QDesktopServices::openUrl(QUrl(shikimoriAuthorizeUrl(redirect, m_state)));
        auto *task = Task::run([pending, redirect]() -> QVariant {
            return shikimoriCode(waitForCode(pending, QStringLiteral("Shikimori")), redirect);
        });
        watch(task);
        return task;
    }
    return Task::run([]() -> QVariant { throw Error(QStringLiteral("Unknown service.")); });
}

Task *Trackers::passwordSignIn(const QString &service, const QString &user, const QString &password) {
    auto *task = Task::run([service, user, password]() -> QVariant { return tracking::passwordSignIn(service, user, password); });
    task->then(this, [this](const QVariant &) { emit changed(); });
    return task;
}

Task *Trackers::useToken(const QString &service, const QString &token) {
    if (service == QLatin1String("anilist"))
        return anilistUseToken(token);
    auto *task = Task::run([service, token]() -> QVariant { return tokenSignIn(service, token); });
    task->then(this, [this](const QVariant &) { emit changed(); });
    return task;
}

QString Trackers::tokenPage(const QString &service) const {
    return service == QLatin1String("bangumi") ? QStringLiteral("https://next.bgm.tv/demo/access-token") : anilistAuthorizeUrl();
}

QString Trackers::entryUrl(const QString &service, const QString &media, const QVariant &id) const {
    const bool anime = media == QLatin1String("anime");
    const auto key = id.toString();
    if (service == QLatin1String("anilist"))
        return QStringLiteral("https://anilist.co/%1/%2").arg(anime ? "anime" : "manga", key);
    if (service == QLatin1String("mal"))
        return QStringLiteral("https://myanimelist.net/%1/%2").arg(anime ? "anime" : "manga", key);
    if (service == QLatin1String("comick"))
        return QStringLiteral("https://comick.dev/%1/%2").arg(anime ? "anime" : "comic", key);
    if (service == QLatin1String("kitsu"))
        return QStringLiteral("https://kitsu.app/%1/%2").arg(anime ? "anime" : "manga", key);
    // MangaUpdates' addresses carry the id in base 36.
    if (service == QLatin1String("mangaupdates"))
        return QStringLiteral("https://www.mangaupdates.com/series/") + QString::number(key.toLongLong(), 36);
    if (service == QLatin1String("shikimori"))
        return QStringLiteral("https://shikimori.io/%1/%2").arg(anime ? "animes" : media == QLatin1String("novel") ? "ranobe" : "mangas", key);
    if (service == QLatin1String("bangumi"))
        return QStringLiteral("https://bgm.tv/subject/") + key;
    if (service == QLatin1String("simkl"))
        return QStringLiteral("https://simkl.com/anime/") + key;
    return {};
}

QString Trackers::anilistAuthorizeUrl() const {
    return QStringLiteral("https://anilist.co/api/v2/oauth/authorize?client_id=%1&response_type=token").arg(kAniListId);
}

void Trackers::anilistSignIn() const { QDesktopServices::openUrl(QUrl(anilistAuthorizeUrl())); }

Task *Trackers::anilistUseToken(const QString &pasted) {
    // The whole address bar works too: …#access_token=…&token_type=…
    auto token = pasted.trimmed();
    if (const auto at = token.indexOf(QLatin1String("access_token=")); at >= 0)
        token = token.mid(at + 13).section(QLatin1Char('&'), 0, 0);
    auto *task = Task::run([token]() -> QVariant {
        if (token.isEmpty())
            throw Error(QStringLiteral("Paste the token AniList showed."));
        const auto viewer = anilistQuery(QStringLiteral("query { Viewer { id name avatar { medium } } }"), {}, token).value("Viewer").toObject();
        if (viewer.isEmpty())
            throw Error(QStringLiteral("AniList did not accept that token."));
        Database::setPref(QStringLiteral("tracker.anilist"), QVariantMap{{"token", token},
                                                                         {"userId", viewer.value("id").toInt()},
                                                                         {"name", viewer.value("name").toString()},
                                                                         {"avatar", viewer.value("avatar").toObject().value("medium").toString()}});
        return QVariantMap{{"name", viewer.value("name").toString()}};
    });
    task->then(this, [this](const QVariant &) { emit changed(); });
    return task;
}

std::shared_ptr<void> Trackers::listen(const QString &path, QString *problem) {
    // One sign-in at a time: a new one ends the one still waiting.
    if (auto previous = std::static_pointer_cast<Pending>(m_pending)) {
        QMutexLocker lock(&previous->mutex);
        previous->error = QStringLiteral("Cancelled");
        previous->arrived.wakeAll();
    }
    if (m_redirect)
        m_redirect->deleteLater();
    m_state = QUuid::createUuid().toString(QUuid::Id128);
    auto pending = std::make_shared<Pending>();
    m_pending = pending;
    m_redirect = new QTcpServer(this);
    if (!m_redirect->listen(QHostAddress::LocalHost, kRedirectPort)) {
        *problem = QStringLiteral("Another program is using port %1, which signing in needs.").arg(kRedirectPort);
        return {};
    }
    connect(m_redirect, &QTcpServer::newConnection, this, [this, pending, path, server = m_redirect] {
        while (auto *socket = server->nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, pending, path] {
                const auto line = QString::fromLatin1(socket->readLine(8192));
                const QUrl url(QStringLiteral("http://127.0.0.1") + line.section(QLatin1Char(' '), 1, 1));
                const QUrlQuery query(url);
                const bool ours = url.path() == path && query.queryItemValue(QStringLiteral("state")) == m_state;
                const bool denied = ours && !query.queryItemValue(QStringLiteral("error")).isEmpty();
                const auto page = QByteArray(!ours ? "Nothing to do here."
                                             : denied ? "Sign-in was cancelled. You can go back to SCANCORE."
                                                      : "Signed in. You can go back to SCANCORE.");
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
                              "<!doctype html><meta charset=utf-8><title>SCANCORE</title>"
                              "<body style=\"font:16px system-ui;padding:3em;background:#111;color:#eee\">" +
                              page + "</body>");
                socket->disconnectFromHost();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                if (!ours)
                    return;
                QMutexLocker lock(&pending->mutex);
                pending->code = query.queryItemValue(QStringLiteral("code"));
                pending->error = query.queryItemValue(QStringLiteral("error"));
                pending->arrived.wakeAll();
            });
        }
    });
    return pending;
}

void Trackers::watch(Task *task) {
    connect(task, &Task::changed, this, [this, task] {
        if (task->loading())
            return;
        if (m_redirect)
            m_redirect->deleteLater();
        if (task->ok())
            emit changed();
    });
}

Task *Trackers::malSignIn() {
    QString problem;
    const auto pending = std::static_pointer_cast<Pending>(listen(QStringLiteral("/mal"), &problem));
    if (!pending)
        return Task::run([problem]() -> QVariant { throw Error(problem); });
    // PKCE with the plain method, which is the one MyAnimeList supports.
    const auto verifier = randomText(96);
    QUrl authorize(QStringLiteral("https://myanimelist.net/v1/oauth2/authorize"));
    authorize.setQuery(QUrlQuery{{"response_type", "code"},
                                 {"client_id", kMalId},
                                 {"code_challenge", verifier},
                                 {"code_challenge_method", "plain"},
                                 {"state", m_state},
                                 {"redirect_uri", kMalRedirect}});
    QDesktopServices::openUrl(authorize);

    auto *task = Task::run([pending, verifier]() -> QVariant {
        const auto code = waitForCode(pending, QStringLiteral("MyAnimeList"));
        const auto response = Http::postForm(QUrl(QStringLiteral("https://myanimelist.net/v1/oauth2/token")),
                                             {{"client_id", kMalId},
                                              {"grant_type", "authorization_code"},
                                              {"code", code},
                                              {"code_verifier", verifier},
                                              {"redirect_uri", kMalRedirect}});
        if (!response.ok())
            throw Error(QStringLiteral("MyAnimeList did not accept the sign-in (%1).").arg(response.status));
        const auto json = response.json().object();
        Database::setPref(QStringLiteral("tracker.mal"), QVariantMap{{"access", json.value("access_token").toString()},
                                                                     {"refresh", json.value("refresh_token").toString()},
                                                                     {"expires", Database::now() + json.value("expires_in").toInteger(3600)}});
        const auto me = malCall("GET", QStringLiteral("/users/@me")).json().object();
        auto account = Database::pref(QStringLiteral("tracker.mal")).toMap();
        account.insert("name", me.value("name").toString());
        account.insert("avatar", me.value("picture").toString());
        Database::setPref(QStringLiteral("tracker.mal"), account);
        return QVariantMap{{"name", me.value("name").toString()}};
    });
    watch(task);
    return task;
}

Task *Trackers::comickSignIn() {
    QString problem;
    const auto pending = std::static_pointer_cast<Pending>(listen(QStringLiteral("/comick"), &problem));
    if (!pending)
        return Task::run([problem]() -> QVariant { throw Error(problem); });
    // PKCE with SHA-256, which Comick requires.
    const auto verifier = randomText(64);
    const auto challenge = QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    QUrl authorize(kComickAuth + QStringLiteral("/authorize"));
    authorize.setQuery(QUrlQuery{{"response_type", "code"},
                                 {"client_id", kComickId},
                                 {"redirect_uri", kComickRedirect},
                                 {"scope", "library:read library:write offline_access"},
                                 {"resource", kComickApi},
                                 {"state", m_state},
                                 {"code_challenge", challenge},
                                 {"code_challenge_method", "S256"}});
    QDesktopServices::openUrl(authorize);

    auto *task = Task::run([pending, verifier]() -> QVariant {
        const auto code = waitForCode(pending, QStringLiteral("Comick"));
        const auto response = Http::postForm(QUrl(kComickAuth + QStringLiteral("/token")),
                                             {{"grant_type", "authorization_code"},
                                              {"client_id", kComickId},
                                              {"code", code},
                                              {"redirect_uri", kComickRedirect},
                                              {"code_verifier", verifier},
                                              {"resource", kComickApi}},
                                             {{"Accept", "application/json"}});
        if (!response.ok())
            throw Error(QStringLiteral("Comick did not accept the sign-in (%1).").arg(response.status));
        const auto json = response.json().object();
        if (!json.value("scope").toString().contains(QLatin1String("library:write")))
            throw Error(QStringLiteral("SCANCORE needs permission to update your Comick library. Connect again and allow it."));
        Database::setPref(QStringLiteral("tracker.comick"),
                          QVariantMap{{"access", json.value("access_token").toString()},
                                      {"refresh", json.value("refresh_token").toString()},
                                      {"expires", json.contains("expires_at") ? json.value("expires_at").toInteger()
                                                                              : Database::now() + json.value("expires_in").toInteger(900)}});
        const auto me = comickCall("GET", QStringLiteral("/me")).json().object();
        auto account = Database::pref(QStringLiteral("tracker.comick")).toMap();
        account.insert("name", me.value("username").toString());
        account.insert("avatar", me.value("avatar_url").toString());
        Database::setPref(QStringLiteral("tracker.comick"), account);
        return QVariantMap{{"name", me.value("username").toString()}};
    });
    watch(task);
    return task;
}

void Trackers::signOut(const QString &service) {
    const auto other = tracking::service(service);
    if (other && other->own)
        return; // signed in with the server itself
    if (service == QLatin1String("mal") || service == QLatin1String("comick") || service == QLatin1String("anilist") || other)
        Database::setPref(QStringLiteral("tracker.") + service, QVariantMap());
    emit changed();
}

Task *Trackers::search(const QString &service, const QString &media, const QString &title) {
    return Task::run([service, media, title]() -> QVariant {
        if (const auto other = tracking::service(service))
            return other->search(media, title);
        return service == QLatin1String("mal")      ? malSearch(media, title)
               : service == QLatin1String("comick") ? comickSearch(media, title)
                                                    : anilistSearch(media, title);
    });
}

QVariantMap Trackers::links(const QString &seriesId) const {
    auto entry = allLinks().value(seriesId).toMap();
    // Anime are on AniList already, by their id.
    if (!entry.contains(QStringLiteral("anilist")) && seriesId.startsWith(QLatin1String("anime:anilist:")))
        entry.insert(QStringLiteral("anilist"), QVariantMap{{"id", seriesId.section(QLatin1Char(':'), 2).toInt()}, {"auto", true}});
    return entry;
}

QStringList Trackers::misses(const QString &seriesId) const {
    QStringList out;
    const auto entry = allMisses().value(seriesId).toMap();
    const auto services = links(seriesId);
    for (auto it = entry.begin(); it != entry.end(); ++it)
        if (!services.contains(it.key()))
            out.append(it.key());
    return out;
}

QString Trackers::lookupTitle(const QString &seriesId, const QString &title) const {
    const auto extra = seriesExtra(seriesId);
    const auto catalogue = extra.value("catalogTitle").toString();
    return catalogue.isEmpty() ? title : catalogue;
}

void Trackers::link(const QString &seriesId, const QString &service, const QVariant &remoteId, const QString &title) {
    saveLink(seriesId, service, App::plain(remoteId), title);
    saveMiss(seriesId, service, false);
    emit changed();
    unitFinished(seriesId);
}

void Trackers::unlink(const QString &seriesId, const QString &service) {
    saveLink(seriesId, service, QVariant(), {});
    emit changed();
}

QVariantList Trackers::comickLibrary() {
    QVariantList out;
    QString cursor;
    do {
        QUrlQuery query{{"limit", "100"}};
        if (!cursor.isEmpty())
            query.addQueryItem(QStringLiteral("cursor"), cursor);
        const auto response = comickCall("GET", QStringLiteral("/me/library?") + query.toString(QUrl::FullyEncoded));
        if (!response.ok())
            throw Error(QStringLiteral("Comick answered %1.").arg(response.status), response.status);
        const auto page = response.json().object();
        for (const auto &value : page.value("data").toArray())
            out.append(value.toObject().toVariantMap());
        cursor = page.value("next_cursor").toString();
    } while (!cursor.isEmpty() && !CancelScope::cancelled());
    return out;
}

int Trackers::comickAniListId(const QString &hid) {
    const auto response = comickCall("GET", QStringLiteral("/titles/") + QString::fromLatin1(QUrl::toPercentEncoding(hid)));
    return response.ok() ? response.json().object().value("data").toObject().value("external_ids").toObject().value("anilist").toString().toInt() : 0;
}

void Trackers::remember(const QString &seriesId, const QString &service, const QVariant &remoteId, const QString &title) {
    saveLink(seriesId, service, remoteId, title);
}

void Trackers::moveLinks(const QString &fromId, const QString &toId, bool keepOld) {
    QMutexLocker lock(&g_linksMutex);
    moveLinkEntries(QStringLiteral("tracker.links"), fromId, toId, keepOld);
    moveLinkEntries(QStringLiteral("tracker.misses"), fromId, toId, keepOld);
}

namespace {

// A service that refused SCANCORE's sign-in (the token was revoked or ran
// out): kept, marked, and the reader told once, so it can be connected again.
void signedOut(const QString &service, const QString &label) {
    const auto key = QStringLiteral("tracker.") + service;
    auto account = Database::pref(key).toMap();
    if (account.isEmpty() || account.value("expired").toBool())
        return;
    account.insert(QStringLiteral("expired"), true);
    Database::setPref(key, account);
    QMetaObject::invokeMethod(Trackers::instance(), [service, label] {
        Notifications::post({QStringLiteral("app"), QStringLiteral("tracker:") + service,
                             QCoreApplication::translate("Trackers", "%1 needs you to connect again").arg(label),
                             QCoreApplication::translate("Trackers", "Your progress is not reaching %1. Connect it again in Settings → Connections.").arg(label),
                             QVariantMap{{"settings", "connections"}}, true});
        emit Trackers::instance()->changed();
    });
}

} // namespace

void Trackers::unitFinished(const QString &seriesId) {
    bool any = false;
    for (const auto *key : {"anilist", "mal", "comick", "kitsu", "mangaupdates", "shikimori", "bangumi", "simkl"})
        any = any || !Database::pref(QStringLiteral("tracker.") + QLatin1String(key)).toMap().isEmpty();
    // The reader's Komga and Kavita servers track as well.
    for (const auto &server : servers::load())
        any = any || server.kind == QLatin1String("komga") || server.kind == QLatin1String("kavita");
    if (!any)
        return;
    runInBackground([seriesId] {
        const auto row = Database::one(QStringLiteral("SELECT media, title FROM series WHERE id = ?"), {seriesId});
        const auto media = row.value("media").toString();
        if (media.isEmpty() || seriesId.contains(QLatin1String(":local:")))
            return;
        const int progress = progressOf(seriesId);
        if (progress <= 0)
            return;
        const auto title = row.value("title").toString();
        try {
            updateAniList(seriesId, media, title, progress);
        } catch (const Error &e) {
            if (e.status() == 401)
                signedOut(QStringLiteral("anilist"), QStringLiteral("AniList"));
            qWarning().noquote() << "AniList:" << e.message();
        }
        try {
            updateMal(seriesId, media, title, progress);
        } catch (const Error &e) {
            if (e.status() == 401)
                signedOut(QStringLiteral("mal"), QStringLiteral("MyAnimeList"));
            qWarning().noquote() << "MyAnimeList:" << e.message();
        }
        try {
            updateComick(seriesId, media, title, progress);
        } catch (const Error &e) {
            if (e.status() == 401)
                signedOut(QStringLiteral("comick"), QStringLiteral("Comick"));
            qWarning().noquote() << "Comick:" << e.message();
        }
        for (const auto &other : tracking::allServices()) {
            try {
                update(other, seriesId, media, title, progress);
            } catch (const Error &e) {
                if (e.status() == 401 && !other.own)
                    signedOut(other.key, other.label);
                qWarning().noquote() << other.label + QLatin1Char(':') << e.message();
            }
        }
        QMetaObject::invokeMethod(instance(), [] { emit instance()->changed(); });
    });
}

} // namespace sc
