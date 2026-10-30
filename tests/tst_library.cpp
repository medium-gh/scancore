// The library's own logic on a scratch database: moving series between
// sources.

#include "core/Backups.h"
#include "core/Database.h"
#include "core/Http.h"
#include "core/ListOrder.h"
#include "core/Migration.h"
#include "core/Proto.h"
#include "sources/local/Local.h"
#include "sources/local/MihonLayout.h"
#include "core/ThemeLook.h"
#include "core/UpdateRules.h"

#include <QBuffer>
#include <QTemporaryDir>
#include <QtCore/private/qzipwriter_p.h>
#include <QtTest>

using namespace sc;

class LibraryTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    static void addUnits(const QString &seriesId, const QStringList &numbers) {
        int index = 0;
        for (const auto &n : numbers)
            Database::exec(QStringLiteral("INSERT INTO units(series_id, unit_key, idx, number, title, url) VALUES(?, ?, ?, ?, ?, ?)"),
                           {seriesId, seriesId + QLatin1Char('/') + n, index++, n, QStringLiteral("Chapter ") + n,
                            seriesId + QLatin1Char('/') + n});
    }
    static void read(const QString &seriesId, const QString &number, double percent, qint64 at) {
        Database::exec(QStringLiteral("INSERT INTO progress(series_id, unit_key, unit_number, percent, completed, updated_at) "
                                      "VALUES(?, ?, ?, ?, ?, ?)"),
                       {seriesId, seriesId + QLatin1Char('/') + number, number, percent, percent >= 1 ? 1 : 0, at});
    }
    static QVariantMap progress(const QString &seriesId, const QString &number) {
        return Database::one(QStringLiteral("SELECT percent, completed, updated_at FROM progress WHERE series_id = ? AND unit_number = ?"),
                             {seriesId, number});
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        Database::open(m_dir.filePath(QStringLiteral("library.db")));
    }

    // Library lists in the user's order (issue #9): built-in and own lists
    // together, new ones after their neighbour, hidden ones skipped when
    // moving in the library.
    void listOrder() {
        const QStringList builtin{"reading", "planned", "paused", "completed", "dropped"};
        QCOMPARE(lists::ordered(builtin + QStringList{"c-a"}, {}), builtin + QStringList{"c-a"});
        // Stored by an older version: no order at all, or before c-b existed.
        const QStringList stored{"c-a", "completed", "reading", "gone"};
        QCOMPARE(lists::ordered(builtin + QStringList{"c-a", "c-b"}, stored),
                 (QStringList{"c-a", "c-b", "completed", "dropped", "reading", "planned", "paused"}));
        const QStringList order{"reading", "planned", "paused", "completed", "dropped", "c-a"};
        QCOMPARE(lists::moved(order, {}, "c-a", -1, false), (QStringList{"reading", "planned", "paused", "completed", "c-a", "dropped"}));
        QCOMPARE(lists::moved(order, {"dropped", "completed"}, "c-a", -1, true),
                 (QStringList{"reading", "planned", "c-a", "paused", "completed", "dropped"}));
        QCOMPARE(lists::moved(order, {}, "reading", -1, false), order);
        QCOMPARE(lists::moved(order, {"planned", "paused", "completed", "dropped"}, "reading", 1, true),
                 (QStringList{"planned", "paused", "completed", "dropped", "c-a", "reading"}));
    }

    // Mihon's downloads folder: sources, series, chapter folders and CBZ,
    // matched to the chapters of a library entry by hash, name or number.
    void mihonDownloads() {
        QTemporaryDir tree;
        QVERIFY(tree.isValid());
        const auto touch = [&](const QString &relative) {
            const auto path = tree.filePath(relative);
            QVERIFY(QDir().mkpath(QFileInfo(path).path()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        };
        const QString url1 = QStringLiteral("/chapter/one"), url2 = QStringLiteral("/chapter/two");
        const auto hash1 = local::mihon::hashesFor(url1).first();
        touch(QStringLiteral("downloads/Comix (EN)/Solo Leveling/Group_Chapter 1_%1/001.jpg").arg(hash1));
        touch(QStringLiteral("downloads/Comix (EN)/Solo Leveling/Chapter 2.cbz"));
        touch(QStringLiteral("downloads/Comix (EN)/Solo Leveling/Group_Chapter 3_ffffff.cbz"));
        touch(QStringLiteral("downloads/Comix (EN)/Solo Leveling/Chapter 9_tmp/001.jpg"));
        QVERIFY(local::mihon::isDownloads(tree.filePath(QStringLiteral("downloads"))));
        QVERIFY(local::mihon::isDownloads(tree.filePath(QStringLiteral("downloads/Comix (EN)"))));
        QVERIFY(!local::mihon::isDownloads(tree.filePath(QStringLiteral("downloads/Comix (EN)/Solo Leveling"))));
        const auto sources = local::mihon::sources(tree.filePath(QStringLiteral("downloads")));
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources[0].name, QStringLiteral("Comix"));
        QCOMPARE(sources[0].lang, QStringLiteral("en"));
        const auto series = local::mihon::series(sources[0]);
        QCOMPARE(series.size(), 1);
        auto chapters = local::mihon::chapters(series[0].path);
        QCOMPARE(chapters.size(), 3); // the unfinished "_tmp" one is left out
        std::sort(chapters.begin(), chapters.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
        const QList<local::mihon::Unit> units{{url1, QStringLiteral("Chapter 1 renamed"), QStringLiteral("Group"), QStringLiteral("1")},
                                              {url2, QStringLiteral("Chapter 2"), {}, QStringLiteral("2")},
                                              {QStringLiteral("/chapter/three"), QStringLiteral("Chapter 3"), QStringLiteral("Group"), QStringLiteral("3")}};
        QList<int> matched;
        for (const auto &c : chapters)
            matched.append(local::mihon::matchChapters({c}, units).first());
        // "Chapter 2.cbz" (no scanlator), "Group_Chapter 1_<hash>" (by hash,
        // though the title changed), "Group_Chapter 3_ffffff" (a hash that is
        // not the address's, by name).
        for (int i = 0; i < chapters.size(); ++i) {
            const auto name = QFileInfo(chapters[i].path).fileName();
            QCOMPARE(matched[i], name.contains(QLatin1String("Chapter 1")) ? 0 : name.contains(QLatin1String("Chapter 2")) ? 1 : 2);
        }
        QCOMPARE(local::mihon::chapterTitle(QStringLiteral("Group_Chapter 1_%1").arg(hash1)), QStringLiteral("Chapter 1"));
        QCOMPARE(local::mihon::chapterTitle(QStringLiteral("Chapter 2.cbz")), QStringLiteral("Chapter 2"));
        QCOMPARE(local::mihon::normalized(QStringLiteral("Re:Zero — Starting Life!")), local::mihon::normalized(QStringLiteral("Re_Zero  Starting Life")));
    }

    // Release radar: the next chapter from a series' rhythm.
    void releaseRhythm() {
        constexpr qint64 day = 86400;
        const qint64 now = 1'800'000'000;
        QCOMPARE(*updates::releaseDate(QStringLiteral("2026-10-21T12:00:00Z")), qint64(1792584000));
        QVERIFY(updates::releaseDate(QStringLiteral("2026-10-21")).has_value());
        QVERIFY(!updates::releaseDate(QStringLiteral("3 days ago")));
        // Weekly, last out two days ago: due in five days.
        QList<qint64> weekly;
        for (int i = 0; i < 8; ++i)
            weekly.append(now - 2 * day - i * 7 * day);
        auto r = updates::rhythm(weekly, now);
        QVERIFY(r.known);
        QCOMPARE(qRound(r.gapDays), 7);
        QCOMPARE(r.next, now + 5 * day);
        QVERIFY(!r.late);
        // One late chapter does not move the median; two chapters on one day
        // are one release.
        weekly[3] -= 3 * day;
        weekly.append(weekly[0] - 3600);
        QCOMPARE(qRound(updates::rhythm(weekly, now).gapDays), 7);
        // Due yesterday: late. Three weeks overdue: stopped keeping time.
        QList<qint64> due;
        for (int i = 0; i < 5; ++i)
            due.append(now - 8 * day - i * 7 * day);
        QVERIFY(updates::rhythm(due, now).late);
        QList<qint64> stopped;
        for (int i = 0; i < 5; ++i)
            stopped.append(now - 21 * day - i * 7 * day);
        QVERIFY(!updates::rhythm(stopped, now).known);
        // Too few releases, or monthly-plus gaps: no guess.
        QVERIFY(!updates::rhythm({now - day, now - 8 * day}, now).known);
        QVERIFY(!updates::rhythm({now - day, now - 61 * day, now - 121 * day}, now).known);
    }

    // Library health: series saved twice share a title or tracker entry.
    void duplicateGroups() {
        const auto groups = updates::groupsSharingKeys({{"c:t:solo"}, {"c:t:other"}, {"c:t:sololeveling", "c:a:1"},
                                                        {"c:a:1"}, {"c:t:solo"}, {}});
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups[0], QList<int>({0, 4}));
        QCOMPARE(groups[1], QList<int>({2, 3}));
        // A large library stays fast (one pass, no pairwise comparison).
        QList<QStringList> many;
        for (int i = 0; i < 50000; ++i)
            many.append({QStringLiteral("c:t:%1").arg(i % 40000), QStringLiteral("c:a:%1").arg(i)});
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(updates::groupsSharingKeys(many).size(), 10000);
        QVERIFY2(timer.elapsed() < 500, qPrintable(QStringLiteral("%1 ms").arg(timer.elapsed())));
    }

    void chapterValues() {
        QCOMPARE(*migration::chapterValue(QStringLiteral("12")), 12.0);
        QCOMPARE(*migration::chapterValue(QStringLiteral(" 12.5 ")), 12.5);
        QCOMPARE(*migration::chapterValue(QStringLiteral("0.003")), 0.003);
        QCOMPARE(*migration::chapterValue(QStringLiteral("Chapter 7")), 7.0);
        // As discussions number them (sc::chapterNumber): a prologue is 0,
        // and a volume's number is not the chapter's.
        QCOMPARE(*migration::chapterValue(QStringLiteral("Prologue")), 0.0);
        QCOMPARE(*migration::chapterValue(QStringLiteral("Vol.3 Ch.12")), 12.0);
        QCOMPARE(*migration::chapterValue(QString(), QStringLiteral("Chapter 45: The End")), 45.0);
        QVERIFY(!migration::chapterValue(QStringLiteral("Extra 2")));
    }

    void moveCarriesProgressAndEntry() {
        const auto from = QStringLiteral("comic:old:1");
        const auto to = QStringLiteral("comic:new:1");
        addUnits(from, {"1", "2", "3", "4", "5"});
        Database::exec(QStringLiteral("INSERT INTO library(series_id, category, favourite, added_at, known_units, changed_at) "
                                      "VALUES(?, 'paused', 1, 100, 5, 100)"),
                       {from});
        read(from, "1", 1, 1000);
        read(from, "2", 1, 2000);
        read(from, "3", 1, 3000);
        read(from, "4", 0.4, 4000);
        // The new source splits chapter 2 and has one more.
        addUnits(to, {"1", "2", "2.5", "3", "4", "5", "6"});
        Database::exec(QStringLiteral("INSERT INTO bookmarks(series_id, unit_key, created_at) VALUES(?, ?, 1)"), {from, from + "/5"});
        Database::exec(QStringLiteral("INSERT INTO series_settings(series_id, value) VALUES(?, '{\"sort\":\"asc\"}')"), {from});

        const auto result = migration::move(from, to);
        // 1, 2, 2.5 and 3: everything up to the furthest chapter read.
        QCOMPARE(result.read, 4);
        QCOMPARE(result.nextKey, to + QStringLiteral("/4"));
        QCOMPARE(progress(to, "2.5").value("completed").toInt(), 1);
        QCOMPARE(progress(to, "3").value("updated_at").toLongLong(), 3000);
        // Chapter 4 keeps how far it got, unfinished, and stays the latest.
        QCOMPARE(progress(to, "4").value("percent").toDouble(), 0.4);
        QCOMPARE(progress(to, "4").value("completed").toInt(), 0);
        QVERIFY(progress(to, "5").isEmpty());
        // Its bookmark and settings came along.
        QVERIFY(Database::scalar(QStringLiteral("SELECT 1 FROM bookmarks WHERE series_id = ? AND unit_key = ?"), {to, to + "/5"}).isValid());
        QCOMPARE(Database::scalar(QStringLiteral("SELECT value FROM series_settings WHERE series_id = ?"), {to}).toString(),
                 QStringLiteral("{\"sort\":\"asc\"}"));

        const auto entry = Database::one(QStringLiteral("SELECT category, favourite, added_at, known_units FROM library WHERE series_id = ?"), {to});
        QCOMPARE(entry.value("category").toString(), QStringLiteral("paused"));
        QCOMPARE(entry.value("favourite").toInt(), 1);
        QCOMPARE(entry.value("added_at").toLongLong(), 100);
        QCOMPARE(entry.value("known_units").toInt(), 7);
        // The old one is gone (and remembered as removed, for sync), with
        // its history.
        QVERIFY(!Database::scalar(QStringLiteral("SELECT 1 FROM library WHERE series_id = ?"), {from}).isValid());
        QVERIFY(Database::scalar(QStringLiteral("SELECT 1 FROM library_removed WHERE series_id = ?"), {from}).isValid());
        QCOMPARE(Database::scalar(QStringLiteral("SELECT COUNT(*) FROM progress WHERE series_id = ?"), {from}).toInt(), 0);
        // The move is remembered, so sync does not bring the old progress
        // back (it stood in Continue for the moved series).
        const auto moved = migration::movedAt(from, migration::moves());
        QVERIFY(moved > 0);
        QCOMPARE(migration::movedAt(to, migration::moves()), 0);

        // Another device made the same move: its old progress from before
        // goes, anything read there since stays.
        Database::exec(QStringLiteral("DELETE FROM prefs WHERE key = 'library.moved'"));
        read(from, "1", 1, moved - 10);
        read(from, "6", 0.5, moved + 10);
        QCOMPARE(migration::applyMoves({{from, QVariantList{to, moved}}}), 1);
        QCOMPARE(Database::scalar(QStringLiteral("SELECT COUNT(*) FROM progress WHERE series_id = ?"), {from}).toInt(), 1);
        QCOMPARE(migration::applyMoves({{from, QVariantList{to, moved}}}), 0);
        Database::exec(QStringLiteral("DELETE FROM progress WHERE series_id = ?"), {from});

        // Moving back: that move no longer stands.
        migration::move(to, from);
        QCOMPARE(migration::movedAt(from, migration::moves()), 0);
        QVERIFY(migration::movedAt(to, migration::moves()) > 0);
    }

    void copyKeepsTheOld() {
        const auto from = QStringLiteral("novel:old:2");
        const auto to = QStringLiteral("novel:new:2");
        addUnits(from, {"1", "2"});
        addUnits(to, {"1", "2"});
        Database::exec(QStringLiteral("INSERT INTO library(series_id, category, added_at, changed_at) VALUES(?, 'reading', 5, 5)"), {from});
        read(from, "1", 1, 10);
        migration::Options options;
        options.keepOld = true;
        QCOMPARE(migration::move(from, to, options).read, 1);
        QVERIFY(Database::scalar(QStringLiteral("SELECT 1 FROM library WHERE series_id = ?"), {from}).isValid());
        QVERIFY(Database::scalar(QStringLiteral("SELECT 1 FROM library WHERE series_id = ?"), {to}).isValid());
        QCOMPARE(Database::scalar(QStringLiteral("SELECT COUNT(*) FROM progress WHERE series_id = ?"), {from}).toInt(), 1);
    }

    void updateRules() {
        using namespace updates;
        QVERIFY(finishedStatus(QStringLiteral("Completed")));
        QVERIFY(finishedStatus(QStringLiteral("Finalizado")));
        QVERIFY(finishedStatus(QString::fromUtf8("완결")));
        QVERIFY(!finishedStatus(QStringLiteral("Ongoing")));
        QVERIFY(!finishedStatus(QString::fromUtf8("연재중")));
        QVERIFY(!finishedStatus(QString()));

        const Rules rules;
        // Dropped series are not checked unless asked for.
        QVERIFY(shouldCheck({QStringLiteral("reading"), QStringLiteral("Ongoing"), 10, 10, true}, rules));
        QVERIFY(!shouldCheck({QStringLiteral("dropped"), QStringLiteral("Ongoing"), 10, 10, true}, rules));
        QVERIFY(!shouldCheck({QStringLiteral("reading"), QStringLiteral("Completed"), 10, 10, true}, rules));
        Rules strict;
        strict.skipUnread = true;
        strict.skipNotStarted = true;
        QVERIFY(!shouldCheck({QStringLiteral("reading"), QString(), 10, 4, true}, strict));
        QVERIFY(!shouldCheck({QStringLiteral("planned"), QString(), 10, 10, false}, strict));
        QVERIFY(shouldCheck({QString(), QString(), 10, 10, true}, strict));
    }

    // A theme file's look keeps known settings with sensible values and
    // known pages only.
    void themeLook() {
        // Built in code: a raw string literal here makes AutoMoc skip the file.
        const QJsonObject in{
            {"mode", "dark"}, {"accentName", "violet"},
            {"settings", QJsonObject{{"navStyle", "dock"}, {"roundness", "blobby"}, {"textScale", 9},
                                     {"readerRailCurrent", "#000"}, {"readerRailRead", "red"}, {"libraryCheckHours", 1},
                                     {"readerProgressWidth", 6.2}}},
            {"pages", QJsonObject{
                {"home", QJsonObject{{"order", QJsonArray{"continue", "trending", "../x", "continue"}},
                                     {"hidden", QJsonObject{{"popular", true}, {"x", false}}},
                                     {"look", QJsonObject{{"continue", QJsonObject{{"style", "grid"}, {"count", 999},
                                                                                  {"subtitles", false}, {"evil", 1}}}}}}},
                {"settings", QJsonObject{{"order", QJsonArray{"a"}}}}}}};
        const auto out = theme::sanitizeLook(in);
        QCOMPARE(out.value("mode").toString(), QStringLiteral("dark"));
        QCOMPARE(out.value("accentName").toString(), QStringLiteral("violet"));
        const auto settings = out.value("settings").toObject();
        QCOMPARE(settings.value("navStyle").toString(), QStringLiteral("dock"));
        QVERIFY(!settings.contains("roundness"));
        QCOMPARE(settings.value("textScale").toDouble(), 1.4);
        QCOMPARE(settings.value("readerRailCurrent").toString(), QStringLiteral("#000000"));
        QVERIFY(!settings.contains("readerRailRead"));
        QVERIFY(!settings.contains("libraryCheckHours"));
        QCOMPARE(settings.value("readerProgressWidth").toInt(), 6);
        const auto pages = out.value("pages").toObject();
        QCOMPARE(pages.keys(), QStringList{QStringLiteral("home")});
        const auto home = pages.value("home").toObject();
        QCOMPARE(home.value("order").toArray().size(), 2);
        QCOMPARE(home.value("hidden").toObject().keys(), QStringList{QStringLiteral("popular")});
        const auto look = home.value("look").toObject().value("continue").toObject();
        QCOMPARE(look.value("count").toInt(), 200);
        QVERIFY(!look.contains("evil"));
        QVERIFY(theme::sanitizeLook(QJsonObject{{"mode", "neon"}}).isEmpty());
    }

    // Aniyomi's backup: anime in the current layout (3, 4, 103) and the
    // older one (501-503), categories by name, and episodes seen, part-way
    // and bookmarked by their links.
    void aniyomiBackup() {
        const auto episode = [](const QString &link, float number, bool seen, qint64 at = 0, qint64 total = 0, bool bookmark = false) {
            ProtoWriter e;
            e.text(1, link);
            e.boolean(4, seen);
            e.boolean(5, bookmark);
            e.varint(6, quint64(at));
            e.fixed32(9, number);
            e.varint(16, quint64(total));
            return e;
        };
        const auto anime = [&](quint64 source, const QString &url, const QString &title, quint64 category) {
            ProtoWriter a;
            a.varint(1, source);
            a.text(2, url);
            a.text(3, title);
            a.message(16, episode(QStringLiteral("/ep/1"), 1, true));
            a.message(16, episode(QStringLiteral("/ep/2"), 2, false, 600000, 1440000, true));
            a.varint(17, category);
            ProtoWriter tracking;
            tracking.varint(1, 2);
            tracking.varint(100, 21);
            a.message(18, tracking);
            return a;
        };
        const auto category = [](const QString &name, quint64 order) {
            ProtoWriter c;
            c.text(1, name);
            c.varint(2, order);
            return c;
        };
        const auto source = [](const QString &name, quint64 id) {
            ProtoWriter c;
            c.text(1, name);
            c.varint(2, id);
            return c;
        };
        ProtoWriter current;
        current.message(4, category(QStringLiteral("Plan to watch"), 3));
        current.message(3, anime(777, QStringLiteral("/anime/one"), QStringLiteral("One Piece"), 3));
        current.message(103, source(QStringLiteral("Some Source"), 777));
        // Aniyomi's repositories: anime (107) and manga (108); its 106 is
        // extension packages, with no address.
        const auto repo = [](const QString &url, const QString &name) {
            ProtoWriter r;
            r.text(1, url);
            r.text(2, name);
            return r;
        };
        current.message(106, repo(QStringLiteral("eu.kanade.tachiyomi.animeextension.en.one"), QStringLiteral("apk bytes")));
        current.message(107, repo(QStringLiteral("https://example.org/anime/index.min.json"), QStringLiteral("Anime repo")));
        current.message(108, repo(QStringLiteral("https://example.org/manga"), QStringLiteral("Manga repo")));
        const auto repos = backups::parseMihonBackup(current.data()).repositories;
        QCOMPARE(repos.size(), 2);
        QCOMPARE(repos[0].url, QStringLiteral("https://example.org/anime/index.min.json"));
        QCOMPARE(repos[1].name, QStringLiteral("Manga repo"));
        ProtoWriter legacy;
        legacy.message(502, category(QStringLiteral("On hold"), 1));
        legacy.message(501, anime(778, QStringLiteral("/anime/two"), QStringLiteral("Two"), 1));
        legacy.message(503, source(QStringLiteral("Old Source"), 778));

        for (const auto &[data, key, list] : {std::tuple{current.data(), QStringLiteral("aniyomi:777"), QStringLiteral("planned")},
                                              std::tuple{legacy.data(), QStringLiteral("aniyomi:778"), QStringLiteral("paused")}}) {
            const auto items = backups::parseMihon(data);
            QCOMPARE(items.size(), 1);
            const auto &item = items.first();
            QCOMPARE(item.media, QStringLiteral("anime"));
            QCOMPARE(item.extensionKey, key);
            QCOMPARE(item.category, list);
            QCOMPARE(item.anilistId, 21);
            QCOMPARE(item.doneLinks, QSet<QString>{QStringLiteral("/ep/1")});
            QCOMPARE(item.bookmarkLinks, QSet<QString>{QStringLiteral("/ep/2")});
            QCOMPARE(item.parts.keys(), QStringList{QStringLiteral("/ep/2")});
            QCOMPARE(item.parts.value(QStringLiteral("/ep/2")).position.value("seconds").toDouble(), 600.0);
            QCOMPARE(item.numbers, QList<double>{1.0});
        }
        QCOMPARE(backups::categoryNamed(QStringLiteral("Reading")), QStringLiteral("reading"));
        QCOMPARE(backups::categoryNamed(QStringLiteral("Completed")), QStringLiteral("completed"));
        QCOMPARE(backups::categoryNamed(QStringLiteral("Favourites")), QString());
    }

    // Mihon's backup as Mihon writes it today: lists in its order (empty
    // ones too, Default being none), a series in two lists, date added,
    // history, a chapter part-read, chapter and reader settings, hidden
    // groups, other trackers and the extension repositories.
    void mihonBackup() {
        const auto chapter = [](const QString &link, float number, bool read, qint64 page = 0) {
            ProtoWriter c;
            c.text(1, link);
            c.boolean(4, read);
            c.varint(6, quint64(page));
            c.fixed32(9, number);
            return c;
        };
        const auto category = [](const QString &name, quint64 order) {
            ProtoWriter c;
            c.text(1, name);
            c.varint(2, order);
            return c;
        };
        ProtoWriter manga;
        manga.varint(1, 2499283573021220255ull);
        manga.text(2, QStringLiteral("/manga/x"));
        manga.text(3, QStringLiteral("Some Manga"));
        manga.varint(13, 1700000000000ull);
        manga.message(16, chapter(QStringLiteral("/c/1"), 1, true));
        manga.message(16, chapter(QStringLiteral("/c/2"), 2, false, 7));
        manga.message(16, chapter(QStringLiteral("/c/3"), 3, false));
        // In "Favourites" (order 3) and "Weekly" (order 2): Weekly comes first.
        manga.varint(17, 3);
        manga.varint(17, 2);
        ProtoWriter kitsu;
        kitsu.varint(1, 3);
        kitsu.varint(100, 4321);
        manga.message(18, kitsu);
        ProtoWriter updates;
        updates.varint(1, 7);
        updates.varint(100, 55555);
        manga.message(18, updates);
        manga.varint(101, 0x1 | 0x2);
        manga.varint(103, 2);
        ProtoWriter history;
        history.text(1, QStringLiteral("/c/2"));
        history.varint(2, 1710000000000ull);
        manga.message(104, history);
        manga.text(108, QStringLiteral("Bad Group"));
        ProtoWriter repo;
        repo.text(1, QStringLiteral("https://raw.githubusercontent.com/keiyoushi/extensions/repo/index.min.json"));
        repo.text(2, QStringLiteral("Keiyoushi"));
        repo.text(5, QStringLiteral("9add655a78e96c4ec7a53ef89dccb557cb5d767489fac5e785d671a5a75d4da2"));
        ProtoWriter backup;
        backup.message(1, manga);
        backup.message(2, category(QStringLiteral("Favourites"), 3));
        backup.message(2, category(QStringLiteral("Weekly"), 2));
        backup.message(2, category(QStringLiteral("Plan to read"), 1));
        backup.message(2, category(QStringLiteral("Empty one"), 4));
        backup.message(106, repo);
        // Settings of two sources set up there (one with none of the
        // library on it), and an app setting that is not a source's.
        for (const auto *key : {"source_2499283573021220255", "source_7", "source_7", "source_x"}) {
            ProtoWriter prefs;
            prefs.text(1, QString::fromLatin1(key));
            backup.message(105, prefs);
        }

        const auto parsed = backups::parseMihonBackup(backup.data());
        QCOMPARE(parsed.configuredSources, (QList<quint64>{2499283573021220255ull, 7}));
        QCOMPARE(parsed.lists, (QStringList{QStringLiteral("Weekly"), QStringLiteral("Favourites"), QStringLiteral("Empty one")}));
        QCOMPARE(parsed.repositories.size(), 1);
        QCOMPARE(parsed.repositories[0].name, QStringLiteral("Keiyoushi"));
        QCOMPARE(parsed.repositories[0].signingKey.size(), 64);
        QCOMPARE(parsed.items.size(), 1);
        const auto &item = parsed.items[0];
        QCOMPARE(item.extensionKey, QStringLiteral("mihon:2499283573021220255"));
        QCOMPARE(item.category, QStringLiteral("custom:Weekly"));
        QCOMPARE(item.otherCategories, QStringList{QStringLiteral("custom:Favourites")});
        QCOMPARE(item.addedAt, qint64(1700000000));
        QCOMPARE(item.doneLinks, QSet<QString>{QStringLiteral("/c/1")});
        QCOMPARE(item.parts.keys(), QStringList{QStringLiteral("/c/2")});
        QCOMPARE(item.parts.value(QStringLiteral("/c/2")).position.value("panel").toInt(), 7);
        QCOMPARE(item.readAt.value(QStringLiteral("/c/2")), qint64(1710000000));
        QCOMPARE(item.trackerIds.value("kitsu").toLongLong(), 4321);
        QCOMPARE(item.trackerIds.value("mangaupdates").toLongLong(), 55555);
        QCOMPARE(item.settings.value("sort").toString(), QStringLiteral("asc"));
        QCOMPARE(item.settings.value("onlyUnread").toBool(), true);
        QCOMPARE(item.settings.value("hiddenScanlators").toStringList(), QStringList{QStringLiteral("Bad Group")});
        QCOMPARE(item.reader.value("readerMode").toString(), QStringLiteral("paged"));
        QCOMPARE(item.reader.value("readerDirection").toString(), QStringLiteral("rtl"));
    }

    // LNReader's zip: the novel on its plugin, chapters read by link, the
    // one part-way, bookmarks, the category; novels outside the library and
    // local ones left out.
    // A real Komikku 1.14.1 backup made on the emulator (no user data):
    // a merged entry (source 6969) and its parts written again after it,
    // a hidden category, custom info, a saved search and feed, trackers
    // and history.
    void komikkuBackup() {
        QFile file(QStringLiteral(SCANCORE_SOURCE_DIR "/tests/fixtures/komikku-1.14.1.tachibk"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto backup = backups::parseMihonBackup(local::gunzip(file.readAll()));
        // Plain, Weekly, Finished, Hidden List; the merged entry is Plain's
        // MangaDex entry, so it joins it; the merged child is not a favourite.
        QStringList titles;
        for (const auto &item : backup.items)
            titles.append(item.title());
        QCOMPARE(titles, (QStringList{"Plain Series", "Weekly Series", "Finished Series", "Hidden List Series"}));
        QCOMPARE(backup.lists, (QStringList{"Weekly", "Secret", "Empty one"}));
        QCOMPARE(backup.hiddenLists, QStringList{"custom:Secret"});
        QCOMPARE(backup.savedSearches, 1);
        QCOMPARE(backup.merged, 1);
        QCOMPARE(backup.customInfo, 2);
        const auto &plain = backup.items[0];
        QCOMPARE(plain.extensionKey, QStringLiteral("mihon:2499283573021220255"));
        QVERIFY(plain.merged);
        QVERIFY(plain.titles.contains(QStringLiteral("Merged Thing")));
        // The merged entry's list, as Plain itself had none.
        QCOMPARE(plain.category, QStringLiteral("custom:Weekly"));
        QCOMPARE(plain.anilistId, 30002);
        QCOMPARE(plain.trackerIds.value("mangaupdates").toLongLong(), 123456);
        QVERIFY(plain.doneLinks.contains(QStringLiteral("/chapter/c1")));
        QVERIFY(plain.bookmarkLinks.contains(QStringLiteral("/chapter/c2")));
        QVERIFY(plain.parts.contains(QStringLiteral("/chapter/c2")));
        QCOMPARE(plain.readAt.value(QStringLiteral("/chapter/c1")), qint64(1791999900));
        const auto &weekly = backup.items[1];
        QCOMPARE(weekly.titles, (QStringList{"Weekly Series", "My Custom Title"}));
        QCOMPARE(weekly.malId, 4242);
        QCOMPARE(weekly.settings.value("hiddenScanlators").toStringList(), QStringList{"Bad Group"});
        QCOMPARE(weekly.reader.value("readerMode").toString(), QStringLiteral("strip"));
        // In Weekly (order 1) and Completed (2): the first by order.
        QCOMPARE(backup.items[2].category, QStringLiteral("custom:Weekly"));
        QCOMPARE(backup.items[2].otherCategories, QStringList{"completed"});
        QCOMPARE(backup.items[3].category, QStringLiteral("custom:Secret"));
    }

    void lnreaderBackup() {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        {
            QZipWriter zip(&buffer);
            zip.addFile(QStringLiteral("Version.json"), QByteArray("{\"version\":\"2.0.0\"}"));
            const QJsonObject novel{{"id", 5}, {"path", "novel/lord"}, {"pluginId", "royalroad"}, {"name", "Lord of Mysteries"},
                                    {"cover", "https://example.com/c.jpg"}, {"inLibrary", 1}, {"isLocal", 0},
                                    {"chapters", QJsonArray{QJsonObject{{"path", "c/1"}, {"unread", 0}, {"chapterNumber", 1}},
                                                            QJsonObject{{"path", "c/2"}, {"unread", 1}, {"progress", 40}, {"bookmark", 1}},
                                                            QJsonObject{{"path", "c/3"}, {"unread", 1}}}}};
            zip.addFile(QStringLiteral("NovelAndChapters/5.json"), QJsonDocument(novel).toJson());
            const QJsonObject away{{"id", 6}, {"path", "x"}, {"pluginId", "royalroad"}, {"name", "Not saved"}, {"inLibrary", 0}};
            zip.addFile(QStringLiteral("NovelAndChapters/6.json"), QJsonDocument(away).toJson());
            zip.addFile(QStringLiteral("Category.json"),
                        QJsonDocument(QJsonArray{QJsonObject{{"id", 2}, {"name", "Completed"}, {"novelIds", QJsonArray{5}}}}).toJson());
        }
        const auto items = backups::parseLNReader(buffer.data());
        QCOMPARE(items.size(), 1);
        const auto &item = items.first();
        QCOMPARE(item.media, QStringLiteral("novel"));
        QCOMPARE(item.extensionKey, QStringLiteral("lnreader:royalroad"));
        QCOMPARE(item.url, QStringLiteral("novel/lord"));
        QCOMPARE(item.category, QStringLiteral("completed"));
        QCOMPARE(item.doneLinks, QSet<QString>{QStringLiteral("c/1")});
        QCOMPARE(item.parts.keys(), QStringList{QStringLiteral("c/2")});
        QCOMPARE(item.parts.value(QStringLiteral("c/2")).percent, 0.4);
        QCOMPARE(item.bookmarkLinks, QSet<QString>{QStringLiteral("c/2")});
        QVERIFY_THROWS_EXCEPTION(Error, backups::parseLNReader(QByteArray("not a zip")));
    }

    void refusesNothingToMoveTo() {
        QVERIFY_THROWS_EXCEPTION(Error, migration::move(QStringLiteral("comic:a:1"), QStringLiteral("comic:a:1")));
        QVERIFY_THROWS_EXCEPTION(Error, migration::move(QStringLiteral("comic:a:1"), QStringLiteral("comic:empty:1")));
    }
};

QTEST_GUILESS_MAIN(LibraryTests)
#include "tst_library.moc"
