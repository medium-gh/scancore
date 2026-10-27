#include "Comics.h"
#include "sources/servers/Servers.h"

#include "App.h"
#include "core/QuietSites.h"
#include "Downloads.h"
#include "Library.h"
#include "Resolve.h"
#include "core/Database.h"
#include "core/ImageCache.h"
#include "core/Parallel.h"
#include "core/Text.h"
#include "extensions/ExtensionSource.h"
#include "sources/Manifest.h"
#include "sources/catalogue/Catalogue.h"
#ifdef SCANCORE_BUNDLED_SOURCES
#include "sources/bundled/comics/Engines.h"
#endif
#include "extensions/AnimeSource.h"
#include "sources/anime/Registry.h"
#include "sources/comics/Registry.h"
#include "sources/novels/Registry.h"

#include <QCache>
#include <optional>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QUrlQuery>

namespace sc {

using namespace sc::comics;

namespace {

QMutex g_searchMutex;
QCache<QString, QVariantMap> g_searches(40);
QHash<QString, qint64> g_searchTimes;

std::shared_ptr<Source> requireSource(const QString &url) {
    auto source = Registry::forUrl(url);
    if (!source)
        throw Error(QStringLiteral("SCANCORE does not support %1 yet.").arg(QUrl(url).host()));
    return source;
}

// Exact title, then prefix, then all words, then the rest, preserving each
// source's own order within a tier.
int relevance(const QString &title, const QString &query) {
    const auto t = normalizedTitle(title);
    const auto q = normalizedTitle(query);
    if (t == q)
        return 0;
    if (t.startsWith(q))
        return 1;
    const auto words = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (std::all_of(words.begin(), words.end(), [&](const QString &w) { return t.contains(w); }))
        return 2;
    return 3;
}

// Settings → Sources can limit "All sources" to one content language;
// empty (the default) searches every language. Files on this device are
// always searched.
QString searchLanguage() { return Database::pref(QStringLiteral("setting.sourceLanguages")).toString(); }

bool inSearchLanguage(const Source &source, const QString &language) {
    // Files on this device and the reader's own servers are in any language.
    return language.isEmpty() || source.key() == QLatin1String("local") || source.language() == QLatin1String("*") ||
           source.language().section(QLatin1Char('-'), 0, 0) == language;
}

QVariantList chaptersToUnits(const QList<Chapter> &chapters) {
    QVariantList units;
    for (const auto &c : chapters) {
        auto map = c.toMap();
        map.insert(QStringLiteral("key"), c.url);
        units.append(map);
    }
    return units;
}

} // namespace

namespace {

QVariantMap sourceRow(const std::shared_ptr<comics::Source> &source) {
    return QVariantMap{{"key", source->key()},
                               {"label", source->label()},
                               {"enabled", source->enabled()},
                               {"disabledReason", source->config().disabledReason},
                               {"nsfw", source->config().nsfw},
                               {"language", source->language()},
                               {"languageName", languageName(source->language())},
                               {"base", source->config().base},
                               {"hasLatest", source->hasLatest()},
                               {"canBrowse", source->canBrowse()},
                               {"browseLatest", source->hasLatestListing()},
                               {"extension", source->key().contains(QLatin1Char(':'))}};
}

} // namespace

QVariantList Comics::sources() const {
    QVariantList out;
    for (const auto &source : Registry::all())
        out.append(sourceRow(source));
    App::pinnedFirst(out);
    return out;
}

QVariantList Comics::builtinSources() const {
    QVariantList out;
    for (const auto &source : Registry::all())
        if (!source->key().contains(QLatin1Char(':')))
            out.append(sourceRow(source));
    App::pinnedFirst(out);
    return out;
}

QStringList Comics::sourceKeys() const {
    QStringList out;
    for (const auto &source : Registry::all())
        out.append(source->key());
    return out;
}

QVariantList Comics::languages() const {
    QVariantList out;
    QSet<QString> seen;
    for (const auto &source : Registry::all())
        if (source->enabled() && source->key() != QLatin1String("local") && !seen.contains(source->language())) {
            seen.insert(source->language());
            out.append(QVariantMap{{"value", source->language()}, {"label", languageName(source->language())}});
        }
    return out;
}

QString Comics::sourceLabel(const QString &key) const {
    auto source = Registry::byKey(key);
    return source ? source->label() : key;
}

QString Comics::sourceForUrl(const QString &url) const {
    auto source = Registry::forUrl(url);
    return source ? source->key() : QString();
}

QString Comics::seriesId(const QString &url) const {
    return Library::idFor(QStringLiteral("comic"), sourceForUrl(url), url);
}

Task *Comics::webUrl(const QString &url) {
    return Task::run([url]() -> QVariant {
        if (url.startsWith(QLatin1String("http")))
            return url;
        if (auto extension = std::dynamic_pointer_cast<ext::ExtensionSource>(Registry::forUrl(url)))
            return extension->webUrl(url);
        throw Error(QStringLiteral("This series has no page on the web."));
    });
}

QString Comics::imageReferer(const QString &url) const {
    auto source = Registry::forUrl(url);
    return source ? source->imageReferer() : QString();
}

void Comics::prefetch(const QVariantList &panels, const QString &referer, int count) {
    QStringList urls;
    for (const auto &value : panels.mid(0, count))
        urls.append(value.toMap().value(QStringLiteral("url")).toString());
    runInBackground([urls, referer] {
        for (const auto &url : urls)
            if (!url.startsWith(QLatin1String("file:")) && !url.startsWith(QLatin1String("content:")))
                try {
                    ImageCache::fetch(url, referer);
                } catch (const Error &) {
                }
    });
}

Task *Comics::search(const QString &query, const QString &only) {
    const auto q = query.simplified();
    // "extensions": the installed extensions' sources only (beside a
    // catalogue search, which has none of them).
    const bool extensionsOnly = only == QLatin1String("extensions");
    const auto language = only == QLatin1String("all") || extensionsOnly ? searchLanguage() : QString();
    const auto cacheKey = q.toLower() + QLatin1Char('|') + only + QLatin1Char('|') + language + QLatin1Char('|') + contentLanguage();
    {
        QMutexLocker lock(&g_searchMutex);
        if (auto *hit = g_searches.object(cacheKey);
            hit && QDateTime::currentSecsSinceEpoch() - g_searchTimes.value(cacheKey) < 180)
            return Task::resolved(*hit);
    }
    return Task::run([q, only, language, cacheKey, extensionsOnly]() -> QVariant {
        // Files on this device are listed without a search.
        if (q.isEmpty() && only != QLatin1String("local"))
            return QVariantMap{{"comics", QVariantList()}, {"providers", QVariantList()}};
        QList<std::shared_ptr<Source>> selected;
        for (const auto &source : Registry::all())
            if (source->enabled() && (only == QLatin1String("all") ? inSearchLanguage(*source, language)
                                      : extensionsOnly ? source->isExtension() && inSearchLanguage(*source, language)
                                                       : source->key() == only))
                selected.append(source);
        if (selected.isEmpty() && extensionsOnly)
            return QVariantMap{{"comics", QVariantList()}, {"providers", QVariantList()}};
        if (selected.isEmpty())
            throw Error(QStringLiteral("That source is unavailable."));

        // Many sites at once: a browser check waits for its button (for as
        // long as that source's call runs, past this search's time limit).
        const bool many = selected.size() > 1;
        QList<std::function<QList<Summary>()>> jobs;
        for (const auto &source : selected)
            jobs.append([source, q, many] {
                std::optional<QuietSite> quiet;
                if (many)
                    quiet.emplace(source->config().base);
                return source->search(q);
            });

        struct Ranked {
            int tier;
            bool preferred;
            int order;
            Summary item;
        };
        const auto wantedLanguage = contentLanguage();
        QList<Ranked> ranked;
        QList<QVariantMap> providerAt(selected.size());
        QSet<QString> seen;
        bool anyOk = false;
        // Equally good titles in the content language first.
        const auto snapshot = [&] {
            auto order = ranked;
            std::stable_sort(order.begin(), order.end(), [](const Ranked &a, const Ranked &b) {
                if (a.tier != b.tier)
                    return a.tier < b.tier;
                if (a.preferred != b.preferred)
                    return a.preferred;
                return a.order < b.order;
            });
            QVariantList comics;
            comics.reserve(order.size());
            for (const auto &r : order) {
                auto map = r.item.toMap();
                map.insert(QStringLiteral("id"), Library::idFor(QStringLiteral("comic"), r.item.source, r.item.url));
                map.insert(QStringLiteral("media"), QStringLiteral("comic"));
                map.insert(QStringLiteral("sourceUrl"), r.item.url);
                if (const auto source = Registry::byKey(r.item.source)) {
                    map.insert(QStringLiteral("sourceLabel"), source->label());
                    map.insert(QStringLiteral("referer"), source->imageReferer());
                } else {
                    map.insert(QStringLiteral("sourceLabel"), r.item.source);
                }
                comics.append(map);
            }
            QVariantList providers;
            int pending = 0;
            for (int i = 0; i < selected.size(); ++i) {
                if (providerAt[i].isEmpty()) {
                    ++pending;
                    providers.append(QVariantMap{{"source", selected[i]->key()}, {"label", selected[i]->label()}, {"pending", true}});
                } else {
                    providers.append(providerAt[i]);
                }
            }
            return QVariantMap{{"comics", comics}, {"providers", providers}, {"pending", pending}};
        };
        // Results show source by source as they come in, a few times a
        // second at most so the grid is not rebuilt for every answer.
        QElapsedTimer sinceReport;
        sinceReport.start();
        int answered = 0;
        parallelEach<QList<Summary>>(jobs, 20000, [&](int i, const Outcome<QList<Summary>> &r) {
            ++answered;
            providerAt[i] = QVariantMap{{"source", selected[i]->key()},
                                        {"label", selected[i]->label()},
                                        {"ok", r.ok()},
                                        {"count", r.ok() ? r.value->size() : 0},
                                        {"error", r.error},
                                        {"verifyUrl", r.verifyUrl}};
            if (r.ok()) {
                anyOk = true;
                int order = 0;
                for (const auto &item : *r.value) {
                    if (item.title.isEmpty() || item.url.isEmpty() || seen.contains(item.url))
                        continue;
                    seen.insert(item.url);
                    ranked.append({relevance(item.title, q), baseLanguage(selected[i]->language()) == wantedLanguage, order++, item});
                }
            }
            if (answered < selected.size() && r.ok() && !r.value->isEmpty() && (answered == 1 || sinceReport.elapsed() > 250)) {
                Task::report(snapshot());
                sinceReport.restart();
            }
        });
        // One source: its own reason (a browser check, a site that is down).
        if (!anyOk && selected.size() == 1 && !providerAt[0].value(QStringLiteral("error")).toString().isEmpty())
            throw Error(providerAt[0].value(QStringLiteral("error")).toString(), 0, providerAt[0].value(QStringLiteral("verifyUrl")).toString());
        if (!anyOk)
            throw Error(QStringLiteral("Comic sources are unavailable right now. Check your connection and try again."));
        const auto result = snapshot();
        QMutexLocker lock(&g_searchMutex);
        g_searches.insert(cacheKey, new QVariantMap(result));
        g_searchTimes.insert(cacheKey, QDateTime::currentSecsSinceEpoch());
        return result;
    });
}

namespace {

QVariantList entryMaps(const QList<catalogue::Entry> &entries) {
    QVariantList out;
    for (const auto &e : entries)
        out.append(e.toMap());
    return out;
}

} // namespace

QVariantMap Comics::catalogShelves(const QString &media, bool mature) {
    auto catalogue = catalogue::forMedia(catalogue::chosen(media), media);
    if (!catalogue)
        return QVariantMap{{"catalog", QString()}, {"shelves", QVariantList()}};
    const auto cacheKey = QStringLiteral("catalogue:%1:%2:%3").arg(catalogue->key(), media, mature ? "all" : "sfw");
    if (auto cached = Database::cacheGet(cacheKey))
        return QJsonDocument::fromJson(*cached).toVariant().toMap();
    QList<catalogue::Shelf> shelves;
    try {
        shelves = catalogue->shelves(media, mature);
    } catch (const Error &) {
        if (auto stale = Database::cacheGet(cacheKey, true))
            return QJsonDocument::fromJson(*stale).toVariant().toMap();
        throw;
    }
    QVariantList list;
    for (const auto &shelf : shelves)
        if (!shelf.entries.isEmpty())
            list.append(QVariantMap{{"key", shelf.key}, {"items", entryMaps(shelf.entries)}});
    const QVariantMap result{{"catalog", catalogue->key()}, {"label", catalogue->label()}, {"shelves", list}};
    Database::cachePut(cacheKey, QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact), 1800);
    return result;
}

