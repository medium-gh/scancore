#include "ImageCache.h"

#include "sources/local/Local.h"

#include "Http.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QSaveFile>
#include <QScopeGuard>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>

namespace sc {

namespace {

QString g_dir;
std::atomic<qint64> g_budget = 1024LL * 1024 * 1024;

// Concurrent requests for the same image share one download.
QMutex g_inflightMutex;
QWaitCondition g_inflightDone;
QHash<QString, int> g_inflight;

QMutex g_writersMutex;
QHash<QString, ImageCache::Writer> g_writers;
QHash<QString, ImageCache::RefererWriter> g_refererWriters;

ImageCache::RefererWriter refererWriterFor(const QString &referer) {
    const auto colon = referer.indexOf(QLatin1Char(':'));
    if (colon <= 0 || referer.startsWith(QLatin1String("http")))
        return {};
    QMutexLocker lock(&g_writersMutex);
    return g_refererWriters.value(referer.left(colon));
}

ImageCache::Writer writerFor(const QString &url) {
    const auto colon = url.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return {};
    QMutexLocker lock(&g_writersMutex);
    return g_writers.value(url.left(colon));
}

QString pathFor(const QString &url) {
    const auto hash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("%1/%2/%3").arg(g_dir, QString::fromLatin1(hash.left(2)), QString::fromLatin1(hash));
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    auto data = file.readAll();
    // Touched so trimming keeps recently viewed images; a day is close
    // enough for that, and spares a write for every cover drawn.
    const auto now = QDateTime::currentDateTime();
    if (file.fileTime(QFileDevice::FileModificationTime).secsTo(now) > 24 * 3600)
        file.setFileTime(now, QFileDevice::FileModificationTime);
    file.close();
    return data;
}

} // namespace

void ImageCache::init(const QString &dir, qint64 budgetBytes) {
    g_dir = dir;
    g_budget = budgetBytes;
    QDir().mkpath(dir);
}

void ImageCache::setBudget(qint64 bytes) { g_budget = bytes; }

void ImageCache::setSchemeWriter(const QString &scheme, Writer writer) {
    QMutexLocker lock(&g_writersMutex);
    g_writers.insert(scheme, std::move(writer));
}

void ImageCache::setRefererWriter(const QString &scheme, RefererWriter writer) {
    QMutexLocker lock(&g_writersMutex);
    g_refererWriters.insert(scheme, std::move(writer));
}

void ImageCache::forget(const QString &url) {
    if (url.startsWith(QLatin1String("file:")) || url.startsWith(QLatin1String("content:")) || url.startsWith(QLatin1Char('/')) ||
        url.startsWith(QLatin1String("pdfpage:")))
        return;
    QFile::remove(pathFor(url));
}

namespace {
// A page (a browser check, "image not available") where a picture was
// expected: extensions' clients hand over whatever the site answered.
bool looksLikePage(const QByteArray &data) {
    const auto start = data.left(256).trimmed().toLower();
    return start.startsWith('<') && (start.contains("<html") || start.contains("<!doctype") || start.contains("<head"));
}
} // namespace

QByteArray ImageCache::fetch(const QString &url, const QString &referer) {
    if (url.startsWith(QLatin1String("pdfpage:")))
        return local::renderPdfPage(url);
    // Android: files in a folder chosen there are content:// addresses.
    if (url.startsWith(QLatin1String("content:"))) {
        QFile file(url);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    if (url.startsWith(QLatin1String("file:")) || url.startsWith(QLatin1Char('/'))) {
        QFile file(url.startsWith(QLatin1String("file:")) ? QUrl(url).toLocalFile() : url);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    const auto path = pathFor(url);
    {
        QMutexLocker lock(&g_inflightMutex);
        while (g_inflight.contains(path)) {
            if (CancelScope::cancelled())
                throw Error(QStringLiteral("Cancelled"), 499);
            g_inflightDone.wait(&g_inflightMutex, 100);
        }
        if (QFileInfo::exists(path)) {
            lock.unlock();
            if (auto data = readFile(path); !data.isEmpty())
                return data;
            lock.relock();
        }
        g_inflight.insert(path, 1);
    }
    auto release = qScopeGuard([&] {
        QMutexLocker lock(&g_inflightMutex);
        g_inflight.remove(path);
        g_inflightDone.wakeAll();
    });

    QDir().mkpath(QFileInfo(path).absolutePath());
    // What an extension wrote is kept only when it is a picture.
    const auto written = [&]() {
        auto data = readFile(path);
        if (data.isEmpty() || looksLikePage(data)) {
            QFile::remove(path);
            throw Error(data.isEmpty() ? QStringLiteral("Image unavailable.") : QStringLiteral("The site returned a page instead of an image."),
                        502);
        }
        return data;
    };
    if (const auto writer = writerFor(url)) {
        writer(url, path);
        return written();
    }
    if (const auto writer = refererWriterFor(referer)) {
        writer(url, referer, path);
        return written();
    }

    Request request;
    request.url = QUrl(url);
    request.lane = Lane::Images;
    request.timeoutMs = 30000;
    request.maxBytes = 48 * 1024 * 1024;
    request.headers = {{"Accept", Http::ImageAccept}};
    if (!referer.isEmpty())
        request.headers.append(QPair<QByteArray, QByteArray>("Referer", referer.toUtf8()));
    Response response;
    for (int attempt = 0;; ++attempt) {
        try {
            response = Http::send(request);
        } catch (const Error &e) {
            if (attempt >= 1 || e.status() == 499)
                throw;
            continue;
        }
        if (response.ok() || attempt >= 1 || response.status < 500)
            break;
    }
    if (!response.ok())
        throw Error(QStringLiteral("Image unavailable (HTTP %1).").arg(response.status), response.status);
    if (response.contentType.startsWith("text/"))
        throw Error(QStringLiteral("The site returned a page instead of an image."), 502);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(response.body);
        file.commit();
    }
    return response.body;
}

qint64 ImageCache::sizeOnDisk() {
    qint64 total = 0;
    QDirIterator it(g_dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

void ImageCache::clear() {
    QDir(g_dir).removeRecursively();
    QDir().mkpath(g_dir);
}

void ImageCache::trim() {
    struct Entry {
        QString path;
        qint64 size;
        QDateTime used;
    };
    QList<Entry> entries;
    qint64 total = 0;
    QDirIterator it(g_dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        entries.append({info.absoluteFilePath(), info.size(), info.lastModified()});
        total += info.size();
    }
    if (total <= g_budget)
        return;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.used < b.used; });
    const qint64 target = g_budget * 8 / 10;
    for (const auto &entry : entries) {
        if (total <= target)
            break;
        if (QFile::remove(entry.path))
            total -= entry.size;
    }
}

} // namespace sc
