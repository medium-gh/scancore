#include "Database.h"

#include "Http.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRecursiveMutex>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QUuid>

namespace sc {

namespace {

QString g_path;
QRecursiveMutex g_writeLock;

// Schema migrations, applied in order. Never edit a shipped step; append.
const char *const kMigrations[] = {
    R"(
    CREATE TABLE series (
        id TEXT PRIMARY KEY,
        media TEXT NOT NULL,
        source TEXT NOT NULL,
        source_url TEXT NOT NULL,
        title TEXT NOT NULL,
        cover TEXT,
        description TEXT,
        author TEXT,
        status TEXT,
        genres TEXT,
        extra TEXT,
        updated_at INTEGER
    );
    CREATE TABLE units (
        series_id TEXT NOT NULL,
        unit_key TEXT NOT NULL,
        idx INTEGER NOT NULL,
        number TEXT,
        title TEXT,
        url TEXT,
        released_at TEXT,
        PRIMARY KEY (series_id, unit_key)
    );
    CREATE INDEX units_series ON units(series_id, idx);
    CREATE TABLE library (
        series_id TEXT PRIMARY KEY,
        category TEXT NOT NULL DEFAULT 'reading',
        added_at INTEGER NOT NULL,
        last_checked INTEGER,
        known_units INTEGER NOT NULL DEFAULT 0,
        new_units INTEGER NOT NULL DEFAULT 0,
        latest_title TEXT
    );
    CREATE TABLE progress (
        series_id TEXT NOT NULL,
        unit_key TEXT NOT NULL,
        unit_title TEXT,
        unit_number TEXT,
        unit_url TEXT,
        position TEXT,
        percent REAL NOT NULL DEFAULT 0,
        completed INTEGER NOT NULL DEFAULT 0,
        updated_at INTEGER NOT NULL,
        PRIMARY KEY (series_id, unit_key)
    );
    CREATE INDEX progress_recent ON progress(updated_at DESC);
    CREATE TABLE prefs (key TEXT PRIMARY KEY, value TEXT);
    CREATE TABLE cache (key TEXT PRIMARY KEY, value BLOB, expires INTEGER NOT NULL);
    CREATE TABLE downloads (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        series_id TEXT NOT NULL,
        unit_key TEXT NOT NULL,
        unit_title TEXT,
        unit_url TEXT,
        status TEXT NOT NULL,
        done INTEGER NOT NULL DEFAULT 0,
        total INTEGER NOT NULL DEFAULT 0,
        error TEXT,
        path TEXT,
        created_at INTEGER NOT NULL,
        UNIQUE (series_id, unit_key)
    );
    )",
    // 2: activity per day (time and finished chapters or episodes, per
    // device so synced devices add up), and what file sync needs to merge
    // libraries: when an entry last changed, and removals.
    R"(
    CREATE TABLE activity (
        day TEXT NOT NULL,
        device TEXT NOT NULL,
        media TEXT NOT NULL,
        seconds INTEGER NOT NULL DEFAULT 0,
        units INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (day, device, media)
    );
    ALTER TABLE library ADD COLUMN changed_at INTEGER;
    UPDATE library SET changed_at = added_at;
    CREATE TABLE library_removed (series_id TEXT PRIMARY KEY, removed_at INTEGER NOT NULL);
    )",
    // 3: extensions (2.0). Repositories the user added, with the last index
    // read from each; installed extensions and the key that signed them;
    // extensions trusted by hand; each extension source's switch, health
    // and settings.
    R"(
    CREATE TABLE ext_repos (
        url TEXT PRIMARY KEY,
        kind TEXT NOT NULL,
        info TEXT NOT NULL,
        packages TEXT,
        etag TEXT,
        modified TEXT,
        refreshed_at INTEGER,
        error TEXT,
        added_at INTEGER NOT NULL
    );
    CREATE TABLE ext_installed (
        kind TEXT NOT NULL,
        pkg TEXT NOT NULL,
        package TEXT NOT NULL,
        repo_url TEXT,
        signers TEXT,
        path TEXT NOT NULL,
        installed_at INTEGER NOT NULL,
        PRIMARY KEY (kind, pkg)
    );
    CREATE TABLE ext_trust (
        kind TEXT NOT NULL,
        pkg TEXT NOT NULL,
        version_code INTEGER NOT NULL,
        signer TEXT NOT NULL,
        PRIMARY KEY (kind, pkg)
    );
    CREATE TABLE ext_sources (
        key TEXT PRIMARY KEY,
        enabled INTEGER NOT NULL DEFAULT 1,
        error TEXT,
        error_at INTEGER,
        ok_at INTEGER
    );
    CREATE TABLE ext_prefs (
        source_key TEXT NOT NULL,
        key TEXT NOT NULL,
        value TEXT,
        PRIMARY KEY (source_key, key)
    );
    )",
    // 4: favourites in the library (2.0), beside its categories (reading,
    // planned, paused, completed, dropped).
    R"(
    ALTER TABLE library ADD COLUMN favourite INTEGER NOT NULL DEFAULT 0;
    )",
    // 5: the notification area (2.0): new chapters, announcements, updates.
    // action is JSON ({seriesId} | {url} | {page}); key replaces an earlier
    // notification about the same thing.
    R"(
    CREATE TABLE notifications (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        kind TEXT NOT NULL,
        key TEXT UNIQUE,
        title TEXT NOT NULL,
        body TEXT,
        action TEXT,
        created_at INTEGER NOT NULL,
        read_at INTEGER
    );
    CREATE INDEX notifications_created ON notifications(created_at);
    )",
    // 6: chapter tools (2.1): who translated a chapter, bookmarked
    // chapters, and each series' own settings (chapter sort and filters,
    // reader options) as JSON.
    R"(
    ALTER TABLE units ADD COLUMN scanlator TEXT;
    CREATE TABLE bookmarks (
        series_id TEXT NOT NULL,
        unit_key TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        PRIMARY KEY (series_id, unit_key)
    );
    CREATE TABLE series_settings (series_id TEXT PRIMARY KEY, value TEXT NOT NULL);
    )",
    // 7: bookmarks and series settings in sync (2.2): when a bookmark was
    // taken off, and when a series' settings last changed, so the latest
    // change on any device wins.
    R"(
    CREATE TABLE bookmarks_removed (
        series_id TEXT NOT NULL,
        unit_key TEXT NOT NULL,
        removed_at INTEGER NOT NULL,
        PRIMARY KEY (series_id, unit_key)
    );
    ALTER TABLE series_settings ADD COLUMN changed_at INTEGER NOT NULL DEFAULT 0;
    )",
    // 8: anime downloads (2.2): where an episode's video comes from
    // ({source, episodeUrl, animeUrl, number}).
    R"(
    ALTER TABLE downloads ADD COLUMN extra TEXT;
    )",
    // 9: source health (2.3): how each library series' last update checks
    // went, for "this source has not answered for a week" and moves.
    R"(
    CREATE TABLE series_health (
        series_id TEXT PRIMARY KEY,
        last_ok INTEGER,
        failing_since INTEGER,
        failures INTEGER NOT NULL DEFAULT 0,
        last_new INTEGER,
        problem TEXT
    );
    )",
    // 10: songs listened to (2.16), per day, for Wrapped's music slide.
    // This device's only; not synced.
    R"(
    CREATE TABLE music_plays (
        day TEXT NOT NULL,
        song TEXT NOT NULL,
        title TEXT,
        artist TEXT,
        cover TEXT,
        seconds INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (day, song)
    );
    )",
    // 11: history taken out (2.16.14), so sync and the website do not bring
    // it back: progress saved until cleared_at is gone. scope is a series
    // id, "<media>:*" for one kind's whole history, or "*" for all of it.
    R"(
    CREATE TABLE history_cleared (scope TEXT PRIMARY KEY, cleared_at INTEGER NOT NULL);
    )",
};

