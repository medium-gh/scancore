// Comic sources that publish a JSON API: Asura, MangaDex, Iken (Vortex),
// HiveToons, Qi Manga, ManhwaWeb, Naver Webtoon, Kakao Page, ZonaTMO,
// Olympus, ZeroComics (LectorManga.lat) and Kagane.

#include "Engines.h"

#include "core/Database.h"
#include "core/Html.h"
#include "core/Parallel.h"
#include "core/Text.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimeZone>
#include <QUrlQuery>

#include <mutex>

namespace sc::comics {

namespace {

QJsonArray arr(const QJsonValue &v) { return v.toArray(); }
QJsonObject obj(const QJsonValue &v) { return v.toObject(); }

QUrl apiUrl(const QString &base, const QString &path, const QList<QPair<QString, QString>> &params = {}) {
    QUrl url(base + path);
    QUrlQuery query(url);
    for (const auto &[key, value] : params)
        query.addQueryItem(QString::fromUtf8(QUrl::toPercentEncoding(key)),
                           QString::fromUtf8(QUrl::toPercentEncoding(value)));
    url.setQuery(query);
    return url;
}

QStringList names(const QJsonArray &rows, const QString &field = QStringLiteral("name")) {
    QStringList out;
    for (const auto &row : rows) {
        const auto value = row.isObject() ? str(row.toObject().value(field)) : str(row);
        if (!value.isEmpty())
            out.append(value);
    }
    return out;
}

QString stripTags(const QString &html) {
    if (!html.contains(QLatin1Char('<')))
        return decodeEntities(html);
    HtmlDocument doc(html);
    QStringList paragraphs;
    for (const auto &p : doc.select(QStringLiteral("p")))
        if (auto text = p.textTrim(); !text.isEmpty())
            paragraphs.append(text);
    return paragraphs.isEmpty() ? doc.root().textTrim() : paragraphs.join(QStringLiteral("\n\n"));
}

} // namespace

// ---------------------------------------------------------------- Asura

namespace {
QString asuraSlug(const QString &seriesUrl) {
    const QUrl url(seriesUrl);
    const auto path = url.path();
    const auto segment = pathSegment(url, -1);
    if (path.startsWith(QLatin1String("/comics/"))) {
        static const QRegularExpression suffix(QStringLiteral("-[a-f0-9]{8}$"),
                                               QRegularExpression::CaseInsensitiveOption);
        return QString(segment).remove(suffix);
    }
    return segment;
}
} // namespace

QList<Summary> AsuraSource::search(const QString &query) {
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/search"),
                                          {{"q", query}, {"page", "1"}, {"per_page", "20"}}),
                                   jsonHeaders());
    QList<Summary> out;
    for (const auto &row : arr(doc.object().value("data"))) {
        const auto s = row.toObject();
        Summary item;
        item.title = str(s.value("title"));
        const auto publicUrl = str(s.value("public_url"));
        item.url = QUrl(m_config.base)
                       .resolved(QUrl(publicUrl.isEmpty() ? QStringLiteral("/s/") + str(s.value("id")) : publicUrl))
                       .toString();
        item.cover = str(s.value("cover"));
        if (item.cover.isEmpty())
            item.cover = str(s.value("cover_url"));
        item.type = QStringLiteral("Manhwa");
        item.source = key();
        if (!item.title.isEmpty())
            out.append(item);
    }
    return out;
}

QList<Chapter> AsuraSource::chapters(const QString &seriesUrl) {
    const auto slug = asuraSlug(seriesUrl);
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/series/%1/chapters").arg(slug),
                                          {{"page", "1"}, {"per_page", "2000"}, {"order", "asc"}}),
                                   jsonHeaders(), 20000);
    QList<Chapter> out;
    for (const auto &row : arr(doc.object().value("data"))) {
        const auto c = row.toObject();
        Chapter ch;
        ch.number = str(c.value("number"));
        ch.slug = str(c.value("slug"));
        const auto title = str(c.value("title"));
        ch.title = chapterLabel(ch.number, title);
        ch.url = QStringLiteral("%1/api/series/%2/chapters/%3").arg(m_config.api, slug, ch.slug);
        for (const char *field : {"created_at", "published_at", "createdAt", "publishedAt"})
            if (ch.released.isEmpty())
                ch.released = str(c.value(QLatin1String(field)));
        if (!ch.number.isEmpty() && !ch.slug.isEmpty())
            out.append(ch);
    }
    sortChapters(out);
    return out;
}

QList<Panel> AsuraSource::panels(const QString &chapterUrl) {
    const auto doc = Http::getJson(QUrl(chapterUrl), jsonHeaders());
    QList<Panel> out;
    for (const auto &page : arr(obj(obj(doc.object().value("data")).value("chapter")).value("pages"))) {
        const auto p = page.toObject();
        Panel panel{str(p.value("url")), p.value("width").toInt(), p.value("height").toInt()};
        if (!panel.url.isEmpty())
            out.append(panel);
    }
    return out;
}

std::optional<Details> AsuraSource::details(const QString &seriesUrl) {
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/series/%1").arg(asuraSlug(seriesUrl))),
                                   jsonHeaders());
    auto s = doc.object().value("series").toObject();
    if (s.isEmpty())
        s = doc.object().value("data").toObject();
    if (s.contains("series"))
        s = s.value("series").toObject();
    if (s.isEmpty())
        return std::nullopt;
    Details d;
    d.title = str(s.value("title"));
    d.cover = str(s.value("cover"));
    if (d.cover.isEmpty())
        d.cover = str(s.value("cover_url"));
    for (const char *field : {"description", "synopsis", "summary"})
        if (d.description.isEmpty())
            d.description = stripTags(str(s.value(QLatin1String(field))));
    d.status = str(s.value("status"));
    d.genres = names(arr(s.value("genres")));
    d.author = str(s.value("author"));
    return d;
}

QList<LatestItem> AsuraSource::latest(int limit) {
    QList<LatestItem> out;
    for (int offset = 0; out.size() < limit && offset < 200; offset += 20) {
        const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/series"),
                                              {{"offset", QString::number(offset)},
                                               {"order", "updated_at"},
                                               {"sort", "desc"}}),
                                       jsonHeaders());
        const auto rows = arr(doc.object().value("data"));
        for (const auto &row : rows) {
            const auto s = row.toObject();
            const auto chapter = arr(s.value("latest_chapters")).first().toObject();
            LatestItem item;
            item.source = key();
            item.sourceLabel = label();
            item.title = str(s.value("title"));
            const auto publicUrl = str(s.value("public_url"));
            item.url = QUrl(m_config.base)
                           .resolved(QUrl(publicUrl.isEmpty() ? QStringLiteral("/s/") + str(s.value("id")) : publicUrl))
                           .toString();
            item.cover = str(s.value("cover"));
            if (item.cover.isEmpty())
                item.cover = str(s.value("cover_url"));
            item.chapterNumber = str(chapter.value("number"));
            if (!item.chapterNumber.isEmpty())
                item.chapterTitle = chapterLabel(item.chapterNumber);
            if (!str(chapter.value("slug")).isEmpty() && !str(s.value("slug")).isEmpty())
                item.chapterUrl = QStringLiteral("%1/api/series/%2/chapters/%3")
                                      .arg(m_config.api, str(s.value("slug")), str(chapter.value("slug")));
            for (const auto &value : {chapter.value("published_at"), s.value("last_chapter_at"), s.value("updated_at")})
                if (item.publishedAt.isEmpty())
                    item.publishedAt = str(value);
            if (!item.title.isEmpty())
                out.append(item);
        }
        if (rows.isEmpty() || !obj(doc.object().value("meta")).value("has_more").toBool())
            break;
    }
    return out;
}

// ---------------------------------------------------------------- MangaDex

namespace {
std::mutex g_dexMutex;
qint64 g_dexNext = 0;
qint64 g_dexNextAtHome = 0;

QString dexId(const QString &value, const QString &type) {
    const QUrl url(value);
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"),
        QRegularExpression::CaseInsensitiveOption);
    if (!hostMatches(url.host(), QStringLiteral("mangadex.org")) || pathSegment(url, 0) != type ||
        !uuid.match(pathSegment(url, 1)).hasMatch())
        throw Error(QStringLiteral("That is not a MangaDex %1 link.").arg(type));
    return pathSegment(url, 1);
}

// The name in the first wanted language that has one (other languages'
// names are mostly among the alternative titles), then English, then any.
QString dexTitle(const QJsonObject &attrs, const QStringList &languages) {
    const auto titles = attrs.value("title").toObject();
    for (const auto &language : languages) {
        if (const auto title = str(titles.value(language)); !title.isEmpty())
            return title;
        // English keeps the main title, as before languages were added.
        if (language == QLatin1String("en"))
            continue;
        for (const auto &alt : arr(attrs.value("altTitles")))
            if (const auto title = str(alt.toObject().value(language)); !title.isEmpty())
                return title;
    }
    if (const auto title = str(titles.value("en")); !title.isEmpty())
        return title;
    // An English name before a romanised one ("Na Honjaman Level-Up").
    for (const auto &alt : arr(attrs.value("altTitles")))
        if (const auto title = str(alt.toObject().value("en")); !title.isEmpty())
            return title;
    return titles.isEmpty() ? QString() : str(titles.begin().value());
}

QString dexText(const QJsonObject &texts, const QStringList &languages) {
    for (const auto &language : languages + QStringList{QStringLiteral("en")})
        if (const auto text = str(texts.value(language)); !text.isEmpty())
            return text;
    for (const auto &text : texts)
        if (!str(text).isEmpty())
            return str(text);
    return {};
}
} // namespace

QStringList MangaDexSource::languages() const {
    QStringList out;
    for (const auto &value : m_config.options.value("languages").toArray())
        if (const auto language = str(value).trimmed(); !language.isEmpty() && !out.contains(language))
            out.append(language);
    return out.isEmpty() ? QStringList{QStringLiteral("en")} : out;
}

QJsonDocument MangaDexSource::api(const QString &path, const QList<QPair<QString, QString>> &params, bool atHome) {
    // MangaDex asks clients to stay under ~5 requests a second, and far fewer
    // at-home lookups. Requests are spaced instead of rejected.
    qint64 wait = 0;
    {
        std::lock_guard lock(g_dexMutex);
        const auto now = QDateTime::currentMSecsSinceEpoch();
        const auto at = std::max({now, g_dexNext, atHome ? g_dexNextAtHome : qint64(0)});
        if (at - now > 8000)
            throw Error(QStringLiteral("MangaDex is busy. Try again in a moment."), 429);
        g_dexNext = at + 260;
        if (atHome)
            g_dexNextAtHome = at + 1600;
        wait = at - now;
    }
    while (wait > 0 && !CancelScope::cancelled()) {
        QThread::msleep(std::min<qint64>(wait, 50));
        wait -= 50;
    }
    return Http::getJson(apiUrl(m_config.api, path, params), {{"User-Agent", "SCANCORE-Desktop/1.0"}});
}

namespace {
// MangaDex's content ratings to ask for: adult ones too when the reader
// turned on Settings → Sources → adult sources (titles rated erotica or
// pornographic were never found, though the website lists them).
void addRatings(QList<QPair<QString, QString>> &params) {
    params.append({{"contentRating[]", "safe"}, {"contentRating[]", "suggestive"}});
    if (Database::pref(QStringLiteral("setting.showNsfwSources"), false).toBool())
        params.append({{"contentRating[]", "erotica"}, {"contentRating[]", "pornographic"}});
}
} // namespace

