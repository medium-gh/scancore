#include "Story.h"

#include "Account.h"
#include "TrackerCommon.h"
#include "core/Database.h"
#include "core/Http.h"
#include "core/Text.h"
#include "sources/catalogue/Catalogue.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace sc {

namespace {

// What every form needs: enough for a card, and for opening it as a
// catalogue entry when it is not in the library.
const char *kFields = R"(
  id idMal type format status(version: 2) chapters volumes episodes countryOfOrigin
  startDate { year } averageScore isAdult siteUrl
  title { romaji english native } synonyms coverImage { large }
)";

QJsonObject anilist(const QString &query, const QJsonObject &variables) {
    const auto response = Http::postJson(QUrl(QStringLiteral("https://graphql.anilist.co/")),
                                         QJsonDocument(QJsonObject{{"query", query}, {"variables", variables}}).toJson(QJsonDocument::Compact),
                                         {{"Accept", "application/json"}});
    if (response.status == 429)
        throw Error(QStringLiteral("AniList is busy. Try again in a minute."), 429);
    const auto data = response.json().object().value("data").toObject();
    if (!response.ok() || data.isEmpty())
        throw Error(QStringLiteral("AniList is unavailable right now (%1).").arg(response.status), response.status);
    return data;
}

QString mediaOf(const QJsonObject &node) {
    if (node.value("type").toString() == QLatin1String("ANIME"))
        return QStringLiteral("anime");
    return node.value("format").toString() == QLatin1String("NOVEL") ? QStringLiteral("novel") : QStringLiteral("comic");
}

QString titleOf(const QJsonObject &node) {
    const auto t = node.value("title").toObject();
    for (const auto *key : {"english", "romaji", "native"})
        if (const auto s = t.value(QLatin1String(key)).toString(); !s.isEmpty())
            return s;
    return {};
}

// A series and the forms around it, two steps out (an anime's source and
// that source's other adaptations), cached for three days.
QJsonObject relationsOf(int id) {
    const auto key = QStringLiteral("story:rel:%1").arg(id);
    if (auto cached = Database::cacheGet(key))
        return QJsonDocument::fromJson(*cached).object();
    const auto query = QStringLiteral(
        "query ($id: Int) { Media(id: $id) { %1 relations { edges { relationType node { %1 "
        "relations { edges { relationType node { %1 } } } } } } } }")
                           .arg(QString::fromLatin1(kFields));
    const auto media = anilist(query, {{"id", id}}).value("Media").toObject();
    Database::cachePut(key, QJsonDocument(media).toJson(QJsonDocument::Compact), 3 * 86400);
    return media;
}

// Your series by AniList entry: tracker links and what sources stored.
// Series in the library win over ones only in the history.
QHash<int, QString> seriesByAniList() {
    QHash<int, QString> out;
    const auto links = tracking::allLinks();
    for (auto it = links.begin(); it != links.end(); ++it)
        if (const int id = it.value().toMap().value("anilist").toMap().value("id").toInt(); id > 0)
            out.insert(id, it.key());
    const auto rows = Database::query(QStringLiteral(
        "SELECT s.id, s.extra, (l.series_id IS NOT NULL) AS saved FROM series s LEFT JOIN library l ON l.series_id = s.id "
        "WHERE s.media IN ('comic', 'novel') AND s.extra LIKE '%\"anilist\"%' "
        "AND (l.series_id IS NOT NULL OR EXISTS (SELECT 1 FROM progress p WHERE p.series_id = s.id)) ORDER BY saved"));
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const int id = QJsonDocument::fromJson(row.value("extra").toString().toUtf8()).object().value("anilist").toInt();
        if (id > 0)
            out.insert(id, row.value("id").toString());
    }
    return out;
}

bool saved(const QString &seriesId) {
    return Database::scalar(QStringLiteral("SELECT 1 FROM library WHERE series_id = ?"), {seriesId}).isValid();
}

int unitCount(const QString &seriesId) {
    return Database::scalar(QStringLiteral("SELECT COUNT(*) FROM units WHERE series_id = ?"), {seriesId}).toInt();
}

