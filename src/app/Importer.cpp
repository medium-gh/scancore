#include "Importer.h"

#include "Anime.h"
#include "Comics.h"
#include "Downloads.h"
#include "Library.h"
#include "Novels.h"
#include "Trackers.h"
#include "core/Backups.h"
#include "core/Database.h"
#include "core/Parallel.h"
#include "core/Proto.h"
#include "core/Text.h"
#include "App.h"
#include "extensions/Host.h"
#include "extensions/Manager.h"
#include "sources/anime/Registry.h"
#include "sources/comics/Registry.h"
#include "sources/local/Local.h"
#include "sources/novels/Registry.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QXmlStreamReader>

#include <cstring>
#include <limits>

namespace sc {

namespace {

const QString kUnmatched = QStringLiteral("import.unmatched");
QMutex g_unmatchedMutex;

using backups::Item;
using backups::parseLNReader;
using backups::parseMihon;

QVariantMap toMap(const Item &item) {
    QVariantList numbers;
    for (const double n : item.numbers)
        numbers.append(n);
    QVariantMap map{{"title", item.title()}, {"titles", item.titles},       {"media", item.media},     {"cover", item.cover},
                    {"category", item.category}, {"source", item.sourceName}, {"anilistId", item.anilistId}, {"malId", item.malId},
                    {"comick", item.comickHid},
                    {"numbers", numbers},        {"count", item.count},       {"read", item.numbers.isEmpty() ? item.count : int(item.numbers.size())}};
    // The extension it came from and its address there, so installing that
    // extension later brings it in where it was, with what was done.
    if (!item.extensionKey.isEmpty() && !item.url.isEmpty()) {
        map.insert("extension", item.extensionKey);
        map.insert("url", item.url);
        if (!item.doneLinks.isEmpty())
            map.insert("done", QStringList(item.doneLinks.cbegin(), item.doneLinks.cend()));
    }
    return map;
}

Item fromMap(const QVariantMap &map) {
    Item item;
    item.media = map.value("media").toString();
    item.titles = map.value("titles").toStringList();
    if (item.titles.isEmpty())
        item.titles = {map.value("title").toString()};
    item.cover = map.value("cover").toString();
    item.category = map.value("category").toString();
    item.sourceName = map.value("source").toString();
    item.anilistId = map.value("anilistId").toInt();
    item.malId = map.value("malId").toInt();
    item.comickHid = map.value("comick").toString();
    for (const auto &n : map.value("numbers").toList())
        item.numbers.append(n.toDouble());
    item.count = map.value("count").toInt();
    item.extensionKey = map.value("extension").toString();
    item.url = map.value("url").toString();
    for (const auto &link : map.value("done").toStringList())
        item.doneLinks.insert(link);
    return item;
}

// ------------------------------------------------------------ AniList

QJsonObject anilist(const QString &query, const QJsonObject &variables) {
    const auto response = Http::postJson(QUrl(QStringLiteral("https://graphql.anilist.co")),
                                         QJsonDocument(QJsonObject{{"query", query}, {"variables", variables}}).toJson(QJsonDocument::Compact),
                                         {{"Accept", "application/json"}});
    const auto json = response.json().object();
    if (!response.ok()) {
        const auto message = json.value("errors").toArray().first().toObject().value("message").toString();
        if (response.status == 404)
            throw Error(QStringLiteral("AniList has no public list by that name."), 404);
        if (response.status == 429)
            throw Error(QStringLiteral("AniList is busy. Try again in a minute."), 429);
        throw Error(message.isEmpty() ? QStringLiteral("AniList answered %1.").arg(response.status) : QStringLiteral("AniList: ") + message,
                    response.status);
    }
    return json.value("data").toObject();
}

const char *kMediaFields = R"(
  id idMal type format status(version: 2) episodes chapters averageScore seasonYear startDate { year }
  title { romaji english native userPreferred } synonyms description(asHtml: false) genres bannerImage
  coverImage { extraLarge large color })";

QStringList titlesOf(const QJsonObject &media) {
    const auto t = media.value("title").toObject();
    QStringList titles{t.value("english").toString(), t.value("romaji").toString(), t.value("userPreferred").toString(),
                       t.value("native").toString()};
    for (const auto &s : media.value("synonyms").toArray())
        titles.append(s.toString());
    titles.removeAll(QString());
    titles.removeDuplicates();
    return titles;
}

QString categoryFor(const QString &status) {
    const auto s = status.toUpper().remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
    if (s == QLatin1String("COMPLETED") || s == QLatin1String("2"))
        return QStringLiteral("completed");
    if (s == QLatin1String("DROPPED") || s == QLatin1String("4"))
        return QStringLiteral("dropped");
    if (s == QLatin1String("PLANNING") || s.startsWith(QLatin1String("PLANTO")) || s == QLatin1String("6"))
        return QStringLiteral("planned");
    if (s == QLatin1String("PAUSED") || s == QLatin1String("ONHOLD") || s == QLatin1String("3"))
        return QStringLiteral("paused");
    return QStringLiteral("reading");
}

Item itemFrom(const QJsonObject &media) {
    Item item;
    const auto format = media.value("format").toString();
    item.media = media.value("type").toString() == QLatin1String("ANIME") ? QStringLiteral("anime")
                 : format == QLatin1String("NOVEL")                       ? QStringLiteral("novel")
                                                                          : QStringLiteral("comic");
    item.titles = titlesOf(media);
    item.cover = media.value("coverImage").toObject().value("extraLarge").toString(
        media.value("coverImage").toObject().value("large").toString());
    item.anilistId = media.value("id").toInt();
    item.malId = media.value("idMal").toInt();
    if (item.media == QLatin1String("anime"))
        item.anime = media;
    return item;
}

// AniList media for MyAnimeList ids, 50 at a time.
QHash<int, QJsonObject> byMalIds(const QList<int> &ids, bool anime) {
    QHash<int, QJsonObject> out;
    for (int i = 0; i < ids.size(); i += 50) {
        QJsonArray chunk;
        for (const int id : ids.mid(i, 50))
            chunk.append(id);
        const auto data = anilist(QStringLiteral("query ($ids: [Int], $type: MediaType) { Page(perPage: 50) { media(idMal_in: $ids, type: $type) {%1} } }")
                                      .arg(QString::fromLatin1(kMediaFields)),
                                  {{"ids", chunk}, {"type", anime ? "ANIME" : "MANGA"}});
        for (const auto &value : data.value("Page").toObject().value("media").toArray())
            out.insert(value.toObject().value("idMal").toInt(), value.toObject());
    }
    return out;
}

// ------------------------------------------------------------ MyAnimeList

QList<Item> parseMal(const QByteArray &data) {
    QList<Item> out;
    QXmlStreamReader xml(data);
    Item item;
    bool inEntry = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("anime") || name == QLatin1String("manga")) {
                item = Item();
                item.media = name == QLatin1String("anime") ? QStringLiteral("anime") : QStringLiteral("comic");
                inEntry = true;
            } else if (inEntry) {
                const auto value = xml.readElementText().trimmed();
                if (name == QLatin1String("series_animedb_id") || name == QLatin1String("manga_mangadb_id"))
                    item.malId = value.toInt();
                else if (name == QLatin1String("series_title") || name == QLatin1String("manga_title"))
                    item.titles = {value};
                else if (name == QLatin1String("my_watched_episodes") || name == QLatin1String("my_read_chapters"))
                    item.count = value.toInt();
                else if (name == QLatin1String("my_status"))
                    item.category = categoryFor(value);
            }
        } else if (xml.isEndElement() && (xml.name() == QLatin1String("anime") || xml.name() == QLatin1String("manga"))) {
            if (inEntry && item.malId > 0)
                out.append(item);
            inEntry = false;
        }
    }
    if (xml.hasError() && out.isEmpty())
        throw Error(QStringLiteral("This is not a MyAnimeList export."));
    return out;
}