QVariantMap Comics::catalogSearch(const QString &media, const QString &query, bool mature, const QVariantMap &filters) {
    auto catalogue = catalogue::forMedia(catalogue::chosen(media), media);
    if (!catalogue)
        throw Error(QStringLiteral("No catalogue is chosen. Pick one in Settings → Connections."));
    const auto cacheKey = QStringLiteral("catalogue|%1|%2|%3|%4|%5")
                              .arg(catalogue->key(), media, query.toLower())
                              .arg(mature)
                              .arg(QString::fromUtf8(QJsonDocument::fromVariant(filters).toJson(QJsonDocument::Compact)));
    {
        QMutexLocker lock(&g_searchMutex);
        if (auto *hit = g_searches.object(cacheKey);
            hit && QDateTime::currentSecsSinceEpoch() - g_searchTimes.value(cacheKey) < 180)
            return *hit;
    }
    const auto entries = catalogue->search(media, query, mature, filters);
    // Comics and novels pages read the list under their own name.
    QVariantMap result{{media == QLatin1String("novel") ? "books" : "comics", entryMaps(entries)},
                       {"providers", QVariantList()},
                       {"catalog", catalogue->key()},
                       {"label", catalogue->label()}};
    QMutexLocker lock(&g_searchMutex);
    g_searches.insert(cacheKey, new QVariantMap(result));
    g_searchTimes.insert(cacheKey, QDateTime::currentSecsSinceEpoch());
    return result;
}

