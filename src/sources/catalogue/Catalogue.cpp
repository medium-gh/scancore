#include "Catalogue.h"

#include "core/Database.h"
#include "core/Http.h"
#include "core/Parallel.h"

#include <algorithm>

#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QUrlQuery>

namespace sc::catalogue {

QVariantMap Entry::toMap() const {
    return {{"catalog", catalog},
            {"catalogId", id},
            // Comick's id under the name the comic pages already use.
            {"hid", ids.value(QStringLiteral("comick")).toString()},
            {"media", media},
            {"title", title},
            {"titles", titles},
            {"cover", cover},
            {"type", type},
            {"status", status},
            {"year", year},
            {"lastChapter", lastChapter},
            {"rating", rating},
            {"description", description},
            {"slug", slug},
            {"catalogUrl", url},
            {"adult", adult},
            {"ids", ids},
            {"genres", QVariantList()}};
}

Entry Entry::fromMap(const QVariantMap &map) {
    Entry e;
    e.catalog = map.value("catalog").toString();
    e.id = map.value("catalogId").toString();
    if (e.id.isEmpty() && e.catalog == QLatin1String("comick"))
        e.id = map.value("hid").toString();
    e.media = map.value("media").toString();
    e.title = map.value("title").toString();
    e.titles = map.value("titles").toStringList();
    if (e.titles.isEmpty() && !e.title.isEmpty())
        e.titles = {e.title};
    e.cover = map.value("cover").toString();
    e.type = map.value("type").toString();
    e.status = map.value("status").toString();
    e.year = map.value("year").toInt();
    e.lastChapter = map.value("lastChapter").toString();
    e.rating = map.value("rating").toString();
    e.description = map.value("description").toString();
    e.slug = map.value("slug").toString();
    e.url = map.value("catalogUrl").toString();
    e.adult = map.value("adult").toBool();
    e.ids = map.value("ids").toMap();
    return e;
}

namespace {

QMutex g_malMutex;
QString g_malClientId;

QString rating(double value) { return value > 0 ? QString::number(value, 'f', 1) : QString(); }

QStringList cleanTitles(QStringList titles) {
    titles.removeAll(QString());
    titles.removeDuplicates();
    return titles;
}

// A page of a shelf's whole list ("See all").
constexpr int kListPage = 30;

// ---------------------------------------------------------------- Comick

const Headers kComickHeaders{{"Accept", "application/json"}, {"Referer", "https://comick.dev/"}};

// Comick's shelves in the order they show, and where each one's titles come
// from: a list on its home page (/top, some by period) or a search, which
// goes on page after page.
struct ComickShelf {
    const char *key;
    const char *top;     // the list in /top, or null
    bool periods;        // that list is by period: {"7": [...], "30": [...], "90": [...]}
    const char *search;  // the search, or null
};
const ComickShelf kComickShelves[] = {
    {"trending", "trending", true, nullptr},
    {"ongoing", nullptr, false, "sort=follow&status=1"},
    {"newfollow", "topFollowNewComics", true, nullptr},
    {"popular", nullptr, false, "sort=view"},
    {"top", nullptr, false, "sort=rating"},
    {"newest", "news", false, nullptr},
    {"manhwa", nullptr, false, "sort=follow&country=kr"},
    {"manga", nullptr, false, "sort=follow&country=jp"},
    {"manhua", nullptr, false, "sort=follow&country=cn"},
    {"season", "comicsByCurrentSeason", false, nullptr},
    {"finished", nullptr, false, "sort=follow&status=2"},
    {"completed", "completions", false, nullptr},
};
const char *const kPeriods[] = {"7", "30", "90"};

const ComickShelf *comickShelf(const QString &key) {
    for (const auto &shelf : kComickShelves)
        if (key == QLatin1String(shelf.key))
            return &shelf;
    return nullptr;
}

QByteArray encodeLists(const QHash<QString, QList<Entry>> &lists) {
    QVariantMap map;
    for (auto it = lists.cbegin(); it != lists.cend(); ++it) {
        QVariantList entries;
        for (const auto &e : it.value())
            entries.append(e.toMap());
        map.insert(it.key(), entries);
    }
    return QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact);
}

QHash<QString, QList<Entry>> decodeLists(const QByteArray &bytes) {
    QHash<QString, QList<Entry>> out;
    const auto map = QJsonDocument::fromJson(bytes).toVariant().toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        QList<Entry> entries;
        for (const auto &e : it.value().toList())
            entries.append(Entry::fromMap(e.toMap()));
        out.insert(it.key(), entries);
    }
    return out;
}

