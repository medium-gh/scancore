#include "Library.h"
#include "sources/servers/Servers.h"

#include "Activity.h"
#include "Anime.h"
#include "App.h"
#include "Comics.h"
#include "Downloads.h"
#include "Notifications.h"
#include "Novels.h"
#include "Platform.h"
#include "Trackers.h"
#include "Upkeep.h"
#include "core/Database.h"
#include "core/ListOrder.h"
#include "core/Migration.h"
#include "core/Parallel.h"
#include "core/Text.h"
#include "core/UpdateRules.h"
#include "sources/comics/Registry.h"

#include <QJsonDocument>
#include <algorithm>
#include <QMutex>
#include <QUuid>
#include <QTimer>
#include <QRegularExpression>
#include <QSet>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkInformation>

namespace sc {

namespace {

QString json(const QVariant &value) {
    if (!value.isValid() || value.isNull())
        return {};
    return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QVariant parse(const QVariant &text) {
    const auto doc = QJsonDocument::fromJson(text.toByteArray());
    return doc.isNull() ? QVariant() : doc.toVariant();
}

QVariantMap shapeEntry(QVariantMap row) {
    row.insert(QStringLiteral("position"), parse(row.value(QStringLiteral("position"))));
    row.insert(QStringLiteral("extra"), parse(row.value(QStringLiteral("extra"))));
    row.insert(QStringLiteral("genres"),
               row.value(QStringLiteral("genres")).toString().split(QStringLiteral(", "), Qt::SkipEmptyParts));
    return row;
}

const QString kEntrySelect = QStringLiteral(R"(
    SELECT s.id, s.media, s.source, s.source_url AS sourceUrl, s.title, s.cover, s.description,
           s.author, s.status, s.genres, s.extra,
           l.category, l.favourite, l.added_at AS addedAt, l.new_units AS newUnits, l.known_units AS knownUnits,
           l.latest_title AS latestTitle,
           p.unit_key AS unitKey, p.unit_title AS unitTitle, p.unit_number AS unitNumber, p.unit_url AS unitUrl,
           p.position, p.percent, p.completed, p.updated_at AS progressAt
)");

} // namespace

Library::Library(QObject *parent) : QObject(parent) {}

Library *Library::instance() {
    static auto *library = [] {
        auto *created = new Library;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return library;
}

void Library::refreshCover(const QString &seriesId) {
    static QSet<QString> tried;
    if (tried.contains(seriesId))
        return;
    // An AniList anime: the covers that failed together (a screen of the
    // library) are asked for in one go.
    static const QRegularExpression anilist(QStringLiteral("^anime:anilist:(\\d+)$"));
    if (const auto match = anilist.match(seriesId); match.hasMatch()) {
        tried.insert(seriesId);
        static QList<int> pending;
        pending.append(match.captured(1).toInt());
        if (pending.size() == 1)
            QTimer::singleShot(400, this, [this] {
                runInBackground([this, ids = std::exchange(pending, {})] {
                    try {
                        bool changed = false;
                        const auto covers = Anime::currentCovers(ids);
                        for (auto it = covers.begin(); it != covers.end(); ++it) {
                            if (it.value().isEmpty())
                                continue;
                            const auto id = QStringLiteral("anime:anilist:%1").arg(it.key());
                            if (Database::scalar(QStringLiteral("SELECT cover FROM series WHERE id = ?"), {id}).toString() == it.value())
                                continue;
                            Database::exec(QStringLiteral("UPDATE series SET cover = ? WHERE id = ?"), {it.value(), id});
                            changed = true;
                        }
                        if (changed)
                            QMetaObject::invokeMethod(this, [this] { notifyChanged(); }, Qt::QueuedConnection);
                    } catch (const Error &) {
                    }
                });
            });
        return;
    }
    if (!seriesId.startsWith(QLatin1String("comic:")))
        return;
    tried.insert(seriesId);
    const auto row = seriesRow(seriesId);
    const auto url = row.value(QStringLiteral("sourceUrl")).toString();
    const auto old = row.value(QStringLiteral("cover")).toString();
    if (url.isEmpty())
        return;
    runInBackground([this, seriesId, url, old] {
        try {
            const auto source = comics::Registry::forUrl(url);
            const auto details = source ? source->details(url) : std::nullopt;
            if (!details || details->cover.isEmpty() || details->cover == old)
                return;
            Database::exec(QStringLiteral("UPDATE series SET cover = ? WHERE id = ?"), {details->cover, seriesId});
            Database::exec(QStringLiteral("DELETE FROM cache WHERE key = ?"), {QStringLiteral("comic-details:") + url});
            QMetaObject::invokeMethod(this, [this] { notifyChanged(); }, Qt::QueuedConnection);
        } catch (const Error &) {
        }
    });
}

void Library::notifyChanged() {
    ++m_revision;
    emit changed();
}

QString Library::idFor(const QString &media, const QString &source, const QString &key) {
    // Extension links already start with their source key
    // ("mihon:<id>:<link>"); the id is "comic:mihon:<id>:<link>".
    if (!source.isEmpty() && key.startsWith(source) && key.size() > source.size() && key.at(source.size()) == QLatin1Char(':'))
        return QStringLiteral("%1:%2").arg(media, key);
    return QStringLiteral("%1:%2:%3").arg(media, source, key);
}

int Library::unreadUpdates() const {
    return Database::scalar(QStringLiteral("SELECT COALESCE(SUM(new_units), 0) FROM library")).toInt();
}

bool Library::isSaved(const QString &seriesId) const {
    return Database::scalar(QStringLiteral("SELECT 1 FROM library WHERE series_id = ?"), {seriesId}).isValid();
}

QString Library::category(const QString &seriesId) const {
    return Database::scalar(QStringLiteral("SELECT category FROM library WHERE series_id = ?"), {seriesId}).toString();
}

void Library::upsertSeries(const QVariantMap &s) {
    const auto id = s.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;
    // Ids are "<media>:<source>:<link>", for summaries without those.
    auto media = s.value(QStringLiteral("media")).toString();
    if (media.isEmpty())
        media = id.section(QLatin1Char(':'), 0, 0);
    auto source = s.value(QStringLiteral("source")).toString();
    if (source.isEmpty())
        source = id.section(QLatin1Char(':'), 1, 1);
    auto genres = s.value(QStringLiteral("genres"));
    const auto genreText = genres.typeId() == QMetaType::QStringList || genres.typeId() == QMetaType::QVariantList
                               ? genres.toStringList().join(QStringLiteral(", "))
                               : genres.toString();
    // COALESCE keeps richer details already stored when a sparse summary
    // (for example from a search result) is saved again.
    Database::exec(QStringLiteral(R"(
        INSERT INTO series(id, media, source, source_url, title, cover, description, author, status, genres, extra, updated_at)
        VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            title = COALESCE(NULLIF(excluded.title, ''), series.title),
            cover = COALESCE(NULLIF(excluded.cover, ''), series.cover),
            description = COALESCE(NULLIF(excluded.description, ''), series.description),
            author = COALESCE(NULLIF(excluded.author, ''), series.author),
            status = COALESCE(NULLIF(excluded.status, ''), series.status),
            genres = COALESCE(NULLIF(excluded.genres, ''), series.genres),
            extra = COALESCE(NULLIF(excluded.extra, ''), series.extra),
            updated_at = excluded.updated_at
    )"),
                   {id, media, source,
                    s.value(QStringLiteral("sourceUrl")).toString(), s.value(QStringLiteral("title")).toString(),
                    s.value(QStringLiteral("cover")).toString(), s.value(QStringLiteral("description")).toString(),
                    s.value(QStringLiteral("author")).toString(), s.value(QStringLiteral("status")).toString(),
                    genreText, json(s.value(QStringLiteral("extra"))), Database::now()});
}

void Library::storeUnits(const QString &seriesId, const QVariantList &units) {
    if (units.isEmpty())
        return;
    Database::transaction([&] {
        Database::query(QStringLiteral("DELETE FROM units WHERE series_id = ?"), {seriesId});
        int index = 0;
        for (const auto &value : units) {
            const auto u = value.toMap();
            Database::query(QStringLiteral("INSERT OR REPLACE INTO units(series_id, unit_key, idx, number, title, url, "
                                           "released_at, scanlator) VALUES(?, ?, ?, ?, ?, ?, ?, ?)"),
                            {seriesId, u.value(QStringLiteral("key")).toString(), index++,
                             u.value(QStringLiteral("number")).toString(), u.value(QStringLiteral("title")).toString(),
                             u.value(QStringLiteral("url")).toString(), u.value(QStringLiteral("released")).toString(),
                             u.value(QStringLiteral("scanlator")).toString()});
        }
    });
}

QVariantList Library::cachedUnits(const QString &seriesId) {
    return Database::query(QStringLiteral("SELECT unit_key AS key, number, title, url, released_at AS released, scanlator "
                                          "FROM units WHERE series_id = ? ORDER BY idx"),
                           {seriesId});
}

void Library::save(const QVariantMap &series, const QString &category) {
    const auto id = series.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;
    // Called from QML: a series it cannot store (no source) is refused,
    // not thrown through the interface (which ends the app).
    try {
        upsertSeries(series);
        const auto known = Database::scalar(QStringLiteral("SELECT COUNT(*) FROM units WHERE series_id = ?"), {id}).toInt();
        Database::transaction([&] {
            Database::query(QStringLiteral("INSERT INTO library(series_id, category, added_at, known_units, changed_at) "
                                           "VALUES(?, ?, ?, ?, ?) ON CONFLICT(series_id) DO UPDATE SET "
                                           "category = excluded.category, changed_at = excluded.changed_at"),
                            {id, category, Database::now(), known, Database::now()});
            Database::query(QStringLiteral("DELETE FROM library_removed WHERE series_id = ?"), {id});
        });
    } catch (const Error &e) {
        qWarning("Library: %s could not be saved: %s", qPrintable(id), e.what());
        return;
    }
    notifyChanged();
    emit saved(id);
}

void Library::remove(const QString &seriesId) {
    // The removal is kept so file sync can remove it on other devices too.
    Database::transaction([&] {
        Database::query(QStringLiteral("DELETE FROM library WHERE series_id = ?"), {seriesId});
        Database::query(QStringLiteral("INSERT OR REPLACE INTO library_removed(series_id, removed_at) VALUES(?, ?)"),
                        {seriesId, Database::now()});
    });
    notifyChanged();
    emit removed(seriesId);
}

void Library::setCategory(const QString &seriesId, const QString &category) {
    Database::exec(QStringLiteral("UPDATE library SET category = ?, changed_at = ? WHERE series_id = ?"),
                   {category, Database::now(), seriesId});
    notifyChanged();
}

namespace {

QMutex g_categoriesMutex;
const QString kCategoriesKey = QStringLiteral("libraryCategories");

// A new list is checked for new chapters like the built-in reading lists.
void checkListForUpdates(const QString &id) {
    QMetaObject::invokeMethod(
        App::instance(),
        [id] {
            // Unless it is gone again by now.
            const auto own = Library::storedCategories();
            if (std::none_of(own.begin(), own.end(), [&](const QVariant &c) { return c.toMap().value(QStringLiteral("id")) == id; }))
                return;
            auto lists = App::instance()->settings().value(QStringLiteral("updateLists")).toList();
            if (!lists.contains(id)) {
                lists.append(id);
                App::instance()->setSetting(QStringLiteral("updateLists"), lists);
            }
        },
        Qt::QueuedConnection);
}

} // namespace

QVariantList Library::storedCategories() {
    return Database::pref(kCategoriesKey).toMap().value(QStringLiteral("list")).toList();
}

qint64 Library::categoriesChangedAt() {
    return Database::pref(kCategoriesKey).toMap().value(QStringLiteral("changed")).toLongLong();
}

QVariantMap Library::storedListLayout() {
    return Database::pref(kCategoriesKey).toMap().value(QStringLiteral("layout")).toMap();
}

const QStringList &Library::builtinCategories() {
    static const QStringList lists{QStringLiteral("reading"), QStringLiteral("planned"), QStringLiteral("paused"),
                                   QStringLiteral("completed"), QStringLiteral("dropped")};
    return lists;
}

void Library::storeCategories(const QVariantList &categories, qint64 changedAt) {
    storeCategories(categories, changedAt, storedListLayout());
}

void Library::storeCategories(const QVariantList &categories, qint64 changedAt, const QVariantMap &layout) {
    QVariantMap value{{QStringLiteral("list"), categories}, {QStringLiteral("changed"), changedAt}};
    if (!layout.isEmpty())
        value.insert(QStringLiteral("layout"), layout);
    Database::setPref(kCategoriesKey, value);
    QMetaObject::invokeMethod(
        instance(),
        [] {
            ++instance()->m_listsRevision;
            emit instance()->listsChanged();
            instance()->notifyChanged();
        },
        Qt::QueuedConnection);
}

namespace {

QString listId(const QVariant &c) { return c.toMap().value(QStringLiteral("id")).toString(); }

QStringList orderedLists(const QVariantList &custom, const QVariantMap &layout) {
    QStringList all = Library::builtinCategories();
    for (const auto &c : custom)
        all.append(listId(c));
    return lists::ordered(all, layout.value(QStringLiteral("order")).toStringList());
}

// Own lists stored in the shown order too, for older versions and exports.
QVariantList inOrder(QVariantList custom, const QStringList &order) {
    std::stable_sort(custom.begin(), custom.end(),
                     [&](const QVariant &a, const QVariant &b) { return order.indexOf(listId(a)) < order.indexOf(listId(b)); });
    return custom;
}

} // namespace

QVariantMap Library::listLayout() const {
    const auto custom = storedCategories();
    auto layout = storedListLayout();
    layout.insert(QStringLiteral("order"), orderedLists(custom, layout));
    return layout;
}

QString Library::categoryNamed(const QString &name) {
    const auto wanted = name.trimmed();
    if (wanted.isEmpty())
        return QStringLiteral("reading");
    QMutexLocker lock(&g_categoriesMutex);
    // A built-in list under the name it was given here (a backup made
    // after renaming it).
    const auto names = storedListLayout().value(QStringLiteral("names")).toMap();
    for (auto it = names.begin(); it != names.end(); ++it)
        if (it.value().toString().compare(wanted, Qt::CaseInsensitive) == 0)
            return it.key();
    auto list = storedCategories();
    for (const auto &c : std::as_const(list))
        if (c.toMap().value(QStringLiteral("name")).toString().compare(wanted, Qt::CaseInsensitive) == 0)
            return listId(c);
    const auto id = QStringLiteral("c-") + QUuid::createUuid().toString(QUuid::Id128).left(10);
    list.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), wanted.left(60)}});
    storeCategories(list, Database::now());
    checkListForUpdates(id);
    return id;
}