namespace {
// MangaDex's codes for a content language's names.
[[maybe_unused]] QStringList dexLanguages(const QString &language) {
    if (language == QLatin1String("es"))
        return {QStringLiteral("es"), QStringLiteral("es-la")};
    if (language == QLatin1String("pt"))
        return {QStringLiteral("pt-br"), QStringLiteral("pt")};
    if (language == QLatin1String("zh"))
        return {QStringLiteral("zh"), QStringLiteral("zh-hk")};
    return {language};
}

// The catalogue's names, with the title's names in `language` (from
// MangaDex) right after the main one: sources in that language that miss
// the main name are asked for those next. Other languages' names follow,
// for recognising a match only.
QStringList withLocalNames(QStringList names, const QVariantMap &item, const QString &language) {
#ifndef SCANCORE_BUNDLED_SOURCES
    Q_UNUSED(item)
    Q_UNUSED(language)
    return names;
#else
    if (names.isEmpty() || language.isEmpty() || language == QLatin1String("en"))
        return names;
    auto dex = std::dynamic_pointer_cast<MangaDexSource>(Registry::byKey(QStringLiteral("mangadex")));
    if (!dex)
        return names;
    const auto ids = item.value("ids").toMap();
    QList<QPair<QString, QString>> other;
    try {
        other = dex->otherNames(names.first(), ids.value("anilist").toInt(), ids.value("mal").toInt());
    } catch (const Error &) {
        return names;
    }
    QStringList local, rest;
    const auto codes = dexLanguages(language);
    // Earlier codes' names first ("es" before "es-la").
    for (const auto &code : codes)
        for (const auto &[c, name] : other)
            if (c == code)
                local.append(name);
    for (const auto &[c, name] : other)
        if (!codes.contains(c))
            rest.append(name);
    QStringList out{names.first()};
    out += local;
    out += names.mid(1);
    out += rest;
    out.removeAll(QString());
    out.removeDuplicates();
    return out;
#endif
}
} // namespace

