#pragma once

#include "core/Task.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

namespace sc {

// Library, reading/watching progress and history for every media type,
// stored in the local database. Series are identified as
// "<media>:<source>:<source url or id>".
class Library : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Bumped on every change so bindings that read the library re-evaluate.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // Only the lists (names, order, hidden ones): what list pickers follow,
    // as revision moves on every page read.
    Q_PROPERTY(int listsRevision READ listsRevision NOTIFY listsChanged)
    Q_PROPERTY(int unreadUpdates READ unreadUpdates NOTIFY changed)

public:
    static Library *instance();
    static Library *create(QQmlEngine *, QJSEngine *) { return instance(); }

    int revision() const { return m_revision; }
    int listsRevision() const { return m_listsRevision; }
    int unreadUpdates() const;

    Q_INVOKABLE static QString idFor(const QString &media, const QString &source, const QString &key);

    Q_INVOKABLE bool isSaved(const QString &seriesId) const;
    Q_INVOKABLE QString category(const QString &seriesId) const;
    Q_INVOKABLE void save(const QVariantMap &series, const QString &category = QStringLiteral("reading"));
    Q_INVOKABLE void remove(const QString &seriesId);
    // reading (watching, for anime), planned, paused (on hold), completed,
    // dropped.
    Q_INVOKABLE void setCategory(const QString &seriesId, const QString &category);
    // The user's own lists (issue #9): [{id, name}] in their order.
    // Removing one moves its series to reading.
    Q_INVOKABLE QVariantList customCategories() const { return storedCategories(); }
    Q_INVOKABLE QString addCategory(const QString &name);
    // Built-in lists keep their ids (trackers, sync and backups rely on
    // them); renaming one changes only its display name, and an empty name
    // gives it back its own.
    Q_INVOKABLE void renameCategory(const QString &id, const QString &name);
    Q_INVOKABLE void removeCategory(const QString &id);
    // Moves a list (built-in or own) `by` places among all lists; with
    // skipHidden, hidden ones are passed over as the library does not show
    // them.
    Q_INVOKABLE void moveCategory(const QString &id, int by, bool skipHidden = false);
    // A hidden list leaves the library's chips and pickers; its series stay
    // on it (and under All).
    Q_INVOKABLE void setCategoryHidden(const QString &id, bool hidden);
    // How the lists are shown: {order: [every list id], hidden: [ids],
    // names: {builtinId: name}}.
    Q_INVOKABLE QVariantMap listLayout() const;
    Q_INVOKABLE bool isFavourite(const QString &seriesId) const;
    Q_INVOKABLE void setFavourite(const QString &seriesId, bool favourite);
    Q_INVOKABLE void markSeen(const QString &seriesId);

    // brief: only what the library page shows (no description, genres,
    // extra or reading position).
    Q_INVOKABLE sc::Task *entries(const QString &media = QString(), bool brief = false);
    Q_INVOKABLE sc::Task *history(const QString &media = QString(), int limit = 60);
    Q_INVOKABLE sc::Task *continueItems(const QString &media, int limit = 12);
    Q_INVOKABLE void forget(const QString &seriesId);
    Q_INVOKABLE void clearHistory(const QString &media = QString());

    Q_INVOKABLE void recordProgress(const QVariantMap &series, const QVariantMap &unit, const QVariantMap &position,
                                    double percent, bool completed);
    Q_INVOKABLE void markUnits(const QVariantMap &series, const QVariantList &units, bool completed);
    Q_INVOKABLE QVariantMap progressMap(const QString &seriesId) const;
    Q_INVOKABLE QVariantMap lastProgress(const QString &seriesId) const;
    Q_INVOKABLE QVariantMap seriesRow(const QString &seriesId) const;
    // A stored cover that no longer loads (sites move their images): asks
    // the comic's source for its details again, once per series and run,
    // and stores the new cover.
    Q_INVOKABLE void refreshCover(const QString &seriesId);
    // The chapter list saved the last time the series was opened, so its
    // page can show it before the site answers.
    Q_INVOKABLE QVariantList storedUnits(const QString &seriesId) const { return cachedUnits(seriesId); }