// ------------------------------------------------------------ saving

struct Tally {
    int imported = 0;
    int progress = 0;
    QList<Item> unmatched;
    QStringList saved;
    // Reader settings per series (readerSeries), applied on the main thread.
    QVariantMap reader;
    // Series that were in more than one list.
    int inSeveral = 0;
};

// Into the library (when not there yet), with its chapters or episodes done.
int store(const QVariantMap &series, const QString &list, const QVariantList &units, const Item &item) {
    const auto id = series.value("id").toString();
    // A backup's own list becomes one of the user's (made once, by name).
    const auto category = list.startsWith(QLatin1String("custom:")) ? Library::categoryNamed(list.mid(7)) : list;
    const auto now = Database::now();
    const auto when = item.updatedAt > 0 ? std::min(item.updatedAt, now) : now;
    // When it was added there, else when it was last read.
    const auto added = item.addedAt > 0 ? std::min(item.addedAt, now) : when;
    // Mihon's history has the chapter read last; the others just before it,
    // so Continue picks that one.
    auto unrecorded = when;
    for (const auto at : item.readAt)
        unrecorded = std::min(unrecorded, at - 60);
    QSet<qint64> numbers;
    for (const double n : item.numbers)
        numbers.insert(qRound64(n * 1000));
    int marked = 0;
    Library::upsertSeries(series);
    if (!units.isEmpty() && series.value("media").toString() != QLatin1String("anime"))
        Library::storeUnits(id, units);
    Database::transaction([&] {
        Database::query(QStringLiteral("INSERT OR IGNORE INTO library(series_id, category, added_at, known_units, changed_at) VALUES(?, ?, ?, ?, ?)"),
                        {id, category, added, units.size(), now});
        Database::query(QStringLiteral("DELETE FROM library_removed WHERE series_id = ?"), {id});
        for (const auto &value : units) {
            const auto unit = value.toMap();
            // The extension's own link, to match the backup's exactly.
            auto link = unit.value("url").toString();
            if (link.isEmpty())
                link = unit.value("key").toString();
            if (const auto wrapped = ext::unwrapUrl(link))
                link = wrapped->url;
            // History: when this one was last read there.
            const auto at = std::min(item.readAt.value(link, unrecorded), now);
            if (item.bookmarkLinks.contains(link))
                Database::query(QStringLiteral("INSERT OR IGNORE INTO bookmarks(series_id, unit_key, created_at) VALUES(?, ?, ?)"),
                                {id, unit.value("key").toString(), at});
            bool ok = false;
            const double number = unit.value("number").toString().toDouble(&ok);
            const bool done = item.doneLinks.contains(link)
                || (ok && (numbers.isEmpty() ? number <= item.count && number > 0 : numbers.contains(qRound64(number * 1000))));
            if (!done) {
                if (const auto part = item.parts.find(link); part != item.parts.end())
                    Database::query(QStringLiteral(R"(
                        INSERT INTO progress(series_id, unit_key, unit_title, unit_number, unit_url, position, percent, completed, updated_at)
                        VALUES(?, ?, ?, ?, ?, ?, ?, 0, ?)
                        ON CONFLICT(series_id, unit_key) DO NOTHING
                    )"),
                                    {id, unit.value("key").toString(), unit.value("title").toString(), unit.value("number").toString(),
                                     unit.value("url").toString(),
                                     QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(part->position)).toJson(QJsonDocument::Compact)),
                                     part->percent, at});
                continue;
            }
            Database::query(QStringLiteral(R"(
                INSERT INTO progress(series_id, unit_key, unit_title, unit_number, unit_url, percent, completed, updated_at)
                VALUES(?, ?, ?, ?, ?, 1, 1, ?)
                ON CONFLICT(series_id, unit_key) DO UPDATE SET completed = 1, percent = 1
            )"),
                            {id, unit.value("key").toString(), unit.value("title").toString(), unit.value("number").toString(),
                             unit.value("url").toString(), at});
            ++marked;
        }
        // Its chapter list settings, where it has none of its own yet.
        if (!item.settings.isEmpty()) {
            auto settings = QJsonDocument::fromJson(Database::scalar(QStringLiteral("SELECT value FROM series_settings WHERE series_id = ?"), {id})
                                                        .toString()
                                                        .toUtf8())
                                .object()
                                .toVariantMap();
            bool changed = false;
            for (auto it = item.settings.cbegin(); it != item.settings.cend(); ++it)
                if (!settings.contains(it.key())) {
                    settings.insert(it.key(), it.value());
                    changed = true;
                }
            if (changed)
                Database::query(QStringLiteral("INSERT OR REPLACE INTO series_settings(series_id, value, changed_at) VALUES(?, ?, ?)"),
                                {id, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact)), now});
        }
    });
    if (item.anilistId > 0 && series.value("media").toString() != QLatin1String("anime"))
        Trackers::remember(id, QStringLiteral("anilist"), item.anilistId, item.title());
    if (item.malId > 0)
        Trackers::remember(id, QStringLiteral("mal"), item.malId, item.title());
    if (!item.comickHid.isEmpty())
        Trackers::remember(id, QStringLiteral("comick"), item.comickHid, item.title());
    for (auto it = item.trackerIds.cbegin(); it != item.trackerIds.cend(); ++it)
        Trackers::remember(id, it.key(), it.value(), item.title());
    return marked;
}

void storeAnime(const Item &item, Tally &outcome) {
    const auto &m = item.anime;
    const auto cover = m.value("coverImage").toObject();
    const int id = m.value("id").toInt();
    QVariantMap extra{{"anilistId", id},
                      {"banner", m.value("bannerImage").toString()},
                      {"color", cover.value("color").toString()},
                      {"format", m.value("format").toString()},
                      {"episodes", m.value("episodes").toInt()},
                      {"score", m.value("averageScore").toInt()},
                      {"year", m.value("seasonYear").toInt() ? m.value("seasonYear").toInt() : m.value("startDate").toObject().value("year").toInt()}};
    const QVariantMap series{{"id", Library::idFor(QStringLiteral("anime"), QStringLiteral("anilist"), QString::number(id))},
                             {"media", QStringLiteral("anime")},
                             {"source", QStringLiteral("anilist")},
                             {"sourceUrl", QStringLiteral("https://anilist.co/anime/%1").arg(id)},
                             {"title", item.title()},
                             {"cover", item.cover},
                             {"description", m.value("description").toString().remove(QRegularExpression(QStringLiteral("<[^>]+>")))},
                             {"genres", m.value("genres").toVariant()},
                             {"extra", extra}};
    // Episodes are keyed by their number.
    QVariantList episodes;
    for (int n = 1; n <= item.count; ++n)
        episodes.append(QVariantMap{{"key", QString::number(n)}, {"number", QString::number(n)}, {"title", QStringLiteral("Episode %1").arg(n)}});
    outcome.progress += store(series, item.category, episodes, item);
    outcome.saved.append(series.value("id").toString());
    ++outcome.imported;
}