QString Library::addCategory(const QString &name) {
    const auto wanted = name.trimmed();
    if (wanted.isEmpty())
        return {};
    return categoryNamed(wanted);
}

void Library::renameCategory(const QString &id, const QString &name) {
    const auto wanted = name.trimmed().left(60);
    QMutexLocker lock(&g_categoriesMutex);
    auto list = storedCategories();
    if (builtinCategories().contains(id)) {
        auto layout = storedListLayout();
        auto names = layout.value(QStringLiteral("names")).toMap();
        if (wanted.isEmpty())
            names.remove(id);
        else
            names.insert(id, wanted);
        layout.insert(QStringLiteral("names"), names);
        storeCategories(list, Database::now(), layout);
        return;
    }
    if (wanted.isEmpty())
        return;
    for (auto &c : list)
        if (listId(c) == id) {
            auto m = c.toMap();
            m.insert(QStringLiteral("name"), wanted);
            c = m;
        }
    storeCategories(list, Database::now());
}

void Library::removeCategory(const QString &id) {
    if (builtinCategories().contains(id))
        return;
    QMutexLocker lock(&g_categoriesMutex);
    auto list = storedCategories();
    list.removeIf([&](const QVariant &c) { return listId(c) == id; });
    auto layout = storedListLayout();
    for (const auto *key : {"order", "hidden"}) {
        auto ids = layout.value(QLatin1String(key)).toStringList();
        if (ids.removeAll(id) > 0)
            layout.insert(QLatin1String(key), ids);
    }
    storeCategories(list, Database::now(), layout);
    Database::exec(QStringLiteral("UPDATE library SET category = 'reading', changed_at = ? WHERE category = ?"), {Database::now(), id});
    for (const auto *key : {"updateLists", "autoDownloadLists"}) {
        auto lists = App::instance()->settings().value(QLatin1String(key)).toList();
        if (lists.removeAll(id) > 0)
            App::instance()->setSetting(QLatin1String(key), lists);
    }
    if (App::instance()->settings().value(QStringLiteral("libraryCategory")) == id)
        App::instance()->setSetting(QStringLiteral("libraryCategory"), QStringLiteral("all"));
}