class Comick : public Catalogue {
public:
    QString key() const override { return QStringLiteral("comick"); }
    QString label() const override { return QStringLiteral("Comick"); }
    bool supports(const QString &media) const override { return media == QLatin1String("comic"); }

    // /top and the first page of every searched shelf, side by side; a
    // shelf whose search fails is left out.
    QList<Shelf> shelves(const QString &, bool mature) override {
        using Lists = QHash<QString, QList<Entry>>;
        QList<std::function<Lists()>> jobs{[mature] { return topLists(mature); }};
        for (const auto &key : comickSearchedKeys())
            jobs.append([key, mature] { return Lists{{key, searched(key, 1, 24, mature).entries}}; });
        const auto results = parallel(jobs);
        Lists top, found;
        QString error;
        int status = 0;
        for (int i = 0; i < results.size(); ++i) {
            if (!results[i].ok()) {
                error = results[i].error;
                status = results[i].status;
            } else if (i == 0) {
                top = *results[i].value;
            } else {
                found.insert(*results[i].value);
            }
        }
        if (top.isEmpty() && found.isEmpty())
            throw Error(error, status);
        return comickShelves(top, found);
    }

    ListPage list(const QString &media, const QString &key, int page, bool mature, const QString &period) override {
        const auto *shelf = comickShelf(key);
        if (!shelf)
            return Catalogue::list(media, key, page, mature, period);
        if (shelf->search)
            return searched(key, page, kListPage, mature);
        const auto lists = topLists(mature);
        const auto byPeriod = key + QLatin1Char('|') + period;
        const auto all = lists.value(shelf->periods && lists.contains(byPeriod) ? byPeriod : key);
        ListPage out;
        out.entries = all.mid((page - 1) * kListPage, kListPage);
        out.hasNext = all.size() > page * kListPage;
        return out;
    }

    QList<Entry> search(const QString &, const QString &query, bool, const QVariantMap &filters) override {
        QUrl url(QStringLiteral("https://api.comick.dev/v1.0/search/"));
        url.setQuery(comickQuery(query, filters));
        const auto response = Http::get(url, kComickHeaders);
        if (!response.ok())
            throw Error(QStringLiteral("Comick's catalogue is unavailable right now (%1).").arg(response.status), response.status);
        QList<Entry> out;
        for (const auto &value : response.json().array())
            if (value.toObject().value("media_type").toString() != QLatin1String("anime"))
                out.append(comickEntry(value.toObject()));
        return out;
    }

    QStringList names(const Entry &entry) override {
        if (entry.titles.size() > 1 || entry.id.isEmpty())
            return entry.titles;
        const auto details = Http::get(QUrl(QStringLiteral("https://api.comick.dev/v1.0/comic/%1/").arg(entry.id)), kComickHeaders);
        if (!details.ok())
            return entry.titles;
        const auto comic = details.json().object().value("comic").toObject();
        QStringList titles = entry.titles;
        titles.append(comic.value("title").toString());
        for (const auto &t : comic.value("md_titles").toArray())
            titles.append(t.toObject().value("title").toString());
        return cleanTitles(titles);
    }

private:
    // The lists in /top (3.7 MB of JSON), read at most once each half hour;
    // the last copy when Comick is unreachable.
    static QHash<QString, QList<Entry>> topLists(bool mature) {
        const auto cacheKey = QStringLiteral("comick-top:%1").arg(mature ? "all" : "sfw");
        if (const auto hit = Database::cacheGet(cacheKey))
            return decodeLists(*hit);
        const auto response = Http::get(QUrl(QStringLiteral("https://api.comick.dev/top?accept_mature_content=%1")
                                                 .arg(mature ? "true" : "false")),
                                        kComickHeaders);
        if (!response.ok()) {
            if (const auto stale = Database::cacheGet(cacheKey, true))
                return decodeLists(*stale);
            throw Error(QStringLiteral("Comick's catalogue is unavailable right now (%1).").arg(response.status), response.status);
        }
        const auto lists = comickTopLists(response.json().object(), mature);
        Database::cachePut(cacheKey, encodeLists(lists), 1800);
        return lists;
    }

