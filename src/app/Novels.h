#pragma once

#include "core/Task.h"

#include <QObject>
#include <QQmlEngine>

namespace sc {

class Novels : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Novels(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QVariantList sources() const;
    Q_INVOKABLE QString seriesId(const QString &url) const;

    // {shelves: [{title, books}]}
    // One source by itself (plugins), as Comics.browse and Comics.filters.
    Q_INVOKABLE sc::Task *browse(const QString &source, const QString &listing, int page,
                                 const QString &query = QString(), const QVariantList &filters = {});
    Q_INVOKABLE sc::Task *filters(const QString &source);
    Q_INVOKABLE QString imageReferer(const QString &url) const;
    // The book's page on its site, for "Open on the website".
    Q_INVOKABLE sc::Task *webUrl(const QString &url);
    Q_INVOKABLE sc::Task *discover();
    // Novels in the novel catalogue the user chose (AniList or MyAnimeList): {books: [{catalog, catalogId, title, titles, cover, ...}]}.
    Q_INVOKABLE sc::Task *catalog(const QString &query);
    // {catalog, label, shelves: [{key, items, more, periods}]}
    Q_INVOKABLE sc::Task *catalogFeeds(bool mature = false);
    // A shelf's whole list a page at a time, as Comics.catalogList.
    Q_INVOKABLE sc::Task *catalogList(const QString &key, int page, const QString &period = {});
    // Where a catalogue novel can be read, as Comics.resolve (same scopes).
    Q_INVOKABLE sc::Task *resolve(const QVariantMap &item, const QString &scope = QString());
    // Whether an LNReader plugin is installed and on.
    Q_INVOKABLE bool hasExtensions() const;
    Q_INVOKABLE bool isExtension(const QString &sourceKey) const;
    // {books, providers}
    Q_INVOKABLE sc::Task *search(const QString &query, const QString &source = QStringLiteral("all"));
    // {series, chapters, offline}
    Q_INVOKABLE sc::Task *book(const QString &url, const QVariantMap &summary = {});
    // {title, blocks: [{kind, text, html}], offline}
    Q_INVOKABLE sc::Task *chapter(const QString &chapterUrl, const QString &seriesId = QString());

    static QVariantList fetchUnits(const QString &url);
    static QVariantMap fetchChapter(const QString &chapterUrl);
};

} // namespace sc