void Library::moveCategory(const QString &id, int by, bool skipHidden) {
    QMutexLocker lock(&g_categoriesMutex);
    const auto list = storedCategories();
    auto layout = storedListLayout();
    auto order = orderedLists(list, layout);
    const auto before = order;
    order = lists::moved(order, layout.value(QStringLiteral("hidden")).toStringList(), id, by, skipHidden);
    if (order == before)
        return;
    layout.insert(QStringLiteral("order"), order);
    storeCategories(inOrder(list, order), Database::now(), layout);
}

void Library::setCategoryHidden(const QString &id, bool hidden) {
    {
        QMutexLocker lock(&g_categoriesMutex);
        const auto list = storedCategories();
        auto layout = storedListLayout();
        auto ids = layout.value(QStringLiteral("hidden")).toStringList();
        if (ids.contains(id) == hidden)
            return;
        if (hidden)
            ids.append(id);
        else
            ids.removeAll(id);
        layout.insert(QStringLiteral("hidden"), ids);
        storeCategories(list, Database::now(), layout);
    }
    if (hidden && App::instance()->settings().value(QStringLiteral("libraryCategory")) == id)
        App::instance()->setSetting(QStringLiteral("libraryCategory"), QStringLiteral("all"));
}

bool Library::isFavourite(const QString &seriesId) const {
    return Database::scalar(QStringLiteral("SELECT favourite FROM library WHERE series_id = ?"), {seriesId}).toBool();
}