QList<Summary> MangaDexSource::search(const QString &query) {
    const auto wanted = languages();
    QList<QPair<QString, QString>> params{{"title", query}, {"limit", "24"}, {"includes[]", "cover_art"}};
    for (const auto &language : wanted)
        params.append({"availableTranslatedLanguage[]", language});
    addRatings(params);
    params.append(qMakePair(QStringLiteral("order[relevance]"), QStringLiteral("desc")));
    const auto doc = api(QStringLiteral("/manga"), params);
    QList<Summary> out;
    for (const auto &row : arr(doc.object().value("data"))) {
        const auto manga = row.toObject();
        const auto attrs = manga.value("attributes").toObject();
        Summary item;
        item.title = dexTitle(attrs, wanted);
        // Shown in the wanted language, but a catalogue knows it by another.
        for (const auto &title : attrs.value("title").toObject())
            item.names.append(str(title));
        for (const auto &alt : arr(attrs.value("altTitles")))
            for (const auto &title : alt.toObject())
                item.names.append(str(title));
        item.names.removeAll(item.title);
        item.names.removeDuplicates();
        const auto id = str(manga.value("id"));
        item.url = tagged(QStringLiteral("https://mangadex.org/title/%1").arg(id));
        for (const auto &rel : arr(manga.value("relationships"))) {
            const auto r = rel.toObject();
            if (str(r.value("type")) == QLatin1String("cover_art")) {
                const auto file = str(obj(r.value("attributes")).value("fileName"));
                if (!file.isEmpty())
                    item.cover = QStringLiteral("https://uploads.mangadex.org/covers/%1/%2.512.jpg").arg(id, file);
            }
        }
        QStringList tags;
        for (const auto &tag : arr(attrs.value("tags")))
            tags.append(str(obj(obj(tag.toObject().value("attributes")).value("name")).value("en")));
        tags.removeAll(QString());
        item.genres = tags.join(QStringLiteral(", "));
        item.type = QStringLiteral("Manga");
        item.source = key();
        if (!item.title.isEmpty())
            out.append(item);
    }
    return out;
}

QList<QPair<QString, QString>> MangaDexSource::otherNames(const QString &title, int anilist, int mal) {
    const auto wanted = normalizedTitle(title);
    if (wanted.isEmpty())
        return {};
    const auto cacheKey = QStringLiteral("mangadex-names:%1:%2:%3").arg(anilist).arg(mal).arg(wanted);
    QList<QPair<QString, QString>> out;
    if (auto cached = Database::cacheGet(cacheKey)) {
        for (const auto &row : QJsonDocument::fromJson(*cached).array())
            out.append({str(row.toArray().at(0)), str(row.toArray().at(1))});
        return out;
    }
    QList<QPair<QString, QString>> params{{"title", title}, {"limit", "10"}};
    addRatings(params);
    if (!Database::pref(QStringLiteral("setting.showNsfwSources"), false).toBool())
        params.append(qMakePair(QStringLiteral("contentRating[]"), QStringLiteral("erotica")));
    params.append(qMakePair(QStringLiteral("order[relevance]"), QStringLiteral("desc")));
    const auto doc = api(QStringLiteral("/manga"), params);
    const auto namesOf = [](const QJsonObject &attrs) {
        QList<QPair<QString, QString>> names;
        const auto titles = attrs.value("title").toObject();
        for (auto it = titles.begin(); it != titles.end(); ++it)
            names.append({it.key(), str(it.value())});
        for (const auto &alt : arr(attrs.value("altTitles"))) {
            const auto o = alt.toObject();
            for (auto it = o.begin(); it != o.end(); ++it)
                names.append({it.key(), str(it.value())});
        }
        return names;
    };
    const QString al = anilist > 0 ? QString::number(anilist) : QString();
    const QString ml = mal > 0 ? QString::number(mal) : QString();
    for (const auto &row : arr(doc.object().value("data"))) {
        const auto attrs = row.toObject().value("attributes").toObject();
        const auto links = attrs.value("links").toObject();
        const auto names = namesOf(attrs);
        bool same = (!al.isEmpty() && str(links.value("al")) == al) || (!ml.isEmpty() && str(links.value("mal")) == ml);
        // Without ids, only an exact name will do.
        if (!same && al.isEmpty() && ml.isEmpty())
            same = std::any_of(names.begin(), names.end(), [&](const auto &n) { return normalizedTitle(n.second) == wanted; });
        if (same) {
            out = names;
            break;
        }
    }
    QJsonArray rows;
    for (const auto &[language, name] : out)
        rows.append(QJsonArray{language, name});
    Database::cachePut(cacheKey, QJsonDocument(rows).toJson(QJsonDocument::Compact), 7 * 86400);
    return out;
}

QList<Chapter> MangaDexSource::feedChapters(const QJsonArray &rows, QSet<QString> &seen, int *takenDown) const {
    QList<Chapter> out;
    for (const auto &row : rows) {
        const auto c = row.toObject();
        const auto attrs = c.value("attributes").toObject();
        const auto cid = str(c.value("id"));
        if (attrs.value("isUnavailable").toBool()) {
            if (takenDown)
                ++*takenDown;
            continue;
        }
        if (!attrs.value("pages").toInt() || !str(attrs.value("externalUrl")).isEmpty() || seen.contains(cid))
            continue;
        seen.insert(cid);
        Chapter ch;
        ch.number = str(attrs.value("chapter"));
        if (ch.number.isEmpty())
            ch.number = QStringLiteral("0");
        const auto title = str(attrs.value("title"));
        ch.title = chapterLabel(ch.number, title);
        ch.url = tagged(QStringLiteral("https://mangadex.org/chapter/%1").arg(cid));
        ch.slug = cid;
        ch.released = str(attrs.value("publishAt"));
        QStringList groups;
        for (const auto &rel : arr(c.value("relationships")))
            if (const auto r = rel.toObject(); str(r.value("type")) == QLatin1String("scanlation_group"))
                if (const auto name = str(r.value("attributes").toObject().value("name")).trimmed(); !name.isEmpty())
                    groups.append(name);
        ch.scanlator = groups.join(QStringLiteral(", "));
        out.append(ch);
    }
    return out;
}

namespace {

QList<QPair<QString, QString>> feedParams(int offset, const QStringList &languages) {
    QList<QPair<QString, QString>> params{{"offset", QString::number(offset)}, {"limit", "500"}};
    for (const auto &language : languages)
        params.append({"translatedLanguage[]", language});
    params.append({{"order[chapter]", "asc"}, {"order[volume]", "asc"}, {"includeExternalUrl", "0"}, {"includeFuturePublishAt", "0"},
                   {"includes[]", "scanlation_group"}});
    // The feed of one series already chosen: its rating was the search's
    // business. Without these MangaDex leaves out a pornographic series'
    // chapters, so it looked unreadable.
    params.append({{"contentRating[]", "safe"}, {"contentRating[]", "suggestive"}, {"contentRating[]", "erotica"},
                   {"contentRating[]", "pornographic"}});
    return params;
}

const QString kTakenDown = QStringLiteral(
    "MangaDex took these chapters down, usually because the series is licensed. Try another source.");

} // namespace

QList<Chapter> MangaDexSource::chapters(const QString &seriesUrl) {
    const auto id = dexId(seriesUrl, QStringLiteral("title"));
    const auto wanted = languages();
    QList<Chapter> out;
    QSet<QString> seen;
    int takenDown = 0;
    for (int offset = 0; offset < 10000; offset += 500) {
        const auto doc = api(QStringLiteral("/manga/%1/feed").arg(id), feedParams(offset, wanted));
        const auto rows = arr(doc.object().value("data"));
        out.append(feedChapters(rows, seen, &takenDown));
        if (rows.isEmpty() || offset + rows.size() >= doc.object().value("total").toInt())
            break;
    }
    // A feed of chapters none of which can be read is a takedown, not an
    // empty series: say so instead of showing an empty page.
    if (out.isEmpty() && takenDown > 0)
        throw Error(kTakenDown, 451);
    return out;
}

bool MangaDexSource::readable(const QString &seriesUrl) {
    const auto id = dexId(seriesUrl, QStringLiteral("title"));
    const auto cacheKey = QStringLiteral("mangadex-readable:%1:%2").arg(id, languages().join(QLatin1Char(',')));
    if (auto cached = Database::cacheGet(cacheKey))
        return *cached == "1";
    const auto doc = api(QStringLiteral("/manga/%1/feed").arg(id), feedParams(0, languages()));
    const auto rows = arr(doc.object().value("data"));
    QSet<QString> seen;
    // Chapters beyond the first page may still be readable.
    const bool ok = !feedChapters(rows, seen, nullptr).isEmpty() || doc.object().value("total").toInt() > rows.size();
    Database::cachePut(cacheKey, ok ? "1" : "0", 86400);
    return ok;
}

QList<Panel> MangaDexSource::panels(const QString &chapterUrl) {
    const auto id = dexId(chapterUrl, QStringLiteral("chapter"));
    const auto doc = api(QStringLiteral("/at-home/server/%1").arg(id), {}, true);
    const auto root = doc.object();
    const auto chapter = root.value("chapter").toObject();
    const auto hash = str(chapter.value("hash"));
    const QUrl base(str(root.value("baseUrl")));
    if (hash.isEmpty() || !(hostMatches(base.host(), QStringLiteral("mangadex.network")) ||
                            hostMatches(base.host(), QStringLiteral("mangadex.org"))))
        throw Error(QStringLiteral("This MangaDex chapter is unavailable."));
    QList<Panel> out;
    for (const auto &file : arr(chapter.value("data")))
        out.append({QStringLiteral("%1://%2/data/%3/%4")
                        .arg(base.scheme(), base.authority(), hash, QString::fromUtf8(QUrl::toPercentEncoding(str(file))))});
    return out;
}

std::optional<Details> MangaDexSource::details(const QString &seriesUrl) {
    const auto id = dexId(seriesUrl, QStringLiteral("title"));
    const auto doc = api(QStringLiteral("/manga/%1").arg(id), {{"includes[]", "cover_art"}, {"includes[]", "author"}});
    const auto manga = doc.object().value("data").toObject();
    const auto attrs = manga.value("attributes").toObject();
    const auto wanted = languages();
    Details d;
    d.title = dexTitle(attrs, wanted);
    d.description = dexText(attrs.value("description").toObject(), wanted);
    d.status = str(attrs.value("status"));
    // The same title on AniList ("al") and MyAnimeList ("mal"), when
    // MangaDex knows it; trackers use these before searching by name.
    const auto links = attrs.value("links").toObject();
    if (const auto anilist = str(links.value("al")).toInt(); anilist > 0)
        d.trackerIds.insert(QStringLiteral("anilist"), anilist);
    if (const auto mal = str(links.value("mal")).toInt(); mal > 0)
        d.trackerIds.insert(QStringLiteral("mal"), mal);
    for (const auto &tag : arr(attrs.value("tags")))
        d.genres.append(str(obj(obj(tag.toObject().value("attributes")).value("name")).value("en")));
    QStringList authors;
    for (const auto &rel : arr(manga.value("relationships"))) {
        const auto r = rel.toObject();
        const auto type = str(r.value("type"));
        if (type == QLatin1String("author"))
            authors.append(str(obj(r.value("attributes")).value("name")));
        if (type == QLatin1String("cover_art")) {
            const auto file = str(obj(r.value("attributes")).value("fileName"));
            if (!file.isEmpty())
                d.cover = QStringLiteral("https://uploads.mangadex.org/covers/%1/%2.512.jpg").arg(id, file);
        }
    }
    authors.removeAll(QString());
    d.author = authors.join(QStringLiteral(", "));
    return d;
}

// ---------------------------------------------------------------- Iken

QList<Summary> IkenSource::search(const QString &query) {
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/query"),
                                          {{"page", "1"}, {"perPage", "24"}, {"searchTerm", query}}),
                                   jsonHeaders());
    QList<Summary> out;
    for (const auto &row : arr(doc.object().value("posts"))) {
        const auto p = row.toObject();
        Summary item;
        item.title = str(p.value("postTitle"));
        item.url = QStringLiteral("%1/series/%2#%3").arg(m_config.base, str(p.value("slug")), str(p.value("id")));
        item.cover = str(p.value("featuredImage"));
        item.type = str(p.value("seriesType"));
        item.genres = names(arr(p.value("genres"))).join(QStringLiteral(", "));
        item.source = key();
        if (!item.title.isEmpty())
            out.append(item);
    }
    return out;
}