    static ListPage searched(const QString &key, int page, int limit, bool mature) {
        QUrl url(QStringLiteral("https://api.comick.dev/v1.0/search/"));
        url.setQuery(comickListQuery(key, page, limit, mature));
        const auto response = Http::get(url, kComickHeaders);
        if (!response.ok())
            throw Error(QStringLiteral("Comick's catalogue is unavailable right now (%1).").arg(response.status), response.status);
        const auto array = response.json().array();
        ListPage out;
        for (const auto &value : array) {
            if (value.toObject().value("media_type").toString() == QLatin1String("anime"))
                continue;
            auto e = comickEntry(value.toObject());
            if (!e.adult || mature)
                out.entries.append(e);
        }
        out.hasNext = array.size() >= limit;
        return out;
    }
};

// ---------------------------------------------------------------- AniList

// Shelves leave out descriptions (a quarter of a shelf's size); the series
// page has its source's own.
const char *kAniListCardFields = R"(
  id idMal format status(version: 2) chapters countryOfOrigin startDate { year }
  averageScore isAdult siteUrl
  title { romaji english native } synonyms coverImage { large }
)";
const char *kAniListFields = R"(
  id idMal format status(version: 2) chapters countryOfOrigin startDate { year }
  averageScore isAdult siteUrl description(asHtml: false)
  title { romaji english native } synonyms coverImage { large }
)";

QJsonObject anilist(const QString &query, const QJsonObject &variables) {
    const auto response = Http::postJson(QUrl(QStringLiteral("https://graphql.anilist.co/")),
                                         QJsonDocument(QJsonObject{{"query", query}, {"variables", variables}}).toJson(QJsonDocument::Compact),
                                         {{"Accept", "application/json"}});
    if (response.status == 429)
        throw Error(QStringLiteral("AniList is busy. Try again in a minute."), 429);
    const auto data = QJsonDocument::fromJson(response.body).object().value("data").toObject();
    if (!response.ok() || data.isEmpty())
        throw Error(QStringLiteral("AniList's catalogue is unavailable right now (%1).").arg(response.status), response.status);
    return data;
}

class AniList : public Catalogue {
public:
    QString key() const override { return QStringLiteral("anilist"); }
    QString label() const override { return QStringLiteral("AniList"); }
    bool supports(const QString &media) const override {
        return media == QLatin1String("comic") || media == QLatin1String("novel");
    }

    // Every shelf in one request.
    QList<Shelf> shelves(const QString &media, bool mature) override {
        QString pages;
        for (const auto &[key, args] : kShelves)
            pages += QStringLiteral("%1: Page(perPage: 24) { media(type: MANGA, %2, %3, isAdult: $adult) { ...f } }\n")
                         .arg(key, args, formatFilter(media));
        const auto data = anilist(QStringLiteral("query ($adult: Boolean) {\n%1}\nfragment f on Media { %2 }")
                                      .arg(pages, QString::fromLatin1(kAniListCardFields)),
                                  adultVariable(mature));
        QList<Shelf> out;
        for (const auto &shelf : kShelves) {
            Shelf s{shelf.first, {}, true};
            for (const auto &m : data.value(shelf.first).toObject().value("media").toArray())
                s.entries.append(entry(m.toObject(), media));
            out.append(s);
        }
        return out;
    }