QSqlDatabase connection() {
    const auto name = QStringLiteral("sc-%1").arg(quintptr(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name)) {
        auto db = QSqlDatabase::database(name, false);
        if (db.isOpen())
            return db;
    }
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(g_path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open())
        throw Error(QStringLiteral("Could not open the library database: %1").arg(db.lastError().text()));
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return db;
}

QSqlQuery run(const QString &sql, const QVariantList &binds) {
    auto db = connection();
    QSqlQuery query(db);
    query.setForwardOnly(true);
    if (!query.prepare(sql))
        throw Error(QStringLiteral("Database error: %1").arg(query.lastError().text()));
    for (const auto &value : binds)
        query.addBindValue(value);
    if (!query.exec())
        throw Error(QStringLiteral("Database error: %1").arg(query.lastError().text()));
    return query;
}

bool isWrite(const QString &sql) {
    const auto head = sql.trimmed().left(8).toUpper();
    return !head.startsWith(QLatin1String("SELECT")) && !head.startsWith(QLatin1String("WITH"));
}

} // namespace

void Database::open(const QString &path) {
    g_path = path;
    auto db = connection();
    QSqlQuery query(db);
    query.exec(QStringLiteral("PRAGMA user_version"));
    int version = query.next() ? query.value(0).toInt() : 0;
    const int target = int(std::size(kMigrations));
    for (; version < target; ++version) {
        db.transaction();
        for (const auto &statement : QString::fromUtf8(kMigrations[version]).split(QLatin1Char(';'))) {
            if (statement.trimmed().isEmpty())
                continue;
            QSqlQuery step(db);
            if (!step.exec(statement)) {
                db.rollback();
                throw Error(QStringLiteral("Database upgrade failed: %1").arg(step.lastError().text()));
            }
        }
        QSqlQuery(db).exec(QStringLiteral("PRAGMA user_version = %1").arg(version + 1));
        db.commit();
    }
}

