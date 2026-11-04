#include "CastProxy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

namespace sc {

namespace {

// Kept in memory per connection before reading from the source pauses.
constexpr qint64 kHighWater = 4 * 1024 * 1024;
constexpr qint64 kChunk = 256 * 1024;

const QByteArray kCors = "Access-Control-Allow-Origin: *\r\n"
                         "Access-Control-Allow-Headers: Range, Content-Type\r\n"
                         "Access-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n";

QByteArray reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 416: return "Range Not Satisfiable";
    default: return status >= 500 ? "Bad Gateway" : "Error";
    }
}

void respond(QTcpSocket *socket, int status, const QByteArray &type = {}, const QByteArray &body = {}, const QByteArray &extra = {}) {
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\n" + kCors + "Connection: close\r\n";
    if (!type.isEmpty())
        head += "Content-Type: " + type + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n" + extra + "\r\n";
    socket->write(head + body);
    socket->disconnectFromHost();
}

QByteArray typeFor(const QString &path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("m3u8"))
        return "application/vnd.apple.mpegurl";
    if (suffix == QLatin1String("ts"))
        return "video/mp2t";
    if (suffix == QLatin1String("m4s") || suffix == QLatin1String("mp4") || suffix == QLatin1String("m4v"))
        return "video/mp4";
    if (suffix == QLatin1String("mkv"))
        return "video/x-matroska";
    if (suffix == QLatin1String("webm"))
        return "video/webm";
    if (suffix == QLatin1String("vtt"))
        return "text/vtt; charset=utf-8";
    if (suffix == QLatin1String("aac"))
        return "audio/aac";
    if (suffix == QLatin1String("m4a"))
        return "audio/mp4";
    return "application/octet-stream";
}

// SubRip to WebVTT (Chromecast reads only WebVTT): a header, and dots for
// the commas in the times.
QByteArray srtToVtt(const QByteArray &srt) {
    static const QRegularExpression times(QStringLiteral(R"((\d\d:\d\d:\d\d),(\d\d\d))"));
    auto text = QString::fromUtf8(srt);
    text.remove(QChar(0xFEFF));
    text.replace(times, QStringLiteral("\\1.\\2"));
    return "WEBVTT\n\n" + text.toUtf8();
}

// bytes=a-b of a Range header, against a size; false when it does not fit.
bool parseRange(const QByteArray &range, qint64 size, qint64 &from, qint64 &to) {
    from = 0;
    to = size - 1;
    if (range.isEmpty())
        return true;
    const auto spec = range.mid(range.indexOf('=') + 1).split(',').first().trimmed();
    const auto dash = spec.indexOf('-');
    if (dash < 0)
        return false;
    const auto left = spec.left(dash), right = spec.mid(dash + 1);
    if (left.isEmpty()) {
        from = std::max<qint64>(0, size - right.toLongLong());
    } else {
        from = left.toLongLong();
        if (!right.isEmpty())
            to = std::min(size - 1, right.toLongLong());
    }
    return from <= to && from < size;
}

} // namespace

bool CastProxy::Share::allows(const QUrl &url) const {
    const auto key = url.toEncoded();
    return allowed.contains(key) || older.contains(key);
}

void CastProxy::Share::allow(const QUrl &url) {
    if (allowed.size() >= 20000) {
        older = std::move(allowed);
        allowed = {};
    }
    allowed.insert(url.toEncoded());
}

CastProxy::CastProxy(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {
    m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            // A few receivers fetch at once; more is someone else.
            if (m_server.findChildren<QTcpSocket *>().size() > 64) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { serve(socket); });
            // The request's head comes within a few seconds, or the
            // connection goes.
            QTimer::singleShot(15000, socket, [socket] {
                if (!socket->property("served").toBool())
                    socket->abort();
            });
        }
    });
}

bool CastProxy::ensureListening() {
    return m_server.isListening() || m_server.listen(QHostAddress::AnyIPv4, 0);
}

QString CastProxy::base(const QHostAddress &device) const {
    QHostAddress chosen;
    for (const auto &interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || (interface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const auto &entry : interface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            // The address on the device's own network, else any.
            if (device.isInSubnet(entry.ip(), entry.prefixLength()))
                return QStringLiteral("http://%1:%2").arg(entry.ip().toString()).arg(m_server.serverPort());
            if (chosen.isNull())
                chosen = entry.ip();
        }
    }
    if (device.isLoopback())
        chosen = QHostAddress::LocalHost;
    return chosen.isNull() ? QString() : QStringLiteral("http://%1:%2").arg(chosen.toString()).arg(m_server.serverPort());
}

QString CastProxy::wrap(const QString &token, const QUrl &target) const {
    // The last part of the address stays at the end, so receivers that go
    // by the name (.m3u8, .vtt) see it.
    auto name = QFileInfo(target.path()).fileName();
    if (name.isEmpty())
        name = QStringLiteral("media");
    return QStringLiteral("%1/r/%2/%3/%4")
        .arg(m_host, token, QString::fromLatin1(target.toEncoded().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)),
             QString::fromLatin1(QUrl::toPercentEncoding(name)));
}