void Library::setFavourite(const QString &seriesId, bool favourite) {
    Database::exec(QStringLiteral("UPDATE library SET favourite = ?, changed_at = ? WHERE series_id = ?"),
                   {favourite ? 1 : 0, Database::now(), seriesId});
    notifyChanged();
}

void Library::markSeen(const QString &seriesId) {
    Database::exec(QStringLiteral("UPDATE library SET new_units = 0, known_units = "
                                  "(SELECT COUNT(*) FROM units WHERE series_id = ?) WHERE series_id = ? AND new_units > 0"),
                   {seriesId, seriesId});
    notifyChanged();
}

// The library page's fields only: a few hundred full rows (descriptions,
// genres, extra) take long to hand to QML, on its thread.
const QString kBriefSelect = QStringLiteral(R"(
    SELECT s.id, s.media, s.source, s.source_url AS sourceUrl, s.title, s.cover,
           l.category, l.favourite, l.added_at AS addedAt, l.new_units AS newUnits,
           p.unit_key AS unitKey, p.unit_title AS unitTitle, p.unit_number AS unitNumber,
           p.percent, p.completed, p.updated_at AS progressAt,
           (SELECT COUNT(*) FROM units u WHERE u.series_id = s.id
                AND NOT EXISTS (SELECT 1 FROM progress q WHERE q.series_id = u.series_id AND q.unit_key = u.unit_key AND q.completed)) AS unread
)");

Task *Library::entries(const QString &media, bool brief) {
    return Task::run([media, brief]() -> QVariant {
        const auto rows = Database::query((brief ? kBriefSelect : kEntrySelect) + QStringLiteral(R"(
            FROM library l JOIN series s ON s.id = l.series_id
            LEFT JOIN progress p ON p.series_id = s.id AND p.updated_at =
                (SELECT MAX(updated_at) FROM progress WHERE series_id = s.id)
            WHERE (? = '' OR s.media = ?)
            GROUP BY s.id
            ORDER BY l.new_units > 0 DESC, COALESCE(p.updated_at, l.added_at) DESC
        )"),
                                          {media.isNull() ? QStringLiteral("") : media, media.isNull() ? QStringLiteral("") : media});
        if (brief)
            return rows;
        QVariantList out;
        for (const auto &row : rows)
            out.append(shapeEntry(row.toMap()));
        return out;
    });
}

namespace {

// Each series once, at its latest progress. Imports, syncs and "mark as
// read" save many chapters or episodes at the same moment: then the furthest
// one counts, not whichever the database happens to list first.
QVariantList historyRows(const QString &media, int limit, int offset = 0) {
    const auto rows = Database::query(QStringLiteral(R"(
        WITH latest AS (
            SELECT *, ROW_NUMBER() OVER (PARTITION BY series_id
                ORDER BY updated_at DESC, CAST(unit_number AS REAL) DESC, completed DESC, rowid DESC) AS rank
            FROM progress WHERE (? = '' OR series_id LIKE ? || ':%')
        )
    )") + kEntrySelect + QStringLiteral(R"(
        FROM latest p JOIN series s ON s.id = p.series_id
        LEFT JOIN library l ON l.series_id = s.id
        WHERE p.rank = 1
        ORDER BY p.updated_at DESC LIMIT ? OFFSET ?
    )"),
                                      // A null QString binds as NULL, which no
                                      // "? = ''" matches: "" for every media.
                                      {media.isNull() ? QStringLiteral("") : media, media.isNull() ? QStringLiteral("") : media, limit, offset});
    QVariantList out;
    for (const auto &row : rows)
        out.append(shapeEntry(row.toMap()));
    return out;
}

} // namespace