QStringList Comics::catalogNames(const QVariantMap &item) {
    const auto entry = catalogue::Entry::fromMap(item);
    QStringList names{entry.title};
    if (auto catalogue = catalogue::byKey(entry.catalog))
        names += catalogue->names(entry);
    else
        names += entry.titles;
    return names;
}

Task *Comics::catalog(const QString &query, const QVariantMap &filters) {
    const auto q = query.simplified();
    const bool mature = App::instance()->settings().value(QStringLiteral("showNsfwSources")).toBool();
    const auto f = App::plain(filters).toMap();
    return Task::run([q, mature, f]() -> QVariant { return catalogSearch(QStringLiteral("comic"), q, mature, f); });
}

namespace {

// One of Comick's lists, through the cache (a day).
QJsonArray comickList(const QString &path) {
    const auto key = QStringLiteral("comick-list|") + path;
    if (const auto hit = Database::cacheGet(key))
        return QJsonDocument::fromJson(*hit).array();
    const auto response = Http::get(QUrl(QStringLiteral("https://api.comick.dev") + path),
                                    {{"Accept", "application/json"}, {"Referer", "https://comick.dev/"}});
    if (!response.ok()) {
        if (const auto stale = Database::cacheGet(key, true))
            return QJsonDocument::fromJson(*stale).array();
        throw Error(QStringLiteral("Comick's lists are unavailable right now (%1).").arg(response.status), response.status);
    }
    const auto list = response.json().array();
    if (!list.isEmpty())
        Database::cachePut(key, QJsonDocument(list).toJson(QJsonDocument::Compact), 86400);
    return list;
}

} // namespace

Task *Comics::comickGenres() {
    return Task::run([]() -> QVariant {
        QVariantList out;
        for (const auto &value : comickList(QStringLiteral("/genre/"))) {
            const auto g = value.toObject();
            out.append(QVariantMap{{"slug", g.value("slug").toString()},
                                   {"name", g.value("name").toString()},
                                   {"group", g.value("group").toString()},
                                   {"count", g.value("comic_count").toInt()}});
        }
        return out;
    });
}