QString IkenSource::postId(const QUrl &seriesUrl) {
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    if (digits.match(seriesUrl.fragment()).hasMatch())
        return seriesUrl.fragment();
    const auto slug = pathSegment(seriesUrl, -1);
    if (slug.isEmpty())
        return {};
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/post"), {{"postSlug", slug}}),
                                   jsonHeaders());
    return str(doc.object().value("post").toObject().value("id"));
}

QList<Chapter> IkenSource::chapters(const QString &seriesUrl) {
    const auto id = postId(QUrl(seriesUrl));
    if (id.isEmpty())
        throw Error(QStringLiteral("%1 did not recognise this series.").arg(label()));
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/chapters"),
                                          {{"postId", id}, {"skip", "0"}, {"take", "999"}, {"order", "desc"}}),
                                   jsonHeaders(), 20000);
    QList<Chapter> out;
    for (const auto &row : arr(doc.object().value("post").toObject().value("chapters"))) {
        const auto c = row.toObject();
        if (c.value("isLocked").toBool() || c.value("isPermanentlyLocked").toBool() ||
            (c.contains("isAccessible") && !c.value("isAccessible").toBool()))
            continue;
        Chapter ch;
        ch.number = str(c.value("number"));
        const auto title = str(c.value("title"));
        ch.title = chapterLabel(ch.number, title);
        ch.url = QStringLiteral("%1/api/chapter?chapterId=%2").arg(m_config.api, str(c.value("id")));
        ch.slug = str(c.value("slug"));
        if (ch.slug.isEmpty())
            ch.slug = str(c.value("id"));
        ch.released = str(c.value("createdAt"));
        if (!ch.number.isEmpty())
            out.append(ch);
    }
    sortChapters(out);
    return out;
}

QList<Panel> IkenSource::panels(const QString &chapterUrl) {
    QUrl url(chapterUrl);
    if (url.path().startsWith(QLatin1String("/series/"))) {
        // A browser chapter link; find its API id through the chapter list.
        const auto seriesSlug = pathSegment(url, 1);
        const auto chapterSlug = pathSegment(url, 2);
        const auto list = chapters(QStringLiteral("%1/series/%2").arg(m_config.base, seriesSlug));
        auto it = std::find_if(list.begin(), list.end(), [&](const Chapter &c) { return c.slug == chapterSlug; });
        if (it == list.end())
            throw Error(QStringLiteral("This chapter is unavailable on %1.").arg(label()));
        url = QUrl(it->url);
    }
    const auto doc = Http::getJson(url, jsonHeaders());
    QList<std::pair<int, Panel>> rows;
    int index = 0;
    for (const auto &row : arr(doc.object().value("chapter").toObject().value("images"))) {
        const auto image = row.toObject();
        const auto order = image.contains("order") ? image.value("order").toInt() : index;
        Panel panel{str(image.value("url")), image.value("width").toInt(), image.value("height").toInt()};
        if (!panel.url.isEmpty())
            rows.append({order, panel});
        ++index;
    }
    std::stable_sort(rows.begin(), rows.end(), [](auto &a, auto &b) { return a.first < b.first; });
    QList<Panel> out;
    for (auto &[order, panel] : rows)
        out.append(panel);
    return out;
}

std::optional<Details> IkenSource::details(const QString &seriesUrl) {
    const auto slug = pathSegment(QUrl(seriesUrl), -1);
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/post"), {{"postSlug", slug}}),
                                   jsonHeaders());
    const auto p = doc.object().value("post").toObject();
    if (p.isEmpty())
        return std::nullopt;
    Details d;
    d.title = str(p.value("postTitle"));
    d.cover = str(p.value("featuredImage"));
    d.description = stripTags(str(p.value("postContent")));
    d.status = str(p.value("seriesStatus"));
    d.author = str(p.value("author"));
    d.genres = names(arr(p.value("genres")));
    return d;
}

QList<LatestItem> IkenSource::latest(int limit) {
    const int pageSize = std::clamp(limit, 12, 100);
    QList<LatestItem> out;
    for (int page = 1; out.size() < limit && page <= 5; ++page) {
        const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/query"),
                                              {{"page", QString::number(page)}, {"perPage", QString::number(pageSize)}}),
                                       jsonHeaders());
        const auto rows = arr(doc.object().value("posts"));
        for (const auto &row : rows) {
            const auto p = row.toObject();
            QJsonObject newest;
            for (const auto &c : arr(p.value("chapters")))
                if (newest.isEmpty() || c.toObject().value("number").toDouble() > newest.value("number").toDouble())
                    newest = c.toObject();
            LatestItem item;
            item.source = key();
            item.sourceLabel = label();
            item.title = str(p.value("postTitle"));
            item.url = QStringLiteral("%1/series/%2#%3").arg(m_config.base, str(p.value("slug")), str(p.value("id")));
            item.cover = str(p.value("featuredImage"));
            item.chapterNumber = str(newest.value("number"));
            item.chapterTitle = str(newest.value("title"));
            if (item.chapterTitle.isEmpty() && !item.chapterNumber.isEmpty())
                item.chapterTitle = chapterLabel(item.chapterNumber);
            if (!str(newest.value("id")).isEmpty())
                item.chapterUrl = QStringLiteral("%1/api/chapter?chapterId=%2").arg(m_config.api, str(newest.value("id")));
            for (const auto &value : {newest.value("createdAt"), p.value("lastChapterAddedAt"), p.value("updatedAt")})
                if (item.publishedAt.isEmpty())
                    item.publishedAt = str(value);
            if (!item.title.isEmpty())
                out.append(item);
        }
        if (rows.size() < pageSize)
            break;
    }
    return out;
}

// ---------------------------------------------------------------- HiveToons

QList<Summary> HiveSource::search(const QString &query) {
    const auto doc = Http::getJson(
        apiUrl(m_config.api, QStringLiteral("/api/query"),
               {{"page", "1"}, {"perPage", "40"}, {"view", "archive"}, {"searchTerm", query}}),
        jsonHeaders());
    QList<Summary> out;
    for (const auto &row : arr(doc.object().value("posts"))) {
        const auto p = row.toObject();
        Summary item;
        item.title = str(p.value("postTitle"));
        item.url = QStringLiteral("%1/series/%2").arg(m_config.base, str(p.value("slug")));
        item.cover = str(p.value("featuredImage"));
        item.type = str(p.value("seriesType"));
        item.genres = names(arr(p.value("genres"))).join(QStringLiteral(", "));
        item.source = key();
        if (!item.title.isEmpty())
            out.append(item);
    }
    return out;
}

QList<Chapter> HiveSource::chapters(const QString &seriesUrl) {
    const QUrl url(seriesUrl);
    const auto slug = pathSegment(url, 1);
    if (pathSegment(url, 0) != QLatin1String("series") || slug.isEmpty())
        throw Error(QStringLiteral("That is not a HiveToons series link."));
    // Match the complete slug so a related result cannot supply another
    // book's id.
    const auto catalog = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/query"),
                                              {{"page", "1"},
                                               {"perPage", "40"},
                                               {"view", "archive"},
                                               {"searchTerm", QString(slug).replace(QLatin1Char('-'), QLatin1Char(' '))}}),
                                       jsonHeaders());
    QString id;
    for (const auto &row : arr(catalog.object().value("posts")))
        if (str(row.toObject().value("slug")) == slug)
            id = str(row.toObject().value("id"));
    if (id.isEmpty())
        throw Error(QStringLiteral("HiveToons did not recognise this series."));
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/chapters"),
                                          {{"postId", id}, {"skip", "0"}, {"take", "all"}, {"order", "desc"}, {"view", "manage"}}),
                                   jsonHeaders(), 20000);
    static const QRegularExpression chapterSlug(QStringLiteral(R"(^chapter-\d+(?:\.\d+)?$)"));
    QList<Chapter> out;
    for (const auto &row : arr(doc.object().value("post").toObject().value("chapters"))) {
        const auto c = row.toObject();
        if (c.value("isAccessible").toBool() != true || c.value("isLocked").toBool() ||
            c.value("isPermanentlyLocked").toBool() || c.value("isShortLinkLocked").toBool() ||
            !chapterSlug.match(str(c.value("slug"))).hasMatch())
            continue;
        Chapter ch;
        ch.number = str(c.value("number"));
        ch.title = str(c.value("title"));
        if (ch.title.isEmpty())
            ch.title = chapterLabel(ch.number);
        ch.slug = str(c.value("slug"));
        ch.url = QStringLiteral("%1/series/%2/%3").arg(m_config.base, slug, ch.slug);
        ch.thumbnail = str(c.value("featuredImage"));
        ch.released = str(c.value("createdAt"));
        out.append(ch);
    }
    sortChapters(out);
    return out;
}

QList<Panel> HiveSource::panels(const QString &chapterUrl) {
    const auto page = Http::getOk(QUrl(chapterUrl), htmlHeaders());
    HtmlDocument doc(page.body);
    QSet<QString> seen;
    QList<Panel> out;
    const auto cdn = m_config.options.value("imageHost").toString(QStringLiteral("https://storage.hivetoon.com/"));
    for (const auto &img : doc.select(QStringLiteral("img[alt*=\"Page \"][src]"))) {
        const auto url = img.attr("src").trimmed();
        if (!url.startsWith(cdn) || seen.contains(url))
            continue;
        seen.insert(url);
        out.append({url, img.attr("width").toInt(), img.attr("height").toInt()});
    }
    return out;
}

// ---------------------------------------------------------------- Qi Manga

QList<Summary> QiMangaSource::search(const QString &query) {
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/series/search"),
                                          {{"q", query}, {"page", "1"}, {"perPage", "20"}}),
                                   jsonHeaders());
    QList<Summary> out;
    for (const auto &row : arr(doc.object().value("data"))) {
        const auto s = row.toObject();
        Summary item;
        item.title = str(s.value("title"));
        const auto slug = str(s.value("slug"));
        item.url = QStringLiteral("%1/series/%2").arg(m_config.base, slug);
        item.cover = str(s.value("cover"));
        item.type = str(s.value("type"));
        item.source = key();
        if (!item.title.isEmpty() && !slug.isEmpty())
            out.append(item);
    }
    return out;
}

QList<Chapter> QiMangaSource::chapters(const QString &seriesUrl) {
    const QUrl url(seriesUrl);
    const auto parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const auto at = parts.indexOf(QStringLiteral("series"));
    const auto slug = at >= 0 && at + 1 < parts.size() ? parts.at(at + 1) : QString();
    if (slug.isEmpty())
        throw Error(QStringLiteral("That is not a Qi Manga series link."));
    QList<Chapter> out;
    int totalPages = 1;
    for (int page = 1; page <= totalPages && page <= 50; ++page) {
        const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/series/%1/chapters").arg(slug),
                                              {{"page", QString::number(page)}, {"perPage", "100"}, {"sort", "desc"}}),
                                       jsonHeaders(), 20000);
        totalPages = std::max(1, doc.object().value("totalPages").toInt(1));
        for (const auto &row : arr(doc.object().value("data"))) {
            const auto c = row.toObject();
            Chapter ch;
            ch.slug = str(c.value("slug"));
            ch.number = str(c.value("number"));
            ch.title = str(c.value("title"));
            if (ch.title.isEmpty())
                ch.title = chapterLabel(ch.number);
            ch.url = QStringLiteral("%1/series/%2/%3").arg(m_config.base, slug, ch.slug);
            ch.thumbnail = str(c.value("cover"));
            ch.released = str(c.value("createdAt"));
            if (!ch.number.isEmpty() && !ch.slug.isEmpty())
                out.append(ch);
        }
    }
    sortChapters(out);
    return out;
}

