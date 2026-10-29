#include "Downloads.h"

#include "Anime.h"
#include "Library.h"
#include "Novels.h"
#include "OnDevice.h"
#include "App.h"
#include "Platform.h"
#include "core/Database.h"
#include "core/Epub.h"
#include "core/ImageCache.h"
#include "core/Http.h"
#include "core/VideoSave.h"
#include "extensions/Host.h"
#include "sources/comics/Registry.h"
#include "sources/local/Local.h"
#include "sources/local/MihonLayout.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <functional>

namespace sc {

namespace {

constexpr int kConcurrent = 2;

QString hashed(const QString &value) {
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

QString root() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/downloads");
}

// A LIKE pattern for paths in the app's own downloads folder.
QString ownPattern() {
    auto prefix = root() + QLatin1Char('/');
    prefix.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('%'), QLatin1String("\\%")).replace(QLatin1Char('_'), QLatin1String("\\_"));
    return prefix + QLatin1Char('%');
}

QString extensionFor(const QByteArray &contentType, const QString &url) {
    if (contentType.contains("webp"))
        return QStringLiteral("webp");
    if (contentType.contains("png"))
        return QStringLiteral("png");
    if (contentType.contains("gif"))
        return QStringLiteral("gif");
    if (contentType.contains("avif"))
        return QStringLiteral("avif");
    if (contentType.contains("jpeg") || contentType.contains("jpg"))
        return QStringLiteral("jpg");
    const auto suffix = QFileInfo(QUrl(url).path()).suffix().toLower();
    return suffix.isEmpty() ? QStringLiteral("jpg") : suffix;
}

// Low-resolution pages enlarged twice on the computer (waifu2x), when the
// series asks (its reader's readerUpscale, else the reader's own setting)
// and the upscaler is installed. A page that fails keeps its own picture.
bool wantsUpscale(const QString &seriesId) {
    auto wanted = Library::instance()->seriesSettings(seriesId).value(QStringLiteral("reader")).toMap().value(QStringLiteral("readerUpscale"));
    if (!wanted.isValid())
        wanted = App::instance()->settings().value(QStringLiteral("readerUpscale"));
    return wanted.toBool() && OnDevice::instance()->upscalerInstalled();
}

// `progress(i)` after each page.
void upscalePages(const QString &folder, QJsonArray &index, const std::function<void(int)> &progress) {
    for (int i = 0; i < index.size(); ++i) {
        progress(i);
        if (CancelScope::cancelled())
            return;
        auto entry = index[i].toObject();
        const auto path = folder + QLatin1Char('/') + entry.value(QStringLiteral("file")).toString();
        QImage page(path);
        // Pages this wide already look sharp on any screen.
        if (page.isNull() || page.width() >= 1200)
            continue;
        try {
            const auto big = OnDevice::instance()->upscale(page);
            const auto name = QFileInfo(path).completeBaseName() + QStringLiteral(".jpg");
            if (!big.save(folder + QLatin1Char('/') + name + QStringLiteral(".part"), "JPEG", 92))
                continue;
            if (name != QFileInfo(path).fileName())
                QFile::remove(path);
            QFile::remove(folder + QLatin1Char('/') + name);
            QFile::rename(folder + QLatin1Char('/') + name + QStringLiteral(".part"), folder + QLatin1Char('/') + name);
            entry.insert(QStringLiteral("file"), name);
            entry.insert(QStringLiteral("width"), big.width());
            entry.insert(QStringLiteral("height"), big.height());
            entry.insert(QStringLiteral("upscaled"), true);
            index[i] = entry;
        } catch (const Error &e) {
            qWarning("Upscaling %s: %s", qPrintable(path), e.what());
            return;
        }
    }
}

void writeJson(const QString &path, const QJsonDocument &doc) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw Error(QStringLiteral("Could not write to the downloads folder."));
    file.write(doc.toJson(QJsonDocument::Compact));
    if (!file.commit())
        throw Error(QStringLiteral("Could not write to the downloads folder."));
}

} // namespace

Downloads::Downloads(QObject *parent) : QObject(parent) {
    m_throttle.setSingleShot(true);
    m_throttle.setInterval(250);
    connect(&m_throttle, &QTimer::timeout, this, [this] {
        ++m_revision;
        emit changed();
    });
    // Anything interrupted by the last exit starts again.
    QTimer::singleShot(1500, this, [this] {
        Database::exec(QStringLiteral("UPDATE downloads SET status = 'queued' WHERE status = 'running'"));
        pump();
    });
    // What Mihon downloaded since the last start (it may still be in use).
    QTimer::singleShot(20000, this, [this] {
        if (!local::folders().isEmpty() || linkedCount() > 0)
            relinkMihon();
    });
}