// Counts a series brought in, with what is applied once the import is done.
void stored(const QVariantMap &series, const Item &item, Tally &outcome) {
    outcome.saved.append(series.value("id").toString());
    ++outcome.imported;
    if (!item.reader.isEmpty() && series.value("media").toString() == QLatin1String("comic"))
        outcome.reader.insert(series.value("id").toString(), item.reader);
    if (!item.otherCategories.isEmpty())
        ++outcome.inSeveral;
}

template <typename SourcePtr>
bool sameSource(const SourcePtr &source, const QString &name) {
    const auto a = titleKey(source->label());
    const auto b = titleKey(name);
    return !a.isEmpty() && !b.isEmpty() && (a == b || a.startsWith(b) || b.startsWith(a));
}

// A comic or novel on one of SCANCORE's sources, saved with its progress;
// false when no source has it.
bool storeFound(const Item &item, Tally &outcome, bool searchEverywhere) {
    const bool novel = item.media == QLatin1String("novel");
    const bool anime = item.media == QLatin1String("anime");
    QStringList wanted;
    for (const auto &t : item.titles)
        if (!titleKey(t).isEmpty())
            wanted.append(titleKey(t));
    QVariantMap series;
    QVariantList units;

    // `exact`: its own extension, where a series with no chapters (yet, or
    // taken down) is still the one the backup had.
    const auto tryUrl = [&](const QString &url, const QString &sourceKey, const QString &title, const QString &cover, bool exact = false) {
        try {
            units = novel ? Novels::fetchUnits(url) : anime ? Anime::fetchUnits(url) : Comics::fetchUnits(url);
        } catch (const Error &) {
            return false;
        }
        if (units.isEmpty() && !exact)
            return false;
        series = {{"id", Library::idFor(item.media, sourceKey, url)}, {"media", item.media}, {"source", sourceKey},
                  {"sourceUrl", url}, {"title", title.isEmpty() ? item.title() : title}, {"cover", cover.isEmpty() ? item.cover : cover}};
        return true;
    };

    // The same extension or plugin installed here: the backup's own
    // address on it, as the app it came from restores it.
    if (!item.extensionKey.isEmpty() && !item.url.isEmpty()) {
        const auto key = item.extensionKey;
        const auto comicSource = novel || anime ? nullptr : comics::Registry::byKey(key);
        if ((novel || anime || (comicSource && comicSource->enabled())) && tryUrl(ext::wrapUrl(key, item.url), key, {}, {}, true)) {
            outcome.progress += store(series, item.category, units, item);
            stored(series, item, outcome);
            return true;
        }
    }
    // Anime: an installed extension of the same name (another version or
    // language of it has another id) at the backup's address. Those without
    // one are found on AniList (storeAnimeOnAniList).
    if (anime) {
        if (item.url.isEmpty() || item.sourceName.isEmpty())
            return false;
        for (const auto &source : anime::Registry::all()) {
            if (!source->enabled() || source->key() == item.extensionKey || !sameSource(source, item.sourceName))
                continue;
            if (tryUrl(ext::wrapUrl(source->key(), item.url), source->key(), {}, {})) {
                outcome.progress += store(series, item.category, units, item);
                stored(series, item, outcome);
                return true;
            }
        }
        return false;
    }
    if (!novel) {
        for (const auto &source : comics::Registry::all()) {
            if (!source->enabled() || source->key() == QLatin1String("local"))
                continue;
            const bool named = !item.sourceName.isEmpty() && sameSource(source, item.sourceName);
            if (!named && !searchEverywhere)
                continue;
            // Mihon keeps the address on the site, usually without the site.
            if (named && !item.url.isEmpty()) {
                auto url = item.url.startsWith(QLatin1String("http")) ? item.url : source->config().base + item.url;
                if (source->key() == QLatin1String("mangadex")) // Mihon: /manga/<id>, the site: /title/<id>
                    url.replace(QLatin1String("/manga/"), QLatin1String("/title/"));
                if (comics::Registry::forUrl(url) == source && tryUrl(url, source->key(), {}, {}))
                    break;
            }
            QList<comics::Summary> found;
            try {
                found = source->search(item.title());
            } catch (const Error &) {
                continue;
            }
            bool done = false;
            for (const auto &summary : found)
                if (wanted.contains(titleKey(summary.title)) && tryUrl(summary.url, source->key(), summary.title, summary.cover)) {
                    done = true;
                    break;
                }
            if (done)
                break;
        }
    } else {
        for (const auto &source : novels::Registry::all()) {
            if (!source->config().enabled || !source->config().searchable || source->key() == QLatin1String("local"))
                continue;
            if (!searchEverywhere && !(item.sourceName.size() && sameSource(source, item.sourceName)))
                continue;
            QList<novels::Book> found;
            try {
                found = source->search(item.title());
            } catch (const Error &) {
                continue;
            }
            bool done = false;
            for (const auto &book : found)
                if (wanted.contains(titleKey(book.title)) && tryUrl(book.url, source->key(), book.title, book.cover)) {
                    done = true;
                    break;
                }
            if (done)
                break;
        }
    }
    if (series.isEmpty())
        return false;
    outcome.progress += store(series, item.category, units, item);
    stored(series, item, outcome);
    return true;
}

// Chapters in Mihon's downloads folder (when one is chosen) as downloads of
// the series that came in: how many.
int linkDownloads() {
    if (local::folders().isEmpty())
        return 0;
    const auto result = Downloads::linkMihonNow();
    QMetaObject::invokeMethod(Downloads::instance(), [result] { Downloads::instance()->linked(result); });
    return result.value(QStringLiteral("chapters")).toInt();
}