QList<Panel> QiMangaSource::panels(const QString &chapterUrl) {
    const QUrl url(chapterUrl);
    const auto parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const auto at = parts.indexOf(QStringLiteral("series"));
    if (at < 0 || at + 2 >= parts.size())
        throw Error(QStringLiteral("That is not a Qi Manga chapter link."));
    const auto doc = Http::getJson(
        apiUrl(m_config.api, QStringLiteral("/series/%1/chapters/%2").arg(parts.at(at + 1), parts.last())),
        jsonHeaders());
    QList<std::pair<int, Panel>> rows;
    int index = 0;
    for (const auto &row : arr(doc.object().value("images"))) {
        const auto image = row.toObject();
        const auto order = image.value("order").isDouble() ? image.value("order").toInt() : index;
        Panel panel{str(image.value("url")), image.value("width").toInt(), image.value("height").toInt()};
        if (!panel.url.isEmpty())
            rows.append({order, panel});
        ++index;
    }
    std::stable_sort(rows.begin(), rows.end(), [](auto &a, auto &b) { return a.first < b.first; });
    QList<Panel> out;
    for (auto &[order, panel] : rows)
        out.append(panel);
    return out;
}

std::optional<Details> QiMangaSource::details(const QString &seriesUrl) {
    const QUrl url(seriesUrl);
    const auto parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const auto at = parts.indexOf(QStringLiteral("series"));
    if (at < 0 || at + 1 >= parts.size())
        return std::nullopt;
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/series/%1").arg(parts.at(at + 1))),
                                   jsonHeaders());
    auto s = doc.object();
    if (s.value("data").isObject())
        s = s.value("data").toObject();
    if (s.isEmpty())
        return std::nullopt;
    Details d;
    d.title = str(s.value("title"));
    d.cover = str(s.value("cover"));
    d.description = stripTags(str(s.value("description")));
    d.status = str(s.value("status"));
    d.genres = names(arr(s.value("genres")));
    d.author = str(s.value("author"));
    return d;
}

QList<LatestItem> QiMangaSource::latest(int limit) {
    const int pageSize = std::clamp(limit, 12, 100);
    QList<LatestItem> out;
    for (int page = 1; out.size() < limit && page <= 5; ++page) {
        const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/home/latest"),
                                              {{"page", QString::number(page)}, {"perPage", QString::number(pageSize)}}),
                                       jsonHeaders());
        const auto rows = arr(doc.object().value("data"));
        for (const auto &row : rows) {
            const auto p = row.toObject();
            QJsonObject newest;
            for (const auto &c : arr(p.value("chapters")))
                if (newest.isEmpty() || c.toObject().value("number").toDouble() > newest.value("number").toDouble())
                    newest = c.toObject();
            LatestItem item;
            item.source = key();
            item.sourceLabel = label();
            item.title = str(p.value("title"));
            item.url = QStringLiteral("%1/series/%2").arg(m_config.base, str(p.value("slug")));
            item.cover = str(p.value("cover"));
            item.chapterNumber = str(newest.value("number"));
            item.chapterTitle = str(newest.value("title"));
            if (item.chapterTitle.isEmpty() && !item.chapterNumber.isEmpty())
                item.chapterTitle = chapterLabel(item.chapterNumber);
            if (!str(newest.value("slug")).isEmpty())
                item.chapterUrl = QStringLiteral("%1/series/%2/%3").arg(m_config.base, str(p.value("slug")), str(newest.value("slug")));
            for (const auto &value : {newest.value("createdAt"), p.value("updatedAt")})
                if (item.publishedAt.isEmpty())
                    item.publishedAt = str(value);
            if (!item.title.isEmpty())
                out.append(item);
        }
        if (rows.size() < pageSize)
            break;
    }
    return out;
}

// ---------------------------------------------------------------- ManhwaWeb

namespace {
// Series: https://manhwaweb.com/manhwa/<id>; chapters:
// https://manhwaweb.com/leer/<id>-<number>_<version>.
QString manhwaWebId(const QString &url) { return pathSegment(QUrl(url), -1); }

QString manhwaWebStatus(const QString &status) {
    static const QHash<QString, QString> names{{"publicandose", QString::fromUtf8("En publicación")},
                                               {"finalizado", "Finalizado"},
                                               {"pausado", "Pausado"},
                                               {"cancelado", "Cancelado"}};
    return names.value(status, status);
}
} // namespace

QList<Summary> ManhwaWebSource::search(const QString &query) {
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/manhwa/library"),
                                          {{"buscar", query},
                                           {"estado", ""},
                                           {"tipo", ""},
                                           {"erotico", ""},
                                           {"demografia", ""},
                                           {"order_item", "alfabetico"},
                                           {"order_dir", "desc"},
                                           {"page", "0"},
                                           {"generes", ""}}),
                                   jsonHeaders());
    // Adult titles stay out unless the manifest asks for them.
    const bool adult = m_config.options.value("adult").toBool(false);
    QList<Summary> out;
    for (const auto &row : arr(doc.object().value("data"))) {
        const auto m = row.toObject();
        if (!adult && str(m.value("_erotico")) == QLatin1String("si"))
            continue;
        auto id = str(m.value("real_id"));
        if (id.isEmpty())
            id = str(m.value("_id"));
        Summary item;
        item.title = str(m.value("name_esp"));
        if (item.title.isEmpty())
            item.title = str(m.value("the_real_name"));
        if (id.isEmpty() || item.title.isEmpty())
            continue;
        item.url = QStringLiteral("%1/manhwa/%2").arg(m_config.base, id);
        item.cover = str(m.value("_imagen"));
        const auto type = str(m.value("_tipo"));
        item.type = type.isEmpty() ? QString() : type.left(1).toUpper() + type.mid(1);
        item.source = key();
        out.append(item);
    }
    return out;
}

QJsonObject ManhwaWebSource::series(const QString &seriesUrl) {
    const auto id = manhwaWebId(seriesUrl);
    if (id.isEmpty())
        throw Error(QStringLiteral("That is not a ManhwaWeb series link."));
    return Http::getJson(apiUrl(m_config.api, QStringLiteral("/manhwa/see/") + QString::fromUtf8(QUrl::toPercentEncoding(id))),
                         jsonHeaders())
        .object();
}

QList<Chapter> ManhwaWebSource::parseChapters(const QJsonObject &series, const QString &id, const QString &base,
                                              const QString &language) {
    QList<Chapter> out;
    QSet<QString> seen;
    for (const auto &row : arr(series.value("chapters"))) {
        const auto c = row.toObject();
        const auto number = str(c.value("chapter"));
        // The link names the upload ("…-12_01"); its series part can be an
        // older id the API no longer answers to, so the id is the series'.
        const auto version = str(c.value("link")).section(QLatin1Char('-'), -1);
        if (number.isEmpty() || !version.contains(QLatin1Char('_')))
            continue;
        const auto chapterId = id + QLatin1Char('-') + version;
        if (seen.contains(chapterId))
            continue;
        seen.insert(chapterId);
        Chapter ch;
        ch.number = number;
        ch.title = comics::chapterLabel(language, number);
        ch.url = QStringLiteral("%1/leer/%2").arg(base, chapterId);
        ch.slug = chapterId;
        if (const auto created = c.value("create").toDouble(); created > 0)
            ch.released = QDateTime::fromMSecsSinceEpoch(qint64(created), QTimeZone::UTC).toString(Qt::ISODate);
        out.append(ch);
    }
    sortChapters(out);
    return out;
}

QList<Chapter> ManhwaWebSource::chapters(const QString &seriesUrl) {
    return parseChapters(series(seriesUrl), manhwaWebId(seriesUrl), m_config.base, m_config.language);
}

QList<Panel> ManhwaWebSource::panels(const QString &chapterUrl) {
    const auto id = manhwaWebId(chapterUrl);
    const auto doc = Http::getJson(
        apiUrl(m_config.api, QStringLiteral("/chapters/see/") + QString::fromUtf8(QUrl::toPercentEncoding(id))), jsonHeaders());
    QList<Panel> out;
    for (const auto &image : arr(doc.object().value("chapter").toObject().value("img")))
        if (const auto url = str(image); url.startsWith(QLatin1String("https://")))
            out.append({url});
    return out;
}

std::optional<Details> ManhwaWebSource::details(const QString &seriesUrl) {
    const auto data = series(seriesUrl);
    Details d;
    d.title = str(data.value("name_esp"));
    if (d.title.isEmpty())
        d.title = str(data.value("the_real_name"));
    d.cover = str(data.value("_imagen"));
    d.description = str(data.value("_sinopsis")).trimmed();
    d.status = manhwaWebStatus(str(data.value("_status")));
    for (const auto &value : arr(data.value("_categoris")))
        for (const auto &name : value.toObject())
            if (const auto genre = str(name); !genre.isEmpty())
                d.genres.append(genre);
    QStringList authors;
    for (const auto &value : arr(data.value("autor")))
        authors.append(str(value.toObject().value("name")).trimmed());
    authors.removeAll(QString());
    authors.removeDuplicates();
    d.author = authors.join(QStringLiteral(", "));
    return d;
}

// ---------------------------------------------------------------- Naver Webtoon

namespace {
QString naverTitleId(const QString &url) {
    const auto id = QUrlQuery(QUrl(url)).queryItemValue(QStringLiteral("titleId"));
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    if (!digits.match(id).hasMatch())
        throw Error(QStringLiteral("That is not a Naver Webtoon link."));
    return id;
}

// "26.06.09" (yy.MM.dd), or a time of day for today's episodes.
QString naverDate(const QString &text) {
    if (text.contains(QLatin1Char(':')))
        return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const auto date = QDate::fromString(text, QStringLiteral("yy.MM.dd"));
    // Two-digit years read as 19xx.
    return date.isValid() ? date.addYears(date.year() < 2000 ? 100 : 0).toString(Qt::ISODate) : QString();
}
} // namespace

QString NaverSource::seriesLink(const QUrl &url) const {
    // Also the old pages (list.nhn) and the mobile site.
    if (!owns(url) || !url.path().startsWith(QLatin1String("/webtoon/list")))
        return {};
    try {
        return QStringLiteral("%1/webtoon/list?titleId=%2").arg(m_config.base, naverTitleId(url.toString()));
    } catch (const Error &) {
        return {};
    }
}

QList<Summary> NaverSource::search(const QString &query) {
    const auto doc = Http::getJson(apiUrl(m_config.base, QStringLiteral("/api/search/webtoon"), {{"keyword", query}, {"page", "1"}}),
                                   jsonHeaders());
    QList<Summary> out;
    for (const auto &row : arr(doc.object().value("searchList"))) {
        const auto m = row.toObject();
        // Adult titles open only after signing in.
        if (m.value("adult").toBool())
            continue;
        Summary item;
        item.title = str(m.value("titleName"));
        const auto id = str(m.value("titleId"));
        if (item.title.isEmpty() || id.isEmpty())
            continue;
        item.url = QStringLiteral("%1/webtoon/list?titleId=%2").arg(m_config.base, id);
        item.cover = str(m.value("thumbnailUrl"));
        item.author = str(m.value("displayAuthor"));
        item.genres = names(arr(m.value("genreList")), QStringLiteral("description")).join(QStringLiteral(", "));
        item.type = QStringLiteral("Manhwa");
        item.source = key();
        out.append(item);
    }
    return out;
}