Downloads *Downloads::instance() {
    static auto *downloads = [] {
        auto *created = new Downloads;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return downloads;
}

void Downloads::notify() {
    if (!m_throttle.isActive())
        m_throttle.start();
}

int Downloads::pending() const {
    return Database::scalar(QStringLiteral("SELECT COUNT(*) FROM downloads WHERE status IN ('queued', 'running')")).toInt();
}

QString Downloads::unitFolder(const QString &seriesId, const QString &unitKey) {
    return QStringLiteral("%1/%2/%3").arg(root(), hashed(seriesId), hashed(unitKey));
}

void Downloads::enqueue(const QVariantMap &series, const QVariantList &units) {
    const auto seriesId = series.value(QStringLiteral("id")).toString();
    if (seriesId.isEmpty())
        return;
    // Called from QML: a storage failure is reported, not thrown through
    // the interface (which ends the app).
    try {
        // Kept, so the downloads list can name it before anything is read.
        if (!series.value(QStringLiteral("title")).toString().isEmpty())
            Library::upsertSeries(series);
        Database::transaction([&] {
            for (const auto &value : units) {
                const auto u = value.toMap();
                Database::query(QStringLiteral(R"(
                    INSERT INTO downloads(series_id, unit_key, unit_title, unit_url, status, created_at, extra)
                    VALUES(?, ?, ?, ?, 'queued', ?, ?)
                    ON CONFLICT(series_id, unit_key) DO UPDATE SET
                        status = CASE WHEN downloads.status = 'done' THEN 'done' ELSE 'queued' END, error = NULL,
                        extra = COALESCE(excluded.extra, downloads.extra)
                )"),
                                {seriesId, u.value(QStringLiteral("key")).toString(), u.value(QStringLiteral("title")).toString(),
                                 u.value(QStringLiteral("url")).toString(), Database::now(),
                                 // Anime: where the episode's video comes from.
                                 u.contains(QStringLiteral("download"))
                                     ? QVariant(QString::fromUtf8(QJsonDocument::fromVariant(u.value(QStringLiteral("download"))).toJson(QJsonDocument::Compact)))
                                     : QVariant()});
            }
        });
    } catch (const Error &e) {
        qWarning() << "Downloads: not queued:" << e.message();
        return;
    }
    notify();
    pump();
}

void Downloads::pump() {
    while (m_running.size() < kConcurrent) {
        QVariantList exclude;
        QStringList marks;
        for (int id : std::as_const(m_running)) {
            exclude.append(id);
            marks.append(QStringLiteral("?"));
        }
        const auto sql = QStringLiteral("SELECT d.*, s.title AS series_title FROM downloads d "
                                        "LEFT JOIN series s ON s.id = d.series_id WHERE d.status = 'queued'%1 "
                                        "ORDER BY d.created_at, d.id LIMIT 1")
                             .arg(marks.isEmpty() ? QString() : QStringLiteral(" AND d.id NOT IN (%1)").arg(marks.join(',')));
        const auto row = Database::one(sql, exclude);
        if (row.isEmpty())
            return;
        const int id = row.value(QStringLiteral("id")).toInt();
        m_running.insert(id);
        const auto flag = std::make_shared<std::atomic_bool>(false);
        m_flags.insert(id, flag);
        Database::exec(QStringLiteral("UPDATE downloads SET status = 'running' WHERE id = ?"), {id});
        notify();
        runInBackground([this, row, flag] { runJob(row, flag); });
    }
}

void Downloads::runJob(const QVariantMap &row, const CancelFlag &flag) {
    CancelScope scope(flag);
    const int id = row.value(QStringLiteral("id")).toInt();
    const auto seriesId = row.value(QStringLiteral("series_id")).toString();
    const auto unitKey = row.value(QStringLiteral("unit_key")).toString();
    const auto unitUrl = row.value(QStringLiteral("unit_url")).toString();
    const auto folder = unitFolder(seriesId, unitKey);
    QString error;
    try {
        QDir().mkpath(folder);
        if (seriesId.startsWith(QLatin1String("novel:"))) {
            const auto chapter = Novels::fetchChapter(unitUrl);
            writeJson(folder + QStringLiteral("/chapter.json"), QJsonDocument::fromVariant(chapter));
            Database::exec(QStringLiteral("UPDATE downloads SET done = 1, total = 1 WHERE id = ?"), {id});
        } else if (seriesId.startsWith(QLatin1String("comic:"))) {
            auto source = comics::Registry::forUrl(unitUrl);
            if (!source)
                throw Error(QStringLiteral("This source is no longer available."));
            const auto panels = source->panels(unitUrl);
            if (panels.isEmpty())
                throw Error(QStringLiteral("This chapter has no readable pages."));
            const bool upscale = wantsUpscale(seriesId);
            Database::exec(QStringLiteral("UPDATE downloads SET total = ?, done = 0 WHERE id = ?"), {panels.size() * (upscale ? 2 : 1), id});
            QJsonArray index;
            for (int i = 0; i < panels.size(); ++i) {
                if (CancelScope::cancelled())
                    throw Error(QStringLiteral("Cancelled"), 499);
                const auto referer = source->imageReferer();
                Response response;
                // Extension pages ("extimage:") and images an extension
                // fetches itself come through the image cache's writers.
                if (!panels[i].url.startsWith(QLatin1String("http")) || !referer.startsWith(QLatin1String("http"))) {
                    for (int attempt = 0; attempt < 3 && !response.ok(); ++attempt) {
                        try {
                            response.body = ImageCache::fetch(panels[i].url, referer);
                            response.status = 200;
                            response.contentType = QMimeDatabase().mimeTypeForData(response.body).name().toLatin1();
                        } catch (const Error &e) {
                            if (attempt == 2 || e.status() == 499)
                                throw;
                        }
                    }
                }
                Request request;
                request.url = QUrl(panels[i].url);
                request.lane = Lane::Images;
                request.timeoutMs = 30000;
                request.maxBytes = 40 * 1024 * 1024;
                request.headers = {{"Referer", referer.toUtf8()},
                                   {"Accept", Http::ImageAccept}};
                for (int attempt = 0; attempt < 3 && !response.ok(); ++attempt) {
                    try {
                        response = Http::send(request);
                        if (response.ok())
                            break;
                    } catch (const Error &e) {
                        if (attempt == 2 || e.status() == 499)
                            throw;
                    }
                }
                if (!response.ok())
                    throw Error(QStringLiteral("Page %1 could not be downloaded (HTTP %2).").arg(i + 1).arg(response.status));
                const auto name = QStringLiteral("%1.%2").arg(i + 1, 4, 10, QLatin1Char('0'))
                                      .arg(extensionFor(response.contentType, panels[i].url));
                QSaveFile file(folder + QLatin1Char('/') + name);
                if (!file.open(QIODevice::WriteOnly) || file.write(response.body) != response.body.size() || !file.commit())
                    throw Error(QStringLiteral("Could not write to the downloads folder."));
                index.append(QJsonObject{{"file", name}, {"width", panels[i].width}, {"height", panels[i].height}});
                Database::exec(QStringLiteral("UPDATE downloads SET done = ? WHERE id = ?"), {i + 1, id});
                QMetaObject::invokeMethod(this, [this] { notify(); });
            }
            // Enlarging is the second half of the progress bar.
            if (upscale)
                upscalePages(folder, index, [&](int i) {
                    Database::exec(QStringLiteral("UPDATE downloads SET done = ? WHERE id = ?"), {int(panels.size()) + i, id});
                    QMetaObject::invokeMethod(this, [this] { notify(); });
                });
            writeJson(folder + QStringLiteral("/index.json"), QJsonDocument(QJsonObject{{"panels", index}}));
        } else if (seriesId.startsWith(QLatin1String("anime:"))) {
            const auto from = QJsonDocument::fromJson(row.value(QStringLiteral("extra")).toByteArray()).object();
            if (from.value("source").toString().isEmpty())
                throw Error(QStringLiteral("Only episodes from an anime source can be downloaded."));
            const auto ask = [&] {
                return Anime::videoForDownload(from.value("source").toString(), from.value("episodeUrl").toString(),
                                               from.value("animeUrl").toString(), from.value("number").toDouble(-1));
            };
            // The video the player had (same server, address already
            // resolved), else what the source gives now.
            auto video = from.value("video").toObject().toVariantMap();
            if (video.value(QStringLiteral("url")).toString().isEmpty())
                video = ask();
            const auto headersOf = [](const QVariantMap &v) {
                Headers out;
                const auto wanted = v.value(QStringLiteral("headers")).toMap();
                for (auto it = wanted.begin(); it != wanted.end(); ++it)
                    out.append({it.key().toUtf8(), it.value().toString().toUtf8()});
                return out;
            };
            // Progress in thousandths: parts of a stream, or bytes of a file.
            Database::exec(QStringLiteral("UPDATE downloads SET total = 1000, done = 0 WHERE id = ?"), {id});
            const auto progress = [&](qint64 done, qint64 total) {
                const qint64 permille = total > 0 ? std::min<qint64>(999, done * 1000 / total) : 0;
                Database::exec(QStringLiteral("UPDATE downloads SET done = ? WHERE id = ?"), {permille, id});
                QMetaObject::invokeMethod(this, [this] { notify(); });
            };
            auto headers = headersOf(video);
            video::Saved saved;
            try {
                saved = video::save(QUrl(video.value(QStringLiteral("url")).toString()), headers, folder, progress);
            } catch (const Error &e) {
                // A player's address can expire (signed links); the source
                // is asked once more.
                if (e.status() == 499 || !from.contains("video"))
                    throw;
                video = ask();
                headers = headersOf(video);
                saved = video::save(QUrl(video.value(QStringLiteral("url")).toString()), headers, folder, progress);
            }
            // Subtitles and separate audio tracks beside it. One that cannot
            // be fetched leaves the episode without it, not undownloaded.
            QJsonObject index{{"file", saved.file}, {"hls", saved.hls}, {"skips", QJsonArray::fromVariantList(video.value("skips").toList())}};
            int n = 0;
            for (const auto *kind : {"subtitles", "audio"}) {
                const bool subtitles = QLatin1String(kind) == QLatin1String("subtitles");
                QJsonArray tracks;
                for (const auto &value : video.value(QLatin1String(kind)).toList()) {
                    const auto track = value.toMap();
                    const QUrl url(track.value(QStringLiteral("url")).toString());
                    if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
                        continue;
                    auto suffix = QFileInfo(url.path()).suffix().toLower();
                    if (suffix.isEmpty() || suffix.size() > 5)
                        suffix = subtitles ? QStringLiteral("vtt") : QStringLiteral("m4a");
                    const auto name = QStringLiteral("%1-%2.%3").arg(subtitles ? QStringLiteral("sub") : QStringLiteral("audio")).arg(++n).arg(suffix);
                    try {
                        video::streamToFile(url, headers, folder + QLatin1Char('/') + name, [](qint64, qint64) {});
                        tracks.append(QJsonObject{{"file", name}, {"lang", track.value(QStringLiteral("lang")).toString()}});
                    } catch (const Error &e) {
                        if (e.status() == 499)
                            throw;
                        qWarning().noquote() << "Track not downloaded:" << e.message();
                    }
                }
                index.insert(QLatin1String(kind), tracks);
            }
            writeJson(folder + QStringLiteral("/video.json"), QJsonDocument(index));
            Database::exec(QStringLiteral("UPDATE downloads SET done = 1000 WHERE id = ?"), {id});
        } else {
            throw Error(QStringLiteral("This can not be downloaded."));
        }
    } catch (const Error &e) {
        error = e.message();
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
    }
    Database::exec(QStringLiteral("UPDATE downloads SET status = ?, error = ?, path = ? WHERE id = ?"),
                   {error.isEmpty() ? QStringLiteral("done") : QStringLiteral("failed"),
                    error.isEmpty() ? QVariant() : QVariant(error), folder, id});
    const auto title = QStringLiteral("%1 · %2").arg(row.value(QStringLiteral("series_title")).toString(),
                                                      row.value(QStringLiteral("unit_title")).toString());
    QMetaObject::invokeMethod(this, [this, id, title, ok = error.isEmpty(), flag, folder] {
        m_running.remove(id);
        m_flags.remove(id);
        // Removed while it ran: what it had written goes too.
        if (*flag) {
            QDir(folder).removeRecursively();
            Database::exec(QStringLiteral("DELETE FROM downloads WHERE id = ?"), {id});
        }
        notify();
        if (ok && !*flag)
            emit finished(title);
        pump();
    });
}

Task *Downloads::items() {
    return Task::run([]() -> QVariant {
        // SCANCORE's own downloads one by one; chapters read from Mihon's
        // folder one row per series (there can be thousands).
        auto rows = Database::query(QStringLiteral(
            "SELECT d.id, d.series_id AS seriesId, d.unit_key AS unitKey, d.unit_title AS unitTitle, d.unit_url AS unitUrl, "
            "d.status, d.done, d.total, d.error, d.created_at AS createdAt, s.title AS seriesTitle, s.cover, s.media, "
            "s.source_url AS sourceUrl, s.source, s.extra FROM downloads d LEFT JOIN series s ON s.id = d.series_id "
            "WHERE d.status != 'done' OR d.path IS NULL OR d.path LIKE ? ESCAPE '\\' "
            "ORDER BY CASE d.status WHEN 'running' THEN 0 WHEN 'queued' THEN 1 WHEN 'failed' THEN 2 ELSE 3 END, "
            "d.created_at DESC, d.id"),
                                    {ownPattern()});
        const auto linked = Database::query(QStringLiteral(
            "SELECT -MIN(d.id) AS id, d.series_id AS seriesId, COUNT(*) AS linked, 'done' AS status, 1 AS done, 1 AS total, "
            "MAX(d.created_at) AS createdAt, s.title AS seriesTitle, s.cover, s.media, s.source_url AS sourceUrl, s.source, s.extra "
            "FROM downloads d LEFT JOIN series s ON s.id = d.series_id "
            "WHERE d.status = 'done' AND d.path NOT LIKE ? ESCAPE '\\' GROUP BY d.series_id ORDER BY s.title COLLATE NOCASE"),
                                                {ownPattern()});
        rows.append(linked);
        // The series' extra (an AniList anime's id) as an object.
        for (auto &value : rows) {
            auto row = value.toMap();
            row.insert(QStringLiteral("extra"), QJsonDocument::fromJson(row.value(QStringLiteral("extra")).toByteArray()).toVariant());
            value = row;
        }
        return rows;
    });
}

void Downloads::unlinkSeries(const QString &seriesId) {
    auto unlinked = Database::pref(QStringLiteral("mihon.unlinked")).toStringList();
    const auto rows = Database::query(QStringLiteral("SELECT id, path FROM downloads WHERE series_id = ? AND status = 'done' AND path NOT LIKE ? ESCAPE '\\'"),
                                      {seriesId, ownPattern()});
    Database::transaction([&] {
        for (const auto &value : rows) {
            const auto row = value.toMap();
            if (const auto path = row.value(QStringLiteral("path")).toString(); !unlinked.contains(path))
                unlinked.append(path);
            Database::query(QStringLiteral("DELETE FROM downloads WHERE id = ?"), {row.value(QStringLiteral("id"))});
        }
    });
    Database::setPref(QStringLiteral("mihon.unlinked"), unlinked);
    notify();
}

QVariantMap Downloads::states(const QString &seriesId) const {
    QVariantMap out;
    for (const auto &value : Database::query(
             QStringLiteral("SELECT unit_key, status, done, total FROM downloads WHERE series_id = ?"), {seriesId})) {
        const auto row = value.toMap();
        out.insert(row.value(QStringLiteral("unit_key")).toString(),
                   QVariantMap{{"status", row.value(QStringLiteral("status"))},
                               {"done", row.value(QStringLiteral("done"))},
                               {"total", row.value(QStringLiteral("total"))}});
    }
    return out;
}

void Downloads::remove(int id) {
    const auto row = Database::one(QStringLiteral("SELECT series_id, unit_key, path FROM downloads WHERE id = ?"), {id});
    if (row.isEmpty())
        return;
    // The user's own files (Mihon's downloads) stay where they are; the
    // chapter is remembered as unlinked so the next scan leaves it alone.
    if (const auto path = row.value(QStringLiteral("path")).toString(); isExternal(path)) {
        auto unlinked = Database::pref(QStringLiteral("mihon.unlinked")).toStringList();
        if (!unlinked.contains(path))
            unlinked.append(path);
        Database::setPref(QStringLiteral("mihon.unlinked"), unlinked);
        Database::exec(QStringLiteral("DELETE FROM downloads WHERE id = ?"), {id});
        notify();
        return;
    }
    // Running: it stops, then goes (see runJob).
    if (m_running.contains(id)) {
        if (const auto flag = m_flags.value(id))
            *flag = true;
        return;
    }
    QDir(unitFolder(row.value(QStringLiteral("series_id")).toString(), row.value(QStringLiteral("unit_key")).toString()))
        .removeRecursively();
    Database::exec(QStringLiteral("DELETE FROM downloads WHERE id = ?"), {id});
    notify();
}

void Downloads::removeUnit(const QString &seriesId, const QString &unitKey) {
    const auto id = Database::scalar(QStringLiteral("SELECT id FROM downloads WHERE series_id = ? AND unit_key = ?"),
                                     {seriesId, unitKey});
    if (id.isValid())
        remove(id.toInt());
}

void Downloads::retry(int id) {
    Database::exec(QStringLiteral("UPDATE downloads SET status = 'queued', error = NULL WHERE id = ? AND status = 'failed'"),
                   {id});
    notify();
    pump();
}

void Downloads::retryFailed() {
    Database::exec(QStringLiteral("UPDATE downloads SET status = 'queued', error = NULL WHERE status = 'failed'"));
    notify();
    pump();
}

void Downloads::clearFinished() {
    Database::exec(QStringLiteral("DELETE FROM downloads WHERE status = 'failed'"));
    notify();
}

QString Downloads::folder() const { return root(); }

QString Downloads::sizeOnDisk() const {
    qint64 total = 0;
    QDirIterator it(root(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return QLocale().formattedDataSize(total);
}

bool Downloads::isExternal(const QString &path) {
    return !path.isEmpty() && !path.startsWith(root() + QLatin1Char('/'));
}


namespace {

// The chapters' pages, from the folder of images or CBZ Mihon saved.
QVariantList externalPanels(const QString &path) {
    QVariantList out;
    try {
        for (const auto &url : local::pages(path))
            out.append(QVariantMap{{"url", url}, {"width", 0}, {"height", 0}});
    } catch (const Error &) {
        // Moved or deleted: the chapter reads from its source instead.
    }
    return out;
}

} // namespace

int Downloads::linkedCount() const {
    return Database::scalar(QStringLiteral("SELECT COUNT(*) FROM downloads WHERE status = 'done' AND path NOT LIKE ? ESCAPE '\\'"),
                            {ownPattern()})
        .toInt();
}

QVariantMap Downloads::mihonStatus() const {
    auto status = Database::pref(QStringLiteral("mihon.link")).toMap();
    status.insert(QStringLiteral("linking"), m_linking);
    return status;
}

void Downloads::relinkMihon() {
    if (m_linking) {
        m_linkAgain = true;
        return;
    }
    m_linking = true;
    emit mihonChanged();
    runInBackground([this] {
        QVariantMap result;
        QElapsedTimer timer;
        timer.start();
        try {
            result = linkMihonNow();
            qInfo().noquote() << "Mihon downloads:" << result.value(QStringLiteral("chapters")).toInt() << "chapters linked in"
                              << timer.elapsed() << "ms";
        } catch (const Error &e) {
            result = {{"error", e.message()}};
        }
        QMetaObject::invokeMethod(this, [this, result]() mutable {
            m_linking = false;
            // Asked again while it ran (a folder added meanwhile): this
            // result is not the last word.
            if (std::exchange(m_linkAgain, false)) {
                result.insert(QStringLiteral("more"), true);
                relinkMihon();
            }
            linked(result);
        });
    });
}

void Downloads::linked(const QVariantMap &result) {
    notify();
    emit mihonChanged();
    emit mihonLinked(result);
}

QVariantMap Downloads::linkMihonNow() {
    using namespace local;
    // One at a time (start-up, a folder added and an import can overlap).
    static QMutex running;
    QMutexLocker lock(&running);
    struct Root {
        QString chosen;
        QString downloads;
    };
    QList<Root> roots;
    for (const auto &folder : folders())
        if (const auto downloads = mihon::downloadsFolder(folder); !downloads.isEmpty())
            roots.append({folder, downloads});
    int linkedSeries = 0, linkedChapters = 0, unmatchedSeries = 0, unmatchedChapters = 0;
    // Every chapter file linked by this pass; links to anything else (gone
    // from the folder, or its folder no longer chosen) are dropped below.
    QSet<QString> linkedPaths;
    // Series folders no library entry has, for "On this device".
    QVariantList others;
    QVariantMap rootsOut;
    if (!roots.isEmpty()) {
        // The comics the user has: the library, and what they have read.
        struct Entry {
            QString id;
            QString source;
        };
        QHash<QString, QList<Entry>> byTitle;
        QList<Entry> all;
        for (const auto &value : Database::query(QStringLiteral(
                 "SELECT id, title, source FROM series WHERE media = 'comic' AND (id IN (SELECT series_id FROM library) "
                 "OR id IN (SELECT series_id FROM progress))"))) {
            const auto row = value.toMap();
            const Entry entry{row.value(QStringLiteral("id")).toString(), row.value(QStringLiteral("source")).toString()};
            byTitle[mihon::normalized(row.value(QStringLiteral("title")).toString())].append(entry);
            all.append(entry);
        }
        // "Name (EN)" for each source key, as Mihon names its folders.
        QHash<QString, QPair<QString, QString>> sourceNames;
        const auto sourceOf = [&](const QString &key) {
            if (!sourceNames.contains(key)) {
                const auto found = comics::Registry::byKey(key);
                // Built-in sources name their language: "MangaDex (Español)".
                auto label = found ? found->label() : QString();
                label.remove(QRegularExpression(QStringLiteral(R"(\s*\([^)]*\)$)")));
                sourceNames.insert(key, found ? qMakePair(mihon::normalized(label), found->language().toLower())
                                              : qMakePair(QString(), QString()));
            }
            return sourceNames.value(key);
        };
        const auto sameSource = [&](const QString &key, const mihon::Source &folder) {
            const auto [name, lang] = sourceOf(key);
            return !name.isEmpty() && name == mihon::normalized(folder.name) &&
                   (folder.lang.isEmpty() || lang.isEmpty() || lang == folder.lang || folder.lang == QLatin1String("all") ||
                    lang.section(QLatin1Char('-'), 0, 0) == folder.lang.section(QLatin1Char('-'), 0, 0));
        };
        // Mihon's hash of each chapter address, for the series of one source
        // (built when a folder's name matches no title: renamed series).
        QHash<QString, QHash<QString, QString>> hashesBySource;
        const auto hashOwners = [&](const mihon::Source &folder) -> const QHash<QString, QString> & {
            const auto key = folder.name + QLatin1Char('|') + folder.lang;
            if (!hashesBySource.contains(key)) {
                QHash<QString, QString> owners;
                for (const auto &entry : all) {
                    if (!sameSource(entry.source, folder))
                        continue;
                    for (const auto &value : Database::query(QStringLiteral("SELECT url FROM units WHERE series_id = ?"), {entry.id})) {
                        auto url = value.toMap().value(QStringLiteral("url")).toString();
                        if (const auto wrapped = ext::unwrapUrl(url))
                            url = wrapped->url;
                        for (const auto &hash : mihon::hashesFor(url))
                            owners.insert(hash, entry.id);
                    }
                }
                hashesBySource.insert(key, owners);
            }
            return hashesBySource[key];
        };
        const auto unlinkedList = Database::pref(QStringLiteral("mihon.unlinked")).toStringList();
        const QSet<QString> skip(unlinkedList.begin(), unlinkedList.end());
        for (const auto &root : std::as_const(roots)) {
            rootsOut.insert(root.chosen, root.downloads);
            for (const auto &source : mihon::sources(root.downloads)) {
                for (const auto &series : mihon::series(source)) {
                    if (CancelScope::cancelled())
                        throw Error(QStringLiteral("Cancelled"), 499);
                    const auto found = mihon::chapters(series.path);
                    if (found.isEmpty())
                        continue;
                    const auto candidates = byTitle.value(mihon::normalized(series.name));
                    // The source it was downloaded from, else the only
                    // library entry with that title, else the entry of that
                    // source whose chapters Mihon's hashes name.
                    QString chosen;
                    for (const auto &candidate : candidates)
                        if (sameSource(candidate.source, source)) {
                            chosen = candidate.id;
                            break;
                        }
                    if (chosen.isEmpty() && candidates.size() == 1)
                        chosen = candidates.first().id;
                    if (chosen.isEmpty()) {
                        QHash<QString, int> votes;
                        int hashed = 0;
                        for (const auto &c : found)
                            if (!c.hash.isEmpty()) {
                                ++hashed;
                                if (const auto owner = hashOwners(source).value(c.hash); !owner.isEmpty())
                                    ++votes[owner];
                            }
                        int best = 0;
                        for (auto it = votes.cbegin(); it != votes.cend(); ++it)
                            if (it.value() > best) {
                                best = it.value();
                                chosen = it.key();
                            }
                        // Most of its chapters, not a stray collision.
                        if (best * 2 < hashed)
                            chosen.clear();
                    }
                    if (chosen.isEmpty()) {
                        ++unmatchedSeries;
                        others.append(QVariantMap{{"path", series.path}, {"name", series.name}, {"source", source.name}, {"root", root.chosen}});
                        continue;
                    }
                    const auto units = Library::cachedUnits(chosen);
                    QList<mihon::Unit> wanted;
                    wanted.reserve(units.size());
                    for (const auto &value : units) {
                        const auto u = value.toMap();
                        const auto url = u.value(QStringLiteral("url")).toString();
                        const auto wrapped = ext::unwrapUrl(url);
                        wanted.append({wrapped ? wrapped->url : url, u.value(QStringLiteral("title")).toString(),
                                       u.value(QStringLiteral("scanlator")).toString(), u.value(QStringLiteral("number")).toString()});
                    }
                    const auto matches = mihon::matchChapters(found, wanted);
                    int here = 0;
                    Database::transaction([&] {
                        for (int i = 0; i < found.size(); ++i) {
                            if (matches[i] < 0) {
                                ++unmatchedChapters;
                                continue;
                            }
                            if (skip.contains(found[i].path) || skip.contains(series.path))
                                continue;
                            const auto unit = units[matches[i]].toMap();
                            // A copy SCANCORE downloaded itself stays.
                            Database::query(QStringLiteral(R"(
                                INSERT INTO downloads(series_id, unit_key, unit_title, unit_url, status, done, total, path, created_at)
                                VALUES(?, ?, ?, ?, 'done', 1, 1, ?, ?)
                                ON CONFLICT(series_id, unit_key) DO UPDATE SET path = excluded.path, status = 'done', error = NULL
                                    WHERE downloads.status != 'done' OR downloads.path IS NULL OR downloads.path = '' OR downloads.path = excluded.path
                                        OR downloads.path NOT LIKE ? ESCAPE '\'
                            )"),
                                            {chosen, unit.value(QStringLiteral("key")).toString(), unit.value(QStringLiteral("title")).toString(),
                                             unit.value(QStringLiteral("url")).toString(), found[i].path, Database::now(), ownPattern()});
                            linkedPaths.insert(found[i].path);
                            ++here;
                        }
                    });
                    if (here > 0) {
                        ++linkedSeries;
                        linkedChapters += here;
                    }
                }
            }
        }
    }
    // Links whose file is no longer there (or no longer chosen).
    QVariantList stale;
    for (const auto &value : Database::query(QStringLiteral("SELECT id, path FROM downloads WHERE status = 'done' AND path NOT LIKE ? ESCAPE '\\'"),
                                             {ownPattern()})) {
        const auto row = value.toMap();
        if (!linkedPaths.contains(row.value(QStringLiteral("path")).toString()))
            stale.append(row.value(QStringLiteral("id")));
    }
    if (!stale.isEmpty())
        Database::transaction([&] {
            for (const auto &id : stale)
                Database::query(QStringLiteral("DELETE FROM downloads WHERE id = ?"), {id});
        });
    const QVariantMap result{{"at", Database::now()},
                             {"folders", int(roots.size())},
                             {"roots", rootsOut},
                             {"series", linkedSeries},
                             {"chapters", linkedChapters},
                             {"unmatchedSeries", unmatchedSeries},
                             {"unmatchedChapters", unmatchedChapters}};
    Database::setPref(QStringLiteral("mihon.link"), result);
    Database::setPref(QStringLiteral("mihon.others"), others);
    return result;
}

QVariantList Downloads::localPanels(const QString &seriesId, const QString &unitKey) {
    const auto path = Database::scalar(QStringLiteral("SELECT path FROM downloads WHERE series_id = ? AND unit_key = ? AND status = 'done'"),
                                       {seriesId, unitKey}).toString();
    if (isExternal(path))
        return externalPanels(path);
    const auto folder = unitFolder(seriesId, unitKey);
    QFile file(folder + QStringLiteral("/index.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QVariantList out;
    for (const auto &value : QJsonDocument::fromJson(file.readAll()).object().value("panels").toArray()) {
        const auto p = value.toObject();
        out.append(QVariantMap{{"url", QUrl::fromLocalFile(folder + QLatin1Char('/') + p.value("file").toString()).toString()},
                               {"width", p.value("width").toInt()},
                               {"height", p.value("height").toInt()}});
    }
    return out;
}

QVariantMap Downloads::localVideo(const QString &seriesId, const QString &unitKey) const {
    const auto folder = unitFolder(seriesId, unitKey);
    QFile file(folder + QStringLiteral("/video.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto index = QJsonDocument::fromJson(file.readAll()).object();
    const auto path = folder + QLatin1Char('/') + index.value("file").toString();
    if (index.value("file").toString().isEmpty() || !QFileInfo::exists(path))
        return {};
    const auto tracks = [&](const char *kind) {
        QVariantList out;
        for (const auto &value : index.value(QLatin1String(kind)).toArray()) {
            const auto t = value.toObject();
            out.append(QVariantMap{{"url", QUrl::fromLocalFile(folder + QLatin1Char('/') + t.value("file").toString()).toString()},
                                   {"lang", t.value("lang").toString()}});
        }
        return out;
    };
    QVariantMap video{{"url", QUrl::fromLocalFile(path).toString()},
                      {"offline", true},
                      {"subtitles", tracks("subtitles")},
                      {"audio", tracks("audio")},
                      {"skips", index.value("skips").toArray().toVariantList()}};
    // mpv reads a local playlist as a list of files unless FFmpeg opens it
    // (only the video: external subtitles keep their own demuxer); its keys
    // and parts have names FFmpeg would otherwise refuse.
    if (index.value("hls").toBool())
        video.insert(QStringLiteral("mpvArgs"), QVariantMap{{"demuxer", "lavf"}, {"demuxer-lavf-o", "allowed_extensions=ALL"}});
    return video;
}

Task *Downloads::exportEpub(const QString &seriesId) {
    return Task::run([seriesId]() -> QVariant {
        const auto row = Library::instance()->seriesRow(seriesId);
        EpubBook book;
        book.id = seriesId;
        book.title = row.value(QStringLiteral("title")).toString();
        book.author = row.value(QStringLiteral("author")).toString();
        for (const auto &value : Library::cachedUnits(seriesId)) {
            const auto unit = value.toMap();
            const auto chapter = localNovelChapter(seriesId, unit.value(QStringLiteral("key")).toString());
            if (chapter.isEmpty())
                continue;
            auto title = chapter.value(QStringLiteral("title")).toString();
            if (title.isEmpty())
                title = unit.value(QStringLiteral("title")).toString();
            book.chapters.append({title, chapter.value(QStringLiteral("blocks")).toList()});
        }
        if (book.chapters.isEmpty())
            throw Error(tr("No chapters of this novel are downloaded. Download some first."));
        // The cover, when it still loads (a few seconds at most).
        const QUrl cover(row.value(QStringLiteral("cover")).toString());
        if (cover.scheme().startsWith(QLatin1String("http"))) {
            const auto image = Http::get(cover, {}, 8000);
            if (image.ok() && (image.body.startsWith("\xFF\xD8") || image.body.startsWith("\x89PNG")))
                book.cover = image.body;
        }
        const auto bytes = writeEpub(book);
        auto name = book.title;
        name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|]+)")), QStringLiteral(" "));
        name = name.simplified().left(80);
        if (name.isEmpty())
            name = QStringLiteral("novel");
#ifdef Q_OS_ANDROID
        const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/exports");
#else
        auto folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (folder.isEmpty())
            folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
        QDir().mkpath(folder);
        const auto path = folder + QLatin1Char('/') + name + QStringLiteral(".epub");
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw Error(tr("The book could not be saved in %1.").arg(folder));
        QVariantMap out{{"path", path}, {"chapters", int(book.chapters.size())}};
#ifdef Q_OS_ANDROID
        QMetaObject::invokeMethod(qApp, [path, title = book.title] {
            platform::shareFile(path, QStringLiteral("application/epub+zip"), title);
        });
        out.insert(QStringLiteral("shared"), true);
#endif
        return out;
    });
}

QVariantMap Downloads::localNovelChapter(const QString &seriesId, const QString &unitKey) {
    QFile file(unitFolder(seriesId, unitKey) + QStringLiteral("/chapter.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
}

} // namespace sc
