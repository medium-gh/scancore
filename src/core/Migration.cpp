#include "Migration.h"

#include "Database.h"
#include "Http.h"
#include "Text.h"

#include <QHash>
#include <QSet>
#include <QRegularExpression>

#include <limits>

namespace sc::migration {

std::optional<double> chapterValue(const QString &number, const QString &title) {
    // As discussions read it ("Vol.3 Ch.12" is 12, not 3; a title when
    // the source gives no number).
    const auto canonical = chapterNumber(number, title);
    if (canonical.isEmpty())
        return std::nullopt;
    return canonical.toDouble();
}

Result move(const QString &fromId, const QString &toId, const Options &options) {
    if (fromId.isEmpty() || toId.isEmpty() || fromId == toId)
        throw Error(QStringLiteral("Pick a different source to move this series to."));
    const auto units = Database::query(QStringLiteral("SELECT unit_key, number, title, url FROM units WHERE series_id = ? ORDER BY idx"),
                                       {toId});
    if (units.isEmpty())
        throw Error(QStringLiteral("The new source lists no chapters for this series."));
    Result result;
    Database::transaction([&] {
        const auto entry = Database::one(QStringLiteral("SELECT category, favourite, added_at FROM library WHERE series_id = ?"), {fromId});
        if (options.progress) {
            // The furthest chapter read, and when each numbered chapter was
            // last touched (so history keeps its order).
            double furthest = -std::numeric_limits<double>::infinity();
            qint64 oldest = std::numeric_limits<qint64>::max();
            QHash<double, QVariantMap> byValue;
            for (const auto &value : Database::query(QStringLiteral("SELECT unit_number, unit_title, percent, completed, updated_at FROM progress "
                                                                    "WHERE series_id = ?"),
                                                     {fromId})) {
                const auto row = value.toMap();
                const qint64 at = row.value(QStringLiteral("updated_at")).toLongLong();
                oldest = std::min(oldest, at);
                const auto v = chapterValue(row.value(QStringLiteral("unit_number")).toString(), row.value(QStringLiteral("unit_title")).toString());
                if (!v)
                    continue;
                if (row.value(QStringLiteral("completed")).toBool())
                    furthest = std::max(furthest, *v);
                byValue.insert(*v, row);
            }
            double nextValue = std::numeric_limits<double>::infinity();
            for (const auto &value : units) {
                const auto unit = value.toMap();
                const auto v = chapterValue(unit.value(QStringLiteral("number")).toString(), unit.value(QStringLiteral("title")).toString());
                if (!v)
                    continue;
                const auto match = byValue.value(*v);
                const bool read = *v <= furthest;
                // A chapter started but not finished keeps how far it got.
                const double percent = read ? 1.0 : match.value(QStringLiteral("percent")).toDouble();
                if (!read && *v > furthest && *v < nextValue) {
                    nextValue = *v;
                    result.nextKey = unit.value(QStringLiteral("unit_key")).toString();
                }
                if (!read && percent <= 0)
                    continue;
                const qint64 at = match.isEmpty() ? oldest : match.value(QStringLiteral("updated_at")).toLongLong();
                Database::query(QStringLiteral(R"(
                    INSERT INTO progress(series_id, unit_key, unit_title, unit_number, unit_url, percent, completed, updated_at)
                    VALUES(?, ?, ?, ?, ?, ?, ?, ?)
                    ON CONFLICT(series_id, unit_key) DO UPDATE SET
                        percent = MAX(progress.percent, excluded.percent),
                        completed = MAX(progress.completed, excluded.completed),
                        updated_at = MAX(progress.updated_at, excluded.updated_at)
                )"),
                                {toId, unit.value(QStringLiteral("unit_key")), unit.value(QStringLiteral("title")),
                                 unit.value(QStringLiteral("number")), unit.value(QStringLiteral("url")), percent, read ? 1 : 0,
                                 at == std::numeric_limits<qint64>::max() ? Database::now() : at});
                if (read)
                    ++result.read;
            }
        }
        // Bookmarks follow by chapter number; the series' own settings
        // (chapter sort, reader options) come along unless it has some.
        {
            QSet<double> marked;
            for (const auto &value : Database::query(QStringLiteral("SELECT u.number FROM bookmarks b JOIN units u ON u.series_id = b.series_id "
                                                                    "AND u.unit_key = b.unit_key WHERE b.series_id = ?"),
                                                     {fromId}))
                if (const auto v = chapterValue(value.toMap().value(QStringLiteral("number")).toString()))
                    marked.insert(*v);
            for (const auto &value : units) {
                const auto unit = value.toMap();
                if (const auto v = chapterValue(unit.value(QStringLiteral("number")).toString(), unit.value(QStringLiteral("title")).toString()); v && marked.contains(*v))
                    Database::query(QStringLiteral("INSERT OR IGNORE INTO bookmarks(series_id, unit_key, created_at) VALUES(?, ?, ?)"),
                                    {toId, unit.value(QStringLiteral("unit_key")), Database::now()});
            }
            Database::query(QStringLiteral("INSERT OR IGNORE INTO series_settings(series_id, value, changed_at) "
                                           "SELECT ?, value, ? FROM series_settings WHERE series_id = ?"),
                            {toId, Database::now(), fromId});
        }
        if (!entry.isEmpty()) {
            Database::query(QStringLiteral(R"(
                INSERT INTO library(series_id, category, favourite, added_at, known_units, new_units, changed_at)
                VALUES(?, ?, ?, ?, ?, 0, ?)
                ON CONFLICT(series_id) DO UPDATE SET category = excluded.category, favourite = excluded.favourite,
                    known_units = excluded.known_units, new_units = 0, changed_at = excluded.changed_at
            )"),
                            {toId, entry.value(QStringLiteral("category")), entry.value(QStringLiteral("favourite")),
                             entry.value(QStringLiteral("added_at")), units.size(), Database::now()});
            Database::query(QStringLiteral("DELETE FROM library_removed WHERE series_id = ?"), {toId});
        }
        // Moving back to where it once was: that move no longer stands.
        if (auto all = moves(); all.remove(toId) > 0)
            Database::setPref(QStringLiteral("library.moved"), all);
        if (!options.keepOld) {
            auto all = moves();
            all.insert(fromId, QVariantList{toId, Database::now()});
            Database::setPref(QStringLiteral("library.moved"), all);
            // Recorded as removed so file sync removes it on other devices.
            if (!entry.isEmpty()) {
                Database::query(QStringLiteral("DELETE FROM library WHERE series_id = ?"), {fromId});
                Database::query(QStringLiteral("INSERT OR REPLACE INTO library_removed(series_id, removed_at) VALUES(?, ?)"),
                                {fromId, Database::now()});
            }
            // Its history moved with it.
            if (options.progress)
                Database::query(QStringLiteral("DELETE FROM progress WHERE series_id = ?"), {fromId});
        }
    });
    return result;
}

QVariantMap moves() { return Database::pref(QStringLiteral("library.moved")).toMap(); }

qint64 movedAt(const QString &seriesId, const QVariantMap &moves) {
    const auto it = moves.constFind(seriesId);
    return it == moves.cend() ? 0 : it->toList().value(1).toLongLong();
}

int applyMoves(const QVariantMap &incoming) {
    auto all = moves();
    int added = 0;
    for (auto it = incoming.cbegin(); it != incoming.cend(); ++it) {
        const auto at = it->toList().value(1).toLongLong();
        if (at <= 0 || movedAt(it.key(), all) >= at)
            continue;
        all.insert(it.key(), it.value());
        Database::exec(QStringLiteral("DELETE FROM progress WHERE series_id = ? AND updated_at <= ?"), {it.key(), at});
        ++added;
    }
    if (added > 0)
        Database::setPref(QStringLiteral("library.moved"), all);
    return added;
}

} // namespace sc::migration