QList<Chapter> NaverSource::parseChapters(const QJsonObject &page, const QString &base) {
    const auto titleId = str(page.value("titleId"));
    QList<Chapter> out;
    for (const auto &row : arr(page.value("articleList"))) {
        const auto a = row.toObject();
        // Paid previews ("charge") need an account.
        if (a.value("charge").toBool())
            continue;
        Chapter ch;
        ch.number = str(a.value("no"));
        ch.title = str(a.value("subtitle")).trimmed();
        if (ch.number.isEmpty() || titleId.isEmpty())
            continue;
        if (ch.title.isEmpty())
            ch.title = comics::chapterLabel(QStringLiteral("ko"), ch.number);
        ch.slug = ch.number;
        ch.url = QStringLiteral("%1/webtoon/detail?titleId=%2&no=%3").arg(base, titleId, ch.number);
        ch.thumbnail = str(a.value("thumbnailUrl"));
        ch.released = naverDate(str(a.value("serviceDateDescription")));
        out.append(ch);
    }
    return out;
}

QList<Chapter> NaverSource::chapters(const QString &seriesUrl) {
    const auto id = naverTitleId(seriesUrl);
    const auto page = [this, id](int n) {
        return Http::getJson(apiUrl(m_config.base, QStringLiteral("/api/article/list"), {{"titleId", id}, {"page", QString::number(n)}}),
                             jsonHeaders())
            .object();
    };
    // 20 episodes a page; the rest in parallel.
    const auto first = page(1);
    auto out = parseChapters(first, m_config.base);
    const int pages = std::min(first.value("pageInfo").toObject().value("totalPages").toInt(1), 200);
    QList<std::function<QList<Chapter>()>> jobs;
    for (int n = 2; n <= pages; ++n)
        jobs.append([this, page, n] { return parseChapters(page(n), m_config.base); });
    for (const auto &result : parallel(jobs)) {
        if (!result.ok())
            throw Error(result.error);
        out.append(*result.value);
    }
    sortChapters(out);
    return out;
}

QList<Panel> NaverSource::parsePanels(const QByteArray &html) {
    HtmlDocument doc(html);
    QList<Panel> out;
    for (const auto &img : doc.select(QStringLiteral(".wt_viewer img")))
        // The first image is an age notice, not a page.
        if (img.attr("id").startsWith(QLatin1String("content_image")))
            if (const auto url = img.attr("src"); url.startsWith(QLatin1String("https://")))
                out.append({url});
    return out;
}

QList<Panel> NaverSource::panels(const QString &chapterUrl) {
    naverTitleId(chapterUrl);
    const auto response = Http::getOk(QUrl(chapterUrl), htmlHeaders());
    auto out = parsePanels(response.body);
    if (out.isEmpty())
        throw Error(QStringLiteral("Naver Webtoon shows this episode only after signing in."));
    return out;
}

std::optional<Details> NaverSource::details(const QString &seriesUrl) {
    const auto doc = Http::getJson(apiUrl(m_config.base, QStringLiteral("/api/article/list/info"), {{"titleId", naverTitleId(seriesUrl)}}),
                                   jsonHeaders());
    const auto m = doc.object();
    Details d;
    d.title = str(m.value("titleName"));
    d.cover = str(m.value("posterThumbnailUrl"));
    if (d.cover.isEmpty())
        d.cover = str(m.value("thumbnailUrl"));
    d.description = str(m.value("synopsis")).trimmed();
    d.status = m.value("finished").toBool() ? QStringLiteral("Completed") : m.value("rest").toBool() ? QStringLiteral("Hiatus") : QStringLiteral("Ongoing");
    d.author = names(arr(m.value("communityArtists"))).join(QStringLiteral(", "));
    for (const auto &tag : arr(m.value("curationTagList")))
        if (str(tag.toObject().value("curationType")).startsWith(QLatin1String("GENRE_")))
            d.genres.append(str(tag.toObject().value("tagName")));
    return d;
}

// ---------------------------------------------------------------- Kakao Page

namespace {
QString kakaoSeriesId(const QString &url) {
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    const QUrl u(url);
    const auto id = pathSegment(u, 1);
    if (pathSegment(u, 0) != QLatin1String("content") || !digits.match(id).hasMatch())
        throw Error(QStringLiteral("That is not a Kakao Page series link."));
    return id;
}
} // namespace

QString KakaoPageSource::seriesLink(const QUrl &url) const {
    if (!owns(url))
        return {};
    try {
        return QStringLiteral("%1/content/%2").arg(m_config.base, kakaoSeriesId(url.toString()));
    } catch (const Error &) {
        return {};
    }
}

QString KakaoPageSource::image(const QString &kid, const QString &size) {
    if (kid.isEmpty())
        return {};
    return QStringLiteral("https://page-images.kakaoentcdn.com/download/resource?kid=%1&filename=%2").arg(kid, size);
}

QJsonObject KakaoPageSource::api(const QString &path, const QList<QPair<QString, QString>> &params) {
    const auto doc = Http::getJson(apiUrl(m_config.api, path, params),
                                   {{"Accept", "application/json"}, {"Referer", (m_config.base + QLatin1Char('/')).toUtf8()}});
    const auto root = doc.object();
    if (root.value("result_code").toInt() != 0)
        throw Error(QStringLiteral("Kakao Page: %1").arg(str(root.value("message"))), 502);
    return root.value("result").toObject();
}

QList<Summary> KakaoPageSource::search(const QString &query) {
    const auto result = api(QStringLiteral("/api/v2/search/series"), {{"keyword", query},
                                                                       {"category_uid", "0"},
                                                                       {"is_complete", "false"},
                                                                       {"sort_type", "ACCURACY"},
                                                                       {"page", "0"},
                                                                       {"size", "25"}});
    QList<Summary> out;
    for (const auto &row : arr(result.value("list"))) {
        const auto m = row.toObject();
        // Comics only (11 is web novels), and not adult titles.
        const auto category = str(m.value("category"));
        if (str(m.value("type")) != QLatin1String("SERIES") || m.value("category_uid").toInt() == 11 ||
            m.value("age_grade").toInt() >= 19 || (category != QString::fromUtf8("웹툰") && category != QString::fromUtf8("만화")))
            continue;
        Summary item;
        item.title = str(m.value("title")).trimmed();
        const auto id = str(m.value("series_id"));
        if (item.title.isEmpty() || id.isEmpty())
            continue;
        item.url = QStringLiteral("%1/content/%2").arg(m_config.base, id);
        item.cover = image(str(m.value("thumbnail")), QStringLiteral("o1/dims/resize/384"));
        item.author = str(m.value("authors"));
        item.genres = str(m.value("sub_category"));
        item.type = category == QString::fromUtf8("만화") ? QStringLiteral("Manga") : QStringLiteral("Manhwa");
        item.source = key();
        out.append(item);
    }
    return out;
}

QList<Chapter> KakaoPageSource::parseEpisodes(const QJsonArray &rows, const QString &base, const QString &seriesId, const QString &seriesTitle) {
    static const QRegularExpression numbered(QString::fromUtf8(R"((\d+(?:\.\d+)?)\s*화)"));
    QList<Chapter> out;
    for (const auto &row : rows) {
        const auto o = row.toObject();
        const auto item = o.value("item").toObject();
        // Free to everyone; the viewer refuses the rest without an account.
        // Video teasers (SD01) do not play on the web.
        if (!item.value("is_free").toBool() || item.value("hidden").toBool() || item.value("age_grade").toInt() >= 19 ||
            str(item.value("slide_type")) == QLatin1String("SD01"))
            continue;
        const auto productId = str(item.value("product_id"));
        if (productId.isEmpty())
            continue;
        auto title = str(item.value("title")).trimmed();
        if (!seriesTitle.isEmpty() && title.startsWith(seriesTitle))
            title = title.mid(seriesTitle.size()).trimmed();
        Chapter ch;
        const auto m = numbered.match(title);
        // Episodes without a number (a prologue, a teaser) keep their place.
        ch.number = m.hasMatch() ? m.captured(1) : QStringLiteral("0.%1").arg(o.value("cursor_index").toInt(), 3, 10, QLatin1Char('0'));
        ch.title = title.isEmpty() ? comics::chapterLabel(QStringLiteral("ko"), ch.number) : title;
        ch.url = QStringLiteral("%1/content/%2/viewer/%3").arg(base, seriesId, productId);
        ch.slug = productId;
        ch.thumbnail = image(str(item.value("thumbnail")), QStringLiteral("th3"));
        ch.released = str(item.value("start_sale_dt"));
        out.append(ch);
    }
    return out;
}

QList<Chapter> KakaoPageSource::chapters(const QString &seriesUrl) {
    const auto id = kakaoSeriesId(seriesUrl);
    QList<Chapter> out;
    QString title;
    // A hundred at a time, each page after the last one's cursor.
    int cursor = 0;
    for (int page = 0; page < 60; ++page) {
        const auto result = api(QStringLiteral("/api/v2/content/product/list"),
                                {{"series_id", id},
                                 {"cursor_index", QString::number(cursor)},
                                 {"cursor_direction", page == 0 ? QStringLiteral("ANCHOR") : QStringLiteral("NEXT")},
                                 {"window_size", "100"}});
        if (title.isEmpty())
            title = str(result.value("series_item").toObject().value("title")).trimmed();
        const auto rows = arr(result.value("list"));
        out.append(parseEpisodes(rows, m_config.base, id, title));
        if (rows.isEmpty() || !result.value("has_next").toBool())
            break;
        cursor = rows.last().toObject().value("cursor_index").toInt();
    }
    // Pages can overlap; one entry per episode.
    QSet<QString> seen;
    QList<Chapter> unique;
    for (const auto &ch : out)
        if (!seen.contains(ch.slug)) {
            seen.insert(ch.slug);
            unique.append(ch);
        }
    out = unique;
    sortChapters(out);
    return out;
}

bool KakaoPageSource::readable(const QString &seriesUrl) {
    // A series with no free episode is no use without an account.
    const auto cacheKey = QStringLiteral("kakaopage-readable:") + kakaoSeriesId(seriesUrl);
    if (auto cached = Database::cacheGet(cacheKey))
        return *cached == "1";
    const bool ok = !chapters(seriesUrl).isEmpty();
    Database::cachePut(cacheKey, ok ? "1" : "0", 86400);
    return ok;
}

QList<Panel> KakaoPageSource::panels(const QString &chapterUrl) {
    const QUrl url(chapterUrl);
    const auto seriesId = kakaoSeriesId(chapterUrl);
    const auto productId = pathSegment(url, 3);
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/api/v1/viewer/data"), {{"series_id", seriesId}, {"product_id", productId}}),
                                   {{"Accept", "application/json"}, {"Referer", (m_config.base + QLatin1Char('/')).toUtf8()}});
    const auto root = doc.object();
    if (root.value("result_code").toInt() != 0)
        throw Error(QStringLiteral("Kakao Page shows this episode only after signing in."), 403);
    QList<Panel> out;
    for (const auto &row : arr(root.value("viewer_data").toObject().value("imageDownloadData").toObject().value("files"))) {
        const auto f = row.toObject();
        if (const auto src = str(f.value("secureUrl")); src.startsWith(QLatin1String("https://")))
            out.append({src, f.value("width").toInt(), f.value("height").toInt()});
    }
    if (out.isEmpty())
        throw Error(QStringLiteral("Kakao Page shows this episode only in its app."), 404);
    return out;
}

std::optional<Details> KakaoPageSource::details(const QString &seriesUrl) {
    const auto result = api(QStringLiteral("/api/v2/content/product/list"),
                            {{"series_id", kakaoSeriesId(seriesUrl)}, {"cursor_index", "0"}, {"cursor_direction", "ANCHOR"}, {"window_size", "1"}});
    const auto s = result.value("series_item").toObject();
    Details d;
    d.title = str(s.value("title")).trimmed();
    d.cover = image(str(s.value("thumbnail")), QStringLiteral("o1/dims/resize/384"));
    d.description = str(s.value("description")).trimmed();
    d.author = str(s.value("authors"));
    d.status = str(s.value("on_issue")) == QLatin1String("Y") ? QString::fromUtf8("연재중") : QString::fromUtf8("완결");
    if (const auto genre = str(s.value("sub_category")); !genre.isEmpty())
        d.genres.append(genre);
    return d;
}

