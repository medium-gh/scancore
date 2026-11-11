#pragma once

#include "core/Task.h"

#include <QObject>
#include <QQmlEngine>

namespace sc {

class Comics : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Comics(QObject *parent = nullptr) : QObject(parent) {}

    // [{key, label, enabled, disabledReason, nsfw, hasLatest}]
    Q_INVOKABLE QVariantList sources() const;
    // The same for the built-in sources only (not extensions', of which
    // readers have hundreds), and every source's key.
    Q_INVOKABLE QVariantList builtinSources() const;
    Q_INVOKABLE QStringList sourceKeys() const;
    // The content languages of the enabled sources: [{value, label}].
    Q_INVOKABLE QVariantList languages() const;
    Q_INVOKABLE QString sourceLabel(const QString &key) const;
    Q_INVOKABLE QString sourceForUrl(const QString &url) const;
    Q_INVOKABLE QString seriesId(const QString &url) const;

    // {comics: [...], providers: [{source, label, ok, count, error}]}
    Q_INVOKABLE sc::Task *search(const QString &query, const QString &source = QStringLiteral("all"));
    // Titles from the comic catalogue the user chose (Comick, AniList or
    // MyAnimeList), which knows far more series and their other
    // names than any one source: {comics: [{catalog, catalogId, hid, title,
    // titles, cover, type, status, year, ids, ...}], catalog, label}.
    // `filters`: Comick's search filters (Catalogue.h, comickQuery); with
    // them an empty query browses. `catalogKey`: a catalogue other than the
    // chosen one ("comick" while Comick's filters are on).
    Q_INVOKABLE sc::Task *catalog(const QString &query, const QVariantMap &filters = {}, const QString &catalogKey = {});
    // Comick's genres ([{slug, name, group, count}]) and tags ([{slug,
    // title, count}]: the 300 most used, or with `all` every one, about
    // 9,500), cached for a day.
    Q_INVOKABLE sc::Task *comickGenres();
    Q_INVOKABLE sc::Task *comickTags(bool all = false);
    // That catalogue's shelves: {catalog, label, shelves: [{key, items}]}.
    Q_INVOKABLE sc::Task *catalogFeeds(bool mature = false);
    // Where a catalogue title can be read: the series on SCANCORE's sources
    // listed under any of its names, content language first, without
    // taken-down ones. {matches: [{source, sourceLabel, language, preferred,
    // url, title, cover}], contentLanguage}
    // Finds a catalogue title on the sources: those "All sources" searches,
    // or with `language`, every source in that language (built-in ones
    // switched off for their language too).
    // Only built-in sources by default: one tap must not wait on every
    // installed extension. `scope` "extensions": every installed extension,
    // in any language (moving a series); a source key: that source alone
    // (an extension the reader picked).
    Q_INVOKABLE sc::Task *resolve(const QVariantMap &item, const QString &language = QString(), const QString &scope = QString());
    // Whether an extension for comics is installed and on.
    Q_INVOKABLE bool hasExtensions() const;
    Q_INVOKABLE bool isExtension(const QString &sourceKey) const;
    // Languages other than the content language that have comic sources:
    // [{value, label, count}], English first, then most sources first.
    Q_INVOKABLE QVariantList otherLanguages() const;
    // {series: {...}, chapters: [...], offline: bool, error?: string}
    Q_INVOKABLE sc::Task *series(const QString &url, const QVariantMap &summary = {});
    // {panels: [{url, width, height}], referer, offline}
    Q_INVOKABLE sc::Task *panels(const QString &chapterUrl, const QString &seriesId = QString(),
                                 const QString &unitKey = QString());
    // One source by itself, a page at a time: "popular", "latest" or
    // "search" (with `query` and the states of its filters).
    // {items: [...], hasNextPage}
    Q_INVOKABLE sc::Task *browse(const QString &source, const QString &listing, int page,
                                 const QString &query = QString(), const QVariantList &filters = {});
    // Its search filters as its extension describes them: [{type, name,
    // values, state, filters}] (header, separator, select, text, checkbox,
    // tristate, sort, group).
    Q_INVOKABLE sc::Task *filters(const QString &source);
    // {updates: [...], providers: [...]}
    Q_INVOKABLE sc::Task *latest(int limit = 40);

    Q_INVOKABLE QString imageReferer(const QString &url) const;
    // The reader's own servers (Settings → Sources → Your servers):
    // [{id, kind, label, url, username, hasPassword, hasKey}] (no secrets).
    Q_INVOKABLE QVariantList servers() const;
    // Adds or changes one ({id?, kind, label, url, username, password,
    // apiKey}; an empty password or key keeps the saved one). Returns its id.
    Q_INVOKABLE QString saveServer(const QVariantMap &server);
    Q_INVOKABLE void removeServer(const QString &id);
    // Logs in and searches once: {ok, count} or fails with why; Jellyfin
    // also {video: true, more} (its first page of shows and films).
    Q_INVOKABLE sc::Task *testServer(const QString &id);
    // The series' page on its site, for "Open on the website".
    Q_INVOKABLE sc::Task *webUrl(const QString &url);
    // Warms the image cache for the first pages of an upcoming chapter.
    Q_INVOKABLE void prefetch(const QVariantList &panels, const QString &referer, int count = 6);

    // Catalogue helpers shared with Novels (blocking).
    static QVariantMap catalogShelves(const QString &media, bool mature);
    static QVariantMap catalogSearch(const QString &media, const QString &query, bool mature,
                                     const QVariantMap &filters = {}, const QString &catalogKey = {});
    static QStringList catalogNames(const QVariantMap &item);

    // Chapter list as library units ({key, number, title, url, released}).
    static QVariantList fetchUnits(const QString &url);
};

} // namespace sc