    ListPage list(const QString &media, const QString &key, int page, bool mature, const QString &period) override {
        const auto it = std::find_if(kShelves.cbegin(), kShelves.cend(), [&key](const auto &s) { return s.first == key; });
        if (it == kShelves.cend())
            return Catalogue::list(media, key, page, mature, period);
        auto variables = adultVariable(mature);
        variables.insert("page", page);
        const auto data = anilist(QStringLiteral("query ($adult: Boolean, $page: Int) { Page(page: $page, perPage: %1) { pageInfo { hasNextPage } "
                                                 "media(type: MANGA, %2, %3, isAdult: $adult) { %4 } } }")
                                      .arg(kListPage)
                                      .arg(it->second, formatFilter(media), QString::fromLatin1(kAniListCardFields)),
                                  variables);
        const auto p = data.value("Page").toObject();
        ListPage out;
        for (const auto &m : p.value("media").toArray())
            out.entries.append(entry(m.toObject(), media));
        out.hasNext = p.value("pageInfo").toObject().value("hasNextPage").toBool();
        return out;
    }

    QList<Entry> search(const QString &media, const QString &query, bool mature, const QVariantMap &) override {
        auto variables = adultVariable(mature);
        variables.insert("search", query.left(100));
        const auto data = anilist(QStringLiteral("query ($search: String, $adult: Boolean) { Page(perPage: 40) { media(type: MANGA, "
                                                 "search: $search, sort: SEARCH_MATCH, %1, isAdult: $adult) { %2 } } }")
                                      .arg(formatFilter(media), QString::fromLatin1(kAniListFields)),
                                  variables);
        QList<Entry> out;
        for (const auto &m : data.value("Page").toObject().value("media").toArray())
            out.append(entry(m.toObject(), media));
        return out;
    }

private:
    static inline const QList<QPair<QString, QString>> kShelves{
        {"trending", "sort: TRENDING_DESC"},
        {"popular", "sort: POPULARITY_DESC"},
        {"top", "sort: SCORE_DESC"},
        {"newest", "sort: START_DATE_DESC, status: RELEASING, popularity_greater: 1500"}};
    // Novels are AniList "manga" in the NOVEL format; comics are the rest.
    static QString formatFilter(const QString &media) {
        return media == QLatin1String("novel") ? QStringLiteral("format: NOVEL") : QStringLiteral("format_not_in: [NOVEL]");
    }
    // Adult titles only when asked for; null means either.
    static QJsonObject adultVariable(bool mature) {
        return mature ? QJsonObject{{"adult", QJsonValue::Null}} : QJsonObject{{"adult", false}};
    }
    static Entry entry(const QJsonObject &m, const QString &media) {
        auto e = anilistEntry(m);
        e.media = media;
        return e;
    }
};

// ---------------------------------------------------------------- MyAnimeList

const char *kMalFields = "alternative_titles,media_type,num_chapters,start_date,status,mean,synopsis,main_picture,nsfw";

bool malNovel(const QString &type) { return type == QLatin1String("novel") || type == QLatin1String("light_novel"); }

class Mal : public Catalogue {
public:
    QString key() const override { return QStringLiteral("mal"); }
    QString label() const override { return QStringLiteral("MyAnimeList"); }
    bool supports(const QString &media) const override {
        return media == QLatin1String("comic") || media == QLatin1String("novel");
    }

    QList<Shelf> shelves(const QString &media, bool mature) override {
        const auto wanted = rankings(media);
        QList<std::function<QList<Entry>()>> jobs;
        for (const auto &[key, ranking] : wanted)
            jobs.append([this, ranking, media, mature] {
                const auto rows = call(QStringLiteral("/manga/ranking"),
                                       {{"ranking_type", ranking}, {"limit", "30"}, {"fields", kMalFields}}, mature);
                return entries(rows, media);
            });
        const auto results = parallel(jobs);
        QList<Shelf> out;
        QString error;
        for (int i = 0; i < results.size(); ++i) {
            if (results[i].ok())
                out.append({wanted[i].first, results[i].value->mid(0, 24), true});
            else
                error = results[i].error;
        }
        if (out.isEmpty())
            throw Error(error);
        return out;
    }

    ListPage list(const QString &media, const QString &key, int page, bool mature, const QString &period) override {
        const auto wanted = rankings(media);
        const auto it = std::find_if(wanted.cbegin(), wanted.cend(), [&key](const auto &r) { return r.first == key; });
        if (it == wanted.cend())
            return Catalogue::list(media, key, page, mature, period);
        ListPage out;
        out.entries = entries(call(QStringLiteral("/manga/ranking"),
                                   {{"ranking_type", it->second}, {"limit", QString::number(kListPage)},
                                    {"offset", QString::number((page - 1) * kListPage)}, {"fields", kMalFields}},
                                   mature, &out.hasNext),
                              media);
        return out;
    }