QVariantMap formOf(const QJsonObject &node, const QString &relation, const QHash<int, QString> &mine) {
    const int id = node.value("id").toInt();
    const auto media = mediaOf(node);
    QString seriesId = media == QLatin1String("anime") ? QStringLiteral("anime:anilist:%1").arg(id) : mine.value(id);
    const int done = seriesId.isEmpty() ? 0 : tracking::progressOf(seriesId);
    const bool inLibrary = !seriesId.isEmpty() && saved(seriesId);
    if (media != QLatin1String("anime") && !inLibrary && done == 0)
        seriesId.clear();
    int total = node.value(media == QLatin1String("anime") ? "episodes" : "chapters").toInt();
    if (total == 0 && !seriesId.isEmpty() && media != QLatin1String("anime"))
        total = unitCount(seriesId);
    QVariantMap entry;
    if (media == QLatin1String("anime"))
        entry = {{"media", "anime"}, {"anilistId", id}, {"id", seriesId}, {"title", titleOf(node)},
                 {"cover", node.value("coverImage").toObject().value("large").toString()}};
    else
        entry = catalogue::anilistEntry(node).toMap();
    return {{"media", media},
            {"anilistId", id},
            {"title", titleOf(node)},
            {"cover", node.value("coverImage").toObject().value("large").toString()},
            {"format", node.value("format").toString().replace(QLatin1Char('_'), QLatin1Char(' '))},
            {"status", node.value("status").toString()},
            {"year", node.value("startDate").toObject().value("year").toInt()},
            {"total", total},
            {"volumes", node.value("volumes").toInt()},
            {"relation", relation},
            {"seriesId", seriesId},
            {"saved", inLibrary},
            {"done", done},
            {"entry", entry}};
}

// The AniList entry of a comic or novel known only by its titles: matched
// once and remembered (a miss for three days).
int matchedId(const QString &seriesId, const QString &media, const QString &title) {
    const auto key = QStringLiteral("story:match:") + seriesId;
    if (auto cached = Database::cacheGet(key))
        return cached->toInt();
    const auto match = tracking::matchByTitles([&](const QString &t) { return tracking::anilistSearch(media, t); },
                                               tracking::lookupTitles(seriesId, title));
    const int id = match.value("id").toInt();
    Database::cachePut(key, QByteArray::number(id), id > 0 ? 30 * 86400 : 3 * 86400);
    return id;
}

QVariantMap pointsFor(const QList<int> &ids, bool fresh) {
    QVariantMap out;
    QStringList missing;
    for (const int id : ids) {
        if (id <= 0)
            continue;
        if (auto cached = fresh ? std::nullopt : Database::cacheGet(QStringLiteral("story:points:%1").arg(id)))
            out.insert(QString::number(id), QJsonDocument::fromJson(*cached).array().toVariantList());
        else
            missing.append(QString::number(id));
    }
    for (int at = 0; at < missing.size(); at += 40) {
        const auto chunk = missing.mid(at, 40);
        const auto found = Account::call("GET", QStringLiteral("/api/story?anime=") + chunk.join(QLatin1Char(','))).value("points").toMap();
        for (const auto &id : chunk) {
            const auto list = found.value(id).toList();
            out.insert(id, list);
            Database::cachePut(QStringLiteral("story:points:") + id, QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact),
                               6 * 3600);
        }
    }
    return out;
}

QStringList dismissed() { return Database::pref(QStringLiteral("story.dismissed")).toStringList(); }

// Anime you watched lately: {id: {episodes, status, title, cover}}, a day
// in the cache, the rest in one request.
QHash<int, QJsonObject> animeInfo(const QList<int> &ids) {
    QHash<int, QJsonObject> out;
    QList<int> missing;
    for (const int id : ids) {
        if (auto cached = Database::cacheGet(QStringLiteral("story:anime:%1").arg(id)))
            out.insert(id, QJsonDocument::fromJson(*cached).object());
        else
            missing.append(id);
    }
    for (int at = 0; at < missing.size(); at += 25) {
        QStringList parts;
        for (int i = at; i < std::min(at + 25, int(missing.size())); ++i)
            parts.append(QStringLiteral("a%1: Media(id: %1) { id episodes status(version: 2) title { romaji english native } coverImage { large } }")
                             .arg(missing[i]));
        const auto data = anilist(QStringLiteral("query { %1 }").arg(parts.join(QLatin1Char(' '))), {});
        for (int i = at; i < std::min(at + 25, int(missing.size())); ++i) {
            const auto media = data.value(QStringLiteral("a%1").arg(missing[i])).toObject();
            out.insert(missing[i], media);
            Database::cachePut(QStringLiteral("story:anime:%1").arg(missing[i]), QJsonDocument(media).toJson(QJsonDocument::Compact), 86400);
        }
    }
    return out;
}