Task *Library::history(const QString &media, int limit) {
    return Task::run([media, limit]() -> QVariant { return historyRows(media, limit); });
}

Task *Library::continueItems(const QString &media, int limit) {
    return Task::run([media, limit]() -> QVariant {
        // One entry per title: a series read on several sources, or under a
        // source's changed address, shows only where it was read last.
        // Read page by page until the list is full: an import, a sync or
        // "mark as read" can put many finished or planned series ahead of
        // the ones still going, and a single page left those out (Home's
        // row stopped at a few, and changed with every sync).
        QVariantList out;
        QSet<QString> seen;
        const int page = std::max(limit * 4, 50);
        for (int offset = 0; out.size() < limit && offset < page * 20; offset += page) {
            const auto rows = historyRows(media, page, offset);
            for (const auto &value : rows) {
                const auto entry = value.toMap();
                const auto key = entry.value(QStringLiteral("media")).toString() + QLatin1Char(':') +
                                 titleKey(entry.value(QStringLiteral("title")).toString());
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                // Not something being read or watched: planned, dropped or
                // finished lists, and anime watched to their last episode.
                const auto category = entry.value(QStringLiteral("category")).toString();
                if (category == QLatin1String("planned") || category == QLatin1String("dropped") || category == QLatin1String("completed"))
                    continue;
                if (entry.value(QStringLiteral("media")).toString() == QLatin1String("anime") && entry.value(QStringLiteral("completed")).toBool()) {
                    const int episodes = entry.value(QStringLiteral("extra")).toMap().value(QStringLiteral("episodes")).toInt();
                    if (episodes > 0 && entry.value(QStringLiteral("unitNumber")).toString().toDouble() >= episodes)
                        continue;
                }
                out.append(entry);
                if (out.size() >= limit)
                    break;
            }
            if (rows.size() < page)
                break;
        }
        return out;
    });
}

void Library::forget(const QString &seriesId) {
    Database::exec(QStringLiteral("DELETE FROM progress WHERE series_id = ?"), {seriesId});
    notifyChanged();
}

void Library::clearHistory(const QString &media) {
    const auto m = media.isNull() ? QStringLiteral("") : media; // a null QString would bind as NULL
    Database::exec(QStringLiteral("DELETE FROM progress WHERE (? = '' OR series_id LIKE ? || ':%')"), {m, m});
    notifyChanged();
}

void Library::recordProgress(const QVariantMap &series, const QVariantMap &unit, const QVariantMap &position,
                             double percent, bool completed) {
    const auto seriesId = series.value(QStringLiteral("id")).toString();
    const auto key = unit.value(QStringLiteral("key")).toString();
    if (seriesId.isEmpty() || key.isEmpty())
        return;
    // Incognito: nothing is kept, so no history, activity or tracker update.
    if (App::instance()->incognito())
        return;
    // Called while reading and watching: a storage failure must not end
    // the app.
    try {
        saveProgress(series, unit, position, percent, completed);
        emit progressRecorded(seriesId, key);
    } catch (const Error &e) {
        qWarning() << "Progress not saved:" << e.message();
    }
}

void Library::saveProgress(const QVariantMap &series, const QVariantMap &unit, const QVariantMap &position, double percent,
                           bool completed) {
    const auto seriesId = series.value(QStringLiteral("id")).toString();
    const auto key = unit.value(QStringLiteral("key")).toString();
    if (seriesId.isEmpty())
        return;
    upsertSeries(series);
    const bool wasCompleted =
        Database::scalar(QStringLiteral("SELECT completed FROM progress WHERE series_id = ? AND unit_key = ?"), {seriesId, key})
            .toBool();
    Database::exec(QStringLiteral(R"(
        INSERT INTO progress(series_id, unit_key, unit_title, unit_number, unit_url, position, percent, completed, updated_at)
        VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(series_id, unit_key) DO UPDATE SET
            unit_title = excluded.unit_title, unit_number = excluded.unit_number, unit_url = excluded.unit_url,
            position = excluded.position, percent = MAX(excluded.percent, CASE WHEN excluded.completed THEN 1 ELSE 0 END),
            completed = MAX(progress.completed, excluded.completed), updated_at = excluded.updated_at
    )"),
                   {seriesId, key, unit.value(QStringLiteral("title")).toString(),
                    unit.value(QStringLiteral("number")).toString(), unit.value(QStringLiteral("url")).toString(),
                    json(position), std::clamp(percent, 0.0, 1.0), completed ? 1 : 0, Database::now()});
    // An episode from the reader's Jellyfin: the server keeps the position
    // too, so its other apps resume there.
    if (const auto source = series.value(QStringLiteral("source")).toString();
        source.startsWith(QLatin1String("aniyomi:server-")) && position.value(QStringLiteral("type")).toString() == QLatin1String("video"))
        runInBackground([source, key, seconds = position.value(QStringLiteral("seconds")).toDouble(), completed] {
            servers::videoWatched(source, key, seconds, completed);
        });
    // Started: a series planned in the library is being read or watched now.
    Database::exec(QStringLiteral("UPDATE library SET category = 'reading', changed_at = ? WHERE series_id = ? AND category = 'planned'"),
                   {Database::now(), seriesId});
    // Finished just now, by reading or watching (not marked by hand).
    if (completed && !wasCompleted) {
        Activity::unitFinished(seriesId.section(QLatin1Char(':'), 0, 0));
        Trackers::unitFinished(seriesId);
        // The reader's own server is told too.
        if (series.value(QStringLiteral("source")).toString().startsWith(QLatin1String("server-")))
            runInBackground([source = series.value(QStringLiteral("source")).toString(), seriesUrl = series.value(QStringLiteral("sourceUrl")).toString(),
                             chapterUrl = unit.value(QStringLiteral("url")).toString().isEmpty() ? key : unit.value(QStringLiteral("url")).toString(),
                             number = unit.value(QStringLiteral("number")).toString()] { servers::chapterRead(source, seriesUrl, chapterUrl, number); });
        // "Delete once read": the downloads of chapters finished before this
        // one go (this one may still be open). Mihon's files stay.
        if (App::instance()->settings().value(QStringLiteral("deleteAfterRead")).toBool())
            for (const auto &value : Database::query(QStringLiteral("SELECT d.unit_key, d.path FROM downloads d JOIN progress p ON p.series_id = d.series_id "
                                                                    "AND p.unit_key = d.unit_key WHERE d.series_id = ? AND d.status = 'done' "
                                                                    "AND p.completed = 1 AND d.unit_key != ?"),
                                                     {seriesId, key}))
                if (!Downloads::isExternal(value.toMap().value(QStringLiteral("path")).toString()))
                    Downloads::instance()->removeUnit(seriesId, value.toMap().value(QStringLiteral("unit_key")).toString());
    }
    notifyChanged();
}

