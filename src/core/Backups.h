#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace sc::backups {

// Libraries from other apps' backups, as Importer brings them in.

// A chapter or episode part-way: how far (0-1; small when the backup only
// knows the page) and where, as the reader or player keeps it.
struct Part {
    double percent = 0;
    QVariantMap position;
};

// An episode as an Aniyomi backup lists it: the extension's own link,
// its name and number (-1 when it has none).
struct Episode {
    QString url;
    QString name;
    double number = -1;
};

// One series from elsewhere.
struct Item {
    QString media; // comic, novel or anime
    QStringList titles;
    QString cover;
    // A SCANCORE list, or "custom:<name>" for one of the backup's own.
    QString category = QStringLiteral("reading");
    // The other lists it was in (one list per series here).
    QStringList otherCategories;
    QString sourceName; // Mihon's name for its source
    QString url;        // Mihon's address on that source (often a path)
    quint64 sourceId = 0; // Mihon's id for its source (an extension's)
    // The extension (or LNReader plugin) it came from, as SCANCORE keys it
    // ("mihon:<id>", "aniyomi:<id>", "lnreader:<id>"): restored there
    // exactly when it is installed.
    QString extensionKey;
    // The extension's own links of the chapters or episodes done, bookmarked,
    // and part-way.
    QSet<QString> doneLinks;
    QSet<QString> bookmarkLinks;
    QHash<QString, Part> parts;
    // When each was last read or watched (history), seconds.
    QHash<QString, qint64> readAt;
    // Anime: every episode the backup lists, so one whose extension is
    // installed comes back without asking its site.
    QList<Episode> episodes;
    int anilistId = 0;
    int malId = 0;
    // Other trackers SCANCORE has: {"kitsu", "mangaupdates", "shikimori",
    // "bangumi"} -> their id.
    QVariantMap trackerIds;
    QString comickHid;
    // Chapters or episodes done: the numbers when known (Mihon), else a count.
    QList<double> numbers;
    int count = 0;
    qint64 updatedAt = 0;
    qint64 addedAt = 0; // seconds; 0 when unknown
    // Its chapter list settings (sort, onlyUnread, onlyBookmarked,
    // hiddenScanlators) and reader ones (readerMode, readerDirection).
    QVariantMap settings;
    QVariantMap reader;
    QJsonObject anime; // AniList media, for anime
    // TachiyomiSY / Komikku: info edited there (title, author, cover…),
    // which SCANCORE does not keep; the edited title is matched on too.
    bool customInfo = false;
    // TachiyomiSY / Komikku: a merged entry, brought in as its main part.
    bool merged = false;

    QString title() const { return titles.value(0); }
};

// SCANCORE's list for a category name from another app ("Reading",
// "Plan to watch", "On hold"…); empty when it says nothing about it.
QString categoryNamed(const QString &name);

// An extension repository a backup names.
struct Repository {
    QString url; // its index
    QString name;
    QString signingKey;
};

struct MihonBackup {
    QList<Item> items;
    // The backup's own lists (not SCANCORE's built-in ones), in its order,
    // even those no series is in.
    QStringList lists;
    // Lists the backup hides (Komikku): SCANCORE ids, or "custom:<name>".
    QStringList hiddenLists;
    QList<Repository> repositories;
    // Sources with their own settings in the backup (105, "source_<id>"):
    // ones set up in that app, whether or not a series here is on them.
    QList<quint64> configuredSources;
    // What TachiyomiSY and Komikku keep that SCANCORE has no place for:
    // saved searches and feeds, and entries with edited info.
    int savedSearches = 0;
    int customInfo = 0;
    int merged = 0;
};

// Mihon's Backup, and Aniyomi's (the same with anime): manga (1),
// categories (2), sources (101), source settings (105), extension
// repositories (106); anime (3, or
// 501 in older Aniyomi backups), their categories (4 or 502) and sources
// (103 or 503). Entries outside the library are left out. TachiyomiSY and
// Komikku backups are the same with their own fields: merged entries
// (source 6969) come in as their main part, an entry backed up twice (a
// merged entry's parts are written again) once, hidden categories (900)
// hidden. Throws on a damaged file.
MihonBackup parseMihonBackup(const QByteArray &data);
QList<Item> parseMihon(const QByteArray &data);

// LNReader's backup: a zip with NovelAndChapters/<id>.json (the novel, its
// plugin and path, and its chapters with unread, bookmark and progress) and
// Category.json ([{name, novelIds}]). Older backups put it all inside a
// data.zip. Throws when it is not one.
QList<Item> parseLNReader(const QByteArray &zip);

} // namespace sc::backups