    QList<Entry> search(const QString &media, const QString &query, bool mature, const QVariantMap &) override {
        // MyAnimeList refuses queries shorter than three letters.
        if (query.size() < 3)
            return {};
        return entries(call(QStringLiteral("/manga"), {{"q", query.left(64)}, {"limit", "40"}, {"fields", kMalFields}}, mature), media);
    }

private:
    // Its shelves, and the rankings they are.
    static QList<QPair<QString, QString>> rankings(const QString &media) {
        if (media == QLatin1String("novel"))
            return {{"top", "novels"}, {"lightnovels", "lightnovels"}};
        return {{"top", "manga"}, {"popular", "bypopularity"}, {"manhwa", "manhwa"}, {"manhua", "manhua"}};
    }

    // `more`, when given, is set to whether a next page follows.
    static QJsonArray call(const QString &path, QList<QPair<QString, QString>> params, bool mature, bool *more = nullptr) {
        QString id;
        {
            QMutexLocker lock(&g_malMutex);
            id = g_malClientId;
        }
        if (mature)
            params.append({QStringLiteral("nsfw"), QStringLiteral("true")});
        QUrl url(QStringLiteral("https://api.myanimelist.net/v2") + path);
        QUrlQuery query;
        for (const auto &[k, v] : params)
            query.addQueryItem(k, QString::fromUtf8(QUrl::toPercentEncoding(v)));
        url.setQuery(query);
        const auto response = Http::get(url, {{"X-MAL-CLIENT-ID", id.toUtf8()}, {"Accept", "application/json"}});
        if (!response.ok())
            throw Error(QStringLiteral("MyAnimeList's catalogue is unavailable right now (%1).").arg(response.status), response.status);
        const auto body = response.json().object();
        if (more)
            *more = body.value("paging").toObject().contains("next");
        return body.value("data").toArray();
    }

    // Rankings and search mix comics and novels; keep the asked media.
    static QList<Entry> entries(const QJsonArray &rows, const QString &media) {
        QList<Entry> out;
        for (const auto &row : rows) {
            auto e = malEntry(row.toObject().value("node").toObject());
            const bool novel = e.media == QLatin1String("novel");
            if (novel == (media == QLatin1String("novel")))
                out.append(e);
        }
        return out;
    }
};

} // namespace

ListPage Catalogue::list(const QString &, const QString &, int, bool, const QString &) {
    throw Error(QStringLiteral("%1 has no more of this list.").arg(label()));
}

QUrlQuery comickQuery(const QString &query, const QVariantMap &f) {
    QUrlQuery q;
    if (!query.isEmpty())
        q.addQueryItem(QStringLiteral("q"), query.left(100));
    q.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    q.addQueryItem(QStringLiteral("showall"), f.value("showall").toBool() ? QStringLiteral("true") : QStringLiteral("false"));
    // Comick's limits per list.
    const auto states = [&f](const char *key, int wanted, int most) {
        QStringList out;
        const auto map = f.value(QLatin1String(key)).toMap();
        for (auto it = map.cbegin(); it != map.cend() && out.size() < most; ++it) {
            const auto v = it.value();
            const int state = v.typeId() == QMetaType::QVariantMap ? v.toMap().value("state").toInt() : v.toInt();
            if (state == wanted)
                out.append(it.key());
        }
        return out;
    };
    const auto add = [&q](const QString &name, const QStringList &values) {
        for (const auto &v : values)
            q.addQueryItem(name, v);
    };
    add(QStringLiteral("genres"), states("genres", 1, 10));
    add(QStringLiteral("excludes"), states("genres", 2, 40));
    add(QStringLiteral("tags"), states("tags", 1, 10));
    add(QStringLiteral("excluded-tags"), states("tags", 2, 30));
    add(QStringLiteral("content_rating"), states("content", 1, 4));
    add(QStringLiteral("excluded_content_rating"), states("content", 2, 4));
    add(QStringLiteral("violence_rating"), states("violence", 1, 2));
    add(QStringLiteral("excluded_violence_rating"), states("violence", 2, 2));
    add(QStringLiteral("country"), f.value("country").toStringList().mid(0, 10));
    add(QStringLiteral("demographic"), f.value("demographic").toStringList());
    for (const char *key : {"sort", "status", "time", "from", "to", "minimum", "maximum"}) {
        const auto value = f.value(QLatin1String(key)).toString();
        if (!value.isEmpty() && value != QLatin1String("0"))
            q.addQueryItem(QLatin1String(key), value);
    }
    if (const auto rating = f.value("rating").toDouble(); rating > 0)
        q.addQueryItem(QStringLiteral("minimum_rating"), QString::number(rating));
    if (f.value("completed").toBool())
        q.addQueryItem(QStringLiteral("completed"), QStringLiteral("true"));
    return q;
}

