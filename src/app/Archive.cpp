#include "Archive.h"

#include "core/Http.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#if !defined(Q_OS_ANDROID)
#include <QtCore/private/qzipreader_p.h>
#endif
#ifdef SCANCORE_HAVE_ZLIB
#include <zlib.h>
#endif

namespace sc::archive {

namespace {

[[noreturn]] void damaged() { throw Error(QStringLiteral("The download was damaged. Try again.")); }

QByteArray gunzip(const QByteArray &data) {
#ifdef SCANCORE_HAVE_ZLIB
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        damaged();
    QByteArray out;
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    static thread_local char buffer[256 * 1024];
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof buffer;
        result = inflate(&stream, Z_NO_FLUSH);
        out.append(buffer, qsizetype(sizeof buffer - stream.avail_out));
    }
    inflateEnd(&stream);
    if (result != Z_STREAM_END)
        damaged();
    return out;
#else
    Q_UNUSED(data)
    throw Error(QStringLiteral("This build cannot unpack the download."));
#endif
}

// The entry's path without the archive's top folder, if it is safe and wanted.
QString inside(const QString &name, const Keep &keep) {
    const auto relative = name.section(QLatin1Char('/'), 1);
    if (relative.isEmpty() || relative.contains(QLatin1String("..")) || relative.startsWith(QLatin1Char('/')))
        return {};
    if (keep && !keep(relative))
        return {};
    return relative;
}

void write(const QString &path, const char *data, qint64 size) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data, size) != size)
        throw Error(QStringLiteral("Could not write to the app data folder."));
}

} // namespace

void untarGz(const QByteArray &data, const QString &to, const Keep &keep) {
    const auto tar = gunzip(data);
    const auto octal = [&](qsizetype at, int size) {
        auto field = QByteArray(tar.constData() + at, size);
        field.truncate(field.indexOf('\0') < 0 ? size : field.indexOf('\0'));
        return field.trimmed().toLongLong(nullptr, 8);
    };
    const auto text = [&](qsizetype at, int size) {
        const auto field = QByteArray(tar.constData() + at, size);
        return QString::fromUtf8(field.left(field.indexOf('\0') < 0 ? size : field.indexOf('\0')));
    };
    for (qsizetype at = 0; at + 512 <= tar.size();) {
        if (tar.at(at) == 0)
            break; // the end: blocks of zeros
        auto name = text(at, 100);
        // ustar keeps long names' start in a prefix.
        if (QByteArray(tar.constData() + at + 257, 5) == "ustar") {
            const auto prefix = text(at + 345, 155);
            if (!prefix.isEmpty())
                name = prefix + QLatin1Char('/') + name;
        }
        const auto size = octal(at + 124, 12);
        const auto mode = octal(at + 100, 8);
        const char type = tar.at(at + 156);
        const auto link = text(at + 157, 100);
        const qsizetype start = at + 512;
        at = start + (size + 511) / 512 * 512;
        const auto relative = inside(name, keep);
        if (relative.isEmpty())
            continue;
        const auto path = to + QLatin1Char('/') + relative;
        if (type == '5') {
            QDir().mkpath(path);
        } else if (type == '2') {
            // Only links to a file beside them (libfoo.so -> libfoo.so.1);
            // never to the folder itself or above it.
            if (link.isEmpty() || link.contains(QLatin1Char('/')) || link == QLatin1String(".") || link == QLatin1String(".."))
                continue;
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile::remove(path);
            QFile::link(link, path);
        } else if (type == '0' || type == 0) {
            if (start + size > tar.size())
                damaged();
            write(path, tar.constData() + start, size);
            if (mode & 0111) {
                QFile file(path);
                file.setPermissions(file.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
            }
        }
    }
}

void unzip(const QByteArray &data, const QString &to, const Keep &keep) {
#if !defined(Q_OS_ANDROID)
    QByteArray copy = data;
    QBuffer buffer(&copy);
    buffer.open(QIODevice::ReadOnly);
    QZipReader zip(&buffer);
    if (!zip.isReadable())
        damaged();
    for (const auto &entry : zip.fileInfoList()) {
        const auto relative = inside(entry.filePath, keep);
        if (relative.isEmpty())
            continue;
        const auto path = to + QLatin1Char('/') + relative;
        if (entry.isDir) {
            QDir().mkpath(path);
            continue;
        }
        const auto bytes = zip.fileData(entry.filePath);
        write(path, bytes.constData(), bytes.size());
    }
#else
    Q_UNUSED(data)
    Q_UNUSED(to)
    Q_UNUSED(keep)
    damaged();
#endif
}

} // namespace sc::archive