Task *Comics::comickTags(bool all) {
    return Task::run([all]() -> QVariant {
        QVariantList out;
        for (const auto &value : comickList(all ? QStringLiteral("/category/") : QStringLiteral("/category/popular"))) {
            const auto t = value.toObject();
            const auto slug = t.value("slug").toString();
            if (slug.isEmpty())
                continue;
            out.append(QVariantMap{{"slug", slug}, {"title", t.value("title").toString()}, {"count", t.value("comic_count").toInt()}});
        }
        return out;
    });
}

Task *Comics::catalogFeeds(bool mature) {
    return Task::run([mature]() -> QVariant { return catalogShelves(QStringLiteral("comic"), mature); });
}

QVariantList Comics::otherLanguages() const {
    QHash<QString, int> counts;
    const auto own = contentLanguage();
    for (const auto &source : Registry::all()) {
        const auto language = baseLanguage(source->language());
        if ((source->enabled() || source->config().languageOff) && source->key() != QLatin1String("local") && language != own)
            ++counts[language];
    }
    QVariantList out;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        out.append(QVariantMap{{"value", it.key()}, {"label", languageName(it.key())}, {"count", it.value()}});
    // English first (most series are there), then by how many sources.
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        if ((x.value("value") == QLatin1String("en")) != (y.value("value") == QLatin1String("en")))
            return x.value("value") == QLatin1String("en");
        return x.value("count").toInt() > y.value("count").toInt();
    });
    return out;
}

bool Comics::hasExtensions() const {
    for (const auto &source : Registry::all())
        if (source->enabled() && source->config().engine == QLatin1String("mihon"))
            return true;
    return false;
}

bool Comics::isExtension(const QString &sourceKey) const {
    const auto source = Registry::byKey(sourceKey);
    return source && source->config().engine == QLatin1String("mihon");
}

Task *Comics::resolve(const QVariantMap &item, const QString &only, const QString &scope) {
    return Task::run([item, only, scope]() -> QVariant {
        const auto id = item.value("catalogId").toString().isEmpty() ? item.value("hid").toString()
                                                                       : item.value("catalogId").toString();
        const auto language = only.isEmpty() ? searchLanguage() : baseLanguage(only);
        QList<std::shared_ptr<Source>> sources;
        QStringList keys;
        for (const auto &source : Registry::all())
            if (resolveAsks(scope, source->key(), source->enabled(), source->config().engine == QLatin1String("mihon"),
                            (source->enabled() || (!only.isEmpty() && source->config().languageOff)) &&
                                source->key() != QLatin1String("local") && inSearchLanguage(*source, language))) {
                sources.append(source);
                keys.append(source->key());
            }
        if (sources.isEmpty() && !scope.isEmpty() && scope != QLatin1String("extensions"))
            throw Error(QStringLiteral("That source is unavailable."));
        // Matches depend on the sources switched on (extensions come and go)
        // and the language settings.
        const auto sourcesHash = QCryptographicHash::hash(keys.join(QLatin1Char(',')).toUtf8(), QCryptographicHash::Md5).toHex().left(12);
        const auto cacheKey = QStringLiteral("resolve:comic%7:%1:%2:%3:%4:%5:%6")
                                  .arg(Manifest::version())
                                  .arg(QString::fromLatin1(sourcesHash), language, contentLanguage(), item.value("catalog").toString(), id,
                                       scope.isEmpty() ? QString() : QLatin1Char(':') + scope);
        if (!id.isEmpty())
            if (auto cached = Database::cacheGet(cacheKey))
                return QJsonDocument::fromJson(*cached).toVariant();
        const auto names = withLocalNames(catalogNames(item), item, language.isEmpty() ? contentLanguage() : language);
        if (std::all_of(names.begin(), names.end(), [](const QString &n) { return normalizedTitle(n).isEmpty(); }))
            throw Error(QStringLiteral("This title has no name SCANCORE's sources could list it under."));
        // Where AniList says it can be read, on sources that take such links.
        const auto ids = item.value("ids").toMap();
        int anilistId = ids.value("anilist").toInt();
        if (!anilistId && item.value("catalog").toString() == QLatin1String("anilist"))
            anilistId = id.toInt();
        QList<QUrl> links;
        for (const auto &link : catalogue::readingLinks(anilistId))
            links.append(QUrl(link));
        const auto cover = item.value("cover").toString();
        // The catalogue's cover where a source has none, and the referer
        // covers some sites only send to their own pages (or extensions).
        const auto dress = [&cover](QVariantList matches) {
            for (auto &match : matches) {
                auto map = match.toMap();
                if (map.value("cover").toString().isEmpty())
                    map.insert(QStringLiteral("cover"), cover);
                if (const auto source = Registry::byKey(map.value("source").toString()))
                    map.insert(QStringLiteral("referer"), source->imageReferer());
                match = map;
            }
            return matches;
        };
        const auto matches = dress(resolveAcross<std::shared_ptr<Source>>(
            sources, names, [](const std::shared_ptr<Source> &source, const QString &url) { return source->readable(url); },
            [&links](const std::shared_ptr<Source> &source) {
                for (const auto &link : links)
                    if (const auto url = source->seriesLink(link); !url.isEmpty())
                        return url;
                return QString();
            },
            // Matches show as sources answer, not after the slowest.
            [&](const QVariantList &sofar) {
                Task::report(QVariantMap{{"matches", dress(sofar)}, {"contentLanguage", contentLanguage()}, {"language", language}});
            }));
        const QVariantMap result{{"matches", matches}, {"contentLanguage", contentLanguage()}, {"language", language}};
        if (!id.isEmpty() && !matches.isEmpty())
            Database::cachePut(cacheKey, QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact), 86400);
        return result;
    });
}