// A comic's source novel, if AniList knows one (a week in the cache).
QHash<int, QJsonObject> sourceNovels(const QList<int> &ids) {
    QHash<int, QJsonObject> out;
    QList<int> missing;
    for (const int id : ids) {
        if (auto cached = Database::cacheGet(QStringLiteral("story:novel:%1").arg(id)))
            out.insert(id, QJsonDocument::fromJson(*cached).object());
        else
            missing.append(id);
    }
    for (int at = 0; at < missing.size(); at += 20) {
        QStringList parts;
        for (int i = at; i < std::min(at + 20, int(missing.size())); ++i)
            parts.append(QStringLiteral("c%1: Media(id: %1) { relations { edges { relationType node { %2 } } } }")
                             .arg(missing[i])
                             .arg(QString::fromLatin1(kFields)));
        const auto data = anilist(QStringLiteral("query { %1 }").arg(parts.join(QLatin1Char(' '))), {});
        for (int i = at; i < std::min(at + 20, int(missing.size())); ++i) {
            QJsonObject novel;
            for (const auto &edge : data.value(QStringLiteral("c%1").arg(missing[i])).toObject().value("relations").toObject().value("edges").toArray()) {
                const auto e = edge.toObject();
                const auto node = e.value("node").toObject();
                if (e.value("relationType").toString() == QLatin1String("SOURCE") && mediaOf(node) == QLatin1String("novel")) {
                    novel = node;
                    break;
                }
            }
            out.insert(missing[i], novel);
            Database::cachePut(QStringLiteral("story:novel:%1").arg(missing[i]), QJsonDocument(novel).toJson(QJsonDocument::Compact), 7 * 86400);
        }
    }
    return out;
}

// AniList entries by id, a week in the cache (the comics and novels points
// lead to).
QHash<int, QJsonObject> mediaNodes(const QList<int> &ids) {
    QHash<int, QJsonObject> out;
    QList<int> missing;
    for (const int id : ids) {
        if (id <= 0)
            continue;
        if (auto cached = Database::cacheGet(QStringLiteral("story:media:%1").arg(id)))
            out.insert(id, QJsonDocument::fromJson(*cached).object());
        else if (!missing.contains(id))
            missing.append(id);
    }
    for (int at = 0; at < missing.size(); at += 25) {
        QStringList parts;
        for (int i = at; i < std::min(at + 25, int(missing.size())); ++i)
            parts.append(QStringLiteral("m%1: Media(id: %1) { %2 }").arg(missing[i]).arg(QString::fromLatin1(kFields)));
        const auto data = anilist(QStringLiteral("query { %1 }").arg(parts.join(QLatin1Char(' '))), {});
        for (int i = at; i < std::min(at + 25, int(missing.size())); ++i) {
            const auto media = data.value(QStringLiteral("m%1").arg(missing[i])).toObject();
            out.insert(missing[i], media);
            Database::cachePut(QStringLiteral("story:media:%1").arg(missing[i]), QJsonDocument(media).toJson(QJsonDocument::Compact), 7 * 86400);
        }
    }
    return out;
}