// Anime whose extension is not installed here: their AniList entry (by the
// tracker link the backup has, else by title), with the episodes watched.
void storeAnimeOnAniList(const QList<Item> &items, Tally &outcome) {
    QList<int> anilistIds;
    QList<int> malIds;
    for (const auto &item : items) {
        if (item.anilistId > 0)
            anilistIds.append(item.anilistId);
        else if (item.malId > 0)
            malIds.append(item.malId);
    }
    QHash<int, QJsonObject> byAnilist;
    QHash<int, QJsonObject> byMal;
    try {
        for (int i = 0; i < anilistIds.size(); i += 50) {
            QJsonArray chunk;
            for (const int id : anilistIds.mid(i, 50))
                chunk.append(id);
            const auto data = anilist(QStringLiteral("query ($ids: [Int]) { Page(perPage: 50) { media(id_in: $ids, type: ANIME) {%1} } }")
                                          .arg(QString::fromLatin1(kMediaFields)),
                                      {{"ids", chunk}});
            for (const auto &value : data.value("Page").toObject().value("media").toArray())
                byAnilist.insert(value.toObject().value("id").toInt(), value.toObject());
        }
        byMal = byMalIds(malIds, true);
    } catch (const Error &) {
        // Whatever was not found waits in the list of titles to find.
    }
    // The others by title: ten searches to a request (aliases), only ids
    // and titles, then their details 50 at a time like those above. AniList
    // allows about 30 requests a minute when busy, so they are spaced out
    // and a "busy" answer waits a minute; a whole Aniyomi library (hundreds
    // of anime) takes a few minutes, not one search per anime.
    QList<int> unknown;
    for (int i = 0; i < items.size(); ++i)
        if (!(items[i].anilistId > 0 && byAnilist.contains(items[i].anilistId)) && !(items[i].malId > 0 && byMal.contains(items[i].malId))
            && !titleKey(items[i].title()).isEmpty())
            unknown.append(i);
    const auto pause = [](int ms) {
        for (int waited = 0; waited < ms; waited += 250) {
            if (CancelScope::cancelled())
                throw Error(QStringLiteral("Cancelled"), 499);
            QThread::msleep(250);
        }
    };
    const auto request = [&](const QString &query, const QJsonObject &variables) {
        for (int attempt = 0;; ++attempt) {
            try {
                return anilist(query, variables);
            } catch (const Error &e) {
                if (e.status() != 429 || attempt >= 2)
                    throw;
                pause(61000);
            }
        }
    };
    QHash<int, int> foundIds; // item -> AniList id
    constexpr int kPerRequest = 10;
    for (int i = 0; i < unknown.size(); i += kPerRequest) {
        if (i > 0)
            pause(2100);
        QStringList params;
        QStringList fields;
        QJsonObject variables;
        const auto batch = unknown.mid(i, kPerRequest);
        for (int j = 0; j < batch.size(); ++j) {
            params.append(QStringLiteral("$q%1: String").arg(j));
            fields.append(QStringLiteral("s%1: Page(perPage: 6) { media(search: $q%1, type: ANIME) { id title { romaji english native userPreferred } synonyms } }").arg(j));
            variables.insert(QStringLiteral("q%1").arg(j), items[batch[j]].title());
        }
        QJsonObject data;
        try {
            data = request(QStringLiteral("query (%1) { %2 }").arg(params.join(QStringLiteral(", ")), fields.join(QLatin1Char(' '))), variables);
        } catch (const Error &e) {
            if (e.status() == 499)
                throw;
            continue;
        }
        for (int j = 0; j < batch.size(); ++j) {
            const auto &item = items[batch[j]];
            QSet<QString> wanted;
            for (const auto &t : item.titles)
                if (const auto key = titleKey(t); !key.isEmpty())
                    wanted.insert(key);
            for (const auto &value : data.value(QStringLiteral("s%1").arg(j)).toObject().value("media").toArray()) {
                const auto titles = titlesOf(value.toObject());
                if (std::any_of(titles.begin(), titles.end(), [&](const QString &t) { return wanted.contains(titleKey(t)); })) {
                    foundIds.insert(batch[j], value.toObject().value("id").toInt());
                    break;
                }
            }
        }
    }
    QHash<int, QJsonObject> byFound;
    const auto ids = QSet<int>(foundIds.cbegin(), foundIds.cend()).values();
    for (int i = 0; i < ids.size(); i += 50) {
        pause(2100);
        QJsonArray chunk;
        for (const int id : ids.mid(i, 50))
            chunk.append(id);
        try {
            const auto data = request(QStringLiteral("query ($ids: [Int]) { Page(perPage: 50) { media(id_in: $ids, type: ANIME) {%1} } }")
                                          .arg(QString::fromLatin1(kMediaFields)),
                                      {{"ids", chunk}});
            for (const auto &value : data.value("Page").toObject().value("media").toArray())
                byFound.insert(value.toObject().value("id").toInt(), value.toObject());
        } catch (const Error &e) {
            if (e.status() == 499)
                throw;
        }
    }
    for (int i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        if (CancelScope::cancelled())
            throw Error(QStringLiteral("Cancelled"), 499);
        auto media = item.anilistId > 0 ? byAnilist.value(item.anilistId) : item.malId > 0 ? byMal.value(item.malId) : QJsonObject();
        if (media.isEmpty())
            media = byFound.value(foundIds.value(i));
        if (media.isEmpty()) {
            outcome.unmatched.append(item);
            continue;
        }
        auto found = itemFrom(media);
        found.category = item.category;
        found.numbers = item.numbers;
        found.updatedAt = item.updatedAt;
        double last = item.count;
        for (const double n : item.numbers)
            last = std::max(last, n);
        found.count = int(std::ceil(last));
        storeAnime(found, outcome);
    }
}

// Comics, novels and extension anime, a few at a time so no site gets
// flooded; anime left over are looked up on AniList.
void storeAll(const QList<Item> &items, Tally &outcome, bool searchEverywhere) {
    for (int i = 0; i < items.size(); i += 4) {
        if (CancelScope::cancelled())
            throw Error(QStringLiteral("Cancelled"), 499);
        QList<std::function<Tally()>> jobs;
        for (const auto &item : items.mid(i, 4))
            jobs.append([item, searchEverywhere] {
                Tally one;
                if (!storeFound(item, one, searchEverywhere))
                    one.unmatched.append(item);
                return one;
            });
        for (const auto &result : parallel(jobs)) {
            if (!result.ok())
                continue;
            outcome.imported += result.value->imported;
            outcome.progress += result.value->progress;
            outcome.saved += result.value->saved;
            outcome.unmatched += result.value->unmatched;
            outcome.inSeveral += result.value->inSeveral;
            outcome.reader.insert(result.value->reader);
        }
    }
    QList<Item> anime;
    for (auto it = outcome.unmatched.begin(); it != outcome.unmatched.end();) {
        if (it->media == QLatin1String("anime")) {
            anime.append(*it);
            it = outcome.unmatched.erase(it);
        } else {
            ++it;
        }
    }
    if (!anime.isEmpty())
        storeAnimeOnAniList(anime, outcome);
}

void addUnmatched(const QList<Item> &items) {
    QMutexLocker lock(&g_unmatchedMutex);
    auto list = Database::pref(kUnmatched).toList();
    QSet<QString> known;
    for (const auto &value : list)
        known.insert(value.toMap().value("media").toString() + titleKey(value.toMap().value("title").toString()));
    for (const auto &item : items)
        if (!known.contains(item.media + titleKey(item.title()))) {
            list.append(toMap(item));
            known.insert(item.media + titleKey(item.title()));
        }
    Database::setPref(kUnmatched, list);
}

QVariantMap summary(const Tally &outcome) {
    addUnmatched(outcome.unmatched);
    return {{"imported", outcome.imported},
            {"unmatched", outcome.unmatched.size()},
            {"progress", outcome.progress},
            {"saved", outcome.saved},
            {"inSeveral", outcome.inSeveral},
            {"reader", outcome.reader}};
}