QVariantList Comics::fetchUnits(const QString &url) { return chaptersToUnits(requireSource(url)->chapters(url)); }

Task *Comics::series(const QString &url, const QVariantMap &summary) {
    return Task::run([url, summary]() -> QVariant {
        auto source = requireSource(url);
        const auto id = Library::idFor(QStringLiteral("comic"), source->key(), url);

        QVariantMap series = summary;
        series.insert(QStringLiteral("id"), id);
        series.insert(QStringLiteral("media"), QStringLiteral("comic"));
        series.insert(QStringLiteral("source"), source->key());
        series.insert(QStringLiteral("sourceLabel"), source->label());
        series.insert(QStringLiteral("sourceUrl"), url);
        // Fill gaps from what is already stored (library or history).
        const auto stored = Library::instance()->seriesRow(id);
        for (auto it = stored.begin(); it != stored.end(); ++it)
            if (series.value(it.key()).toString().isEmpty() && !it.value().toString().isEmpty())
                series.insert(it.key(), it.value());

        const auto detailsKey = QStringLiteral("comic-details:") + url;
        std::optional<Details> cachedDetails;
        if (auto cached = Database::cacheGet(detailsKey)) {
            const auto o = QJsonDocument::fromJson(*cached).object();
            cachedDetails = Details{o.value("title").toString(),  o.value("cover").toString(),
                                    o.value("description").toString(), o.value("author").toString(),
                                    o.value("status").toString(), o.value("genres").toVariant().toStringList(),
                                    o.value("trackerIds").toObject().toVariantMap()};
        }

        QList<std::function<QVariant()>> jobs;
        jobs.append([source, url]() -> QVariant { return chaptersToUnits(source->chapters(url)); });
        if (!cachedDetails)
            jobs.append([source, url]() -> QVariant {
                const auto d = source->details(url);
                if (!d)
                    return {};
                return QVariantMap{{"title", d->title},   {"cover", d->cover},   {"description", d->description},
                                   {"author", d->author}, {"status", d->status}, {"genres", d->genres},
                                   {"trackerIds", d->trackerIds}};
            });
        const auto results = parallel(jobs);

        QVariantMap details;
        if (cachedDetails)
            details = QVariantMap{{"title", cachedDetails->title},       {"cover", cachedDetails->cover},
                                  {"description", cachedDetails->description}, {"author", cachedDetails->author},
                                  {"status", cachedDetails->status},     {"genres", cachedDetails->genres},
                                  {"trackerIds", cachedDetails->trackerIds}};
        else if (results.size() > 1 && results[1].ok() && results[1].value->isValid()) {
            details = results[1].value->toMap();
            Database::cachePut(detailsKey, QJsonDocument::fromVariant(details).toJson(QJsonDocument::Compact), 86400);
        }
        // AniList and MyAnimeList ids the source knows go with the series
        // (series.extra), next to a Comick catalogue entry if it has one;
        // tracking uses them before searching by title.
        if (const auto trackerIds = details.take(QStringLiteral("trackerIds")).toMap(); !trackerIds.isEmpty()) {
            auto extra = series.value(QStringLiteral("extra")).toMap();
            if (extra.isEmpty())
                extra = stored.value(QStringLiteral("extra")).toMap();
            for (auto it = trackerIds.begin(); it != trackerIds.end(); ++it)
                extra.insert(it.key(), it.value());
            series.insert(QStringLiteral("extra"), extra);
        }
        for (auto it = details.begin(); it != details.end(); ++it) {
            const bool empty = it.value().typeId() == QMetaType::QStringList ? it.value().toStringList().isEmpty()
                                                                               : it.value().toString().isEmpty();
            // A source's own cover/title wins over a search thumbnail only
            // when the summary had none.
            if (!empty && (series.value(it.key()).toString().isEmpty() || it.key() == QLatin1String("description") ||
                           it.key() == QLatin1String("genres") || it.key() == QLatin1String("status")))
                series.insert(it.key(), it.value());
        }

        QVariantMap result{{"series", series}};
        if (results[0].ok()) {
            const auto units = results[0].value->toList();
            Library::upsertSeries(series);
            Library::storeUnits(id, units);
            result.insert(QStringLiteral("chapters"), units);
            result.insert(QStringLiteral("offline"), false);
        } else {
            const auto cached = Library::cachedUnits(id);
            if (cached.isEmpty())
                throw Error(results[0].error, results[0].status, results[0].verifyUrl);
            result.insert(QStringLiteral("chapters"), cached);
            result.insert(QStringLiteral("offline"), true);
            result.insert(QStringLiteral("error"), results[0].error);
        }
        return result;
    });
}