void Library::markUnits(const QVariantMap &series, const QVariantList &units, bool completed) {
    const auto seriesId = series.value(QStringLiteral("id")).toString();
    upsertSeries(series);
    Database::transaction([&] {
        for (const auto &value : units) {
            const auto u = value.toMap();
            if (completed)
                Database::query(QStringLiteral(R"(
                    INSERT INTO progress(series_id, unit_key, unit_title, unit_number, unit_url, percent, completed, updated_at)
                    VALUES(?, ?, ?, ?, ?, 1, 1, ?)
                    ON CONFLICT(series_id, unit_key) DO UPDATE SET completed = 1, percent = 1
                )"),
                                {seriesId, u.value(QStringLiteral("key")).toString(),
                                 u.value(QStringLiteral("title")).toString(), u.value(QStringLiteral("number")).toString(),
                                 u.value(QStringLiteral("url")).toString(), Database::now() - 1});
            else
                Database::query(QStringLiteral("DELETE FROM progress WHERE series_id = ? AND unit_key = ?"),
                                {seriesId, u.value(QStringLiteral("key")).toString()});
        }
    });
    // Marked as read by hand: lists on AniList and MyAnimeList follow.
    if (completed)
        Trackers::unitFinished(seriesId);
    notifyChanged();
}

QVariantMap Library::progressMap(const QString &seriesId) const {
    QVariantMap out;
    for (const auto &value : Database::query(
             QStringLiteral("SELECT unit_key, percent, completed, position FROM progress WHERE series_id = ?"), {seriesId})) {
        const auto row = value.toMap();
        out.insert(row.value(QStringLiteral("unit_key")).toString(),
                   QVariantMap{{"percent", row.value(QStringLiteral("percent"))},
                               {"completed", row.value(QStringLiteral("completed")).toBool()},
                               {"position", parse(row.value(QStringLiteral("position")))}});
    }
    return out;
}

QVariantMap Library::lastProgress(const QString &seriesId) const {
    auto row = Database::one(QStringLiteral("SELECT unit_key AS unitKey, unit_title AS unitTitle, unit_number AS unitNumber, "
                                            "unit_url AS unitUrl, position, percent, completed, updated_at AS updatedAt "
                                            "FROM progress WHERE series_id = ? ORDER BY updated_at DESC LIMIT 1"),
                             {seriesId});
    if (!row.isEmpty())
        row.insert(QStringLiteral("position"), parse(row.value(QStringLiteral("position"))));
    return row;
}

QVariantMap Library::seriesRow(const QString &seriesId) const {
    auto row = Database::one(QStringLiteral("SELECT id, media, source, source_url AS sourceUrl, title, cover, description, "
                                            "author, status, genres, extra FROM series WHERE id = ?"),
                             {seriesId});
    if (!row.isEmpty())
        row = shapeEntry(row);
    return row;
}

QVariantMap Library::bookmarks(const QString &seriesId) const {
    QVariantMap out;
    for (const auto &value : Database::query(QStringLiteral("SELECT unit_key FROM bookmarks WHERE series_id = ?"), {seriesId}))
        out.insert(value.toMap().value(QStringLiteral("unit_key")).toString(), true);
    return out;
}

void Library::setBookmarked(const QString &seriesId, const QStringList &unitKeys, bool bookmarked) {
    Database::transaction([&] {
        for (const auto &key : unitKeys)
            // Taking one off is kept too, so sync takes it off elsewhere.
            if (bookmarked) {
                Database::query(QStringLiteral("INSERT OR IGNORE INTO bookmarks(series_id, unit_key, created_at) VALUES(?, ?, ?)"),
                                {seriesId, key, Database::now()});
                Database::query(QStringLiteral("DELETE FROM bookmarks_removed WHERE series_id = ? AND unit_key = ?"), {seriesId, key});
            } else {
                Database::query(QStringLiteral("DELETE FROM bookmarks WHERE series_id = ? AND unit_key = ?"), {seriesId, key});
                Database::query(QStringLiteral("INSERT OR REPLACE INTO bookmarks_removed(series_id, unit_key, removed_at) VALUES(?, ?, ?)"),
                                {seriesId, key, Database::now()});
            }
    });
    notifyChanged();
}

