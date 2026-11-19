#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

namespace sc::catalogue {

// One title in a tracker's catalogue. Comics and novels opened from it keep
// the entry (series.extra), so tracking and comments know the title whichever
// source it is read on.
struct Entry {
    QString catalog;       // "comick", "anilist", "mal"
    QString id;            // the title's id in that catalogue
    QString media;         // "comic" or "novel"
    QString title;
    QStringList titles;    // every name the catalogue knows, title first
    QString cover;
    QString type;          // Manga, Manhwa, Manhua, Novel, Light novel
    QString status;        // Ongoing, Completed, Cancelled, Hiatus
    int year = 0;
    QString lastChapter;
    QString rating;
    QString description;
    QString slug;          // Comick's page name
    QString url;           // the title's page on the catalogue's site
    bool adult = false;
    // The same title on the trackers: {"comick": hid, "anilist": id, "mal": id}.
    QVariantMap ids;

    QVariantMap toMap() const;
    static Entry fromMap(const QVariantMap &map);
};

struct Shelf {
    // Shown under a translated title chosen by key (Router.shelfTitle):
    // trending, ongoing, newfollow, popular, top, newest, manhwa, manga,
    // manhua, season, finished, completed, lightnovels.
    QString key;
    QList<Entry> entries;
    // The whole list is there a page at a time (Catalogue::list, "See all").
    bool more = false;
    // That list is by period ("7", "30" or "90" days; the shelf is the week's).
    bool periods = false;
};

// One page of a shelf's whole list.
struct ListPage {
    QList<Entry> entries;
    bool hasNext = false;
};

// Browsing and "search everything" through a tracker's catalogue, chosen per
// media in Settings → Connections. Every call blocks; run on a worker.
class Catalogue {
public:
    virtual ~Catalogue() = default;

    virtual QString key() const = 0;
    virtual QString label() const = 0;
    // "comic" or "novel".
    virtual bool supports(const QString &media) const = 0;
    virtual QList<Shelf> shelves(const QString &media, bool mature) = 0;
    // A shelf's whole list (`key` as in Shelf), `page` from 1. `period`:
    // "7", "30" or "90" for a shelf with periods, else ignored. Throws for a
    // shelf without one (Shelf::more false).
    virtual ListPage list(const QString &media, const QString &key, int page, bool mature, const QString &period);
    // `filters`: the catalogue's own search filters (Comick's, see
    // comickQuery); the others take none. An empty query browses by them.
    virtual QList<Entry> search(const QString &media, const QString &query, bool mature,
                                const QVariantMap &filters) = 0;
    // Every name of a title; shelf entries can carry only one.
    virtual QStringList names(const Entry &entry) { return entry.titles; }
};

// The catalogue the user browses a media with (settings "catalogComic" and
// "catalogNovel"): comick, anilist, mal or none. Safe on any thread.
QString chosen(const QString &media);

// "comick", "anilist" or "mal"; null for an unknown key or a catalogue this
// build cannot use (MyAnimeList without its client id).
std::shared_ptr<Catalogue> byKey(const QString &key);
// Where AniList says a title can be read (its "external links": official
// sites such as WEBTOON editions and Naver Webtoon). Cached for a week;
// empty when AniList is unreachable.
QStringList readingLinks(int anilistId);
// The catalogue for a media: the chosen one when it has that media, else
// AniList. Null when the user chose none.
std::shared_ptr<Catalogue> forMedia(const QString &chosen, const QString &media);
// MyAnimeList's public reads need SCANCORE's client id (set by main()).
void setMalClientId(const QString &id);
bool malAvailable();

// Comick's search query for a title and the filter sheet's choices
// (CatalogueFilters.qml): {sort, status, completed, showall, time, from, to,
// minimum, maximum, rating, country: [], demographic: [], genres: {slug: 1
// include | 2 exclude}, tags: {slug: {state, title}}, content: {rating:
// 1 | 2}, violence: {rating: 1 | 2}}.
QUrlQuery comickQuery(const QString &query, const QVariantMap &filters);

// Parsers, public for the offline tests.
Entry comickEntry(const QJsonObject &comic);
Entry anilistEntry(const QJsonObject &media);
Entry malEntry(const QJsonObject &node);
// The lists in Comick's /top answer by shelf key, the lists by period as
// "key|7", "key|30" and "key|90" (the key alone is the week's); without
// erotica unless mature.
QHash<QString, QList<Entry>> comickTopLists(const QJsonObject &top, bool mature);
// Comick's shelves in the order they show, `size` titles each: lists from
// /top (`top`, as comickTopLists gives them) and searches (`searched`, by
// key). Keys with nothing are left out.
QList<Shelf> comickShelves(const QHash<QString, QList<Entry>> &top, const QHash<QString, QList<Entry>> &searched, int size = 24);
// The search behind one of Comick's searched shelves ("ongoing": the most
// followed of those still coming out, …); empty for a key that is not one.
QUrlQuery comickListQuery(const QString &key, int page, int limit, bool mature);
// Comick's shelf keys whose lists are searches, in order.
QStringList comickSearchedKeys();

} // namespace sc::catalogue