Entry comickEntry(const QJsonObject &c) {
    static const QHash<QString, QString> kinds{{"kr", "Manhwa"}, {"cn", "Manhua"}, {"jp", "Manga"}};
    static const QStringList statuses{QString(), QStringLiteral("Ongoing"), QStringLiteral("Completed"),
                                      QStringLiteral("Cancelled"), QStringLiteral("Hiatus")};
    Entry e;
    e.catalog = QStringLiteral("comick");
    e.id = c.value("hid").toString();
    e.media = QStringLiteral("comic");
    QStringList titles{c.value("title").toString()};
    for (const auto &t : c.value("md_titles").toArray())
        titles.append(t.toObject().value("title").toString());
    e.titles = cleanTitles(titles);
    e.title = e.titles.value(0);
    const auto covers = c.value("md_covers").toArray();
    auto key = covers.isEmpty() ? QString() : covers.first().toObject().value("b2key").toString();
    // The medium size ("-m", always JPEG); the original can be half a megabyte.
    if (const auto dot = key.lastIndexOf(QLatin1Char('.')); dot > 0)
        key = key.left(dot) + QStringLiteral("-m.jpg");
    if (!key.isEmpty())
        e.cover = QStringLiteral("https://meo.comick.pictures/") + key;
    e.type = kinds.value(c.value("country").toString());
    e.status = statuses.value(c.value("status").toInt());
    e.year = c.value("year").toInt();
    const auto last = c.value("last_chapter");
    e.lastChapter = last.isDouble() ? QString::number(last.toDouble()) : QString();
    e.rating = c.value("bayesian_rating").toString();
    e.description = c.value("desc").toString();
    e.slug = c.value("slug").toString();
    if (!e.slug.isEmpty())
        e.url = QStringLiteral("https://comick.dev/comic/") + e.slug;
    e.adult = c.value("content_rating").toString() == QLatin1String("erotica");
    if (!e.id.isEmpty())
        e.ids.insert(QStringLiteral("comick"), e.id);
    return e;
}

QHash<QString, QList<Entry>> comickTopLists(const QJsonObject &top, bool mature) {
    const auto entries = [mature](const QJsonArray &array) {
        QList<Entry> out;
        for (const auto &item : array) {
            auto e = comickEntry(item.toObject());
            if (!e.id.isEmpty() && (!e.adult || mature))
                out.append(e);
        }
        return out;
    };
    QHash<QString, QList<Entry>> out;
    for (const auto &shelf : kComickShelves) {
        if (!shelf.top)
            continue;
        const QString key = QLatin1String(shelf.key);
        const auto value = top.value(QLatin1String(shelf.top));
        if (!shelf.periods) {
            // This season's anime adaptations come as {year, season, data: [...]}.
            out.insert(key, entries(value.isObject() ? value.toObject().value("data").toArray() : value.toArray()));
            continue;
        }
        const auto byDays = value.toObject();
        for (const char *days : kPeriods)
            if (byDays.contains(QLatin1String(days)))
                out.insert(key + QLatin1Char('|') + QLatin1String(days), entries(byDays.value(QLatin1String(days)).toArray()));
        // The shelf: the week's, or the first period there is.
        for (const char *days : kPeriods) {
            const auto list = out.value(key + QLatin1Char('|') + QLatin1String(days));
            if (!list.isEmpty()) {
                out.insert(key, list);
                break;
            }
        }
    }
    return out;
}