QString CastProxy::share(const QUrl &url, const Headers &headers, const QHostAddress &device) {
    if (!ensureListening())
        return {};
    m_host = base(device);
    if (m_host.isEmpty())
        return {};
    // 128 random bits from the system's generator: tokens are seen on the
    // network, never guessed.
    quint64 bits[2];
    QRandomGenerator::system()->fillRange(bits);
    const auto token = QString::number(bits[0], 36) + QString::number(bits[1], 36);
    if (url.isLocalFile()) {
        const QFileInfo file(url.toLocalFile());
        Share share;
        share.headers = headers;
        share.folder = file.absolutePath();
        share.file = file.fileName();
        m_shares.insert(token, share);
        return QStringLiteral("%1/f/%2/%3").arg(m_host, token, QString::fromLatin1(QUrl::toPercentEncoding(file.fileName())));
    }
    Share share;
    share.headers = headers;
    share.allow(url);
    m_shares.insert(token, share);
    return wrap(token, url);
}

void CastProxy::clear() { m_shares.clear(); }

QByteArray CastProxy::rewritePlaylist(const QByteArray &playlist, const QUrl &base, const std::function<QString(const QUrl &)> &wrap) {
    static const QRegularExpression uri(QStringLiteral(R"re(URI="([^"]*)")re"));
    QStringList out;
    for (auto line : QString::fromUtf8(playlist).split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('#'))) {
            if (const auto m = uri.match(line); m.hasMatch())
                line.replace(m.capturedStart(1), m.capturedLength(1), wrap(base.resolved(QUrl(m.captured(1)))));
        } else if (!line.isEmpty()) {
            line = wrap(base.resolved(QUrl(line)));
        }
        out.append(line);
    }
    return out.join(QLatin1Char('\n')).toUtf8();
}

void CastProxy::serve(QTcpSocket *socket) {
    // One request per connection; its head only (receivers send no body).
    auto buffer = socket->property("head").toByteArray() + socket->readAll();
    const auto end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (buffer.size() > 16384)
            socket->abort();
        else
            socket->setProperty("head", buffer);
        return;
    }
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
    socket->setProperty("served", true);
    const auto lines = buffer.left(end).split('\n');
    const auto request = lines.value(0).trimmed().split(' ');
    const auto method = request.value(0);
    QByteArray range;
    for (int i = 1; i < lines.size(); ++i)
        if (lines[i].toLower().startsWith("range:"))
            range = lines[i].mid(6).trimmed();
    if (method == "OPTIONS")
        return respond(socket, 204);
    if (method != "GET" && method != "HEAD")
        return respond(socket, 400);
    const bool head = method == "HEAD";
    const auto parts = QString::fromLatin1(request.value(1)).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 3 || !m_shares.contains(parts[1]))
        return respond(socket, 404);
    const auto &share = m_shares[parts[1]];
    if (parts[0] == QLatin1String("f") && !share.folder.isEmpty()) {
        // Within the download's folder only.
        const auto relative = QDir::cleanPath(QUrl::fromPercentEncoding(parts.mid(2).join(QLatin1Char('/')).toLatin1()));
        if (relative.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(relative))
            return respond(socket, 403);
        // The shared file only; a downloaded playlist also its parts beside
        // it (media, keys and subtitles; no other files of the folder).
        const bool playlist = share.file.endsWith(QLatin1String(".m3u8"), Qt::CaseInsensitive);
        const bool part = typeFor(relative) != "application/octet-stream" ||
                          QFileInfo(relative).suffix().compare(QLatin1String("key"), Qt::CaseInsensitive) == 0;
        if (relative != share.file && (!playlist || !part))
            return respond(socket, 403);
        return sendFile(socket, share.folder + QLatin1Char('/') + relative, range, head);
    }
    if (parts[0] == QLatin1String("r") && share.folder.isEmpty()) {
        const QUrl target = QUrl::fromEncoded(QByteArray::fromBase64(parts[2].toLatin1(), QByteArray::Base64UrlEncoding));
        if (!target.isValid() || (target.scheme() != QLatin1String("https") && target.scheme() != QLatin1String("http")))
            return respond(socket, 400);
        if (!share.allows(target))
            return respond(socket, 403);
        return relay(socket, parts[1], target, range, head);
    }
    respond(socket, 404);
}