QVariantList workOutCards() {
    const auto skip = dismissed();
    const auto mine = seriesByAniList();
    QVariantList cards;

    // 1. Anime seasons you finished: where the comic or novel goes on.
    QList<int> animeIds;
    QHash<int, qint64> watchedAt;
    for (const auto &value : Database::query(QStringLiteral(
             "SELECT series_id, MAX(updated_at) AS at FROM progress WHERE series_id LIKE 'anime:anilist:%' AND completed = 1 "
             "GROUP BY series_id HAVING at > ? ORDER BY at DESC LIMIT 40"),
                                             {Database::now() - 180 * 86400})) {
        const auto row = value.toMap();
        const int id = row.value("series_id").toString().section(QLatin1Char(':'), 2).toInt();
        if (id > 0) {
            animeIds.append(id);
            watchedAt.insert(id, row.value("at").toLongLong());
        }
    }
    QList<int> finished;
    const auto info = animeIds.isEmpty() ? QHash<int, QJsonObject>() : animeInfo(animeIds);
    for (const int id : animeIds) {
        const auto anime = info.value(id);
        const int episodes = anime.value("episodes").toInt();
        if (anime.value("status").toString() == QLatin1String("FINISHED") && episodes > 0 &&
            tracking::progressOf(QStringLiteral("anime:anilist:%1").arg(id)) >= episodes)
            finished.append(id);
    }
    const auto points = finished.isEmpty() ? QVariantMap() : pointsFor(finished, false);
    QList<int> sourceIds;
    for (const auto &list : points)
        for (const auto &value : list.toList())
            sourceIds.append(value.toMap().value("sourceId").toInt());
    const auto sources = sourceIds.isEmpty() ? QHash<int, QJsonObject>() : mediaNodes(sourceIds);
    for (const int id : finished) {
        const auto anime = info.value(id);
        QSet<QString> shown;
        for (const auto &value : points.value(QString::number(id)).toList()) {
            const auto point = value.toMap();
            const auto media = point.value("media").toString();
            // The best point per media; a point voted under zero is no help.
            if (shown.contains(media) || point.value("score").toInt() < 0)
                continue;
            shown.insert(media);
            const auto key = QStringLiteral("anime:%1:%2").arg(id).arg(media);
            if (skip.contains(key))
                continue;
            const int sourceId = point.value("sourceId").toInt();
            const auto seriesId = mine.value(sourceId);
            const double chapter = point.value("chapter").toDouble();
            const int done = seriesId.isEmpty() ? 0 : tracking::progressOf(seriesId);
            // Already read past it.
            if (chapter > 0 && done >= chapter)
                continue;
            QVariantMap card{{"key", key},
                             {"kind", "anime"},
                             {"animeId", id},
                             {"animeTitle", titleOf(anime)},
                             {"media", media},
                             {"chapter", chapter},
                             {"volume", point.value("volume").toInt()},
                             {"sourceId", sourceId},
                             {"seriesId", seriesId},
                             {"done", done},
                             {"cover", anime.value("coverImage").toObject().value("large").toString()},
                             {"at", watchedAt.value(id)}};
            if (const auto source = sources.value(sourceId); !source.isEmpty()) {
                card.insert("title", titleOf(source));
                card.insert("type", catalogue::anilistEntry(source).type);
                card.insert("entry", catalogue::anilistEntry(source).toMap());
            }
            cards.append(card);
        }
    }

    // 2. Comics you caught up on whose novel goes further.
    QList<int> caughtUp;
    QHash<int, QVariantMap> comics;
    for (const auto &value : Database::query(QStringLiteral(
             "SELECT s.id, s.title, s.cover, l.known_units, MAX(p.updated_at) AS at, "
             "SUM(p.completed) AS read_units FROM library l JOIN series s ON s.id = l.series_id "
             "JOIN progress p ON p.series_id = s.id WHERE s.media = 'comic' AND l.known_units > 0 "
             "GROUP BY s.id HAVING read_units >= l.known_units AND at > ? ORDER BY at DESC LIMIT 30"),
                                             {Database::now() - 120 * 86400})) {
        const auto row = value.toMap();
        const auto seriesId = row.value("id").toString();
        const int id = tracking::anilistIdOf(seriesId);
        if (id <= 0 || skip.contains(QStringLiteral("novel:%1").arg(id)))
            continue;
        caughtUp.append(id);
        comics.insert(id, row);
    }
    const auto novels = caughtUp.isEmpty() ? QHash<int, QJsonObject>() : sourceNovels(caughtUp.mid(0, 20));
    for (auto it = novels.begin(); it != novels.end(); ++it) {
        const auto novel = it.value();
        if (novel.isEmpty())
            continue;
        const int novelId = novel.value("id").toInt();
        const auto novelSeries = mine.value(novelId);
        // Reading the novel already.
        if (!novelSeries.isEmpty() && tracking::progressOf(novelSeries) > 0)
            continue;
        const auto comic = comics.value(it.key());
        cards.append(QVariantMap{{"key", QStringLiteral("novel:%1").arg(it.key())},
                                 {"kind", "novel"},
                                 {"comicTitle", comic.value("title")},
                                 {"comicId", comic.value("id")},
                                 {"media", "novel"},
                                 {"type", "Novel"},
                                 {"sourceId", novelId},
                                 {"seriesId", novelSeries},
                                 {"title", titleOf(novel)},
                                 {"cover", novel.value("coverImage").toObject().value("large").toString()},
                                 {"entry", catalogue::anilistEntry(novel).toMap()},
                                 {"status", novel.value("status").toString()},
                                 {"at", comic.value("at")}});
    }
    std::sort(cards.begin(), cards.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("at").toLongLong() > b.toMap().value("at").toLongLong();
    });
    return cards.mid(0, 8);
}

} // namespace