// ---------------------------------------------------------------- Spanish sites: shared bits

namespace {
// "1.00" -> "1", "200.50" -> "200.5".
QString plainNumber(QString number) {
    number = number.trimmed();
    if (number.contains(QLatin1Char('.'))) {
        while (number.endsWith(QLatin1Char('0')))
            number.chop(1);
        if (number.endsWith(QLatin1Char('.')))
            number.chop(1);
    }
    return number.isEmpty() ? QStringLiteral("0") : number;
}

// A chapter title that only repeats "<series> Capítulo 12" says nothing.
QString chapterTitleOrEmpty(const QString &title) {
    static const QRegularExpression bare(QStringLiteral(R"((^|\s)cap[ií]tulo\s*[\d.,]+\s*$)"), QRegularExpression::CaseInsensitiveOption);
    const auto t = title.trimmed();
    return bare.match(t).hasMatch() ? QString() : t;
}

// "2026-05-09 07:11:04" (UTC) -> ISO 8601.
QString sqlDate(const QString &text) {
    const auto at = QDateTime::fromString(text.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!at.isValid())
        return text;
    return QDateTime(at.date(), at.time(), QTimeZone::UTC).toString(Qt::ISODate);
}

// Chapters several groups uploaded under one number: each says whose it is.
void nameGroups(QList<Chapter> &chapters, const QStringList &groups, const QString &language) {
    QHash<QString, int> counts;
    for (const auto &ch : chapters)
        ++counts[ch.number];
    for (int i = 0; i < chapters.size() && i < groups.size(); ++i) {
        auto &ch = chapters[i];
        if (counts.value(ch.number) < 2 || groups[i].isEmpty())
            continue;
        const auto title = ch.slug.section(QLatin1Char('|'), 1);
        ch.title = comics::chapterLabel(language, ch.number, title.isEmpty() ? groups[i] : QStringLiteral("%1 (%2)").arg(title, groups[i]));
    }
    for (auto &ch : chapters)
        ch.slug = ch.slug.section(QLatin1Char('|'), 0, 0);
}
} // namespace

// ---------------------------------------------------------------- ZonaTMO

namespace {
// Type ids: 207 doujinshi, 214 novel.
bool zonaTmoReadable(const QJsonObject &m, bool adult) {
    for (const auto &type : arr(m.value("types"))) {
        const auto id = type.toInt();
        if (id == 214 || (!adult && id == 207))
            return false;
    }
    return true;
}

QString zonaTmoCover(const QString &path, const QString &base) {
    if (path.isEmpty() || path.startsWith(QLatin1String("http")))
        return path;
    return base + QStringLiteral("/wp-content/uploads") + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path);
}

QString zonaTmoStatus(const QJsonArray &ids) {
    for (const auto &value : ids)
        switch (value.toInt()) {
        case 12: return QString::fromUtf8("En emisión");
        case 19: return QStringLiteral("Finalizado");
        case 174: return QStringLiteral("En pausa");
        case 198: return QStringLiteral("Cancelado");
        default: break;
        }
    return {};
}
} // namespace

QList<Summary> ZonaTmoSource::parseSearch(const QJsonObject &root, const QString &base, const QString &source, bool adult) {
    QList<Summary> out;
    for (const auto &row : arr(root.value("data").toObject().value("items"))) {
        const auto m = row.toObject();
        const auto slug = str(m.value("slug")).trimmed();
        Summary item;
        item.title = str(m.value("title")).trimmed();
        if (slug.isEmpty() || item.title.isEmpty() || !zonaTmoReadable(m, adult))
            continue;
        item.url = QStringLiteral("%1/manga/%2").arg(base, slug);
        item.cover = zonaTmoCover(str(m.value("cover")), base);
        item.source = source;
        // Its names in other languages ("synonyms") and spellings.
        for (const auto &synonym : arr(m.value("synonyms")))
            item.names.append(str(synonym.toObject().value("text")));
        for (const auto &alt : arr(m.value("alt_titles")))
            item.names.append(alt.isObject() ? str(alt.toObject().value("text")) : str(alt));
        item.names.append(str(m.value("subtitle")));
        item.names.removeAll(QString());
        item.names.removeAll(item.title);
        item.names.removeDuplicates();
        out.append(item);
    }
    return out;
}

QList<Summary> ZonaTmoSource::search(const QString &query) {
    const bool adult = m_config.options.value("adult").toBool(false);
    const auto page = [this, query](int n) {
        return Http::getJson(apiUrl(m_config.api, QStringLiteral("/listing/manga"), {{"page", QString::number(n)}, {"search", query}}),
                             jsonHeaders())
            .object();
    };
    const auto first = page(1);
    auto out = parseSearch(first, m_config.base, key(), adult);
    // Twelve a page, not ranked by how well they match: the series itself
    // can come after its spin-offs, so two more pages.
    const int pages = std::min(first.value("data").toObject().value("pagination").toObject().value("total_pages").toInt(1), 3);
    QList<std::function<QList<Summary>()>> jobs;
    for (int n = 2; n <= pages; ++n)
        jobs.append([this, page, n, adult] { return parseSearch(page(n), m_config.base, key(), adult); });
    for (const auto &result : parallel(jobs))
        if (result.ok())
            out.append(*result.value);
    return out;
}

QList<Chapter> ZonaTmoSource::parseChapters(const QJsonArray &rows, const QString &base, const QString &slug, const QString &language) {
    QList<Chapter> out;
    QStringList groups;
    QSet<int> seen;
    for (const auto &row : rows) {
        const auto c = row.toObject();
        const int id = c.value("id").toInt();
        const auto chapterSlug = str(c.value("slug"));
        if (chapterSlug.isEmpty() || seen.contains(id))
            continue;
        seen.insert(id);
        Chapter ch;
        ch.number = plainNumber(str(c.value("chapter_number")));
        const auto title = chapterTitleOrEmpty(str(c.value("title")));
        ch.title = comics::chapterLabel(language, ch.number, title);
        ch.url = QStringLiteral("%1/manga/%2/%3").arg(base, slug, chapterSlug);
        // The title rides along until nameGroups() has used it.
        ch.slug = chapterSlug + QLatin1Char('|') + title;
        ch.released = sqlDate(str(c.value("release_date")));
        out.append(ch);
        groups.append(str(c.value("group").toObject().value("name")).trimmed());
    }
    nameGroups(out, groups, language);
    return out;
}

QList<Chapter> ZonaTmoSource::chapters(const QString &seriesUrl) {
    const auto slug = pathSegment(QUrl(seriesUrl), 1);
    if (slug.isEmpty() || pathSegment(QUrl(seriesUrl), 0) != QLatin1String("manga"))
        throw Error(QStringLiteral("That is not a %1 series link.").arg(label()));
    const auto path = QStringLiteral("/single/manga/%1/chapters").arg(slug);
    const auto page = [this, path](int n) {
        return Http::getJson(apiUrl(m_config.api, path, {{"page", QString::number(n)}, {"postsPerPage", "100"}, {"order", "asc"}}),
                             jsonHeaders(), 20000)
            .object()
            .value("data")
            .toObject();
    };
    const auto first = page(1);
    QJsonArray rows = arr(first.value("items"));
    // 25 a page whatever is asked; the rest in parallel.
    const int pages = std::min(first.value("pagination").toObject().value("total_pages").toInt(1), 200);
    QList<std::function<QJsonArray()>> jobs;
    for (int n = 2; n <= pages; ++n)
        jobs.append([page, n] { return arr(page(n).value("items")); });
    for (const auto &result : parallel(jobs)) {
        if (!result.ok())
            throw Error(result.error);
        for (const auto &row : *result.value)
            rows.append(row);
    }
    auto out = parseChapters(rows, m_config.base, slug, m_config.language);
    sortChapters(out);
    return out;
}