Task *Comics::panels(const QString &chapterUrl, const QString &seriesId, const QString &unitKey) {
    return Task::run([chapterUrl, seriesId, unitKey]() -> QVariant {
        // Downloaded chapters are read from disk, online or not.
        if (!seriesId.isEmpty()) {
            const auto local = Downloads::localPanels(seriesId, unitKey.isEmpty() ? chapterUrl : unitKey);
            if (!local.isEmpty())
                return QVariantMap{{"panels", local}, {"referer", QString()}, {"offline", true}};
        }
        auto source = requireSource(chapterUrl);
        const auto panels = source->panels(chapterUrl);
        if (panels.isEmpty())
            throw Error(QStringLiteral("This chapter has no readable pages on %1. It may be locked or removed.")
                            .arg(source->label()));
        QVariantList list;
        for (const auto &p : panels)
            list.append(QVariantMap{{"url", p.url}, {"width", p.width}, {"height", p.height}});
        return QVariantMap{{"panels", list}, {"referer", source->imageReferer()}, {"offline", false}};
    });
}

Task *Comics::browse(const QString &key, const QString &listing, int page, const QString &query, const QVariantList &filters) {
    return Task::run([key, listing, page, query, filters]() -> QVariant {
        auto source = Registry::byKey(key);
        if (!source || !source->canBrowse())
            throw Error(QStringLiteral("That source cannot be browsed."));
        if (!source->enabled())
            throw Error(source->config().disabledReason.isEmpty() ? QStringLiteral("That source is switched off.")
                                                                  : source->config().disabledReason);
        // Popular and latest pages change slowly; a search with filters is
        // asked afresh.
        const bool cached = listing != QLatin1String("search");
        const auto cacheKey = QStringLiteral("browse:%1:%2:%3").arg(key, listing).arg(page);
        if (cached)
            if (auto hit = Database::cacheGet(cacheKey))
                return QJsonDocument::fromJson(*hit).toVariant();
        const auto result = source->browse(listing, page, query, QJsonArray::fromVariantList(filters));
        QVariantList items;
        for (const auto &item : result.items) {
            auto map = item.toMap();
            map.insert(QStringLiteral("id"), Library::idFor(QStringLiteral("comic"), key, item.url));
            map.insert(QStringLiteral("media"), QStringLiteral("comic"));
            map.insert(QStringLiteral("sourceUrl"), item.url);
            map.insert(QStringLiteral("sourceLabel"), source->label());
            map.insert(QStringLiteral("referer"), source->imageReferer());
            items.append(map);
        }
        const QVariantMap out{{"items", items}, {"hasNextPage", result.hasNextPage}};
        if (cached && !items.isEmpty())
            Database::cachePut(cacheKey, QJsonDocument::fromVariant(out).toJson(QJsonDocument::Compact), 900);
        return out;
    });
}

Task *Comics::filters(const QString &key) {
    return Task::run([key]() -> QVariant {
        auto source = Registry::byKey(key);
        if (!source)
            throw Error(QStringLiteral("That source is not installed."));
        return source->filters().toVariantList();
    });
}