QVariantMap Library::seriesSettings(const QString &seriesId) const {
    return parse(Database::scalar(QStringLiteral("SELECT value FROM series_settings WHERE series_id = ?"), {seriesId})).toMap();
}

void Library::setSeriesSetting(const QString &seriesId, const QString &key, const QVariant &input) {
    if (seriesId.isEmpty())
        return;
    const auto value = App::plain(input);
    auto settings = seriesSettings(seriesId);
    if (value.isNull() || !value.isValid())
        settings.remove(key);
    else
        settings.insert(key, value);
    // Kept even when empty, with when it changed, for sync.
    Database::exec(QStringLiteral("INSERT OR REPLACE INTO series_settings(series_id, value, changed_at) VALUES(?, ?, ?)"),
                   {seriesId, json(settings), Database::now()});
    notifyChanged();
}

QVariantMap Library::sourceCounts(const QString &media) const {
    QVariantMap out;
    for (const auto &value : Database::query(QStringLiteral("SELECT s.source, COUNT(*) AS n FROM library l JOIN series s ON s.id = l.series_id "
                                                            "WHERE s.media = ? GROUP BY s.source"),
                                             {media})) {
        const auto row = value.toMap();
        out.insert(row.value(QStringLiteral("source")).toString(), row.value(QStringLiteral("n")));
    }
    return out;
}

QVariantList Library::seriesOn(const QString &media, const QString &source) const {
    QVariantList out;
    for (const auto &value : Database::query(QStringLiteral("SELECT s.id, s.media, s.source, s.source_url AS sourceUrl, s.title, s.cover, s.extra "
                                                            "FROM library l JOIN series s ON s.id = l.series_id WHERE s.media = ? AND s.source = ? "
                                                            "ORDER BY s.title"),
                                             {media, source}))
        out.append(shapeEntry(value.toMap()));
    return out;
}

Task *Library::migrate(const QString &fromId, const QString &toId, const QVariantMap &options) {
    return Task::run([this, fromId, toId, options]() -> QVariant {
        migration::Options o;
        o.progress = options.value(QStringLiteral("progress"), true).toBool();
        o.keepOld = options.value(QStringLiteral("keepOld"), false).toBool();
        const auto result = migration::move(fromId, toId, o);
        Trackers::moveLinks(fromId, toId, o.keepOld);
        QMetaObject::invokeMethod(this, [this, fromId, toId, keepOld = o.keepOld] {
            notifyChanged();
            emit saved(toId);
            if (!keepOld)
                emit removed(fromId);
        });
        return QVariantMap{{"read", result.read}, {"nextKey", result.nextKey}, {"id", toId}};
    });
}