QList<Panel> ZonaTmoSource::panels(const QString &chapterUrl) {
    const QUrl url(chapterUrl);
    const auto slug = pathSegment(url, 1), chapterSlug = pathSegment(url, 2);
    if (slug.isEmpty() || chapterSlug.isEmpty())
        throw Error(QStringLiteral("That is not a %1 chapter link.").arg(label()));
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/single/manga/%1/%2").arg(slug, chapterSlug)), jsonHeaders());
    const auto chapter = doc.object().value("data").toObject().value("chapter").toObject();
    const auto jit = str(chapter.value("jit"));
    QList<QPair<int, QString>> images;
    for (const auto &row : arr(chapter.value("images"))) {
        const auto image = row.toObject();
        if (const auto name = str(image.value("image_url")); !name.isEmpty())
            images.append({image.value("page_number").toInt(), name});
    }
    std::stable_sort(images.begin(), images.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    const auto cdn = QStringLiteral("https://cdn.%1/manga/%2/").arg(QUrl(m_config.base).host(), jit);
    QList<Panel> out;
    for (const auto &image : images)
        out.append({image.second.startsWith(QLatin1String("http")) ? image.second : cdn + image.second});
    return out;
}

std::optional<Details> ZonaTmoSource::details(const QString &seriesUrl) {
    const auto slug = pathSegment(QUrl(seriesUrl), 1);
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/single/manga/%1").arg(slug)), jsonHeaders());
    const auto m = doc.object().value("data").toObject();
    Details d;
    d.title = str(m.value("title")).trimmed();
    d.cover = zonaTmoCover(str(m.value("cover")), m_config.base);
    d.description = str(m.value("overview")).trimmed();
    d.status = zonaTmoStatus(arr(m.value("status")));
    d.author = names(arr(m.value("author"))).join(QStringLiteral(", "));
    return d;
}

// ---------------------------------------------------------------- Olympus

namespace {
// <base>/series/comic-<slug>#<id>
QString olympusId(const QString &url) {
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    const auto id = QUrl(url).fragment();
    return digits.match(id).hasMatch() ? id : QString();
}

QString olympusSlug(const QString &url) {
    const auto segment = pathSegment(QUrl(url), 1);
    return segment.startsWith(QLatin1String("comic-")) ? segment.mid(6) : QString();
}
} // namespace

QJsonArray OlympusSource::seriesList(bool fresh) {
    const auto cacheKey = QStringLiteral("olympus-series:") + m_config.base;
    if (!fresh)
        if (auto cached = Database::cacheGet(cacheKey))
            return QJsonDocument::fromJson(*cached).array();
    const auto doc = Http::getJson(QUrl(m_config.base + QStringLiteral("/api/series/list")), jsonHeaders(), 20000);
    QJsonArray comics;
    for (const auto &row : arr(doc.object().value("data")))
        if (str(row.toObject().value("type")) == QLatin1String("comic"))
            comics.append(row);
    Database::cachePut(cacheKey, QJsonDocument(comics).toJson(QJsonDocument::Compact), 3600);
    return comics;
}

QString OlympusSource::currentSlug(const QString &seriesUrl, bool fresh) {
    const auto id = olympusId(seriesUrl);
    if (!fresh || id.isEmpty())
        return olympusSlug(seriesUrl);
    for (const auto &row : seriesList(true))
        if (str(row.toObject().value("id")) == id)
            return str(row.toObject().value("slug"));
    throw Error(QStringLiteral("%1 no longer lists this series.").arg(label()), 404);
}

QList<Summary> OlympusSource::search(const QString &query) {
    // The site has no search; it lists every series at once.
    const auto wanted = normalizedTitle(query);
    QList<Summary> out;
    for (const auto &row : seriesList()) {
        const auto m = row.toObject();
        Summary item;
        item.title = str(m.value("name")).trimmed();
        if (item.title.isEmpty() || !normalizedTitle(item.title).contains(wanted))
            continue;
        item.url = QStringLiteral("%1/series/comic-%2#%3").arg(m_config.base, str(m.value("slug")), str(m.value("id")));
        item.cover = str(m.value("cover"));
        item.source = key();
        out.append(item);
        if (out.size() >= 30)
            break;
    }
    return out;
}

QList<Chapter> OlympusSource::chapters(const QString &seriesUrl) {
    const auto id = olympusId(seriesUrl);
    // An old slug answers 404: look the series up again by id.
    for (const bool fresh : {false, true}) {
        const auto slug = currentSlug(seriesUrl, fresh);
        const auto path = QStringLiteral("/api/series/%1/chapters").arg(slug);
        const auto page = [this, path](int n) {
            return Http::get(apiUrl(m_config.base, path, {{"page", QString::number(n)}, {"direction", "asc"}, {"type", "comic"}}),
                             jsonHeaders(), 20000);
        };
        const auto first = page(1);
        if (first.status == 404 && !fresh && !id.isEmpty())
            continue;
        if (!first.ok())
            throw Error(QStringLiteral("%1 responded with HTTP %2.").arg(label()).arg(first.status), first.status);
        const auto root = QJsonDocument::fromJson(first.body).object();
        QJsonArray rows = arr(root.value("data"));
        const int pages = std::min(root.value("meta").toObject().value("last_page").toInt(1), 100);
        QList<std::function<QJsonArray()>> jobs;
        for (int n = 2; n <= pages; ++n)
            jobs.append([page, n] {
                const auto response = page(n);
                if (!response.ok())
                    throw Error(QStringLiteral("HTTP %1").arg(response.status), response.status);
                return arr(QJsonDocument::fromJson(response.body).object().value("data"));
            });
        for (const auto &result : parallel(jobs)) {
            if (!result.ok())
                throw Error(result.error);
            for (const auto &row : *result.value)
                rows.append(row);
        }
        QList<Chapter> out;
        QSet<QString> seen;
        for (const auto &row : rows) {
            const auto c = row.toObject();
            const auto chapterId = str(c.value("id"));
            if (chapterId.isEmpty() || seen.contains(chapterId))
                continue;
            seen.insert(chapterId);
            Chapter ch;
            ch.number = plainNumber(str(c.value("name")));
            ch.title = chapterLabel(ch.number);
            ch.slug = chapterId;
            // The reader opens by chapter id; the series id finds its slug
            // after a rename.
            ch.url = QStringLiteral("%1/capitulo/%2/comic-%3#%4").arg(m_config.base, chapterId, slug, id);
            ch.released = str(c.value("published_at"));
            out.append(ch);
        }
        sortChapters(out);
        return out;
    }
    return {};
}

QList<Panel> OlympusSource::parsePanels(const QByteArray &html) {
    HtmlDocument doc(html);
    QList<Panel> out;
    for (const auto &img : doc.select(QStringLiteral("main.strip div.panel > img")))
        if (const auto url = img.attr("src"); url.startsWith(QLatin1String("https://")))
            out.append({url});
    return out;
}

QList<Panel> OlympusSource::panels(const QString &chapterUrl) {
    const QUrl url(chapterUrl);
    const auto chapterId = pathSegment(url, 1);
    const auto seriesId = url.fragment();
    for (const bool fresh : {false, true}) {
        const auto slug = fresh ? currentSlug(QStringLiteral("%1/series/comic-x#%2").arg(m_config.base, seriesId), true)
                                : pathSegment(url, 2).mid(6);
        // The chapter page forwards (a meta refresh) to the reader.
        auto response = Http::get(QUrl(QStringLiteral("%1/capitulo/%2/comic-%3").arg(m_config.base, chapterId, slug)), htmlHeaders());
        static const QRegularExpression refresh(QStringLiteral(R"(http-equiv="refresh"\s+content="\d+;\s*url=([^"]+))"),
                                                QRegularExpression::CaseInsensitiveOption);
        if (const auto m = refresh.match(QString::fromUtf8(response.body.left(2048))); response.ok() && m.hasMatch())
            response = Http::get(QUrl(m_config.base).resolved(QUrl(decodeEntities(m.captured(1)))), htmlHeaders());
        if (response.ok())
            if (auto out = parsePanels(response.body); !out.isEmpty())
                return out;
        if (fresh || seriesId.isEmpty())
            break;
    }
    throw Error(QStringLiteral("%1 did not show the pages of this chapter.").arg(label()));
}

std::optional<Details> OlympusSource::details(const QString &seriesUrl) {
    for (const bool fresh : {false, true}) {
        const auto response = Http::get(apiUrl(m_config.base, QStringLiteral("/api/series/%1").arg(currentSlug(seriesUrl, fresh)), {{"type", "comic"}}),
                                        jsonHeaders());
        if (response.status == 404 && !fresh && !olympusId(seriesUrl).isEmpty())
            continue;
        if (!response.ok())
            throw Error(QStringLiteral("%1 responded with HTTP %2.").arg(label()).arg(response.status), response.status);
        const auto m = QJsonDocument::fromJson(response.body).object().value("data").toObject();
        Details d;
        d.title = str(m.value("name")).trimmed();
        d.cover = str(m.value("cover"));
        d.description = str(m.value("summary")).trimmed();
        d.status = str(m.value("status").toObject().value("name"));
        for (const auto &genre : names(arr(m.value("genres"))))
            d.genres.append(genre.trimmed());
        d.author = str(m.value("team").toObject().value("name")).trimmed();
        return d;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- ZeroComics

QList<Summary> ZeroComicsSource::search(const QString &query) {
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/series"), {{"q", query}, {"page", "1"}}), jsonHeaders());
    const bool adult = m_config.options.value("adult").toBool(false);
    QList<Summary> out;
    for (const auto &row : arr(doc.object().value("data"))) {
        const auto m = row.toObject();
        const auto slug = str(m.value("slug"));
        Summary item;
        item.title = str(m.value("titulo")).trimmed();
        // The API lists a few broken entries without a slug.
        if (slug.isEmpty() || item.title.isEmpty() || (!adult && (m.value("adulto").toBool() || m.value("is_adult").toBool())))
            continue;
        item.url = QStringLiteral("%1/comics/%2").arg(m_config.base, slug);
        item.cover = str(m.value("portada"));
        item.type = str(m.value("tipo"));
        item.source = key();
        for (const auto &name : str(m.value("titulo_alternativo")).split(QRegularExpression(QStringLiteral("[,;/|]")), Qt::SkipEmptyParts))
            item.names.append(name.trimmed());
        out.append(item);
    }
    return out;
}

QJsonObject ZeroComicsSource::series(const QString &seriesUrl) {
    const auto slug = pathSegment(QUrl(seriesUrl), 1);
    if (slug.isEmpty() || pathSegment(QUrl(seriesUrl), 0) != QLatin1String("comics"))
        throw Error(QStringLiteral("That is not a %1 series link.").arg(label()));
    return Http::getJson(apiUrl(m_config.api, QStringLiteral("/series/%1").arg(slug)), jsonHeaders()).object().value("data").toObject();
}

QList<Chapter> ZeroComicsSource::chapters(const QString &seriesUrl) {
    const auto data = series(seriesUrl);
    const auto slug = str(data.value("slug"));
    QList<Chapter> out;
    for (const auto &row : arr(data.value("capitulos"))) {
        const auto c = row.toObject();
        const auto id = str(c.value("id"));
        if (id.isEmpty())
            continue;
        Chapter ch;
        ch.number = plainNumber(str(c.value("numero")));
        ch.title = chapterLabel(ch.number, str(c.value("titulo")).trimmed());
        ch.slug = id;
        ch.url = QStringLiteral("%1/comics/%2/capitulo-%3#%4").arg(m_config.base, slug, ch.number, id);
        ch.released = str(c.value("publicado_en"));
        out.append(ch);
    }
    sortChapters(out);
    return out;
}

QList<Panel> ZeroComicsSource::panels(const QString &chapterUrl) {
    const auto id = QUrl(chapterUrl).fragment();
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    if (!digits.match(id).hasMatch())
        throw Error(QStringLiteral("That is not a %1 chapter link.").arg(label()));
    const auto doc = Http::getJson(apiUrl(m_config.api, QStringLiteral("/capitulos/%1").arg(id)), jsonHeaders());
    QList<Panel> out;
    for (const auto &page : arr(doc.object().value("data").toObject().value("paginas")))
        if (const auto url = str(page); url.startsWith(QLatin1String("https://")))
            out.append({url});
    return out;
}

std::optional<Details> ZeroComicsSource::details(const QString &seriesUrl) {
    const auto m = series(seriesUrl);
    Details d;
    d.title = str(m.value("titulo")).trimmed();
    d.cover = str(m.value("portada"));
    d.description = str(m.value("sinopsis")).trimmed();
    d.status = str(m.value("estado"));
    d.genres = names(arr(m.value("generos")));
    d.author = str(m.value("grupo").toObject().value("nombre")).trimmed();
    return d;
}

// ---------------------------------------------------------------- Kagane

namespace {
std::mutex g_kaganeMutex;
qint64 g_kaganeNext = 0;
// Per site: the integrity token page requests need, and when it expires.
QHash<QString, QPair<QString, qint64>> g_kaganeIntegrity;

const QRegularExpression &kaganeId() {
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"),
        QRegularExpression::CaseInsensitiveOption);
    return uuid;
}

// The series id of <base>/series/<id>[/reader/<book id>], and the book id.
QPair<QString, QString> kaganeIds(const QString &link, const QString &label) {
    const QUrl url(link);
    const auto series = pathSegment(url, 1);
    const auto book = pathSegment(url, 2) == QLatin1String("reader") ? pathSegment(url, 3) : QString();
    if (pathSegment(url, 0) != QLatin1String("series") || !kaganeId().match(series).hasMatch() ||
        (!book.isEmpty() && !kaganeId().match(book).hasMatch()))
        throw Error(QStringLiteral("That is not a %1 link.").arg(label));
    return {series, book};
}

// Covers at the size the site shows them (the original is a lossless
// image of several megabytes).
QString kaganeImage(const QString &api, const QString &id) {
    return id.isEmpty() ? QString() : QStringLiteral("%1/image/%2/compressed").arg(api, id);
}

// "Chapter 3 - Volume 1 (group) {f}": the braces are the site's own notes.
// A title that already carries the number is shown as it is.
QString kaganeTitle(const QString &language, const QString &number, const QString &title) {
    static const QRegularExpression notes(QStringLiteral(R"((\s*\{[^{}]*\})+\s*$)"));
    const auto clean = QString(title).remove(notes).trimmed();
    const QRegularExpression numbered(QStringLiteral("(^|[^\\d.])%1($|[^\\d.])").arg(QRegularExpression::escape(number)));
    return !clean.isEmpty() && numbered.match(clean).hasMatch() ? clean : chapterLabel(language, number, clean);
}
} // namespace

QJsonObject KaganeSource::api(const QByteArray &method, const QString &path, const QList<QPair<QString, QString>> &params,
                              const QByteArray &body, const Headers &extra) {
    // The site asks clients to stay at about three requests a second.
    qint64 wait = 0;
    {
        std::lock_guard lock(g_kaganeMutex);
        const auto now = QDateTime::currentMSecsSinceEpoch();
        const auto at = std::max(now, g_kaganeNext);
        if (at - now > 8000)
            throw Error(QStringLiteral("%1 is busy. Try again in a moment.").arg(label()), 429);
        g_kaganeNext = at + 340;
        wait = at - now;
    }
    while (wait > 0 && !CancelScope::cancelled()) {
        QThread::msleep(std::min<qint64>(wait, 50));
        wait -= 50;
    }
    Request request;
    request.url = apiUrl(m_config.api, path, params);
    request.method = method;
    request.headers = jsonHeaders() + extra;
    if (method == "POST") {
        request.headers.append(QPair<QByteArray, QByteArray>("Content-Type", "application/json"));
        request.body = body.isEmpty() ? QByteArray("{}") : body;
    }
    const auto response = Http::send(request);
    if (!response.ok())
        throw Error(QStringLiteral("%1 answered %2.").arg(label()).arg(response.status), response.status);
    return response.json().object();
}

QString KaganeSource::integrityToken(bool fresh) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    {
        std::lock_guard lock(g_kaganeMutex);
        const auto cached = g_kaganeIntegrity.value(m_config.base);
        if (!fresh && !cached.first.isEmpty() && cached.second - 15000 > now)
            return cached.first;
    }
    Request request;
    request.url = QUrl(m_config.base + QStringLiteral("/api/integrity"));
    request.method = "POST";
    request.headers = jsonHeaders();
    request.headers.append(QPair<QByteArray, QByteArray>("Content-Type", "application/json"));
    const auto response = Http::send(request);
    const auto root = response.json().object();
    const auto token = str(root.value("token"));
    if (!response.ok() || token.isEmpty())
        throw Error(QStringLiteral("%1 did not open the chapter (%2).").arg(label()).arg(response.status), response.status);
    std::lock_guard lock(g_kaganeMutex);
    g_kaganeIntegrity.insert(m_config.base, {token, qint64(root.value("exp").toDouble()) * 1000});
    return token;
}

QByteArray KaganeSource::searchBody(const QString &query) const {
    QJsonObject body;
    if (!query.trimmed().isEmpty())
        body.insert("title", query.trimmed());
    body.insert("source_type", QJsonArray{"Official", "Unofficial", "Mixed"});
    QJsonArray languages = m_config.options.value("languages").toArray();
    if (languages.isEmpty())
        languages.append(m_config.language);
    body.insert("content_lang", languages);
    if (!m_config.options.value("adult").toBool(false)) {
        body.insert("age_rating", QJsonArray{"Safe", "FifteenPlus"});
        body.insert("is_pornographic", false);
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QList<Summary> KaganeSource::parseSearch(const QJsonObject &root) const {
    QList<Summary> out;
    for (const auto &row : arr(root.value("content"))) {
        const auto m = row.toObject();
        const auto id = str(m.value("series_id"));
        Summary item;
        item.title = str(m.value("title")).trimmed();
        if (!kaganeId().match(id).hasMatch() || item.title.isEmpty())
            continue;
        item.url = tagged(QStringLiteral("%1/series/%2").arg(m_config.base, id));
        item.cover = kaganeImage(m_config.api, str(m.value("cover_image_id")));
        item.type = str(m.value("format"));
        item.genres = names(arr(m.value("genre_list"))).join(QStringLiteral(", "));
        item.source = key();
        for (const auto &name : arr(m.value("alternate_titles")))
            if (const auto n = str(name).trimmed(); !n.isEmpty() && n != item.title && !item.names.contains(n))
                item.names.append(n);
        item.units = m.value("current_books").toInt();
        out.append(item);
    }
    // Kagane lists a series once per uploader (4 chapters here, 309 there):
    // of the ones named alike, the fullest first.
    QHash<QString, int> firstAt;
    for (int i = 0; i < out.size(); ++i)
        firstAt.insert(normalizedTitle(out[i].title), std::min(firstAt.value(normalizedTitle(out[i].title), i), i));
    std::stable_sort(out.begin(), out.end(), [&](const Summary &a, const Summary &b) {
        const auto na = normalizedTitle(a.title), nb = normalizedTitle(b.title);
        if (na != nb)
            return firstAt.value(na) < firstAt.value(nb);
        return a.units > b.units;
    });
    return out;
}

QList<Summary> KaganeSource::search(const QString &query) {
    return parseSearch(api("POST", QStringLiteral("/search/series"), {{"page", "0"}, {"size", "35"}}, searchBody(query)));
}

QList<LatestItem> KaganeSource::parseLatest(const QJsonObject &root) const {
    QList<LatestItem> out;
    for (const auto &row : arr(root.value("content"))) {
        const auto m = row.toObject();
        const auto id = str(m.value("series_id"));
        if (!kaganeId().match(id).hasMatch())
            continue;
        LatestItem item;
        item.source = key();
        item.sourceLabel = label();
        item.title = str(m.value("title")).trimmed();
        item.url = tagged(QStringLiteral("%1/series/%2").arg(m_config.base, id));
        item.cover = kaganeImage(m_config.api, str(m.value("cover_image_id")));
        const auto chapter = arr(m.value("latest_chapters")).at(0).toObject();
        if (const auto book = str(chapter.value("book_id")); kaganeId().match(book).hasMatch()) {
            item.chapterNumber = str(chapter.value("chapter_no"));
            item.chapterTitle = kaganeTitle(language(), item.chapterNumber, str(chapter.value("title")));
            item.chapterUrl = tagged(QStringLiteral("%1/series/%2/reader/%3").arg(m_config.base, id, book));
            item.publishedAt = str(chapter.value("created_at"));
        }
        if (!item.title.isEmpty())
            out.append(item);
    }
    return out;
}

QList<LatestItem> KaganeSource::latest(int limit) {
    const auto size = QString::number(std::clamp(limit, 12, 60));
    return parseLatest(api("POST", QStringLiteral("/search/series"), {{"page", "0"}, {"size", size}, {"sort", "updated_at,desc"}},
                           searchBody({})));
}

QList<Chapter> KaganeSource::parseChapters(const QJsonObject &index, const QString &seriesId) const {
    // Rows are arrays in the order "fields" names; groups are by index.
    QHash<QString, int> column;
    const auto fields = arr(index.value("fields"));
    for (int i = 0; i < fields.size(); ++i)
        column.insert(str(fields.at(i)), i);
    const auto groups = arr(index.value("groups"));
    const auto at = [&](const QJsonArray &row, const char *name) { return row.at(column.value(QLatin1String(name), -1)); };
    QList<Chapter> out;
    for (const auto &value : arr(index.value("books"))) {
        const auto row = value.toArray();
        const auto book = str(at(row, "book_id"));
        if (!kaganeId().match(book).hasMatch() || at(row, "page_count").toInt() <= 0)
            continue;
        Chapter ch;
        ch.number = str(at(row, "chapter_no")).trimmed();
        if (ch.number.isEmpty())
            ch.number = plainNumber(str(at(row, "sort_no")));
        ch.title = kaganeTitle(language(), ch.number, str(at(row, "title")));
        ch.url = tagged(QStringLiteral("%1/series/%2/reader/%3").arg(m_config.base, seriesId, book));
        ch.slug = book;
        if (const auto released = at(row, "released_at").toDouble(); released > 0)
            ch.released = QDateTime::fromSecsSinceEpoch(qint64(released), QTimeZone::UTC).toString(Qt::ISODate);
        QStringList by;
        for (const auto &ix : at(row, "group_ix").toArray())
            if (const auto name = str(groups.at(ix.toInt()).toObject().value("title")).trimmed(); !name.isEmpty())
                by.append(name);
        ch.scanlator = by.join(QStringLiteral(", "));
        out.append(ch);
    }
    sortChapters(out);
    return out;
}

QList<Chapter> KaganeSource::chapters(const QString &seriesUrl) {
    const auto id = kaganeIds(seriesUrl, label()).first;
    return parseChapters(api("GET", QStringLiteral("/series/%1/books/index").arg(id), {{"fields", "List"}}), id);
}

QList<Panel> KaganeSource::parsePanels(const QJsonObject &book, const QString &bookId) {
    const auto token = str(book.value("access_token"));
    const QUrl cache(str(book.value("cache_url")));
    if (token.isEmpty() || cache.scheme() != QLatin1String("https") || cache.host().isEmpty())
        throw Error(QStringLiteral("This Kagane chapter is unavailable."));
    QList<QPair<int, Panel>> pages;
    for (const auto &value : arr(book.value("manifest").toObject().value("pages"))) {
        const auto p = value.toObject();
        const auto page = str(p.value("page_id"));
        if (!kaganeId().match(page).hasMatch())
            continue;
        static const QRegularExpression extension(QStringLiteral("^[a-z0-9]{2,5}$"));
        auto ext = str(p.value("ext"));
        if (!extension.match(ext).hasMatch())
            ext = QStringLiteral("jxl");
        // The server sends JPEG to clients that do not ask for JPEG XL.
        QUrl url(QStringLiteral("%1://%2/api/v2/books/page/%3/%4.%5").arg(cache.scheme(), cache.authority(), bookId, page, ext));
        url.setQuery(QStringLiteral("token=") + QString::fromUtf8(QUrl::toPercentEncoding(token)));
        pages.append({p.value("page_no").toInt(), Panel{url.toString(QUrl::FullyEncoded), p.value("width").toInt(), p.value("height").toInt()}});
    }
    std::stable_sort(pages.begin(), pages.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QList<Panel> out;
    for (const auto &page : pages)
        out.append(page.second);
    return out;
}

QList<Panel> KaganeSource::panels(const QString &chapterUrl) {
    const auto book = kaganeIds(chapterUrl, label()).second;
    if (book.isEmpty())
        throw Error(QStringLiteral("That is not a %1 chapter link.").arg(label()));
    // A page request needs the site's integrity token (renewed every few
    // minutes); the answer carries the pages' own token (twenty minutes).
    for (int attempt = 0;; ++attempt) {
        try {
            const auto token = integrityToken(attempt > 0);
            return parsePanels(api("POST", QStringLiteral("/books/%1").arg(book), {{"is_datasaver", "false"}}, {},
                                   {{"x-integrity-token", token.toUtf8()}}),
                               book);
        } catch (const Error &e) {
            if (attempt > 0 || (e.status() != 401 && e.status() != 403))
                throw;
        }
    }
}

Details KaganeSource::parseDetails(const QJsonObject &m) const {
    Details d;
    d.title = str(m.value("title")).trimmed();
    QString cover;
    for (const auto &value : arr(m.value("series_covers"))) {
        const auto c = value.toObject();
        if (cover.isEmpty() || c.value("is_main").toBool())
            cover = str(c.value("image_id"));
        if (c.value("is_main").toBool())
            break;
    }
    d.cover = kaganeImage(m_config.api, cover);
    // Markdown on the site: links and emphasis become plain text.
    static const QRegularExpression link(QStringLiteral(R"(\[([^\]]*)\]\([^)\s]*\))"));
    static const QRegularExpression emphasis(QStringLiteral(R"((\*\*|__)(.+?)\1)"));
    d.description = str(m.value("description")).replace(link, QStringLiteral("\\1")).replace(emphasis, QStringLiteral("\\2")).trimmed();
    d.status = str(m.value("publication_status"));
    d.genres = names(arr(m.value("genres")), QStringLiteral("genre_name"));
    QStringList staff;
    for (const auto &value : arr(m.value("series_staff")))
        if (const auto name = str(value.toObject().value("name")).trimmed(); !name.isEmpty() && !staff.contains(name))
            staff.append(name);
    d.author = staff.join(QStringLiteral(", "));
    // The same title on AniList and MyAnimeList, from the site's links.
    static const QRegularExpression tracker(QStringLiteral(R"(^https?://(?:www\.)?(anilist\.co|myanimelist\.net)/manga/(\d+))"));
    for (const auto &value : arr(m.value("series_links"))) {
        const auto match = tracker.match(str(value.toObject().value("url")));
        if (match.hasMatch())
            d.trackerIds.insert(match.captured(1) == QLatin1String("anilist.co") ? QStringLiteral("anilist") : QStringLiteral("mal"),
                                match.captured(2).toInt());
    }
    return d;
}

std::optional<Details> KaganeSource::details(const QString &seriesUrl) {
    return parseDetails(api("GET", QStringLiteral("/series/%1").arg(kaganeIds(seriesUrl, label()).first)));
}

} // namespace sc::comics