Story::Story(QObject *parent) : QObject(parent) {
    // Last time's cards straight away; refreshCards() brings them up to date.
    if (auto cached = Database::cacheGet(QStringLiteral("story:cards"), true))
        m_cards = QJsonDocument::fromJson(*cached).array().toVariantList();
}

Story *Story::instance() {
    static auto *story = [] {
        auto *created = new Story;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return story;
}

Task *Story::forms(const QString &seriesId, const QString &media, const QString &title, int anilistId) {
    return Task::run([seriesId, media, title, anilistId]() -> QVariant {
        int id = anilistId > 0 ? anilistId : tracking::anilistIdOf(seriesId);
        if (id <= 0 && media != QLatin1String("anime") && !seriesId.contains(QLatin1String(":local:")))
            id = matchedId(seriesId, media, title);
        if (id <= 0)
            return QVariantMap{{"anilistId", 0}, {"forms", QVariantList()}};
        auto self = relationsOf(id);
        // A one-shot (a match or tracker link made before 2.17.1 could land
        // on the pilot) stands for the series it is a version of, whose
        // relations hold the anime and novels.
        if (self.value("format").toString() == QLatin1String("ONE_SHOT"))
            for (const auto &edge : self.value("relations").toObject().value("edges").toArray()) {
                const auto e = edge.toObject();
                const auto node = e.value("node").toObject();
                const auto type = e.value("relationType").toString();
                if ((type == QLatin1String("ALTERNATIVE") || type == QLatin1String("PARENT")) && mediaOf(node) == mediaOf(self)
                    && node.value("format").toString() != QLatin1String("ONE_SHOT") && node.value("id").toInt() > 0) {
                    id = node.value("id").toInt();
                    self = relationsOf(id);
                    break;
                }
            }
        const auto mine = seriesByAniList();
        // Other forms: what this adapts or is adapted into, and (through its
        // source) the source's other adaptations, such as every season.
        static const QSet<QString> kinds{"ADAPTATION", "SOURCE", "ALTERNATIVE"};
        QVariantList forms;
        QSet<int> seen{id};
        const auto ownMedia = mediaOf(self);
        auto add = [&](const QJsonObject &node, const QString &relation) {
            const int nodeId = node.value("id").toInt();
            if (nodeId <= 0 || seen.contains(nodeId) || node.value("format").toString() == QLatin1String("MUSIC"))
                return;
            seen.insert(nodeId);
            // Another season of this anime is a sequel, not another form.
            if (mediaOf(node) == ownMedia && ownMedia == QLatin1String("anime") && relation != QLatin1String("ALTERNATIVE"))
                return;
            forms.append(formOf(node, relation, mine));
        };
        for (const auto &edge : self.value("relations").toObject().value("edges").toArray()) {
            const auto e = edge.toObject();
            const auto type = e.value("relationType").toString();
            if (!kinds.contains(type))
                continue;
            const auto node = e.value("node").toObject();
            add(node, type);
            // One step further: an anime's comic leads to its novel and to
            // the other seasons.
            if (mediaOf(node) != ownMedia)
                for (const auto &inner : node.value("relations").toObject().value("edges").toArray()) {
                    const auto i = inner.toObject();
                    if (kinds.contains(i.value("relationType").toString()))
                        add(i.value("node").toObject(), i.value("relationType").toString() == QLatin1String("SOURCE") ? QStringLiteral("SOURCE") : QStringLiteral("ADAPTATION"));
                }
        }
        static const QHash<QString, int> order{{"comic", 0}, {"novel", 1}, {"anime", 2}};
        std::stable_sort(forms.begin(), forms.end(), [](const QVariant &a, const QVariant &b) {
            const auto x = a.toMap(), y = b.toMap();
            if (x.value("media") != y.value("media"))
                return order.value(x.value("media").toString()) < order.value(y.value("media").toString());
            // Your own forms first, then the main ones (TV before movies),
            // oldest first.
            if (x.value("seriesId").toString().isEmpty() != y.value("seriesId").toString().isEmpty() && x.value("media") != QLatin1String("anime"))
                return !x.value("seriesId").toString().isEmpty();
            return x.value("year").toInt() < y.value("year").toInt();
        });
        return QVariantMap{{"anilistId", id}, {"media", ownMedia}, {"forms", forms}};
    });
}

Task *Story::points(const QVariantList &animeIds) {
    QList<int> ids;
    for (const auto &value : animeIds)
        ids.append(value.toInt());
    return Task::run([ids]() -> QVariant { return pointsFor(ids, false); });
}

Task *Story::submit(int animeId, const QString &media, int sourceId, double chapter, int volume) {
    auto *task = Task::run([animeId, media, sourceId, chapter, volume]() -> QVariant {
        QVariantMap body{{"animeId", animeId}, {"media", media}};
        if (sourceId > 0)
            body.insert("sourceId", sourceId);
        if (chapter > 0)
            body.insert("chapter", chapter);
        if (volume > 0)
            body.insert("volume", volume);
        Account::call("POST", QStringLiteral("/api/story"), body);
        return pointsFor({animeId}, true).value(QString::number(animeId));
    });
    task->then(this, [this](const QVariant &) { refreshCards(); });
    return task;
}

Task *Story::vote(int animeId, const QString &pointId, int value) {
    auto *task = Task::run([animeId, pointId, value]() -> QVariant {
        Account::call("POST", QStringLiteral("/api/story/vote"), {{"id", pointId}, {"value", std::clamp(value, -1, 1)}});
        return pointsFor({animeId}, true).value(QString::number(animeId));
    });
    task->then(this, [this](const QVariant &) { refreshCards(); });
    return task;
}

void Story::refreshCards() {
    if (m_refreshing)
        return;
    m_refreshing = true;
    runInBackground([] {
        QVariantList cards;
        bool ok = true;
        try {
            cards = workOutCards();
            Database::cachePut(QStringLiteral("story:cards"), QJsonDocument(QJsonArray::fromVariantList(cards)).toJson(QJsonDocument::Compact),
                               30 * 86400);
        } catch (const Error &e) {
            ok = false;
            qWarning().noquote() << "Continue the story:" << e.message();
        }
        QMetaObject::invokeMethod(instance(), [cards, ok] {
            auto *story = instance();
            story->m_refreshing = false;
            if (!ok)
                return;
            story->m_cards = cards;
            emit story->cardsChanged();
        });
    });
}

void Story::dismiss(const QString &key) {
    auto list = dismissed();
    if (!list.contains(key))
        list.append(key);
    Database::setPref(QStringLiteral("story.dismissed"), list.mid(std::max(0, int(list.size()) - 300)));
    QVariantList kept;
    for (const auto &card : m_cards)
        if (card.toMap().value("key").toString() != key)
            kept.append(card);
    m_cards = kept;
    emit cardsChanged();
}

} // namespace sc