Task *Library::checkUpdates(bool automatic) {
    const auto settings = App::instance()->settings();
    updates::Rules rules;
    rules.lists = settings.value(QStringLiteral("updateLists")).toStringList();
    rules.skipFinished = settings.value(QStringLiteral("updateSkipFinished")).toBool();
    rules.skipUnread = settings.value(QStringLiteral("updateSkipUnread")).toBool();
    rules.skipNotStarted = settings.value(QStringLiteral("updateSkipNotStarted")).toBool();
    const auto autoDownload = settings.value(QStringLiteral("autoDownloadLists")).toStringList();
    // Read here, on the thread QNetworkInformation lives on.
    QString waitFor;
    if (automatic) {
        const auto *network = QNetworkInformation::instance();
        if (settings.value(QStringLiteral("updateOnlyUnmetered")).toBool() && network &&
            (network->isMetered() || network->transportMedium() == QNetworkInformation::TransportMedium::Cellular))
            waitFor = QStringLiteral("unmetered");
        else if (settings.value(QStringLiteral("updateOnlyCharging")).toBool() && !platform::charging())
            waitFor = QStringLiteral("charging");
    }
    return Task::run([this, rules, autoDownload, waitFor]() -> QVariant {
        if (!waitFor.isEmpty())
            return QVariantMap{{"checked", 0}, {"newUnits", 0}, {"waiting", waitFor}};
        const auto all = Database::query(QStringLiteral(
            "SELECT s.id, s.media, s.source, s.source_url, s.title, s.status, l.category, l.known_units, "
            "(SELECT COUNT(*) FROM progress p WHERE p.series_id = s.id) AS touched, "
            "(SELECT COUNT(*) FROM progress p WHERE p.series_id = s.id AND p.completed = 1) AS read_units "
            "FROM library l JOIN series s ON s.id = l.series_id WHERE s.media IN ('comic', 'novel') AND s.source != 'local'"));
        QVariantList rows;
        for (const auto &value : all) {
            const auto row = value.toMap();
            const updates::Entry entry{row.value(QStringLiteral("category")).toString(), row.value(QStringLiteral("status")).toString(),
                                       row.value(QStringLiteral("known_units")).toInt(), row.value(QStringLiteral("read_units")).toInt(),
                                       row.value(QStringLiteral("touched")).toInt() > 0};
            if (updates::shouldCheck(entry, rules))
                rows.append(row);
        }
        QList<std::function<QVariantMap()>> jobs;
        for (const auto &value : rows) {
            const auto row = value.toMap();
            jobs.append([row]() -> QVariantMap {
                const auto media = row.value(QStringLiteral("media")).toString();
                const auto url = row.value(QStringLiteral("source_url")).toString();
                const auto units = media == QLatin1String("comic") ? Comics::fetchUnits(url)
                                                                    : Novels::fetchUnits(url);
                return {{"id", row.value(QStringLiteral("id"))}, {"units", units}};
            });
        }
        QStringList updated;
        int total = 0;
        const bool digest = Database::pref(QStringLiteral("setting.notifyDigest"), false).toBool();
        const auto results = parallel(jobs);
        const qint64 now = Database::now();
        for (int i = 0; i < results.size(); ++i) {
            const auto row = rows.at(i).toMap();
            const auto id = row.value(QStringLiteral("id")).toString();
            const auto units = results[i].ok() ? results[i].value->value(QStringLiteral("units")).toList() : QVariantList();
            // Source health: when it last answered, and since when it fails.
            if (units.isEmpty()) {
                if (results[i].status == 499)
                    continue;
                Database::exec(QStringLiteral("INSERT INTO series_health(series_id, failing_since, failures, problem) VALUES(?, ?, 1, ?) "
                                              "ON CONFLICT(series_id) DO UPDATE SET failing_since = COALESCE(failing_since, excluded.failing_since), "
                                              "failures = failures + 1, problem = excluded.problem"),
                               {id, now, results[i].ok() ? QStringLiteral("No chapters listed") : results[i].error.left(300)});
                continue;
            }
            Database::exec(QStringLiteral("INSERT INTO series_health(series_id, last_ok, failures) VALUES(?, ?, 0) "
                                          "ON CONFLICT(series_id) DO UPDATE SET last_ok = excluded.last_ok, failing_since = NULL, "
                                          "failures = 0, problem = NULL"),
                           {id, now});
            // Chapters not seen before, by key (a re-numbered list is not
            // news).
            QSet<QString> known;
            for (const auto &u : Library::cachedUnits(id))
                known.insert(u.toMap().value(QStringLiteral("key")).toString());
            const int knownCount = row.value(QStringLiteral("known_units")).toInt();
            QVariantList fresh;
            if (knownCount > 0)
                for (const auto &u : units)
                    if (!known.contains(u.toMap().value(QStringLiteral("key")).toString()))
                        fresh.append(u);
            // Never more than the list grew by: a source that changed its
            // chapters' links would otherwise make every chapter new.
            const int grew = std::max(0, int(units.size()) - knownCount);
            if (fresh.size() > grew)
                fresh = units.mid(units.size() - grew);
            storeUnits(id, units);
            Database::exec(QStringLiteral("UPDATE library SET last_checked = ?, new_units = new_units + ?, "
                                          "known_units = ?, latest_title = ? WHERE series_id = ?"),
                           {Database::now(), fresh.size(), units.size(), units.last().toMap().value(QStringLiteral("title")), id});
            if (fresh.isEmpty())
                continue;
            Database::exec(QStringLiteral("UPDATE series_health SET last_new = ? WHERE series_id = ?"), {now, id});
            total += int(fresh.size());
            const auto title = row.value(QStringLiteral("title")).toString();
            updated.append(title);
            // One per series; more chapters later replace it.
            const auto latest = fresh.last().toMap().value(QStringLiteral("title")).toString();
            if (!digest)
                Notifications::post({QStringLiteral("chapters"), QStringLiteral("chapters:") + id, title,
                                     (fresh.size() == 1 ? tr("1 new chapter") : tr("%1 new chapters").arg(fresh.size()))
                                         + (latest.isEmpty() ? QString() : QStringLiteral(" · ") + latest),
                                     QVariantMap{{"seriesId", id}},
                                     Database::pref(QStringLiteral("setting.notifyUpdates"), true).toBool()});
            const auto category = row.value(QStringLiteral("category")).toString();
            if (autoDownload.contains(category.isEmpty() ? QStringLiteral("reading") : category)) {
                const QVariantMap series{{"id", id}, {"media", row.value(QStringLiteral("media"))}, {"title", title}};
                QMetaObject::invokeMethod(Downloads::instance(), [series, fresh] { Downloads::instance()->enqueue(series, fresh); });
            }
        }
        // One notification for the whole check, when chosen.
        if (digest && total > 0)
            Notifications::post({QStringLiteral("chapters"), QStringLiteral("chapters:digest"),
                                 total == 1 ? tr("1 new chapter in your library") : tr("%1 new chapters in your library").arg(total),
                                 QLocale().createSeparatedList(updated.mid(0, 4)) + (updated.size() > 4 ? QStringLiteral(" …") : QString()),
                                 QVariantMap{{"section", "library"}},
                                 Database::pref(QStringLiteral("setting.notifyUpdates"), true).toBool()});
        QMetaObject::invokeMethod(this, [this, total, updated] {
            notifyChanged();
            Upkeep::instance()->recount();
            if (total > 0)
                emit updatesFound(total, updated);
        });
        return QVariantMap{{"checked", rows.size()}, {"newUnits", total}, {"titles", updated}};
    });
}

QVariantMap Library::stats() const {
    return {{"saved", Database::scalar(QStringLiteral("SELECT COUNT(*) FROM library")).toInt()},
            {"read", Database::scalar(QStringLiteral("SELECT COUNT(*) FROM progress WHERE completed = 1")).toInt()},
            {"series", Database::scalar(QStringLiteral("SELECT COUNT(DISTINCT series_id) FROM progress")).toInt()}};
}

} // namespace sc