QString Database::path() { return g_path; }

QVariantList Database::query(const QString &sql, const QVariantList &binds) {
    std::unique_ptr<QMutexLocker<QRecursiveMutex>> lock;
    if (isWrite(sql))
        lock = std::make_unique<QMutexLocker<QRecursiveMutex>>(&g_writeLock);
    auto q = run(sql, binds);
    QVariantList rows;
    if (!q.isSelect())
        return rows;
    const auto record = q.record();
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < record.count(); ++i)
            row.insert(record.fieldName(i), q.value(i));
        rows.append(row);
    }
    return rows;
}

QVariantMap Database::one(const QString &sql, const QVariantList &binds) {
    const auto rows = query(sql, binds);
    return rows.isEmpty() ? QVariantMap() : rows.first().toMap();
}

QVariant Database::scalar(const QString &sql, const QVariantList &binds) {
    auto q = run(sql, binds);
    return q.next() ? q.value(0) : QVariant();
}

void Database::exec(const QString &sql, const QVariantList &binds) {
    QMutexLocker lock(&g_writeLock);
    run(sql, binds);
}

void Database::transaction(const std::function<void()> &work) {
    QMutexLocker lock(&g_writeLock);
    auto db = connection();
    db.transaction();
    try {
        work();
        db.commit();
    } catch (...) {
        db.rollback();
        throw;
    }
}

std::optional<QByteArray> Database::cacheGet(const QString &key, bool allowStale) {
    auto q = run(QStringLiteral("SELECT value, expires FROM cache WHERE key = ?"), {key});
    if (!q.next())
        return std::nullopt;
    if (!allowStale && q.value(1).toLongLong() < now())
        return std::nullopt;
    return q.value(0).toByteArray();
}

void Database::cachePut(const QString &key, const QByteArray &value, qint64 ttlSeconds) {
    QMutexLocker lock(&g_writeLock);
    run(QStringLiteral("INSERT INTO cache(key, value, expires) VALUES(?, ?, ?) "
                       "ON CONFLICT(key) DO UPDATE SET value = excluded.value, expires = excluded.expires"),
        {key, value, now() + ttlSeconds});
}

void Database::cachePrune() {
    // Keep expired rows for a week as stale fallbacks when a site is down.
    exec(QStringLiteral("DELETE FROM cache WHERE expires < ?"), {now() - 7 * 86400});
}

QVariant Database::pref(const QString &key, const QVariant &fallback) {
    auto q = run(QStringLiteral("SELECT value FROM prefs WHERE key = ?"), {key});
    if (!q.next())
        return fallback;
    const auto doc = QJsonDocument::fromJson(q.value(0).toByteArray());
    if (doc.isNull())
        return fallback;
    return doc.object().value(QStringLiteral("v")).toVariant();
}

void Database::setPref(const QString &key, const QVariant &value) {
    QJsonObject wrapper{{"v", QJsonValue::fromVariant(value)}};
    exec(QStringLiteral("INSERT INTO prefs(key, value) VALUES(?, ?) "
                        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
         {key, QJsonDocument(wrapper).toJson(QJsonDocument::Compact)});
}

qint64 Database::now() { return QDateTime::currentSecsSinceEpoch(); }

} // namespace sc
