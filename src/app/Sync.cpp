#include "Sync.h"

#include "Activity.h"
#include "Library.h"
#include "Live.h"
#include "core/Database.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamReader>

namespace sc {

namespace {

constexpr int kFormatVersion = 1;
// Removals older than this are no longer sent: every device has seen them.
constexpr qint64 kRemovalLifetime = 180 * 86400;

QString platformName() {
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

const auto kRestoreOwnPref = QStringLiteral("sync.restoreOwn");

QString ownFile() { return Activity::deviceId() + QStringLiteral(".json"); }

// ------------------------------------------------------------------ folder

class FolderStore : public Sync::Store {
public:
    explicit FolderStore(const QString &root) : m_dir(root + QStringLiteral("/SCANCORE Sync/devices")) {}
    void ensure() override {
        if (!QDir().mkpath(m_dir.path()))
            throw Error(QStringLiteral("Could not use the folder %1.").arg(QDir::toNativeSeparators(m_dir.path())));
    }
    QList<Sync::Entry> list() override {
        QList<Sync::Entry> entries;
        for (const auto &info : m_dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files))
            entries.append({info.fileName(), QString::number(info.lastModified().toMSecsSinceEpoch()) + QLatin1Char('/') +
                                                 QString::number(info.size()), {}});
        return entries;
    }
    QByteArray read(const QString &name) override {
        QFile file(m_dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            throw Error(QStringLiteral("Could not read %1.").arg(name));
        return file.readAll();
    }
    void write(const QString &name, const QByteArray &data) override {
        QSaveFile file(m_dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            throw Error(QStringLiteral("Could not write to %1.").arg(QDir::toNativeSeparators(m_dir.path())));
    }

private:
    QDir m_dir;
};

// ------------------------------------------------------------------ WebDAV

class WebDavStore : public Sync::Store {
public:
    WebDavStore(QString url, const QString &user, const QString &password) : m_root(std::move(url)) {
        if (!m_root.endsWith(QLatin1Char('/')))
            m_root += QLatin1Char('/');
        m_auth = "Basic " + (user + QLatin1Char(':') + password).toUtf8().toBase64();
    }
    void ensure() override {
        for (const auto &path : {QStringLiteral("SCANCORE/"), QStringLiteral("SCANCORE/devices/")}) {
            const auto response = call("MKCOL", m_root + path);
            // 201 made it; 405 means it is there already.
            if (!response.ok() && response.status != 405 && response.status != 301)
                fail(response, path);
        }
    }
    QList<Sync::Entry> list() override {
        const QByteArray body = R"(<?xml version="1.0" encoding="utf-8"?>)"
                                R"(<d:propfind xmlns:d="DAV:"><d:prop><d:getetag/><d:getlastmodified/>)"
                                R"(<d:getcontentlength/></d:prop></d:propfind>)";
        const auto response = call("PROPFIND", devices(), body, {{"Depth", "1"}, {"Content-Type", "application/xml"}});
        if (response.status != 207)
            fail(response, QStringLiteral("SCANCORE/devices/"));
        QList<Sync::Entry> entries;
        QXmlStreamReader xml(response.body);
        QString href, tag;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.namespaceUri() == QLatin1String("DAV:")) {
                const auto name = xml.name();
                if (name == QLatin1String("response"))
                    href.clear(), tag.clear();
                else if (name == QLatin1String("href"))
                    href = xml.readElementText();
                else if (name == QLatin1String("getetag") || name == QLatin1String("getlastmodified") ||
                         name == QLatin1String("getcontentlength"))
                    tag += xml.readElementText() + QLatin1Char('|');
            } else if (xml.isEndElement() && xml.name() == QLatin1String("response") &&
                       xml.namespaceUri() == QLatin1String("DAV:")) {
                const auto file = QUrl::fromPercentEncoding(href.section(QLatin1Char('/'), -1).toUtf8());
                if (file.endsWith(QLatin1String(".json")))
                    entries.append({file, tag, {}});
            }
        }
        return entries;
    }
    QByteArray read(const QString &name) override {
        const auto response = call("GET", devices() + QString::fromLatin1(QUrl::toPercentEncoding(name)));
        if (!response.ok())
            fail(response, name);
        return response.body;
    }
    void write(const QString &name, const QByteArray &data) override {
        const auto response = call("PUT", devices() + QString::fromLatin1(QUrl::toPercentEncoding(name)), data,
                                   {{"Content-Type", "application/json"}});
        if (!response.ok())
            fail(response, name);
    }

private:
    QString devices() const { return m_root + QStringLiteral("SCANCORE/devices/"); }
    Response call(const QByteArray &method, const QString &url, const QByteArray &body = {}, Headers headers = {}) {
        Request request;
        request.method = method;
        request.url = QUrl(url);
        request.body = body;
        headers.append({"Authorization", m_auth});
        request.headers = headers;
        request.timeoutMs = 30000;
        return Http::send(request);
    }
    [[noreturn]] static void fail(const Response &response, const QString &what) {
        if (response.status == 401 || response.status == 403)
            throw Error(QStringLiteral("The WebDAV server refused the user name or password."), response.status);
        if (response.status == 404 || response.status == 409)
            throw Error(QStringLiteral("The WebDAV server has no folder at that address."), response.status);
        if (response.status == 507)
            throw Error(QStringLiteral("The WebDAV server is out of space."), response.status);
        throw Error(QStringLiteral("The WebDAV server answered %1 for %2.").arg(response.status).arg(what), response.status);
    }

    QString m_root;
    QByteArray m_auth;
};

// ------------------------------------------------------------------ website

// A SCANCORE website keeps each account's device snapshots
// (/api/app/devices), signed in through the app's cookies.
class ServerStore : public Sync::Store {
public:
    explicit ServerStore(QString site) : m_site(std::move(site)) {}
    void ensure() override {}
    QList<Sync::Entry> list() override {
        const auto since = Database::pref(QStringLiteral("account.devicesSince"), 0).toLongLong();
        const auto response =
            Http::get(QUrl(m_site + QStringLiteral("/api/app/devices?since=%1").arg(since)), {{"Accept", "application/json"}}, 30000);
        if (!response.ok())
            throw Error(QStringLiteral("The website answered %1 for device sync.").arg(response.status), response.status);
        QList<Sync::Entry> entries;
        for (const auto &value : response.json().object().value("devices").toArray()) {
            const auto device = value.toObject();
            const auto updated = qint64(device.value("updatedAt").toDouble());
            m_newest = std::max(m_newest, updated);
            entries.append({device.value("deviceId").toString() + QStringLiteral(".json"), QString::number(updated),
                            QJsonDocument(device.value("data").toObject()).toJson(QJsonDocument::Compact)});
        }
        return entries;
    }
    QByteArray read(const QString &) override { return {}; }
    void write(const QString &name, const QByteArray &data) override {
        const auto snapshot = QJsonDocument::fromJson(data).object();
        const auto device = snapshot.value("device").toObject();
        Request request;
        request.method = "PUT";
        request.url = QUrl(m_site + QStringLiteral("/api/app/devices/") + name.chopped(5));
        request.body = QJsonDocument(QJsonObject{{"name", device.value("name")},
                                                 {"platform", device.value("platform")},
                                                 {"data", snapshot}})
                           .toJson(QJsonDocument::Compact);
        request.headers = {{"Content-Type", "application/json"}, {"Accept", "application/json"}};
        request.timeoutMs = 30000;
        const auto response = Http::send(request);
        if (!response.ok())
            throw Error(QStringLiteral("The website answered %1 when saving this device.").arg(response.status), response.status);
        if (m_newest > 0)
            Database::setPref(QStringLiteral("account.devicesSince"), m_newest);
    }

private:
    QString m_site;
    qint64 m_newest = 0;
};

QJsonObject seriesJson(const QVariantMap &row) {
    QJsonObject out;
    for (const auto *key : {"media", "source", "sourceUrl", "title", "cover", "author", "status", "genres"}) {
        const auto value = row.value(QLatin1String(key)).toString();
        if (!value.isEmpty())
            out.insert(QLatin1String(key), value);
    }
    const auto extra = QJsonDocument::fromJson(row.value("extra").toByteArray());
    if (extra.isObject())
        out.insert(QStringLiteral("extra"), extra.object());
    return out;
}

} // namespace

// ------------------------------------------------------------------ snapshot

QJsonObject Sync::snapshot() {
    QJsonObject series;
    for (const auto &value : Database::query(QStringLiteral(R"(
            SELECT id, media, source, source_url AS sourceUrl, title, cover, author, status, genres, extra FROM series
            WHERE id IN (SELECT series_id FROM library UNION SELECT series_id FROM progress) AND source != 'local'
        )"))) {
        const auto row = value.toMap();
        series.insert(row.value("id").toString(), seriesJson(row));
    }
    QJsonObject library;
    for (const auto &value : Database::query(
             QStringLiteral("SELECT series_id, category, added_at, COALESCE(changed_at, added_at) AS changed, favourite FROM library "
                            "WHERE series_id NOT LIKE '%:local:%'"))) {
        const auto row = value.toMap();
        // [category, added, changed, favourite]; apps before 2.0 read the
        // first three.
        library.insert(row.value("series_id").toString(),
                       QJsonArray{row.value("category").toString(), row.value("added_at").toLongLong(), row.value("changed").toLongLong(),
                                  row.value("favourite").toInt()});
    }
    QJsonObject removed;
    for (const auto &value : Database::query(QStringLiteral("SELECT series_id, removed_at FROM library_removed WHERE removed_at > ?"),
                                             {Database::now() - kRemovalLifetime})) {
        const auto row = value.toMap();
        removed.insert(row.value("series_id").toString(), row.value("removed_at").toLongLong());
    }
    QJsonObject progress;
    for (const auto &value : Database::query(QStringLiteral(
             "SELECT series_id, unit_key, unit_title, unit_number, unit_url, position, percent, completed, updated_at FROM progress "
             "WHERE series_id NOT LIKE '%:local:%'"))) {
        const auto row = value.toMap();
        const auto id = row.value("series_id").toString();
        auto units = progress.value(id).toObject();
        units.insert(row.value("unit_key").toString(),
                     QJsonArray{row.value("unit_title").toString(), row.value("unit_number").toString(), row.value("unit_url").toString(),
                                row.value("position").toString(), row.value("percent").toDouble(), row.value("completed").toInt(),
                                row.value("updated_at").toLongLong()});
        progress.insert(id, units);
    }
    // Bookmarks, and those taken off lately: {series: {chapter: when}}.
    QJsonObject bookmarks;
    for (const auto &value : Database::query(QStringLiteral("SELECT series_id, unit_key, created_at FROM bookmarks WHERE series_id NOT LIKE '%:local:%'"))) {
        const auto row = value.toMap();
        auto units = bookmarks.value(row.value("series_id").toString()).toObject();
        units.insert(row.value("unit_key").toString(), row.value("created_at").toLongLong());
        bookmarks.insert(row.value("series_id").toString(), units);
    }
    QJsonObject bookmarksRemoved;
    for (const auto &value : Database::query(QStringLiteral("SELECT series_id, unit_key, removed_at FROM bookmarks_removed WHERE removed_at > ?"),
                                             {Database::now() - kRemovalLifetime})) {
        const auto row = value.toMap();
        auto units = bookmarksRemoved.value(row.value("series_id").toString()).toObject();
        units.insert(row.value("unit_key").toString(), row.value("removed_at").toLongLong());
        bookmarksRemoved.insert(row.value("series_id").toString(), units);
    }
    // Each series' own settings: {series: [settings, changed]}.
    QJsonObject seriesSettings;
    for (const auto &value : Database::query(QStringLiteral("SELECT series_id, value, changed_at FROM series_settings WHERE series_id NOT LIKE '%:local:%'"))) {
        const auto row = value.toMap();
        seriesSettings.insert(row.value("series_id").toString(),
                              QJsonArray{QJsonDocument::fromJson(row.value("value").toByteArray()).object(), row.value("changed_at").toLongLong()});
    }
    return {{"format", "scancore-sync"},
            {"version", kFormatVersion},
            {"device", QJsonObject{{"id", Activity::deviceId()},
                                   {"name", Activity::deviceName()},
                                   {"platform", platformName()},
                                   {"app", QCoreApplication::applicationVersion()}}},
            {"updatedAt", Database::now()},
            {"series", series},
            {"library", library},
            {"removed", removed},
            {"progress", progress},
            {"bookmarks", bookmarks},
            {"bookmarksRemoved", bookmarksRemoved},
            {"seriesSettings", seriesSettings},
            // The user's own library lists: [[{id, name}…], changed, {order,
            // hidden, names}]. Versions before 2.10.0 read the first two.
            {"categories", QJsonArray{QJsonArray::fromVariantList(Library::storedCategories()), Library::categoriesChangedAt(),
                                      QJsonObject::fromVariantMap(Library::storedListLayout())}},
            {"activity", Activity::rowsForSync()}};
}

QString Sync::platform() { return platformName(); }

QJsonObject Sync::unitPayload(const QString &seriesId, const QString &unitKey) {
    if (seriesId.contains(QLatin1String(":local:")))
        return {};
    const auto rows = Database::query(QStringLiteral("SELECT unit_title, unit_number, unit_url, position, percent, completed, updated_at "
                                                     "FROM progress WHERE series_id = ? AND unit_key = ?"),
                                      {seriesId, unitKey});
    const auto series = Database::query(QStringLiteral("SELECT media, source, source_url AS sourceUrl, title, cover, author, status, genres, "
                                                       "extra FROM series WHERE id = ? AND source != 'local'"),
                                        {seriesId});
    if (rows.isEmpty() || series.isEmpty())
        return {};
    const auto row = rows.first().toMap();
    return {{"seriesId", seriesId},
            {"series", seriesJson(series.first().toMap())},
            {"unitKey", unitKey},
            {"unit", QJsonArray{row.value("unit_title").toString(), row.value("unit_number").toString(), row.value("unit_url").toString(),
                                row.value("position").toString(), row.value("percent").toDouble(), row.value("completed").toInt(),
                                row.value("updated_at").toLongLong()}}};
}

bool Sync::mergeUnit(const QJsonObject &payload) {
    const auto id = payload.value("seriesId").toString();
    const auto key = payload.value("unitKey").toString();
    const auto p = payload.value("unit").toArray();
    if (id.isEmpty() || key.isEmpty() || p.size() < 7 || id.contains(QLatin1String(":local:")))
        return false;
    const qint64 at = p.at(6).toInteger();
    const auto local = Database::scalar(QStringLiteral("SELECT updated_at FROM progress WHERE series_id = ? AND unit_key = ?"), {id, key});
    if (local.isValid() && at <= local.toLongLong())
        return false;
    bool merged = false;
    Database::transaction([&] {
        if (!Database::scalar(QStringLiteral("SELECT 1 FROM series WHERE id = ?"), {id}).isValid()) {
            auto map = payload.value("series").toObject().toVariantMap();
            if (map.value(QStringLiteral("title")).toString().isEmpty())
                return;
            map.insert(QStringLiteral("id"), id);
            Library::upsertSeries(map);
        }
        Database::query(QStringLiteral(R"(
            INSERT INTO progress(series_id, unit_key, unit_title, unit_number, unit_url, position, percent, completed, updated_at)
            VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(series_id, unit_key) DO UPDATE SET unit_title = excluded.unit_title,
                unit_number = excluded.unit_number, unit_url = excluded.unit_url, position = excluded.position,
                percent = excluded.percent, completed = MAX(progress.completed, excluded.completed),
                updated_at = excluded.updated_at
        )"),
                        {id, key, p.at(0).toString(), p.at(1).toString(), p.at(2).toString(),
                         p.at(3).toString().isEmpty() ? QVariant() : QVariant(p.at(3).toString()), std::clamp(p.at(4).toDouble(), 0.0, 1.0),
                         p.at(5).toInt() ? 1 : 0, at});
        merged = true;
    });
    return merged;
}

int Sync::merge(const QJsonObject &s, bool own) {
    if (s.value("format").toString() != QLatin1String("scancore-sync"))
        throw Error(QStringLiteral("That is not a SCANCORE sync file."));
    const auto device = s.value("device").toObject().value("id").toString();
    if (device.isEmpty() || (device == Activity::deviceId() && !own))
        return 0;
    const auto series = s.value("series").toObject();
    const auto library = s.value("library").toObject();
    const auto removed = s.value("removed").toObject();
    const auto progress = s.value("progress").toObject();
    int changes = 0;

    QSet<QString> ensured;
    // A series the progress or library refers to, from the snapshot, unless
    // stored here already.
    const auto ensureSeries = [&](const QString &id) {
        if (ensured.contains(id))
            return true;
        ensured.insert(id);
        if (Database::scalar(QStringLiteral("SELECT 1 FROM series WHERE id = ?"), {id}).isValid())
            return true;
        const auto row = series.value(id).toObject();
        if (row.isEmpty())
            return false;
        auto map = row.toVariantMap();
        map.insert(QStringLiteral("id"), id);
        Library::upsertSeries(map);
        return true;
    };

    Database::transaction([&] {
        // Library: the latest change to each series wins.
        QHash<QString, qint64> localSaved;
        for (const auto &value : Database::query(QStringLiteral("SELECT series_id, COALESCE(changed_at, added_at) AS at FROM library")))
            localSaved.insert(value.toMap().value("series_id").toString(), value.toMap().value("at").toLongLong());
        QHash<QString, qint64> localRemoved;
        for (const auto &value : Database::query(QStringLiteral("SELECT series_id, removed_at FROM library_removed")))
            localRemoved.insert(value.toMap().value("series_id").toString(), value.toMap().value("removed_at").toLongLong());
        QSet<QString> ids;
        for (const auto &id : library.keys())
            ids.insert(id);
        for (const auto &id : removed.keys())
            ids.insert(id);
        for (const auto &id : std::as_const(ids)) {
            const auto entry = library.value(id).toArray();
            const qint64 savedAt = entry.isEmpty() ? -1 : entry.at(2).toInteger();
            const qint64 removedAt = removed.contains(id) ? removed.value(id).toInteger() : -1;
            const qint64 remoteAt = std::max(savedAt, removedAt);
            const qint64 localAt = std::max(localSaved.value(id, -1), localRemoved.value(id, -1));
            if (remoteAt <= localAt)
                continue;
            if (savedAt >= removedAt) {
                if (!ensureSeries(id))
                    continue;
                // An older app sends no favourite; the one here stays.
                const bool hasFavourite = entry.size() > 3;
                Database::query(QStringLiteral("INSERT INTO library(series_id, category, added_at, changed_at, favourite) VALUES(?, ?, ?, ?, ?) "
                                               "ON CONFLICT(series_id) DO UPDATE SET category = excluded.category, "
                                               "changed_at = excluded.changed_at%1")
                                    .arg(hasFavourite ? QStringLiteral(", favourite = excluded.favourite") : QString()),
                                {id, entry.at(0).toString(QStringLiteral("reading")), entry.at(1).toInteger(), savedAt,
                                 hasFavourite ? entry.at(3).toInt() : 0});
                Database::query(QStringLiteral("DELETE FROM library_removed WHERE series_id = ?"), {id});
            } else {
                Database::query(QStringLiteral("DELETE FROM library WHERE series_id = ?"), {id});
                Database::query(QStringLiteral("INSERT OR REPLACE INTO library_removed(series_id, removed_at) VALUES(?, ?)"),
                                {id, removedAt});
            }
            ++changes;
        }

        // Progress: the latest save of each chapter or episode wins.
        QHash<QString, qint64> localProgress;
        for (const auto &value : Database::query(QStringLiteral("SELECT series_id, unit_key, updated_at FROM progress"))) {
            const auto row = value.toMap();
            localProgress.insert(row.value("series_id").toString() + QLatin1Char('\n') + row.value("unit_key").toString(),
                                 row.value("updated_at").toLongLong());
        }
        for (auto it = progress.begin(); it != progress.end(); ++it) {
            const auto units = it.value().toObject();
            bool known = false;
            for (auto unit = units.begin(); unit != units.end(); ++unit) {
                const auto p = unit.value().toArray();
                const qint64 at = p.at(6).toInteger();
                if (at <= localProgress.value(it.key() + QLatin1Char('\n') + unit.key(), -1))
                    continue;
                if (!known && !(known = ensureSeries(it.key())))
                    break;
                Database::query(QStringLiteral(R"(
                    INSERT INTO progress(series_id, unit_key, unit_title, unit_number, unit_url, position, percent, completed, updated_at)
                    VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
                    ON CONFLICT(series_id, unit_key) DO UPDATE SET unit_title = excluded.unit_title,
                        unit_number = excluded.unit_number, unit_url = excluded.unit_url, position = excluded.position,
                        percent = excluded.percent, completed = MAX(progress.completed, excluded.completed),
                        updated_at = excluded.updated_at
                )"),
                                {it.key(), unit.key(), p.at(0).toString(), p.at(1).toString(), p.at(2).toString(),
                                 p.at(3).toString().isEmpty() ? QVariant() : QVariant(p.at(3).toString()),
                                 std::clamp(p.at(4).toDouble(), 0.0, 1.0), p.at(5).toInt() ? 1 : 0, at});
                ++changes;
            }
        }

        // Bookmarks: taking one and taking it off, the latest wins.
        const auto bookmarks = s.value("bookmarks").toObject();
        const auto bookmarksRemoved = s.value("bookmarksRemoved").toObject();
        const auto localBookmark = [](const QString &id, const QString &key) {
            const auto added = Database::scalar(QStringLiteral("SELECT created_at FROM bookmarks WHERE series_id = ? AND unit_key = ?"), {id, key});
            const auto removed = Database::scalar(QStringLiteral("SELECT removed_at FROM bookmarks_removed WHERE series_id = ? AND unit_key = ?"), {id, key});
            return std::max(added.isValid() ? added.toLongLong() : -1, removed.isValid() ? removed.toLongLong() : -1);
        };
        for (auto it = bookmarks.begin(); it != bookmarks.end(); ++it) {
            const auto units = it.value().toObject();
            for (auto unit = units.begin(); unit != units.end(); ++unit) {
                const qint64 at = unit.value().toInteger();
                if (at <= localBookmark(it.key(), unit.key()) || !ensureSeries(it.key()))
                    continue;
                Database::query(QStringLiteral("INSERT OR REPLACE INTO bookmarks(series_id, unit_key, created_at) VALUES(?, ?, ?)"),
                                {it.key(), unit.key(), at});
                Database::query(QStringLiteral("DELETE FROM bookmarks_removed WHERE series_id = ? AND unit_key = ?"), {it.key(), unit.key()});
                ++changes;
            }
        }
        for (auto it = bookmarksRemoved.begin(); it != bookmarksRemoved.end(); ++it) {
            const auto units = it.value().toObject();
            for (auto unit = units.begin(); unit != units.end(); ++unit) {
                const qint64 at = unit.value().toInteger();
                if (at <= localBookmark(it.key(), unit.key()))
                    continue;
                Database::query(QStringLiteral("DELETE FROM bookmarks WHERE series_id = ? AND unit_key = ?"), {it.key(), unit.key()});
                Database::query(QStringLiteral("INSERT OR REPLACE INTO bookmarks_removed(series_id, unit_key, removed_at) VALUES(?, ?, ?)"),
                                {it.key(), unit.key(), at});
                ++changes;
            }
        }

        // The user's own lists: the latest change wins, as a whole.
        const auto categories = s.value("categories").toArray();
        if (categories.size() >= 2 && categories.at(1).toInteger() > Library::categoriesChangedAt()) {
            // From an older version (no layout): this device's layout stays.
            Library::storeCategories(categories.at(0).toArray().toVariantList(), categories.at(1).toInteger(),
                                     categories.size() >= 3 ? categories.at(2).toObject().toVariantMap() : Library::storedListLayout());
            ++changes;
        }

        // Series settings: the latest change wins, as a whole.
        const auto settings = s.value("seriesSettings").toObject();
        for (auto it = settings.begin(); it != settings.end(); ++it) {
            const auto entry = it.value().toArray();
            const qint64 at = entry.at(1).toInteger();
            const auto local = Database::scalar(QStringLiteral("SELECT changed_at FROM series_settings WHERE series_id = ?"), {it.key()});
            if (at <= (local.isValid() ? local.toLongLong() : -1) || !ensureSeries(it.key()))
                continue;
            Database::query(QStringLiteral("INSERT OR REPLACE INTO series_settings(series_id, value, changed_at) VALUES(?, ?, ?)"),
                            {it.key(), QString::fromUtf8(QJsonDocument(entry.at(0).toObject()).toJson(QJsonDocument::Compact)), at});
            ++changes;
        }
    });
    Activity::mergeRows(device, s.value("activity").toArray());
    return changes;
}

QVariantMap Sync::exchange(Store &store, const QString &seenKey) {
    store.ensure();
    auto seen = Database::pref(seenKey).toMap();
    auto devices = Database::pref(QStringLiteral("sync.devices")).toMap();
    const auto own = ownFile();
    // This device's own copy is read back once after its library was taken
    // off it (signed out): signing in again brings it back instead of
    // overwriting it with nothing.
    const bool restoreOwn = Database::pref(kRestoreOwnPref).toBool();
    int merged = 0;
    for (const auto &entry : store.list()) {
        if ((entry.name == own && !restoreOwn) || (entry.name != own && seen.value(entry.name).toString() == entry.tag))
            continue;
        const auto data = entry.inlineData.isEmpty() ? store.read(entry.name) : entry.inlineData;
        const auto snapshot = QJsonDocument::fromJson(data).object();
        try {
            merged += merge(snapshot, entry.name == own);
        } catch (const Error &e) {
            qWarning().noquote() << "Sync:" << entry.name << e.message();
        }
        seen.insert(entry.name, entry.tag);
        const auto device = snapshot.value("device").toObject();
        if (!device.value("id").toString().isEmpty())
            devices.insert(device.value("id").toString(), QVariantMap{{"name", device.value("name").toString()},
                                                                      {"platform", device.value("platform").toString()},
                                                                      {"updatedAt", snapshot.value("updatedAt").toInteger()}});
    }
    // This device's snapshot, unless nothing in it changed since the last
    // write (otherwise two devices would keep answering each other).
    auto mine = snapshot();
    auto content = mine;
    content.remove(QStringLiteral("updatedAt"));
    const auto hash = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(content).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex());
    const auto hashKey = seenKey + QStringLiteral(".written");
    if (Database::pref(hashKey).toString() != hash) {
        store.write(own, QJsonDocument(mine).toJson(QJsonDocument::Compact));
        Database::setPref(hashKey, hash);
    }
    Database::setPref(seenKey, seen);
    Database::setPref(QStringLiteral("sync.devices"), devices);
    if (restoreOwn)
        Database::setPref(kRestoreOwnPref, false);
    return {{"merged", merged}, {"devices", devices.size()}};
}

std::unique_ptr<Sync::Store> Sync::serverStore(const QString &site) { return std::make_unique<ServerStore>(site); }

// ------------------------------------------------------------------ service

Sync::Sync(QObject *parent) : QObject(parent) {
    m_soon.setSingleShot(true);
    connect(&m_soon, &QTimer::timeout, this, [this] {
        if (!kind().isEmpty() && !m_syncing)
            sync();
    });
    // Other devices' changes arrive without anything happening here.
    connect(&m_periodic, &QTimer::timeout, this, [this] {
        if (!kind().isEmpty() && !m_syncing)
            sync();
    });
    m_periodic.start(15 * 60 * 1000);
    connect(Library::instance(), &Library::changed, this, &Sync::schedule);
    if (!kind().isEmpty())
        QTimer::singleShot(8000, this, [this] { sync(); });
}

Sync *Sync::instance() {
    static auto *sync = [] {
        auto *created = new Sync;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return sync;
}

QString Sync::kind() const { return Database::pref(QStringLiteral("sync.kind")).toString(); }
QString Sync::location() const { return Database::pref(QStringLiteral("sync.location")).toString(); }
QString Sync::user() const { return Database::pref(QStringLiteral("sync.user")).toString(); }
QString Sync::deviceName() const { return Activity::deviceName(); }

QString Sync::lastSync() const {
    const auto at = Database::pref(QStringLiteral("sync.lastSync")).toLongLong();
    return at > 0 ? QString::number(at) : QString();
}

QVariantList Sync::devices() const {
    QVariantList out{QVariantMap{{"id", Activity::deviceId()}, {"name", Activity::deviceName()}, {"platform", platformName()},
                                 {"updatedAt", lastSync()}, {"self", true}}};
    const auto seen = Database::pref(QStringLiteral("sync.devices")).toMap();
    for (auto it = seen.begin(); it != seen.end(); ++it) {
        if (it.key() == Activity::deviceId())
            continue;
        auto device = it.value().toMap();
        device.insert(QStringLiteral("id"), it.key());
        device.insert(QStringLiteral("self"), false);
        out.append(device);
    }
    return out;
}

std::unique_ptr<Sync::Store> Sync::currentStore() const {
    const auto k = kind();
    if (k == QLatin1String("folder"))
        return std::make_unique<FolderStore>(location());
    if (k == QLatin1String("webdav"))
        return std::make_unique<WebDavStore>(location(), user(), Database::pref(QStringLiteral("sync.password")).toString());
    return nullptr;
}

void Sync::schedule() {
    if (kind().isEmpty() || Live::applying())
        return;
    if (m_syncing) {
        m_dirty = true;
        return;
    }
    if (m_soon.isActive())
        return;
    constexpr qint64 kQuiet = 20000;
    constexpr qint64 kMinGap = 90000;
    const qint64 sinceLast = QDateTime::currentMSecsSinceEpoch() - m_lastEnded;
    m_soon.start(int(std::clamp(kMinGap - sinceLast, kQuiet, kMinGap)));
}

Task *Sync::useWebDav(const QString &url, const QString &user, const QString &password) {
    const auto address = url.trimmed();
    auto *task = Task::run([address, user, password]() -> QVariant {
        const QUrl parsed(address);
        if (!parsed.isValid() || (parsed.scheme() != QLatin1String("https") && parsed.scheme() != QLatin1String("http")) ||
            parsed.host().isEmpty())
            throw Error(QStringLiteral("Enter the WebDAV address, starting with https://."));
        WebDavStore store(address, user, password);
        store.ensure();
        store.list();
        return QVariantMap{{"ok", true}};
    });
    task->then(this, [this, address, user, password](const QVariant &) {
        Database::setPref(QStringLiteral("sync.kind"), QStringLiteral("webdav"));
        Database::setPref(QStringLiteral("sync.location"), address);
        Database::setPref(QStringLiteral("sync.user"), user);
        Database::setPref(QStringLiteral("sync.password"), password);
        Database::setPref(QStringLiteral("sync.seen"), QVariantMap());
        Database::setPref(QStringLiteral("sync.seen.written"), QString());
        emit changed();
        sync();
    });
    return task;
}

Task *Sync::useFolder(const QUrl &folder) {
    const auto path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    auto *task = Task::run([path]() -> QVariant {
        if (path.isEmpty() || !QFileInfo(path).isDir())
            throw Error(QStringLiteral("Choose a folder."));
        FolderStore store(path);
        store.ensure();
        store.list();
        return QVariantMap{{"ok", true}};
    });
    task->then(this, [this, path](const QVariant &) {
        Database::setPref(QStringLiteral("sync.kind"), QStringLiteral("folder"));
        Database::setPref(QStringLiteral("sync.location"), path);
        Database::setPref(QStringLiteral("sync.user"), QString());
        Database::setPref(QStringLiteral("sync.password"), QString());
        Database::setPref(QStringLiteral("sync.seen"), QVariantMap());
        Database::setPref(QStringLiteral("sync.seen.written"), QString());
        emit changed();
        sync();
    });
    return task;
}

void Sync::stop() {
    for (const auto *key : {"sync.kind", "sync.location", "sync.user", "sync.password", "sync.seen.written"})
        Database::setPref(QLatin1String(key), QString());
    Database::setPref(QStringLiteral("sync.seen"), QVariantMap());
    Database::setPref(QStringLiteral("sync.devices"), QVariantMap());
    Database::setPref(QStringLiteral("sync.lastSync"), 0);
    m_soon.stop();
    m_error.clear();
    emit changed();
}

Task *Sync::sync() {
    if (m_syncing && m_task)
        return m_task;
    auto store = std::shared_ptr<Store>(currentStore());
    if (!store)
        return Task::resolved(QVariantMap{{"merged", 0}, {"devices", 0}});
    m_soon.stop();
    m_dirty = false;
    m_syncing = true;
    emit changed();
    Activity::instance()->flush();
    auto *task = Task::run([store]() -> QVariant { return exchange(*store, QStringLiteral("sync.seen")); });
    QQmlEngine::setObjectOwnership(task, QQmlEngine::CppOwnership);
    m_task = task;
    connect(task, &Task::changed, this, [this, task] {
        if (task->loading())
            return;
        m_error = task->ok() ? QString() : task->error();
        if (task->ok()) {
            Database::setPref(QStringLiteral("sync.lastSync"), Database::now());
            if (task->result().toMap().value(QStringLiteral("merged")).toInt() > 0) {
                // Merged changes are not a reason to sync again.
                const bool dirty = m_dirty;
                Library::instance()->notifyChanged();
                m_dirty = dirty;
            }
        }
        m_syncing = false;
        m_lastEnded = QDateTime::currentMSecsSinceEpoch();
        emit changed();
        task->deleteLater();
        if (m_dirty) {
            m_dirty = false;
            schedule();
        }
    });
    return task;
}

} // namespace sc