Task *Comics::latest(int limit) {
    return Task::run([limit]() -> QVariant {
        const auto cacheKey = QStringLiteral("comic-latest:%1").arg(limit);
        if (auto cached = Database::cacheGet(cacheKey))
            return QJsonDocument::fromJson(*cached).toVariant();
        QList<std::shared_ptr<Source>> selected;
        for (const auto &source : Registry::all())
            if (source->enabled() && source->hasLatest())
                selected.append(source);
        QList<std::function<QList<LatestItem>()>> jobs;
        for (const auto &source : selected)
            jobs.append([source, limit] { return source->latest(limit); });
        const auto results = parallel(jobs);
        QList<LatestItem> all;
        QVariantList providers;
        for (int i = 0; i < results.size(); ++i) {
            providers.append(QVariantMap{{"source", selected[i]->key()}, {"ok", results[i].ok()}, {"error", results[i].error}});
            if (results[i].ok())
                all.append(*results[i].value);
        }
        if (all.isEmpty() && !results.isEmpty()) {
            if (auto stale = Database::cacheGet(cacheKey, true))
                return QJsonDocument::fromJson(*stale).toVariant();
            throw Error(QStringLiteral("Latest updates are unavailable right now."));
        }
        std::stable_sort(all.begin(), all.end(), [](const LatestItem &a, const LatestItem &b) {
            return QDateTime::fromString(a.publishedAt, Qt::ISODate) > QDateTime::fromString(b.publishedAt, Qt::ISODate);
        });
        QVariantList updates;
        QSet<QString> seen;
        for (const auto &item : all) {
            if (seen.contains(item.url))
                continue;
            seen.insert(item.url);
            auto map = item.toMap();
            map.insert(QStringLiteral("id"), Library::idFor(QStringLiteral("comic"), item.source, item.url));
            map.insert(QStringLiteral("media"), QStringLiteral("comic"));
            map.insert(QStringLiteral("sourceUrl"), item.url);
            updates.append(map);
            if (updates.size() >= limit)
                break;
        }
        QVariantMap result{{"updates", updates}, {"providers", providers}};
        Database::cachePut(cacheKey, QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact), 300);
        return result;
    });
}

QVariantList Comics::servers() const {
    QVariantList out;
    for (const auto &s : servers::load())
        out.append(QVariantMap{{"id", s.id}, {"kind", s.kind}, {"label", s.label}, {"url", s.url}, {"username", s.username},
                               {"hasPassword", !s.password.isEmpty()}, {"hasKey", !s.apiKey.isEmpty()},
                               {"source", (s.kind == QLatin1String("jellyfin") ? QStringLiteral("aniyomi:server-") : QStringLiteral("server-")) + s.id}});
    return out;
}

QString Comics::saveServer(const QVariantMap &map) {
    auto list = servers::load();
    servers::Server server;
    server.id = map.value(QStringLiteral("id")).toString();
    if (server.id.isEmpty())
        server.id = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    for (const auto &old : std::as_const(list))
        if (old.id == server.id)
            server = old;
    server.kind = map.value(QStringLiteral("kind")).toString();
    server.url = map.value(QStringLiteral("url")).toString().trimmed();
    if (!server.url.contains(QLatin1String("://")))
        server.url.prepend(QStringLiteral("http://"));
    server.label = map.value(QStringLiteral("label")).toString().trimmed();
    if (server.label.isEmpty())
        server.label = QUrl(server.url).host();
    server.username = map.value(QStringLiteral("username")).toString().trimmed();
    if (const auto password = map.value(QStringLiteral("password")).toString(); !password.isEmpty())
        server.password = password;
    if (const auto key = map.value(QStringLiteral("apiKey")).toString().trimmed(); !key.isEmpty())
        server.apiKey = key;
    bool replaced = false;
    for (auto &old : list)
        if (old.id == server.id) {
            old = server;
            replaced = true;
        }
    if (!replaced)
        list.append(server);
    servers::save(list);
    comics::Registry::rebuild();
    novels::Registry::rebuild();
    anime::Registry::rebuild();
    emit App::instance()->manifestChanged();
    return server.id;
}

void Comics::removeServer(const QString &id) {
    auto list = servers::load();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const servers::Server &s) { return s.id == id; }), list.end());
    servers::save(list);
    comics::Registry::rebuild();
    novels::Registry::rebuild();
    anime::Registry::rebuild();
    emit App::instance()->manifestChanged();
}

Task *Comics::testServer(const QString &id) {
    return Task::run([id]() -> QVariant {
        // Jellyfin: its shows and films.
        if (auto video = anime::Registry::byKey(QStringLiteral("aniyomi:server-") + id)) {
            servers::authHeaders(*servers::byId(id), true);
            const auto page = video->browse(QStringLiteral("latest"), 1);
            return QVariantMap{{"ok", true}, {"count", int(page.items.size())}, {"more", page.hasNextPage}, {"video", true}};
        }
        auto source = comics::Registry::byKey(QStringLiteral("server-") + id);
        if (!source)
            throw Error(tr("That server is not saved."));
        servers::authHeaders(*servers::byId(id), true);
        // Kavita refuses an empty search.
        const auto found = source->search(servers::byId(id)->kind == QLatin1String("kavita") ? QStringLiteral("a") : QString());
        return QVariantMap{{"ok", true}, {"count", int(found.size())}};
    });
}

} // namespace sc
