#pragma once

#include "core/Task.h"

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

namespace sc {

// Brings a library in from elsewhere: a Mihon or Tachiyomi backup, an
// AniList list, a MyAnimeList export, or a connected Comick account. Anime arrive whole (they are found
// by their AniList id). Comics come with their chapters read when their
// site is one SCANCORE reads, and otherwise wait in a list of titles to
// find, which `matchAll` searches every source for.
class Importer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // [{title, media, read, anilistId, malId}] still without a source.
    Q_PROPERTY(QVariantList unmatched READ unmatched NOTIFY changed)

public:
    static Importer *instance();
    static Importer *create(QQmlEngine *, QJSEngine *) { return instance(); }

    QVariantList unmatched() const;

    // Each returns {imported, unmatched, progress} (progress: chapters and
    // episodes marked read).
    // What a Mihon, Tachiyomi or Aniyomi backup holds and needs: {comics,
    // anime, lists: [names], repositories: [{url, name, added, count,
    // error}], extensions: [{kind, pkg, name, series}] not installed that its
    // series came from (and, with series 0, the other sources set up in that
    // app), unknownSources (series no repository here offers)}.
    Q_INVOKABLE sc::Task *previewMihon(const QUrl &file);
    // A Mihon, Tachiyomi or Aniyomi backup (.tachibk, .proto.gz). Series
    // from an extension installed here are restored onto it exactly (its
    // own links, chapters and episodes read or watched with when, bookmarks,
    // the ones part-way, date added, list, chapter list and reader
    // settings, trackers), and so are anime whose extension a repository
    // here offers; other anime are found on AniList. The
    // backup's lists are made in its order. `prepare` first adds
    // repositories ([urls]) and installs extensions ([{kind, pkg}]), as
    // previewMihon offers them. Also returns {lists, inSeveral,
    // repositoriesAdded, extensionsInstalled}.
    Q_INVOKABLE sc::Task *importMihon(const QUrl &file, const QVariantMap &prepare = {});
    // An LNReader backup (.zip): novels onto the same LNReader plugin.
    Q_INVOKABLE sc::Task *importLNReader(const QUrl &file);
    Q_INVOKABLE sc::Task *importAniList(const QString &user);
    Q_INVOKABLE sc::Task *importMal(const QUrl &file);
    // The library of the Comick account connected in Settings.
    Q_INVOKABLE sc::Task *importComick();
    // Writes the comics from extensions (Mihon's sources) to a Mihon backup,
    // with categories and chapters read. {exported, skipped}
    Q_INVOKABLE sc::Task *exportMihon(const QUrl &file);
    // Searches every comic or novel source for the waiting titles, and adds
    // the ones found under exactly that title. {matched, left}
    Q_INVOKABLE sc::Task *matchAll();
    Q_INVOKABLE void forget(const QString &title);
    Q_INVOKABLE void forgetAll();

signals:
    void changed();

private:
    explicit Importer(QObject *parent = nullptr);
    // Tells the library, sync and this list about what a task brought in.
    sc::Task *finish(sc::Task *task);
};

} // namespace sc
