// Offline tests for the tracker catalogues (Comick, AniList, MyAnimeList),
// the content language, "Read from" matching, MangaDex takedowns and the
// anime pages' fallback when AniList is down (Kitsu, ani.zip).

#include "app/Resolve.h"
#include "core/Database.h"
#include "core/Http.h"
#include "core/Text.h"
#include "sources/anime/Kitsu.h"
#include "sources/catalogue/Catalogue.h"
#include "sources/bundled/comics/Engines.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QThread>
#include <QtTest>

using namespace sc;

static QByteArray fixture(const char *name) {
    QFile file(QStringLiteral(SCANCORE_SOURCE_DIR "/tests/fixtures/") + QLatin1String(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

namespace {

// A comic source that answers every search with the same titles after a
// pause, like a site on the network.
class FakeSource : public comics::Source {
public:
    FakeSource(const QString &key, const QString &language, QStringList titles, bool readable = true, int delayMs = 0)
        : Source(config(key, language)), m_titles(std::move(titles)), m_readable(readable), m_delay(delayMs) {}

    QList<comics::Summary> search(const QString &) override {
        if (m_delay)
            QThread::msleep(m_delay);
        QList<comics::Summary> out;
        for (const auto &title : m_titles)
            out.append({title, QStringLiteral("https://%1.test/series/%2").arg(key(), titleKey(title)), {}, {}, {}, {}, {}, key()});
        return out;
    }
    QList<comics::Chapter> chapters(const QString &) override { return {}; }
    QList<comics::Panel> panels(const QString &) override { return {}; }
    bool readable(const QString &) override { return m_readable; }

private:
    static comics::SourceConfig config(const QString &key, const QString &language) {
        comics::SourceConfig c;
        c.key = key;
        c.label = key.toUpper();
        c.base = QStringLiteral("https://%1.test").arg(key);
        c.language = language;
        return c;
    }
    QStringList m_titles;
    bool m_readable;
    int m_delay;
};

using SourcePtr = std::shared_ptr<comics::Source>;

QVariantList resolve(const QList<SourcePtr> &sources, const QStringList &names) {
    return resolveAcross<SourcePtr>(sources, names, [](const SourcePtr &s, const QString &url) { return s->readable(url); });
}

} // namespace

class CatalogueTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        Database::open(m_dir.filePath(QStringLiteral("test.db")));
    }

    // Kitsu's anime in AniList's shape: AniList's ids from the mappings,
    // AniList's names for formats, statuses and genres.
    void kitsuInAniListShape() {
        const auto doc = QJsonDocument::fromJson(fixture("kitsu-anime.json")).object();
        const auto m = anime::kitsu::toAniList(doc.value("data").toObject(), doc.value("included").toArray());
        QCOMPARE(m.value("id").toInt(), 21);
        QCOMPARE(m.value("idMal").toInt(), 21);
        QCOMPARE(m.value("title").toObject().value("english").toString(), QStringLiteral("One Piece"));
        QCOMPARE(m.value("format").toString(), QStringLiteral("TV"));
        QCOMPARE(m.value("status").toString(), QStringLiteral("RELEASING"));
        QCOMPARE(m.value("startDate").toObject().value("year").toInt(), 1999);
        QCOMPARE(m.value("season").toString(), QStringLiteral("FALL"));
        QVERIFY(m.value("bannerImage").toString().startsWith(QStringLiteral("https://")));
        QVERIFY(m.value("coverImage").toObject().value("extraLarge").toString().startsWith(QStringLiteral("https://")));
        QCOMPARE(m.value("trailer").toObject().value("site").toString(), QStringLiteral("youtube"));
        QCOMPARE(m.value("studios").toObject().value("nodes").toArray().first().toObject().value("name").toString(),
                 QStringLiteral("Toei Animation"));
        // Only AniList's genres, in AniList's words.
        static const QStringList anilist{"Action", "Adventure", "Comedy", "Drama", "Ecchi", "Fantasy", "Horror", "Mahou Shoujo", "Mecha",
                                         "Music", "Mystery", "Psychological", "Romance", "Sci-Fi", "Slice of Life", "Sports",
                                         "Supernatural", "Thriller"};
        const auto genres = m.value("genres").toArray();
        QVERIFY(!genres.isEmpty());
        for (const auto &g : genres)
            QVERIFY2(anilist.contains(g.toString()), qPrintable(g.toString()));
        // The next episode's time; which one it is Kitsu does not say.
        const auto next = m.value("nextAiringEpisode").toObject();
        QVERIFY(next.value("airingAt").toDouble() > 1.7e9);
        QCOMPARE(next.value("episode").toInt(), 0);

        // Without an AniList id it is left out.
        auto bare = doc.value("data").toObject();
        auto relationships = bare.value("relationships").toObject();
        relationships.remove("mappings");
        bare.insert("relationships", relationships);
        QVERIFY(anime::kitsu::toAniList(bare, doc.value("included").toArray()).isEmpty());
    }

    // The fallback's requests, answered from fixtures: lists, search,
    // details, the schedule and MyAnimeList ids.
    void kitsuFallback() {
        QMutex mutex;
        QList<QUrl> asked;
        Http::setTestHandler([&](const Request &request) -> std::optional<Response> {
            const auto host = request.url.host();
            if (host != QLatin1String("kitsu.io") && host != QLatin1String("api.ani.zip"))
                return std::nullopt;
            {
                QMutexLocker lock(&mutex);
                asked.append(request.url);
            }
            Response r;
            r.status = 200;
            r.finalUrl = request.url;
            const auto path = request.url.path();
            const QUrlQuery query(request.url);
            if (host == QLatin1String("api.ani.zip")) {
                r.body = query.hasQueryItem("mal_id") ? QByteArray(R"({"mappings":{"anilist_id":154587}})") : fixture("anizip-airing.json");
            } else if (path == QLatin1String("/api/edge/anime")) {
                // Two pages of three: the second one empty.
                r.body = query.queryItemValue("page[offset]") == QLatin1String("0")
                             ? fixture("kitsu-list.json")
                             : QByteArray(R"({"data":[],"included":[]})");
            } else if (path == QLatin1String("/api/edge/mappings")) {
                r.body = R"({"data":[{"id":"1","type":"mappings","relationships":{"item":{"data":{"type":"anime","id":"12"}}}}]})";
            } else if (path == QLatin1String("/api/edge/anime/12")) {
                r.body = fixture("kitsu-anime.json");
            } else {
                r.status = 404;
            }
            return r;
        });
        const auto value = [&](const QString &name) {
            QMutexLocker lock(&mutex);
            for (const auto &url : asked)
                if (const auto v = QUrlQuery(url).queryItemValue(name, QUrl::FullyDecoded); !v.isEmpty())
                    return v;
            return QString();
        };

        const auto popular = anime::kitsu::list(QStringLiteral("popular"));
        QCOMPARE(popular.size(), 3);
        QCOMPARE(popular.first().toObject().value("id").toInt(), 21);
        QCOMPARE(value(QStringLiteral("sort")), QStringLiteral("-userCount"));
        QVERIFY(value(QStringLiteral("include")).contains(QStringLiteral("mappings")));

        asked.clear();
        auto found = anime::kitsu::search({{"genres", QStringList{"Sci-Fi", "Slice of Life"}},
                                           {"yearFrom", 2020},
                                           {"status", "RELEASING"},
                                           {"format", "MOVIE"},
                                           {"sort", "SCORE_DESC"}});
        QCOMPARE(value(QStringLiteral("filter[categories]")), QStringLiteral("science-fiction,slice-of-life"));
        QCOMPARE(value(QStringLiteral("filter[seasonYear]")), QStringLiteral("2020..2100"));
        QCOMPARE(value(QStringLiteral("filter[status]")), QStringLiteral("current"));
        QCOMPARE(value(QStringLiteral("filter[subtype]")), QStringLiteral("movie"));
        QCOMPARE(value(QStringLiteral("sort")), QStringLiteral("ratingRank"));
        QVERIFY(found.value("hasNextPage").toBool());
        asked.clear();
        found = anime::kitsu::search({{"q", "one piece"}, {"page", 2}});
        QCOMPARE(value(QStringLiteral("filter[text]")), QStringLiteral("one piece"));
        QCOMPARE(value(QStringLiteral("page[offset]")), QStringLiteral("20"));
        // Text matches come by relevance: no order asked for.
        QVERIFY(value(QStringLiteral("sort")).isEmpty());

        const auto media = anime::kitsu::media(21);
        QCOMPARE(media.value("id").toInt(), 21);
        QVERIFY(!media.value("description").toString().isEmpty());

        // The schedule: ani.zip's air times and numbers within the range.
        const auto from = QDateTime::fromString(QStringLiteral("2026-10-19T00:00:00Z"), Qt::ISODate).toSecsSinceEpoch();
        const auto rows = anime::kitsu::airing(from, from + 7 * 86400);
        QVERIFY(!rows.isEmpty());
        for (const auto &row : rows) {
            const auto r = row.toObject();
            QVERIFY(r.value("airingAt").toDouble() >= from && r.value("airingAt").toDouble() < from + 7 * 86400);
            QCOMPARE(r.value("episode").toInt(), 6);
        }

        QCOMPARE(anime::kitsu::anilistIdForMal(52991), 154587);
        Http::setTestHandler({});
    }

    void contentLanguageOrder() {
        // The source-language setting wins, then the interface language.
        QCOMPARE(contentLanguage(QStringLiteral("es"), QStringLiteral("fr"), QStringLiteral("de_DE")), QStringLiteral("es"));
        QCOMPARE(contentLanguage(QString(), QStringLiteral("pt_BR"), QStringLiteral("de_DE")), QStringLiteral("pt"));
        // "system" is the system locale.
        QCOMPARE(contentLanguage(QString(), QStringLiteral("system"), QStringLiteral("id_ID")), QStringLiteral("id"));
        // Otherwise English.
        QCOMPARE(contentLanguage(QString(), QStringLiteral("system"), QStringLiteral("C")), QStringLiteral("en"));
        QCOMPARE(baseLanguage(QStringLiteral("es-la")), QStringLiteral("es"));
        QCOMPARE(baseLanguage(QStringLiteral("es_419")), QStringLiteral("es"));
    }

    void comickShelves() {
        const auto top = QJsonDocument::fromJson(fixture("catalogue-comick-top.json")).object();
        const auto lists = catalogue::comickTopLists(top, false);
        // The week's feed, without erotica.
        QCOMPARE(lists.value("trending").size(), 1);
        QCOMPARE(lists.value("trending|7").size(), 1);
        QVERIFY(lists.value("trending|30").isEmpty());
        const auto &first = lists.value("trending")[0];
        QCOMPARE(first.catalog, QStringLiteral("comick"));
        QCOMPARE(first.id, QStringLiteral("aa11"));
        QCOMPARE(first.type, QStringLiteral("Manhwa"));
        QCOMPARE(first.status, QStringLiteral("Ongoing"));
        QCOMPARE(first.lastChapter, QStringLiteral("120"));
        QCOMPARE(first.titles, (QStringList{QStringLiteral("First Title"), QString::fromUtf8("Primer Título")}));
        QCOMPARE(first.cover, QStringLiteral("https://meo.comick.pictures/cover-one-m.jpg"));
        QCOMPARE(first.ids.value("comick").toString(), QStringLiteral("aa11"));
        QCOMPARE(catalogue::comickTopLists(top, true).value("trending").size(), 2);
        // Only a month's list: the shelf is that one.
        QCOMPARE(lists.value("newfollow").size(), 1);
        QCOMPARE(lists.value("newfollow")[0].id, QStringLiteral("ff66"));
        // This season's anime adaptations come inside {year, season, data}.
        QCOMPARE(lists.value("season").size(), 1);
        QCOMPARE(lists.value("season")[0].lastChapter, QStringLiteral("88"));
        QCOMPARE(lists.value("completed")[0].status, QStringLiteral("Completed"));
        QVERIFY(lists.value("newest").isEmpty());
        const auto ranked = catalogue::comickEntry(top.value("rank").toArray()[0].toObject());
        QCOMPARE(ranked.lastChapter, QStringLiteral("12.5"));
        QCOMPARE(ranked.status, QStringLiteral("Hiatus"));

        // The shelves in their order, searched ones among /top's; empty
        // ones are left out, and each has its whole list.
        const QHash<QString, QList<catalogue::Entry>> searched{{QStringLiteral("ongoing"), {ranked, first}},
                                                              {QStringLiteral("manga"), {ranked}}};
        const auto shelves = catalogue::comickShelves(lists, searched, 1);
        QStringList keys;
        for (const auto &s : shelves)
            keys.append(s.key);
        QCOMPARE(keys, (QStringList{"trending", "ongoing", "newfollow", "manga", "season", "completed"}));
        QCOMPARE(shelves[1].entries.size(), 1);
        QVERIFY(shelves[0].more && shelves[0].periods);
        QVERIFY(shelves[1].more && !shelves[1].periods);

        // Searched shelves page on and on; /top's are not searches.
        QCOMPARE(catalogue::comickSearchedKeys().first(), QStringLiteral("ongoing"));
        QVERIFY(!catalogue::comickSearchedKeys().contains("trending"));
        const auto q = catalogue::comickListQuery(QStringLiteral("ongoing"), 3, 30, false);
        QCOMPARE(q.queryItemValue("sort"), QStringLiteral("follow"));
        QCOMPARE(q.queryItemValue("status"), QStringLiteral("1"));
        QCOMPARE(q.queryItemValue("page"), QStringLiteral("3"));
        QCOMPARE(q.queryItemValue("limit"), QStringLiteral("30"));
        QCOMPARE(q.queryItemValue("excluded_content_rating"), QStringLiteral("erotica"));
        QVERIFY(!catalogue::comickListQuery(QStringLiteral("ongoing"), 1, 30, true).hasQueryItem("excluded_content_rating"));
        QCOMPARE(catalogue::comickListQuery(QStringLiteral("manhwa"), 1, 30, false).queryItemValue("country"), QStringLiteral("kr"));
        QVERIFY(catalogue::comickListQuery(QStringLiteral("trending"), 1, 30, false).isEmpty());
    }

    void anilistEntries() {
        const auto media = QJsonDocument::fromJson(fixture("catalogue-anilist.json")).object().value("media").toArray();
        const auto comic = catalogue::anilistEntry(media[0].toObject());
        QCOMPARE(comic.media, QStringLiteral("comic"));
        QCOMPARE(comic.title, QStringLiteral("Test English"));
        QCOMPARE(comic.titles.size(), 4);
        QCOMPARE(comic.type, QStringLiteral("Manhwa"));
        QCOMPARE(comic.rating, QStringLiteral("7.9"));
        QCOMPARE(comic.ids.value("anilist").toInt(), 1001);
        QCOMPARE(comic.ids.value("mal").toInt(), 2001);
        const auto novel = catalogue::anilistEntry(media[1].toObject());
        QCOMPARE(novel.media, QStringLiteral("novel"));
        QCOMPARE(novel.type, QStringLiteral("Novel"));
        QCOMPARE(novel.title, QStringLiteral("Shousetsu"));
        QCOMPARE(novel.lastChapter, QStringLiteral("300"));
        QVERIFY(novel.adult);
        QVERIFY(!novel.ids.contains("mal"));
        QVERIFY(novel.rating.isEmpty());
    }

    void malEntries() {
        const auto rows = QJsonDocument::fromJson(fixture("catalogue-mal.json")).object().value("data").toArray();
        const auto comic = catalogue::malEntry(rows[0].toObject().value("node").toObject());
        QCOMPARE(comic.media, QStringLiteral("comic"));
        QCOMPARE(comic.type, QStringLiteral("Manhwa"));
        QCOMPARE(comic.cover, QStringLiteral("https://img.test/m/3001l.jpg"));
        QCOMPARE(comic.year, 2021);
        QCOMPARE(comic.rating, QStringLiteral("9.1"));
        QCOMPARE(comic.titles.size(), 4);
        QVERIFY(!comic.adult);
        const auto novel = catalogue::malEntry(rows[1].toObject().value("node").toObject());
        QCOMPARE(novel.media, QStringLiteral("novel"));
        QCOMPARE(novel.type, QStringLiteral("Light novel"));
        QCOMPARE(novel.status, QStringLiteral("Ongoing"));
        QVERIFY(novel.lastChapter.isEmpty());
        QVERIFY(novel.adult);
    }

    void entryRoundTrip() {
        const auto media = QJsonDocument::fromJson(fixture("catalogue-anilist.json")).object().value("media").toArray();
        const auto entry = catalogue::anilistEntry(media[0].toObject());
        const auto back = catalogue::Entry::fromMap(entry.toMap());
        QCOMPARE(back.catalog, entry.catalog);
        QCOMPARE(back.id, entry.id);
        QCOMPARE(back.titles, entry.titles);
        QCOMPARE(back.ids, entry.ids);
        // Comick entries saved before 2.0 carry only "hid".
        const auto old = catalogue::Entry::fromMap({{"catalog", "comick"}, {"hid", "xy"}, {"title", "T"}});
        QCOMPARE(old.id, QStringLiteral("xy"));
        QCOMPARE(old.titles, QStringList{QStringLiteral("T")});
    }

    void catalogueChoice() {
        catalogue::setMalClientId(QString());
        QVERIFY(!catalogue::forMedia(QStringLiteral("none"), QStringLiteral("comic")));
        QCOMPARE(catalogue::forMedia(QStringLiteral("comick"), QStringLiteral("comic"))->key(), QStringLiteral("comick"));
        // Comick has no novels: AniList's catalogue stands in.
        QCOMPARE(catalogue::forMedia(QStringLiteral("comick"), QStringLiteral("novel"))->key(), QStringLiteral("anilist"));
        // MyAnimeList needs the client id.
        QVERIFY(!catalogue::byKey(QStringLiteral("mal")));
        catalogue::setMalClientId(QStringLiteral("test-id"));
        QCOMPARE(catalogue::forMedia(QStringLiteral("mal"), QStringLiteral("novel"))->key(), QStringLiteral("mal"));
        catalogue::setMalClientId(QString());
        // Before 2.0 the catalogue was switched by comickCatalog.
        QCOMPARE(catalogue::chosen(QStringLiteral("comic")), QStringLiteral("comick"));
        Database::setPref(QStringLiteral("setting.comickCatalog"), false);
        QCOMPARE(catalogue::chosen(QStringLiteral("comic")), QStringLiteral("none"));
        // Each media has its own choice.
        Database::setPref(QStringLiteral("setting.catalogComic"), QStringLiteral("mal"));
        QCOMPARE(catalogue::chosen(QStringLiteral("comic")), QStringLiteral("mal"));
        QCOMPARE(catalogue::chosen(QStringLiteral("novel")), QStringLiteral("anilist"));
        Database::setPref(QStringLiteral("setting.catalogNovel"), QStringLiteral("none"));
        QCOMPARE(catalogue::chosen(QStringLiteral("novel")), QStringLiteral("none"));
    }

    void readFromPrefersContentLanguage() {
        Database::setPref(QStringLiteral("setting.sourceLanguages"), QStringLiteral("es"));
        const QList<SourcePtr> sources{
            std::make_shared<FakeSource>(QStringLiteral("one"), QStringLiteral("en"), QStringList{QStringLiteral("The Title")}),
            std::make_shared<FakeSource>(QStringLiteral("two"), QStringLiteral("es"), QStringList{QString::fromUtf8("El Título")}),
            std::make_shared<FakeSource>(QStringLiteral("three"), QStringLiteral("es-la"), QStringList{QStringLiteral("Other")}),
            // Listed but taken down: left out.
            std::make_shared<FakeSource>(QStringLiteral("four"), QStringLiteral("es"), QStringList{QStringLiteral("The Title")}, false)};
        const auto matches = resolve(sources, {QStringLiteral("The Title"), QStringLiteral("El Titulo")});
        QCOMPARE(matches.size(), 2);
        // Spanish first, though its source comes second; found by the
        // accent-free second name.
        QCOMPARE(matches[0].toMap().value("source").toString(), QStringLiteral("two"));
        QVERIFY(matches[0].toMap().value("preferred").toBool());
        QCOMPARE(matches[1].toMap().value("source").toString(), QStringLiteral("one"));
        QVERIFY(!matches[1].toMap().value("preferred").toBool());
        Database::setPref(QStringLiteral("setting.sourceLanguages"), QString());
    }

    // Sources are asked at the same time, so twenty slow sources take about
    // as long as one.
    void readFromFansOut() {
        QList<SourcePtr> sources;
        for (int i = 0; i < 20; ++i)
            sources.append(std::make_shared<FakeSource>(QStringLiteral("s%1").arg(i), QStringLiteral("en"),
                                                        QStringList{QStringLiteral("Slow")}, true, 150));
        QElapsedTimer timer;
        timer.start();
        const auto matches = resolve(sources, {QStringLiteral("Slow")});
        QCOMPARE(matches.size(), 20);
        QVERIFY2(timer.elapsed() < 1200, qPrintable(QStringLiteral("took %1 ms").arg(timer.elapsed())));
    }

    void mangadexTakedowns() {
        comics::SourceConfig config;
        config.key = QStringLiteral("mangadex");
        config.base = QStringLiteral("https://mangadex.org");
        comics::MangaDexSource source(config);
        const auto rows = QJsonDocument::fromJson(fixture("mangadex-feed-taken-down.json")).object().value("data").toArray();
        QSet<QString> seen;
        int takenDown = 0;
        const auto chapters = source.feedChapters(rows, seen, &takenDown);
        QCOMPARE(takenDown, 2);
        QCOMPARE(chapters.size(), 1);
        QCOMPARE(chapters[0].number, QStringLiteral("3"));
        QCOMPARE(chapters[0].title, QStringLiteral("Chapter 3: Fin"));
        // A feed of nothing but taken-down chapters reads as none.
        QJsonArray only{rows[0], rows[1]};
        seen.clear();
        takenDown = 0;
        QVERIFY(source.feedChapters(only, seen, &takenDown).isEmpty());
        QCOMPARE(takenDown, 2);
    }
};

QTEST_GUILESS_MAIN(CatalogueTests)
#include "tst_catalogue.moc"