void CastProxy::sendFile(QTcpSocket *socket, const QString &path, const QByteArray &range, bool head) {
    auto *file = new QFile(path, socket);
    if (!file->open(QIODevice::ReadOnly))
        return respond(socket, 404);
    const qint64 size = file->size();
    qint64 from, to;
    if (!parseRange(range, size, from, to))
        return respond(socket, 416, {}, {}, "Content-Range: bytes */" + QByteArray::number(size) + "\r\n");
    const bool partial = !range.isEmpty();
    QByteArray headers = "HTTP/1.1 " + QByteArray(partial ? "206 Partial Content" : "200 OK") + "\r\n" + kCors +
                         "Connection: close\r\nAccept-Ranges: bytes\r\nContent-Type: " + typeFor(path) +
                         "\r\nContent-Length: " + QByteArray::number(to - from + 1) + "\r\n";
    if (partial)
        headers += "Content-Range: bytes " + QByteArray::number(from) + '-' + QByteArray::number(to) + '/' + QByteArray::number(size) + "\r\n";
    socket->write(headers + "\r\n");
    if (head) {
        socket->disconnectFromHost();
        return;
    }
    file->seek(from);
    auto left = std::make_shared<qint64>(to - from + 1);
    const auto pump = [socket, file, left] {
        while (*left > 0 && socket->bytesToWrite() < kHighWater) {
            const auto data = file->read(std::min(kChunk, *left));
            if (data.isEmpty())
                break;
            *left -= data.size();
            socket->write(data);
        }
        if (*left <= 0 || file->atEnd())
            socket->disconnectFromHost();
    };
    connect(socket, &QTcpSocket::bytesWritten, file, pump);
    pump();
}

void CastProxy::relay(QTcpSocket *socket, const QString &token, const QUrl &target, const QByteArray &range, bool head) {
    QNetworkRequest request(target);
    request.setHeader(QNetworkRequest::UserAgentHeader, Http::userAgent());
    for (const auto &[name, value] : m_shares.value(token).headers)
        request.setRawHeader(name, value);
    if (!range.isEmpty())
        request.setRawHeader("Range", range);
    request.setTransferTimeout(60000);
    auto *reply = head ? m_network->head(request) : m_network->get(request);
    // Goes with the connection.
    reply->setParent(socket);
    reply->setReadBufferSize(kHighWater);
    QPointer<QTcpSocket> client(socket);
    const auto path = target.path().toLower();
    // Playlists are rewritten and SubRip turned into WebVTT, so they come
    // whole; everything else streams.
    auto whole = std::make_shared<bool>(path.endsWith(QLatin1String(".m3u8")) || path.endsWith(QLatin1String(".srt")));
    auto started = std::make_shared<bool>(false);
    const auto start = [=, this] {
        if (*started || !client)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto type = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        if (type.toLower().contains("mpegurl"))
            *whole = true;
        if (*whole)
            return;
        *started = true;
        QByteArray headers = "HTTP/1.1 " + QByteArray::number(status ? status : 502) + ' ' + reason(status) + "\r\n" + kCors +
                             "Connection: close\r\nAccept-Ranges: bytes\r\n";
        // Media parts by their name (servers often label them wrongly, or
        // as images to disguise them), anything else as the server says.
        const auto known = typeFor(path);
        headers += "Content-Type: " + (known != "application/octet-stream" || type.isEmpty() ? known : type) + "\r\n";
        for (const auto *name : {"Content-Length", "Content-Range"})
            if (reply->hasRawHeader(name))
                headers += QByteArray(name) + ": " + reply->rawHeader(name) + "\r\n";
        client->write(headers + "\r\n");
    };
    const auto pump = [=] {
        if (!client || !*started)
            return;
        while (reply->bytesAvailable() > 0 && client->bytesToWrite() < kHighWater)
            client->write(reply->read(kChunk));
    };
    connect(reply, &QNetworkReply::metaDataChanged, socket, start);
    connect(reply, &QNetworkReply::readyRead, socket, [=] {
        start();
        pump();
    });
    connect(socket, &QTcpSocket::bytesWritten, reply, pump);
    connect(socket, &QTcpSocket::disconnected, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        if (!client || client->state() != QAbstractSocket::ConnectedState)
            return;
        if (*whole) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError && status == 0)
                return respond(client, 502);
            auto body = reply->readAll();
            QByteArray type = "application/vnd.apple.mpegurl";
            if (path.endsWith(QLatin1String(".srt"))) {
                body = srtToVtt(body);
                type = "text/vtt; charset=utf-8";
            } else if (body.startsWith("#EXTM3U")) {
                const auto final = reply->url();
                body = rewritePlaylist(body, final, [&](const QUrl &u) {
                    if (const auto share = m_shares.find(token); share != m_shares.end())
                        share->allow(u);
                    return wrap(token, u);
                });
            }
            return respond(client, status ? status : 502, type, head ? QByteArray() : body);
        }
        start();
        // What is left goes out as the socket drains.
        const auto drain = [=] {
            if (!client)
                return;
            while (reply->bytesAvailable() > 0 && client->bytesToWrite() < kHighWater)
                client->write(reply->read(kChunk));
            if (reply->bytesAvailable() == 0)
                client->disconnectFromHost();
        };
        connect(client, &QTcpSocket::bytesWritten, reply, drain);
        drain();
    });
}

} // namespace sc