QList<Shelf> comickShelves(const QHash<QString, QList<Entry>> &top, const QHash<QString, QList<Entry>> &searched, int size) {
    QList<Shelf> out;
    for (const auto &shelf : kComickShelves) {
        const auto entries = (shelf.top ? top : searched).value(QLatin1String(shelf.key));
        if (!entries.isEmpty())
            out.append({QLatin1String(shelf.key), entries.mid(0, size), true, shelf.periods});
    }
    return out;
}

QUrlQuery comickListQuery(const QString &key, int page, int limit, bool mature) {
    const auto *shelf = comickShelf(key);
    if (!shelf || !shelf->search)
        return {};
    QUrlQuery q(QString::fromLatin1(shelf->search));
    q.addQueryItem(QStringLiteral("page"), QString::number(page));
    q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    q.addQueryItem(QStringLiteral("showall"), QStringLiteral("false"));
    if (!mature)
        q.addQueryItem(QStringLiteral("excluded_content_rating"), QStringLiteral("erotica"));
    return q;
}

QStringList comickSearchedKeys() {
    QStringList out;
    for (const auto &shelf : kComickShelves)
        if (shelf.search)
            out.append(QLatin1String(shelf.key));
    return out;
}

Entry anilistEntry(const QJsonObject &m) {
    static const QHash<QString, QString> statuses{{"RELEASING", "Ongoing"}, {"FINISHED", "Completed"},
                                                  {"CANCELLED", "Cancelled"}, {"HIATUS", "Hiatus"},
                                                  {"NOT_YET_RELEASED", "Upcoming"}};
    static const QHash<QString, QString> kinds{{"KR", "Manhwa"}, {"CN", "Manhua"}, {"TW", "Manhua"}, {"JP", "Manga"}};
    Entry e;
    e.catalog = QStringLiteral("anilist");
    e.id = QString::number(m.value("id").toInt());
    const bool novel = m.value("format").toString() == QLatin1String("NOVEL");
    e.media = novel ? QStringLiteral("novel") : QStringLiteral("comic");
    const auto t = m.value("title").toObject();
    QStringList titles{t.value("english").toString(), t.value("romaji").toString(), t.value("native").toString()};
    for (const auto &s : m.value("synonyms").toArray())
        titles.append(s.toString());
    e.titles = cleanTitles(titles);
    e.title = e.titles.value(0);
    e.cover = m.value("coverImage").toObject().value("large").toString();
    e.type = novel ? QStringLiteral("Novel") : kinds.value(m.value("countryOfOrigin").toString(), QStringLiteral("Manga"));
    e.status = statuses.value(m.value("status").toString());
    e.year = m.value("startDate").toObject().value("year").toInt();
    if (const auto chapters = m.value("chapters").toInt(); chapters > 0)
        e.lastChapter = QString::number(chapters);
    e.rating = rating(m.value("averageScore").toDouble() / 10);
    e.description = m.value("description").toString();
    e.url = m.value("siteUrl").toString();
    e.adult = m.value("isAdult").toBool();
    e.ids.insert(QStringLiteral("anilist"), m.value("id").toInt());
    if (const auto mal = m.value("idMal").toInt(); mal > 0)
        e.ids.insert(QStringLiteral("mal"), mal);
    return e;
}