    // Bookmarked chapters: {unitKey: true}.
    Q_INVOKABLE QVariantMap bookmarks(const QString &seriesId) const;
    Q_INVOKABLE void setBookmarked(const QString &seriesId, const QStringList &unitKeys, bool bookmarked);
    // A series' own settings (chapter sort and filters, reader options).
    Q_INVOKABLE QVariantMap seriesSettings(const QString &seriesId) const;
    Q_INVOKABLE void setSeriesSetting(const QString &seriesId, const QString &key, const QVariant &value);

    // How many saved series each source holds: {sourceKey: count}.
    Q_INVOKABLE QVariantMap sourceCounts(const QString &media) const;
    // The saved series on one source, as "Move to another source" takes them.
    Q_INVOKABLE QVariantList seriesOn(const QString &media, const QString &source) const;
    // Moves a series to another source (its series and chapters already
    // stored, as the new source's series page stores them): library entry,
    // progress by chapter number and tracker links. options: {progress,
    // keepOld}. {read, nextKey}
    Q_INVOKABLE sc::Task *migrate(const QString &fromId, const QString &toId, const QVariantMap &options = {});

    // Checks saved comics and novels for new chapters, those the update
    // rules (Settings > Library) pick. An automatic check also waits for an
    // unmetered network or a charger when asked to.
    Q_INVOKABLE sc::Task *checkUpdates(bool automatic = false);
    Q_INVOKABLE QVariantMap stats() const;

    // Used by services.
    static void upsertSeries(const QVariantMap &series);
    // Custom lists as stored, with when they last changed (sync keeps the
    // newest); any thread.
    static QVariantList storedCategories();
    static qint64 categoriesChangedAt();
    static void storeCategories(const QVariantList &categories, qint64 changedAt);
    // With the lists' order, hidden ones and built-in names (synced along).
    static QVariantMap storedListLayout();
    static void storeCategories(const QVariantList &categories, qint64 changedAt, const QVariantMap &layout);
    // reading, planned, paused, completed, dropped.
    static const QStringList &builtinCategories();
    // The custom list with this name (case aside), made when there is none
    // (imports from other apps); any thread.
    static QString categoryNamed(const QString &name);
    static void storeUnits(const QString &seriesId, const QVariantList &units);
    static QVariantList cachedUnits(const QString &seriesId);
    // History taken out (forget, clearHistory): {scope: when}, scope being a
    // series id, "<media>:*" or "*". Progress saved until then, from sync or
    // the website, is not taken back; any thread.
    static QHash<QString, qint64> historyCleared();
    static qint64 clearedAt(const QString &seriesId, const QHash<QString, qint64> &cleared);
    // Another device's clearings: the newer ones are kept and remove what
    // they cover here. How many progress rows went.
    static int applyHistoryCleared(const QVariantMap &remote);
    void notifyChanged();

signals:
    void changed();
    void listsChanged();
    void updatesFound(int count, const QStringList &titles);
    // The user saved or removed a series (not sync, which writes directly).
    void saved(const QString &seriesId);
    void removed(const QString &seriesId);
    // Reading or watching moved on in a chapter or episode (recordProgress).
    void progressRecorded(const QString &seriesId, const QString &unitKey);

private:
    void saveProgress(const QVariantMap &series, const QVariantMap &unit, const QVariantMap &position, double percent,
                      bool completed);
    // Private so QML uses create(): Qt builds its own instance of a
    // singleton it can default-construct, and C++ and QML would then
    // work on different objects.
    explicit Library(QObject *parent = nullptr);

    int m_revision = 0;
    int m_listsRevision = 0;
};

} // namespace sc