QByteArray readFile(const QUrl &file) {
    QFile in(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (!in.open(QIODevice::ReadOnly))
        throw Error(QStringLiteral("Could not open %1.").arg(in.fileName()));
    if (in.size() > 200 * 1024 * 1024)
        throw Error(QStringLiteral("That file is too large to import."));
    auto data = in.readAll();
    if (data.startsWith("\x1f\x8b")) {
        data = local::gunzip(data);
        if (data.isEmpty())
            throw Error(QStringLiteral("That file could not be unpacked."));
    }
    return data;
}

} // namespace

Importer::Importer(QObject *parent) : QObject(parent) {}

Importer *Importer::instance() {
    static auto *importer = [] {
        auto *created = new Importer;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return importer;
}

QVariantList Importer::unmatched() const {
    QMutexLocker lock(&g_unmatchedMutex);
    return Database::pref(kUnmatched).toList();
}

Task *Importer::finish(Task *task) {
    task->then(this, [this](const QVariant &value) {
        // Reader settings a backup had per series, where there are none here.
        if (const auto reader = value.toMap().value("reader").toMap(); !reader.isEmpty()) {
            auto all = App::instance()->settings().value(QStringLiteral("readerSeries")).toMap();
            for (auto it = reader.cbegin(); it != reader.cend(); ++it)
                if (!all.contains(it.key()))
                    all.insert(it.key(), it.value());
            App::instance()->setSetting(QStringLiteral("readerSeries"), all);
        }
        const auto saved = value.toMap().value("saved").toStringList();
        for (const auto &id : saved)
            emit Library::instance()->saved(id);
        if (!saved.isEmpty())
            Library::instance()->notifyChanged();
        emit changed();
    });
    return task;
}

Task *Importer::importLNReader(const QUrl &file) {
    return finish(Task::run([file]() -> QVariant {
        const auto items = parseLNReader(readFile(file));
        if (items.isEmpty())
            throw Error(QStringLiteral("This backup has no library."));
        Tally outcome;
        storeAll(items, outcome, false);
        return summary(outcome);
    }));
}

namespace {

// A repository address without its index file, to compare two ways of
// naming the same repository (GitHub serves raw files under two hosts).
QString repositoryKey(QString url) {
    url = url.trimmed().toLower();
    static const QRegularExpression githubRaw(QStringLiteral("^https://github\\.com/([^/]+)/([^/]+)/raw/"));
    url.replace(githubRaw, QStringLiteral("https://raw.githubusercontent.com/\\1/\\2/"));
    for (const auto *file : {"/index.min.json", "/index.json", "/repo.json", "/index.pb"})
        if (url.endsWith(QLatin1String(file)))
            url.chop(int(std::strlen(file)));
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

// The repositories added here, by address and by signing key.
QSet<QString> addedRepositories() {
    QSet<QString> out;
    for (const auto &repo : ext::Manager::repositories()) {
        out.insert(repositoryKey(repo.url));
        if (!repo.listUrl.isEmpty())
            out.insert(repositoryKey(repo.listUrl));
        if (!repo.signingKey.isEmpty())
            out.insert(QStringLiteral("key:") + repo.signingKey.toLower());
    }
    return out;
}

bool isAdded(const QSet<QString> &added, const backups::Repository &repo) {
    return added.contains(repositoryKey(repo.url)) || (!repo.signingKey.isEmpty() && added.contains(QStringLiteral("key:") + repo.signingKey.toLower()));
}

// Extensions the backup's series came from that are not installed here,
// with how many series each brings back: {kind:pkg -> (package, count)};
// then those of the other sources set up in that app (`configured`), with
// none. Looks in the repositories added and in `more` (previews of others).
QHash<QString, QPair<ext::Package, int>> neededExtensions(const QList<Item> &items, const QList<ext::Package> &more,
                                                          int *unknown = nullptr, const QList<quint64> &configured = {}) {
    QSet<QString> installed;
    for (const auto &source : ext::Manager::sources())
        installed.insert(source.key);
    QHash<QString, ext::Package> bySource;
    for (const auto &list : {ext::Manager::available(), more})
        for (const auto &package : list)
            for (const auto &source : package.sources)
                bySource.insert(ext::kindName(package.kind) + QLatin1Char(':') + source.id, package);
    QHash<QString, QPair<ext::Package, int>> out;
    for (const auto &item : items) {
        if (item.extensionKey.isEmpty() || installed.contains(item.extensionKey))
            continue;
        const auto it = bySource.constFind(item.extensionKey);
        if (it == bySource.constEnd()) {
            if (unknown)
                ++*unknown;
            continue;
        }
        auto &entry = out[ext::kindName(it->kind) + QLatin1Char(':') + it->pkg];
        entry.first = *it;
        ++entry.second;
    }
    // A source id alone does not say comic or anime (Aniyomi keeps both).
    for (const auto id : configured)
        for (const auto *kind : {"mihon", "aniyomi"}) {
            const auto key = QLatin1String(kind) + QLatin1Char(':') + QString::number(id);
            const auto it = bySource.constFind(key);
            if (installed.contains(key) || it == bySource.constEnd())
                continue;
            auto &entry = out[ext::kindName(it->kind) + QLatin1Char(':') + it->pkg];
            entry.first = *it;
        }
    return out;
}

} // namespace

Task *Importer::previewMihon(const QUrl &file) {
    return Task::run([file]() -> QVariant {
        const auto backup = backups::parseMihonBackup(readFile(file));
        if (backup.items.isEmpty())
            throw Error(QStringLiteral("This backup has no library."));
        int comics = 0, anime = 0;
        for (const auto &item : backup.items)
            ++(item.media == QLatin1String("anime") ? anime : comics);
        // Repositories not added here are read to see what they offer.
        const auto added = addedRepositories();
        QVariantList repos;
        QList<ext::Package> offered;
        for (const auto &repo : backup.repositories) {
            const bool have = isAdded(added, repo);
            QVariantMap row{{"url", repo.url}, {"name", repo.name.isEmpty() ? QUrl(repo.url).host() : repo.name}, {"added", have}};
            if (!have) {
                try {
                    const auto preview = ext::Manager::preview(repo.url);
                    offered += preview.packages;
                    row.insert("count", int(preview.packages.size()));
                } catch (const Error &e) {
                    row.insert("error", e.message());
                }
            }
            repos.append(row);
        }
        int unknown = 0;
        QVariantList extensions;
        for (const auto &[package, count] : neededExtensions(backup.items, offered, &unknown, backup.configuredSources))
            extensions.append(QVariantMap{{"kind", ext::kindName(package.kind)}, {"pkg", package.pkg}, {"name", package.name}, {"series", count}});
        std::sort(extensions.begin(), extensions.end(),
                  [](const QVariant &a, const QVariant &b) { return a.toMap().value("series").toInt() > b.toMap().value("series").toInt(); });
        // Komikku and TachiyomiSY write the same backups with more in
        // them; their file names say which app made them.
        const auto name = file.fileName().toLower();
        const auto app = name.contains(QLatin1String("komikku")) ? QStringLiteral("Komikku")
                         : name.contains(QLatin1String(".sy_")) || name.contains(QLatin1String("tachiyomisy")) ? QStringLiteral("TachiyomiSY")
                         : name.contains(QLatin1String("aniyomi")) ? QStringLiteral("Aniyomi")
                         : name.contains(QLatin1String("tachiyomi")) ? QStringLiteral("Tachiyomi")
                                                                     : QStringLiteral("Mihon");
        return QVariantMap{{"comics", comics},
                           {"anime", anime},
                           {"app", app},
                           {"lists", backup.lists},
                           {"repositories", repos},
                           {"extensions", extensions},
                           {"unknownSources", unknown}};
    });
}

Task *Importer::importMihon(const QUrl &file, const QVariantMap &prepare) {
    return finish(Task::run([file, prepare]() -> QVariant {
        const auto backup = backups::parseMihonBackup(readFile(file));
        if (backup.items.isEmpty())
            throw Error(QStringLiteral("This backup has no library."));
        // First what the user chose to bring along: the backup's
        // repositories and the extensions its series came from, so those
        // series land on their own sources.
        int reposAdded = 0, installed = 0;
        const auto added = addedRepositories();
        for (const auto &value : prepare.value("repositories").toList()) {
            const auto url = value.toString();
            if (url.isEmpty() || added.contains(repositoryKey(url)))
                continue;
            const auto named = std::find_if(backup.repositories.begin(), backup.repositories.end(),
                                            [&](const backups::Repository &r) { return r.url == url; });
            if (named != backup.repositories.end() && isAdded(added, *named))
                continue;
            try {
                ext::Manager::add(ext::Manager::preview(url));
                ++reposAdded;
            } catch (const Error &) {
                // Left out; its series wait to be found like any other.
            }
        }
        QSet<QString> wanted;
        for (const auto &value : prepare.value("extensions").toList())
            wanted.insert(value.toMap().value("kind").toString() + QLatin1Char(':') + value.toMap().value("pkg").toString());
        if (!wanted.isEmpty()) {
            QList<std::function<bool()>> jobs;
            for (const auto &[package, count] : neededExtensions(backup.items, {}, nullptr, backup.configuredSources)) {
                if (!wanted.contains(ext::kindName(package.kind) + QLatin1Char(':') + package.pkg))
                    continue;
                jobs.append([kind = package.kind, pkg = package.pkg] {
                    ext::Manager::install(kind, pkg);
                    return true;
                });
            }
            for (int i = 0; i < jobs.size(); i += 4) {
                if (CancelScope::cancelled())
                    throw Error(QStringLiteral("Cancelled"), 499);
                for (const auto &result : parallel(jobs.mid(i, 4)))
                    installed += result.ok() ? 1 : 0;
            }
            // Their sources, before the series look for them.
            if (installed > 0)
                QMetaObject::invokeMethod(
                    App::instance(),
                    [] {
                        comics::Registry::rebuild();
                        anime::Registry::rebuild();
                    },
                    Qt::BlockingQueuedConnection);
        }
        // The backup's own lists, in its order, empty ones too, and the
        // ones it hides (Komikku) hidden here.
        for (const auto &name : backup.lists)
            Library::categoryNamed(name);
        QStringList hide;
        for (const auto &list : backup.hiddenLists)
            hide.append(list.startsWith(QLatin1String("custom:")) ? Library::categoryNamed(list.mid(7)) : list);
        if (!hide.isEmpty())
            QMetaObject::invokeMethod(Library::instance(), [hide] {
                for (const auto &id : hide)
                    Library::instance()->setCategoryHidden(id, true);
            });
        Tally outcome;
        // Only the sources the backup names; the rest wait for matchAll.
        storeAll(backup.items, outcome, false);
        auto result = summary(outcome);
        result.insert("lists", int(backup.lists.size()));
        result.insert("hiddenLists", int(backup.hiddenLists.size()));
        // TachiyomiSY / Komikku extras SCANCORE has no place for.
        result.insert("savedSearches", backup.savedSearches);
        result.insert("customInfo", backup.customInfo);
        result.insert("merged", backup.merged);
        result.insert("repositoriesAdded", reposAdded);
        result.insert("extensionsInstalled", installed);
        result.insert("downloadsLinked", linkDownloads());
        return result;
    }));
}

namespace {
// SCANCORE's lists as Mihon's categories, in this order. Reading is
// Mihon's Default (no category), as on import.
const QList<QPair<QString, QString>> kMihonCategories{{"planned", "Plan to read"}, {"paused", "On hold"},
                                                     {"completed", "Completed"}, {"dropped", "Dropped"}};

// Mihon's SManga status: 1 ongoing, 2 completed, 5 cancelled, 6 on hiatus.
int mihonStatus(const QString &status) {
    const auto s = status.toLower();
    if (s.contains(QLatin1String("complet")) || s.contains(QLatin1String("finaliz")) || s.contains(QLatin1String("ended")))
        return 2;
    if (s.contains(QLatin1String("cancel")))
        return 5;
    if (s.contains(QLatin1String("hiatus")) || s.contains(QLatin1String("pausa")))
        return 6;
    if (s.contains(QLatin1String("ongoing")) || s.contains(QLatin1String("emisi")) || s.contains(QLatin1String("releasing")))
        return 1;
    return 0;
}

// Mihon's tracker ids for SCANCORE's trackers.
const QList<QPair<QString, int>> kMihonTrackers{{"mal", 1},      {"anilist", 2},      {"kitsu", 3},
                                               {"shikimori", 4}, {"bangumi", 5},     {"mangaupdates", 7}};

// A series' chapter list settings as Mihon's chapter flags.
quint64 mihonChapterFlags(const QVariantMap &settings) {
    quint64 flags = 0;
    if (settings.value("sort").toString() == QLatin1String("asc"))
        flags |= 0x1;
    if (settings.value("onlyUnread").toBool())
        flags |= 0x2;
    if (settings.value("onlyBookmarked").toBool())
        flags |= 0x20;
    return flags;
}

// Its reader settings as Mihon's reading mode (0 when it follows the
// app's).
quint64 mihonReadingMode(const QVariantMap &reader) {
    const auto mode = reader.value("readerMode").toString();
    if (mode == QLatin1String("strip"))
        return 4;
    if (mode == QLatin1String("paged"))
        return reader.value("readerDirection").toString() == QLatin1String("rtl") ? 2 : 1;
    return 0;
}

// The link an extension knows ("mihon:<id>:<link>" without the key).
QString extensionLink(const QString &url) {
    const auto wrapped = ext::unwrapUrl(url);
    return wrapped ? wrapped->url : QString();
}
} // namespace

Task *Importer::exportMihon(const QUrl &file) {
    return Task::run([file]() -> QVariant {
        const auto rows = Database::query(QStringLiteral(R"(
            SELECT s.id, s.source, s.source_url, s.title, s.cover, s.description, s.author, s.status, s.genres,
                   l.category, l.added_at
            FROM library l JOIN series s ON s.id = l.series_id
            WHERE s.media = 'comic' ORDER BY s.title
        )"));
        ProtoWriter backup;
        QHash<QString, QString> sourceNames;
        int exported = 0, skipped = 0;
        // The built-in lists series are in, then all the user's own.
        QSet<QString> used;
        for (const auto &value : rows)
            if (value.toMap().value("source").toString().startsWith(QLatin1String("mihon:")))
                used.insert(value.toMap().value("category").toString());
        // Under the name given here, if any (an import back finds it again).
        const auto names = Library::storedListLayout().value("names").toMap();
        QList<QPair<QString, QString>> lists;
        for (const auto &list : kMihonCategories)
            if (used.contains(list.first))
                lists.append({list.first, names.value(list.first, list.second).toString()});
        for (const auto &c : Library::storedCategories())
            lists.append({c.toMap().value("id").toString(), c.toMap().value("name").toString()});
        const auto trackerLinks = Database::pref(QStringLiteral("tracker.links")).toMap();
        const auto readerSeries = Database::pref(QStringLiteral("setting.readerSeries")).toMap();
        for (const auto &value : rows) {
            const auto row = value.toMap();
            const auto key = row.value("source").toString();
            const auto url = extensionLink(row.value("source_url").toString());
            bool ok = false;
            const auto sourceId = key.startsWith(QLatin1String("mihon:")) ? key.mid(6).toLongLong(&ok) : 0;
            // Built-in sources have no Mihon source to open them.
            if (!ok || url.isEmpty()) {
                ++skipped;
                continue;
            }
            const auto id = row.value("id").toString();
            if (!sourceNames.contains(key))
                if (const auto source = comics::Registry::byKey(key))
                    sourceNames.insert(key, source->label());
            ProtoWriter manga;
            manga.varint(1, quint64(sourceId));
            manga.text(2, url);
            manga.text(3, row.value("title").toString());
            manga.text(5, row.value("author").toString());
            manga.text(6, row.value("description").toString());
            for (const auto &genre : row.value("genres").toString().split(QStringLiteral(", "), Qt::SkipEmptyParts))
                manga.text(7, genre);
            manga.varint(8, quint64(mihonStatus(row.value("status").toString())));
            manga.text(9, row.value("cover").toString());
            manga.varint(13, quint64(row.value("added_at").toLongLong() * 1000));
            // Chapters, with the ones finished read and the others' place.
            QHash<QString, QVariantMap> progress;
            for (const auto &p : Database::query(QStringLiteral("SELECT unit_key, completed, updated_at, position FROM progress WHERE series_id = ?"), {id}))
                progress.insert(p.toMap().value("unit_key").toString(), p.toMap());
            const auto units = Database::query(QStringLiteral("SELECT unit_key, number, title, url, released_at, scanlator FROM units WHERE series_id = ? ORDER BY idx"), {id});
            QSet<QString> bookmarked;
            for (const auto &b : Database::query(QStringLiteral("SELECT unit_key FROM bookmarks WHERE series_id = ?"), {id}))
                bookmarked.insert(b.toMap().value("unit_key").toString());
            QList<ProtoWriter> history;
            for (int i = 0; i < units.size(); ++i) {
                const auto unit = units[i].toMap();
                const auto key = unit.value("unit_key").toString();
                auto link = extensionLink(unit.value("url").toString());
                if (link.isEmpty())
                    link = extensionLink(key);
                if (link.isEmpty())
                    continue;
                const auto done = progress.value(key);
                ProtoWriter chapter;
                chapter.text(1, link);
                chapter.requiredText(2, unit.value("title").toString());
                // Its group (Mihon hides groups by this name) and release.
                chapter.text(3, unit.value("scanlator").toString());
                chapter.boolean(4, done.value("completed").toBool());
                if (bookmarked.contains(key))
                    chapter.boolean(5, true);
                // The page a chapter part-read is on.
                if (!done.isEmpty() && !done.value("completed").toBool())
                    if (const auto panel = QJsonDocument::fromJson(done.value("position").toString().toUtf8()).object().value("panel").toInteger(); panel > 0)
                        chapter.varint(6, quint64(panel));
                bool numbered = false;
                const auto number = unit.value("number").toString().toFloat(&numbered);
                if (const auto released = QDateTime::fromString(unit.value("released_at").toString(), Qt::ISODateWithMs); released.isValid())
                    chapter.varint(8, quint64(released.toMSecsSinceEpoch()));
                chapter.fixed32(9, numbered ? number : -1.f);
                chapter.varint(10, quint64(units.size() - 1 - i));
                if (!done.isEmpty()) {
                    chapter.varint(11, quint64(done.value("updated_at").toLongLong()));
                    ProtoWriter read;
                    read.text(1, link);
                    read.varint(2, quint64(done.value("updated_at").toLongLong() * 1000));
                    history.append(read);
                }
                manga.message(16, chapter);
            }
            for (int c = 0; c < lists.size(); ++c)
                if (lists[c].first == row.value("category").toString())
                    manga.varint(17, quint64(c + 1));
            // Trackers it is linked to: Mihon's id for each, its id there.
            const auto links = trackerLinks.value(id).toMap();
            for (const auto &[service, syncId] : kMihonTrackers) {
                bool numeric = false;
                const auto remote = links.value(service).toMap().value("id").toString().toLongLong(&numeric);
                if (!numeric || remote <= 0)
                    continue;
                ProtoWriter tracking;
                tracking.varint(1, quint64(syncId));
                tracking.varint(2, 0);
                if (remote <= std::numeric_limits<int>::max())
                    tracking.varint(3, quint64(remote));
                tracking.text(5, links.value(service).toMap().value("title").toString());
                tracking.varint(100, quint64(remote));
                manga.message(18, tracking);
            }
            manga.boolean(100, true);
            const auto settings = QJsonDocument::fromJson(Database::scalar(QStringLiteral("SELECT value FROM series_settings WHERE series_id = ?"), {id})
                                                              .toString()
                                                              .toUtf8())
                                      .object()
                                      .toVariantMap();
            if (const auto flags = mihonChapterFlags(settings))
                manga.varint(101, flags);
            if (const auto mode = mihonReadingMode(readerSeries.value(id).toMap()))
                manga.varint(103, mode);
            for (const auto &read : history)
                manga.message(104, read);
            for (const auto &group : settings.value("hiddenScanlators").toStringList())
                manga.text(108, group);
            backup.message(1, manga);
            ++exported;
        }
        if (exported == 0)
            throw Error(QStringLiteral("No comics from extensions to export. Mihon cannot open series from SCANCORE's built-in sources."));
        for (int c = 0; c < lists.size(); ++c) {
            ProtoWriter category;
            category.requiredText(1, lists[c].second);
            category.varint(2, quint64(c + 1));
            backup.message(2, category);
        }
        for (auto it = sourceNames.cbegin(); it != sourceNames.cend(); ++it) {
            ProtoWriter source;
            source.text(1, it.value());
            source.varint(2, quint64(it.key().mid(6).toLongLong()));
            backup.message(101, source);
        }
        // The extension repositories, so Mihon can offer them back.
        for (const auto &repo : ext::Manager::repositories()) {
            if (repo.kind != ext::Kind::Mihon || repo.signingKey.isEmpty())
                continue;
            ProtoWriter store;
            store.text(1, repo.url);
            store.text(2, repo.name.isEmpty() ? QUrl(repo.url).host() : repo.name);
            store.requiredText(4, repo.website);
            store.text(5, repo.signingKey);
            backup.message(106, store);
        }
        QSaveFile out(file.isLocalFile() ? file.toLocalFile() : file.toString());
        const auto data = local::gzip(backup.data());
        if (data.isEmpty() || !out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
            throw Error(QStringLiteral("Could not write %1.").arg(out.fileName()));
        return QVariantMap{{"exported", exported}, {"skipped", skipped}};
    });
}

Task *Importer::importAniList(const QString &user) {
    const auto name = user.trimmed().section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    return finish(Task::run([name]() -> QVariant {
        if (name.isEmpty())
            throw Error(QStringLiteral("Type your AniList user name."));
        Tally outcome;
        QList<Item> books;
        for (const auto *type : {"ANIME", "MANGA"}) {
            const auto data = anilist(QStringLiteral("query ($user: String, $type: MediaType) { MediaListCollection(userName: $user, type: $type) {"
                                                     " lists { entries { status progress updatedAt media {%1} } } } }")
                                          .arg(QString::fromLatin1(kMediaFields)),
                                      {{"user", name}, {"type", type}});
            for (const auto &list : data.value("MediaListCollection").toObject().value("lists").toArray())
                for (const auto &value : list.toObject().value("entries").toArray()) {
                    const auto entry = value.toObject();
                    auto item = itemFrom(entry.value("media").toObject());
                    item.category = categoryFor(entry.value("status").toString());
                    item.count = entry.value("progress").toInt();
                    item.updatedAt = entry.value("updatedAt").toInteger();
                    if (item.media == QLatin1String("anime"))
                        storeAnime(item, outcome);
                    else
                        books.append(item);
                }
        }
        // Comics and novels have no source yet; matchAll looks for them.
        outcome.unmatched += books;
        return summary(outcome);
    }));
}

Task *Importer::importMal(const QUrl &file) {
    return finish(Task::run([file]() -> QVariant {
        const auto items = parseMal(readFile(file));
        if (items.isEmpty())
            throw Error(QStringLiteral("This export has no titles."));
        QList<int> animeIds;
        QList<int> mangaIds;
        for (const auto &item : items)
            (item.media == QLatin1String("anime") ? animeIds : mangaIds).append(item.malId);
        const auto anime = byMalIds(animeIds, true);
        const auto manga = byMalIds(mangaIds, false);
        Tally outcome;
        for (auto item : items) {
            const auto media = (item.media == QLatin1String("anime") ? anime : manga).value(item.malId);
            if (!media.isEmpty()) {
                auto found = itemFrom(media);
                found.category = item.category;
                found.count = item.count;
                item = found;
            }
            if (item.media == QLatin1String("anime")) {
                if (!item.anime.isEmpty())
                    storeAnime(item, outcome);
            } else {
                outcome.unmatched.append(item);
            }
        }
        return summary(outcome);
    }));
}

Task *Importer::importComick() {
    return finish(Task::run([]() -> QVariant {
        const auto entries = Trackers::comickLibrary();
        if (entries.isEmpty())
            throw Error(QStringLiteral("Your Comick library is empty."));
        Tally outcome;
        QList<Item> anime;
        static const QStringList categories{QString(), QStringLiteral("reading"), QStringLiteral("completed"), QStringLiteral("paused"),
                                            QStringLiteral("dropped"), QStringLiteral("planned")};
        for (const auto &value : entries) {
            const auto entry = value.toMap();
            Item item;
            item.media = entry.value("media_type").toString() == QLatin1String("anime") ? QStringLiteral("anime") : QStringLiteral("comic");
            item.titles = {entry.value("title").toString()};
            for (const auto &t : entry.value("titles").toList())
                item.titles.append(t.toMap().value("title").toString());
            item.titles.removeAll(QString());
            item.titles.removeDuplicates();
            item.cover = entry.value("cover_url").toString();
            item.category = categories.value(entry.value("status").toInt(), QStringLiteral("reading"));
            item.count = int(entry.value("progress").toMap().value("number").toString().toDouble());
            item.comickHid = entry.value("hid").toString();
            item.updatedAt = QDateTime::fromString(entry.value("progressed_at").toString(), Qt::ISODateWithMs).toSecsSinceEpoch();
            (item.media == QLatin1String("anime") ? anime : outcome.unmatched).append(item);
        }
        // Anime are found by their AniList entry, which Comick knows.
        QList<int> ids;
        QHash<int, Item> byId;
        for (auto &item : anime) {
            if (CancelScope::cancelled())
                throw Error(QStringLiteral("Cancelled"), 499);
            item.anilistId = Trackers::comickAniListId(item.comickHid);
            if (item.anilistId > 0) {
                ids.append(item.anilistId);
                byId.insert(item.anilistId, item);
            }
        }
        for (int i = 0; i < ids.size(); i += 50) {
            QJsonArray chunk;
            for (const int id : ids.mid(i, 50))
                chunk.append(id);
            const auto data = anilist(QStringLiteral("query ($ids: [Int]) { Page(perPage: 50) { media(id_in: $ids, type: ANIME) {%1} } }")
                                          .arg(QString::fromLatin1(kMediaFields)),
                                      {{"ids", chunk}});
            for (const auto &media : data.value("Page").toObject().value("media").toArray()) {
                auto item = byId.value(media.toObject().value("id").toInt());
                const auto found = itemFrom(media.toObject());
                item.anime = found.anime;
                item.malId = found.malId;
                if (!item.anime.isEmpty())
                    storeAnime(item, outcome);
            }
        }
        return summary(outcome);
    }));
}

Task *Importer::matchAll() {
    return finish(Task::run([]() -> QVariant {
        QList<Item> waiting;
        for (const auto &value : Importer::instance()->unmatched())
            waiting.append(fromMap(value.toMap()));
        Tally outcome;
        storeAll(waiting, outcome, true);
        {
            // Titles an import added meanwhile stay too.
            QMutexLocker lock(&g_unmatchedMutex);
            QSet<QString> tried;
            for (const auto &item : waiting)
                tried.insert(item.media + titleKey(item.title()));
            QVariantList left;
            for (const auto &value : Database::pref(kUnmatched).toList())
                if (!tried.contains(value.toMap().value("media").toString() + titleKey(value.toMap().value("title").toString())))
                    left.append(value);
            for (const auto &item : outcome.unmatched)
                left.append(toMap(item));
            Database::setPref(kUnmatched, left);
        }
        return QVariantMap{{"matched", outcome.imported}, {"left", outcome.unmatched.size()}, {"progress", outcome.progress},
                           {"saved", outcome.saved}, {"downloadsLinked", outcome.imported > 0 ? linkDownloads() : 0}};
    }));
}

void Importer::forget(const QString &title) {
    {
        QMutexLocker lock(&g_unmatchedMutex);
        auto list = Database::pref(kUnmatched).toList();
        list.removeIf([&](const QVariant &value) { return value.toMap().value("title").toString() == title; });
        Database::setPref(kUnmatched, list);
    }
    emit changed();
}

void Importer::forgetAll() {
    {
        QMutexLocker lock(&g_unmatchedMutex);
        Database::setPref(kUnmatched, QVariantList());
    }
    emit changed();
}

} // namespace sc