Entry malEntry(const QJsonObject &n) {
    static const QHash<QString, QString> statuses{{"currently_publishing", "Ongoing"}, {"finished", "Completed"},
                                                  {"discontinued", "Cancelled"}, {"on_hiatus", "Hiatus"},
                                                  {"not_yet_published", "Upcoming"}};
    static const QHash<QString, QString> kinds{{"manga", "Manga"}, {"manhwa", "Manhwa"}, {"manhua", "Manhua"},
                                               {"one_shot", "One-shot"}, {"doujinshi", "Doujinshi"},
                                               {"novel", "Novel"}, {"light_novel", "Light novel"}, {"oel", "OEL"}};
    Entry e;
    e.catalog = QStringLiteral("mal");
    e.id = QString::number(n.value("id").toInt());
    const auto type = n.value("media_type").toString();
    e.media = malNovel(type) ? QStringLiteral("novel") : QStringLiteral("comic");
    const auto alt = n.value("alternative_titles").toObject();
    QStringList titles{n.value("title").toString(), alt.value("en").toString(), alt.value("ja").toString()};
    for (const auto &s : alt.value("synonyms").toArray())
        titles.append(s.toString());
    e.titles = cleanTitles(titles);
    e.title = e.titles.value(0);
    const auto picture = n.value("main_picture").toObject();
    e.cover = picture.value("large").toString(picture.value("medium").toString());
    e.type = kinds.value(type, QStringLiteral("Manga"));
    e.status = statuses.value(n.value("status").toString());
    e.year = n.value("start_date").toString().left(4).toInt();
    if (const auto chapters = n.value("num_chapters").toInt(); chapters > 0)
        e.lastChapter = QString::number(chapters);
    e.rating = rating(n.value("mean").toDouble());
    e.description = n.value("synopsis").toString();
    e.url = QStringLiteral("https://myanimelist.net/manga/%1").arg(n.value("id").toInt());
    // MyAnimeList's "white" is safe for work; "gray" and "black" are not.
    e.adult = n.contains("nsfw") && n.value("nsfw").toString() != QLatin1String("white");
    e.ids.insert(QStringLiteral("mal"), n.value("id").toInt());
    return e;
}

QString chosen(const QString &media) {
    if (media == QLatin1String("novel"))
        return Database::pref(QStringLiteral("setting.catalogNovel"), QStringLiteral("anilist")).toString();
    const auto comic = Database::pref(QStringLiteral("setting.catalogComic")).toString();
    if (!comic.isEmpty())
        return comic;
    // Before 2.0 the only catalogue was Comick's, switched by comickCatalog.
    return Database::pref(QStringLiteral("setting.comickCatalog"), true).toBool() ? QStringLiteral("comick")
                                                                                 : QStringLiteral("none");
}

void setMalClientId(const QString &id) {
    QMutexLocker lock(&g_malMutex);
    g_malClientId = id;
}

bool malAvailable() {
    QMutexLocker lock(&g_malMutex);
    return !g_malClientId.isEmpty();
}

std::shared_ptr<Catalogue> byKey(const QString &key) {
    static const auto comick = std::make_shared<Comick>();
    static const auto anilistCatalogue = std::make_shared<AniList>();
    static const auto mal = std::make_shared<Mal>();
    if (key == QLatin1String("comick"))
        return comick;
    if (key == QLatin1String("anilist"))
        return anilistCatalogue;
    if (key == QLatin1String("mal") && malAvailable())
        return mal;
    return nullptr;
}

std::shared_ptr<Catalogue> forMedia(const QString &chosen, const QString &media) {
    if (chosen.isEmpty() || chosen == QLatin1String("none"))
        return nullptr;
    if (auto catalogue = byKey(chosen); catalogue && catalogue->supports(media))
        return catalogue;
    return byKey(QStringLiteral("anilist"));
}

QStringList readingLinks(int anilistId) {
    if (anilistId <= 0)
        return {};
    const auto cacheKey = QStringLiteral("anilist-links:%1").arg(anilistId);
    if (auto cached = Database::cacheGet(cacheKey))
        return QString::fromUtf8(*cached).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QStringList out;
    try {
        const auto data = anilist(QStringLiteral("query ($id: Int) { Media(id: $id) { externalLinks { url } } }"), {{"id", anilistId}});
        for (const auto &link : data.value("Media").toObject().value("externalLinks").toArray())
            if (const auto url = link.toObject().value("url").toString(); url.startsWith(QLatin1String("http")))
                out.append(url);
    } catch (const Error &) {
        return {};
    }
    Database::cachePut(cacheKey, out.join(QLatin1Char('\n')).toUtf8(), 7 * 86400);
    return out;
}

} // namespace sc::catalogue
