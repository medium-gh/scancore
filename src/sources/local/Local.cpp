#include "Local.h"

#include "core/Database.h"
#include "core/Http.h"

#include <QBuffer>
#include <QCollator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

namespace sc::local {

namespace {

const QStringList kImages{"jpg", "jpeg", "png", "webp", "gif", "avif", "bmp", "jxl"};
const QStringList kZip{"cbz", "zip"};
const QStringList kOtherArchives{"cbr", "rar", "cb7", "7z", "cbt", "tar"};
// Archives unpacked for reading are kept for this many chapters.
constexpr int kKeepUnpacked = 24;

QMutex g_unpackMutex;
PdfRenderer g_pdf;

QString suffix(const QString &name) { return QFileInfo(name).suffix().toLower(); }

QString cacheRoot() {
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/local");
    QDir().mkpath(dir);
    return dir;
}

// A folder in the cache for one version of a file.
QString cacheFor(const QFileInfo &info) {
    const auto key = info.absoluteFilePath() + QLatin1Char('|') + QString::number(info.size()) + QLatin1Char('|') +
                     QString::number(info.lastModified().toSecsSinceEpoch());
    return cacheRoot() + QLatin1Char('/') +
           QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

// Image files under a folder (and its subfolders, as archives often have
// one), in reading order.
QStringList imagesUnder(const QString &dir, bool recursive) {
    QStringList found;
    if (!recursive) {
        // By name: a content:// address has no file name in it.
        QList<Entry> images;
        for (const auto &entry : entries(dir))
            if (!entry.dir && isImage(entry.name))
                images.append(entry);
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(images.begin(), images.end(), [&](const Entry &a, const Entry &b) { return collator.compare(a.name, b.name) < 0; });
        for (const auto &entry : images)
            found.append(entry.path);
        return found;
    }
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto path = it.next();
        if (isImage(path) && !path.contains(QLatin1String("__MACOSX")))
            found.append(path);
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(found.begin(), found.end(), [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    return found;
}

// A file's address for the image loader: Android's content:// addresses
// (folders chosen there) as they are.
QString addressOf(const QString &path) {
    return path.startsWith(QLatin1String("content:")) ? path : QUrl::fromLocalFile(path).toString();
}

QStringList asFileUrls(const QStringList &paths) {
    QStringList urls;
    for (const auto &path : paths)
        urls.append(addressOf(path));
    return urls;
}

// Keeps the most recently read unpacked archives (by when their marker
// file was last written).
void trimUnpacked() {
    auto entries = QDir(cacheRoot()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    const auto readAt = [](const QFileInfo &dir) { return QFileInfo(dir.absoluteFilePath() + QStringLiteral("/.done")).lastModified(); };
    std::sort(entries.begin(), entries.end(), [&](const QFileInfo &a, const QFileInfo &b) { return readAt(a) > readAt(b); });
    for (int i = kKeepUnpacked; i < entries.size(); ++i)
        QDir(entries[i].absoluteFilePath()).removeRecursively();
}

void touch(const QString &file) {
    QFile marker(file);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        marker.write(QByteArray::number(QDateTime::currentSecsSinceEpoch()));
}

void unpackZip(const QString &archive, const QString &target) {
    QZipReader reader(archive);
    if (!reader.isReadable())
        throw Error(QStringLiteral("%1 could not be opened as a ZIP archive.").arg(QFileInfo(archive).fileName()));
    QStringList names;
    for (const auto &entry : reader.fileInfoList())
        if (entry.isFile && isImage(entry.filePath) && !entry.filePath.contains(QLatin1String("__MACOSX")))
            names.append(entry.filePath);
    naturalSort(names);
    int index = 0;
    for (const auto &name : names) {
        if (CancelScope::cancelled())
            throw Error(QStringLiteral("Cancelled"), 499);
        QFile out(QStringLiteral("%1/%2.%3").arg(target).arg(index++, 4, 10, QLatin1Char('0')).arg(suffix(name)));
        if (!out.open(QIODevice::WriteOnly) || out.write(reader.fileData(name)) < 0)
            throw Error(QStringLiteral("Could not unpack %1.").arg(QFileInfo(archive).fileName()));
    }
}

// RAR and 7z need a program that reads them: bsdtar (built into Windows 10
// and macOS as tar), unrar or 7-Zip.
void unpackOther(const QString &archive, const QString &target) {
    struct Tool {
        QString program;
        QStringList arguments;
    };
    QList<Tool> tools;
    const auto add = [&](const QString &name, const QStringList &arguments) {
        const auto path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            tools.append({path, arguments});
    };
    add(QStringLiteral("bsdtar"), {QStringLiteral("-xf"), archive, QStringLiteral("-C"), target});
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    add(QStringLiteral("tar"), {QStringLiteral("-xf"), archive, QStringLiteral("-C"), target});
#endif
    add(QStringLiteral("unrar"), {QStringLiteral("x"), QStringLiteral("-o+"), QStringLiteral("-inul"), archive, target + QLatin1Char('/')});
    for (const auto &name : {QStringLiteral("7z"), QStringLiteral("7zz"), QStringLiteral("7za")})
        add(name, {QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-o") + target, archive});
#ifdef Q_OS_WIN
    for (const auto &path : {QStringLiteral("C:/Program Files/7-Zip/7z.exe"), QStringLiteral("C:/Program Files (x86)/7-Zip/7z.exe")})
        if (QFileInfo::exists(path))
            tools.append({path, {QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-o") + target, archive}});
#endif
    if (tools.isEmpty())
        throw Error(QStringLiteral("Opening %1 files needs 7-Zip, unrar or bsdtar installed.").arg(suffix(archive).toUpper()));
#if QT_CONFIG(process)
    for (const auto &tool : tools) {
        QProcess process;
        process.start(tool.program, tool.arguments);
        if (process.waitForFinished(180000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
            !imagesUnder(target, true).isEmpty())
            return;
        process.kill();
        QDir(target).removeRecursively();
        QDir().mkpath(target);
    }
#endif
    throw Error(QStringLiteral("%1 could not be unpacked.").arg(QFileInfo(archive).fileName()));
}

// The archive's pages, unpacked once into the cache.
QStringList unpacked(const QFileInfo &info) {
    const auto target = cacheFor(info);
    const auto done = target + QStringLiteral("/.done");
    QMutexLocker lock(&g_unpackMutex);
    if (!QFileInfo::exists(done)) {
        QDir(target).removeRecursively();
        QDir().mkpath(target);
        if (kZip.contains(info.suffix().toLower()))
            unpackZip(info.absoluteFilePath(), target);
        else
            unpackOther(info.absoluteFilePath(), target);
        touch(done);
        trimUnpacked();
    } else {
        // Read again, so trimming keeps it longer.
        touch(done);
    }
    return imagesUnder(target, true);
}

} // namespace

QList<Entry> entries(const QString &dir) {
    QList<Entry> out;
#ifdef Q_OS_ANDROID
    if (dir.startsWith(QLatin1String("content:"))) {
        const auto listed = QJniObject::callStaticObjectMethod("com/mediumdev/scancore/Folders", "list",
                                                               "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                                                               QNativeInterface::QAndroidApplication::context().object(),
                                                               QJniObject::fromString(dir).object<jstring>());
        if (listed.isValid()) {
            for (const auto &line : listed.toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const auto parts = line.split(QLatin1Char('\t'));
                if (parts.size() >= 3 && !parts[0].startsWith(QLatin1Char('.')))
                    out.append({parts[2], parts[0], parts[1] == QLatin1String("1"),
                                parts.size() >= 5 ? parts[3].toLongLong() : -1, parts.size() >= 5 ? parts[4].toLongLong() / 1000 : 0});
            }
            return out;
        }
    }
#endif
    for (const auto &info : QDir(dir).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
        if (!info.fileName().startsWith(QLatin1Char('.')))
            out.append({info.absoluteFilePath(), info.fileName(), info.isDir(), info.size(), info.lastModified().toSecsSinceEpoch()});
    return out;
}

QStringList folders() {
    QStringList out;
    for (const auto &value : Database::pref(QStringLiteral("setting.localFolders")).toList())
        if (QFileInfo(value.toString()).isDir())
            out.append(value.toString());
    return out;
}

bool isImage(const QString &name) { return kImages.contains(suffix(name)); }

bool isPagesFile(const QString &name) {
    // A zipped FB2 is a book for the novels.
    if (name.endsWith(QLatin1String(".fb2.zip"), Qt::CaseInsensitive))
        return false;
    const auto s = suffix(name);
    return kZip.contains(s) || kOtherArchives.contains(s) || s == QLatin1String("pdf");
}

bool isEpub(const QString &name) { return suffix(name) == QLatin1String("epub"); }

void naturalSort(QStringList &names) {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
}

QString titleFrom(const QString &path) {
    const QFileInfo info(path);
    auto name = info.isDir() ? info.fileName() : info.completeBaseName();
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return name.simplified();
}

QStringList pages(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists())
        throw Error(QStringLiteral("%1 is no longer on this device.").arg(QDir::toNativeSeparators(path)), 404);
    if (info.isDir())
        return asFileUrls(imagesUnder(path, false));
    const auto s = info.suffix().toLower();
    if (s == QLatin1String("pdf")) {
        if (!canRenderPdf())
            throw Error(QStringLiteral("This version of SCANCORE cannot open PDF files."));
        const int count = g_pdf.pageCount(path);
        if (count <= 0)
            throw Error(QStringLiteral("%1 could not be opened.").arg(info.fileName()));
        QStringList out;
        const auto url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
        for (int i = 0; i < count; ++i)
            out.append(QStringLiteral("pdfpage:%1#%2").arg(url).arg(i));
        return out;
    }
    if (kZip.contains(s) || kOtherArchives.contains(s))
        return asFileUrls(unpacked(info));
    if (isImage(path))
        return {addressOf(path)};
    throw Error(QStringLiteral("SCANCORE cannot open %1 files.").arg(s.toUpper()));
}

QString cover(const QString &path) {
    const QFileInfo info(path);
    if (info.isDir()) {
        const auto images = imagesUnder(path, false);
        if (!images.isEmpty())
            return addressOf(images.first());
        // The first chapter inside.
        QStringList entries;
        for (const auto &entry : QDir(path).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
            if (entry.isDir() || isPagesFile(entry.fileName()))
                entries.append(entry.absoluteFilePath());
        naturalSort(entries);
        return entries.isEmpty() ? QString() : cover(entries.first());
    }
    const auto s = info.suffix().toLower();
    const auto file = cacheRoot() + QStringLiteral("/cover-") + QFileInfo(cacheFor(info)).fileName();
    if (QFileInfo::exists(file + QStringLiteral(".img")))
        return QUrl::fromLocalFile(file + QStringLiteral(".img")).toString();
    QByteArray data;
    if (kZip.contains(s)) {
        QZipReader reader(path);
        QStringList names;
        for (const auto &entry : reader.fileInfoList())
            if (entry.isFile && isImage(entry.filePath) && !entry.filePath.contains(QLatin1String("__MACOSX")))
                names.append(entry.filePath);
        naturalSort(names);
        if (!names.isEmpty())
            data = reader.fileData(names.first());
    } else if (s == QLatin1String("pdf") && canRenderPdf()) {
        data = g_pdf.render(path, 0, 500);
    } else if (QFileInfo::exists(cacheFor(info) + QStringLiteral("/.done"))) {
        // Another archive already unpacked for reading.
        const auto images = imagesUnder(cacheFor(info), true);
        if (!images.isEmpty())
            return QUrl::fromLocalFile(images.first()).toString();
    }
    if (data.isEmpty())
        return {};
    QSaveFile out(file + QStringLiteral(".img"));
    if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit())
        return QUrl::fromLocalFile(file + QStringLiteral(".img")).toString();
    return {};
}

QString cacheFile(const QString &path, const QString &name) {
    const auto dir = cacheFor(QFileInfo(path));
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + name;
}

QByteArray zipEntry(const QString &archive, const QString &name) {
    QZipReader reader(archive);
    return reader.isReadable() ? reader.fileData(name) : QByteArray();
}

QStringList zipEntries(const QString &archive) {
    QZipReader reader(archive);
    QStringList out;
    for (const auto &entry : reader.fileInfoList())
        if (entry.isFile)
            out.append(entry.filePath);
    return out;
}

QByteArray gzip(const QByteArray &data) {
    // Qt deflates only into ZIP entries: one is written, and its stream and
    // checksum become the gzip member.
    QByteArray zip;
    {
        QBuffer buffer(&zip);
        buffer.open(QIODevice::WriteOnly);
        QZipWriter writer(&buffer);
        writer.setCompressionPolicy(QZipWriter::AlwaysCompress);
        writer.addFile(QStringLiteral("data"), data);
        writer.close();
    }
    const auto u16 = [&](qsizetype at) { return quint32(uchar(zip[at])) | quint32(uchar(zip[at + 1])) << 8; };
    if (zip.size() < 30 || u16(8) != 8)
        return {};
    const auto size = qsizetype(u16(18) | u16(20) << 16);
    const auto at = 30 + qsizetype(u16(26)) + qsizetype(u16(28));
    if (at + size > zip.size())
        return {};
    QByteArray out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10);
    out += zip.mid(at, size);
    out += zip.mid(14, 4); // CRC-32
    out += zip.mid(22, 4); // size
    return out;
}

QHash<QString, QByteArray> unzip(const QByteArray &zip, qint64 maxFile) {
    QHash<QString, QByteArray> out;
    QBuffer buffer;
    buffer.setData(zip);
    if (!buffer.open(QIODevice::ReadOnly))
        return out;
    QZipReader reader(&buffer);
    if (!reader.isReadable() || reader.status() != QZipReader::NoError)
        return out;
    for (const auto &info : reader.fileInfoList())
        if (info.isFile && info.size <= maxFile)
            out.insert(info.filePath, reader.fileData(info.filePath));
    return out;
}

QByteArray gunzip(const QByteArray &data) {
    // Header: magic, method 8 (deflate), flags, then optional fields.
    if (data.size() < 18 || uchar(data[0]) != 0x1f || uchar(data[1]) != 0x8b || data[2] != 8)
        return {};
    const auto flags = uchar(data[3]);
    qsizetype at = 10;
    if (flags & 0x04) // extra
        at += 2 + (uchar(data[at]) | uchar(data[at + 1]) << 8);
    for (const uchar field : {uchar(0x08), uchar(0x10)}) // name, comment
        if (flags & field && (at = data.indexOf('\0', at) + 1) <= 0)
            return {};
    if (flags & 0x02) // header checksum
        at += 2;
    if (at <= 0 || at > data.size() - 8)
        return {};
    // Qt reads raw deflate only from ZIP entries, so the stream becomes the
    // one file of a ZIP made around it.
    const auto deflated = data.mid(at, data.size() - 8 - at);
    const auto crc = data.mid(data.size() - 8, 4);
    const auto size = data.right(4);
    const auto bytes = [](quint32 v, int count) {
        QByteArray out(count, Qt::Uninitialized);
        for (int i = 0; i < count; ++i)
            out[i] = char(v >> (8 * i) & 0xff);
        return out;
    };
    const auto u16 = [&](int v) { return bytes(quint32(v), 2); };
    const auto u32 = [&](quint32 v) { return bytes(v, 4); };
    const QByteArray name("data");
    const auto common = u16(20) + u16(0) + u16(8) + u16(0) + u16(0) + crc + u32(quint32(deflated.size())) + size + u16(name.size()) + u16(0);
    QByteArray zip = u32(0x04034b50) + common + name + deflated;
    const auto directoryAt = quint32(zip.size());
    const auto directory = u32(0x02014b50) + u16(20) + common + u16(0) + u16(0) + u16(0) + u32(0) + u32(0) + name;
    zip += directory + u32(0x06054b50) + u16(0) + u16(0) + u16(1) + u16(1) + u32(quint32(directory.size())) + u32(directoryAt) + u16(0);
    QBuffer buffer(&zip);
    buffer.open(QIODevice::ReadOnly);
    QZipReader reader(&buffer);
    return reader.isReadable() ? reader.fileData(QString::fromLatin1(name)) : QByteArray();
}

void setPdfRenderer(PdfRenderer renderer) { g_pdf = std::move(renderer); }

bool canRenderPdf() { return bool(g_pdf.pageCount) && bool(g_pdf.render); }

QByteArray renderPdfPage(const QString &address) {
    // pdfpage:<file URL>#<page>
    const auto hash = address.lastIndexOf(QLatin1Char('#'));
    const auto path = QUrl(address.mid(8, hash - 8)).toLocalFile();
    const int page = address.mid(hash + 1).toInt();
    if (!canRenderPdf() || path.isEmpty())
        return {};
    // Drawn once, then read from the cache.
    const QFileInfo info(path);
    const auto dir = cacheFor(info);
    QDir().mkpath(dir);
    const auto file = QStringLiteral("%1/page-%2.png").arg(dir).arg(page, 4, 10, QLatin1Char('0'));
    QFile cached(file);
    if (cached.open(QIODevice::ReadOnly))
        return cached.readAll();
    const auto png = g_pdf.render(path, page, 1800);
    QSaveFile out(file);
    if (!png.isEmpty() && out.open(QIODevice::WriteOnly) && out.write(png) == png.size())
        out.commit();
    return png;
}

} // namespace sc::local
