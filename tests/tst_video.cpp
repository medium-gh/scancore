// Saving anime episodes for offline playback: HLS variants, separate audio,
// keys, init sections and byte ranges, against a fixture server; and the
// relay the player streams through (core/StreamRelay.h).
#include "core/Http.h"
#include "core/ImageCache.h"
#include "core/StreamRelay.h"
#include "app/CastProxy.h"
#include "core/VideoSave.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <tuple>

using namespace sc;

// A small HTTP/1.1 server: files by path (query left off), ranges, kept
// alive; it remembers the last request.
class Fixture : public QTcpServer {
public:
    struct File {
        QByteArray type, body;
    };
    QHash<QByteArray, File> files;
    QByteArray lastPath;
    QHash<QByteArray, QByteArray> lastHeaders;
    int connections = 0;

    Fixture() {
        listen(QHostAddress::LocalHost, 0);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = nextPendingConnection()) {
                ++connections;
                auto input = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, input] {
                    *input += socket->readAll();
                    qsizetype end;
                    while ((end = input->indexOf("\r\n\r\n")) >= 0) {
                        const auto lines = input->left(end).split('\n');
                        input->remove(0, end + 4);
                        answer(socket, lines);
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    quint16 port() const { return serverPort(); }

private:
    void answer(QTcpSocket *socket, const QList<QByteArray> &lines) {
        lastPath = lines.value(0).split(' ').value(1);
        lastHeaders.clear();
        for (const auto &line : lines.mid(1))
            lastHeaders.insert(line.left(line.indexOf(':')).trimmed().toLower(), line.mid(line.indexOf(':') + 1).trimmed());
        const auto it = files.constFind(lastPath.split('?').value(0));
        if (it == files.cend()) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            return;
        }
        auto body = it->body;
        QByteArray status = "200 OK", extra = "Accept-Ranges: bytes\r\n";
        if (const auto range = lastHeaders.value("range"); range.startsWith("bytes=")) {
            const auto span = range.mid(6).split('-');
            const qint64 from = span.value(0).toLongLong();
            const qint64 to = span.value(1).isEmpty() ? body.size() - 1 : span.value(1).toLongLong();
            extra += "Content-Range: bytes " + QByteArray::number(from) + '-' + QByteArray::number(to) + '/' + QByteArray::number(body.size()) + "\r\n";
            body = body.mid(from, to - from + 1);
            status = "206 Partial Content";
        }
        socket->write("HTTP/1.1 " + status + "\r\nContent-Type: " + it->type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\n" + extra + "\r\n" + body);
    }
};

class Video : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    QHash<QString, int> m_hits;

    // A 1x1 PNG with padding after it, as anime CDNs wrap parts in.
    static QByteArray png() {
        return QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16) + QByteArray(54, 'p') + QByteArray(182, '\0');
    }
    // `n` transport stream packets.
    static QByteArray ts(int n) {
        QByteArray out;
        for (int i = 0; i < n; ++i)
            out += char(0x47) + QByteArray(187, char('a' + i % 26));
        return out;
    }

    static QString read(const QString &path) {
        QFile file(path);
        file.open(QIODevice::ReadOnly);
        return QString::fromUtf8(file.readAll());
    }

private slots:
    void initTestCase() {
        Http::setTestHandler([this](const Request &request) -> std::optional<Response> {
            if (request.url.host() != QLatin1String("video.test"))
                return std::nullopt;
            const auto path = request.url.path();
            ++m_hits[path];
            Response r;
            r.status = 200;
            r.finalUrl = request.url;
            // The master redirects to a CDN folder; parts are relative to it.
            if (path == QLatin1String("/watch/master.m3u8")) {
                r.finalUrl = QUrl(QStringLiteral("https://video.test/cdn/master.m3u8"));
                r.body = "#EXTM3U\n"
                         "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",NAME=\"Japanese\",DEFAULT=YES,URI=\"audio/ja.m3u8\"\n"
                         "#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=854x480,AUDIO=\"aud\"\nlow/index.m3u8\n"
                         "#EXT-X-STREAM-INF:BANDWIDTH=5000000,RESOLUTION=1920x1080,AUDIO=\"aud\"\nhigh/index.m3u8\n"
                         "#EXT-X-STREAM-INF:BANDWIDTH=9000000,RESOLUTION=3840x2160,AUDIO=\"aud\"\nuhd/index.m3u8\n";
            } else if (path == QLatin1String("/cdn/high/index.m3u8")) {
                r.body = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n"
                         "#EXT-X-KEY:METHOD=AES-128,URI=\"https://video.test/keys/k1\",IV=0x01\n"
                         "#EXTINF:4,\nseg1.jpg\n#EXTINF:4,\nseg2.jpg\n#EXT-X-ENDLIST\n";
            } else if (path == QLatin1String("/wrapped.m3u8")) {
                r.body = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4,\nhttps://video.test/p/1.image?sig=1\n#EXT-X-ENDLIST\n";
            } else if (path == QLatin1String("/p/1.image")) {
                r.body = png() + ts(3);
            } else if (path == QLatin1String("/cdn/audio/ja.m3u8")) {
                r.body = "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4,\n#EXT-X-BYTERANGE:100@0\nall.m4s\n"
                         "#EXTINF:4,\n#EXT-X-BYTERANGE:50\nall.m4s\n#EXT-X-ENDLIST\n";
            } else if (path == QLatin1String("/keys/k1")) {
                r.body = QByteArray(16, 'k');
            } else if (path.endsWith(QLatin1String(".jpg"))) {
                // Transport stream parts under another name.
                r.body = QByteArray(1, char(0x47)) + path.toUtf8();
            } else if (path == QLatin1String("/cdn/audio/init.mp4")) {
                r.body = QByteArray("\0\0\0\x18" "ftypiso6", 12);
            } else if (path == QLatin1String("/cdn/audio/all.m4s")) {
                QByteArray range;
                for (const auto &[name, value] : request.headers)
                    if (name == "Range")
                        range = value;
                r.status = 206;
                r.body = QByteArray("\0\0\0\x10" "moof", 8) + range;
            } else {
                r.status = 404;
            }
            return r;
        });
    }

    void picksVariantAtMost1080() {
        const auto v = video::pickVariant(QStringLiteral("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1,RESOLUTION=1x2160\nuhd.m3u8\n"
                                                         "#EXT-X-STREAM-INF:BANDWIDTH=2,RESOLUTION=1x720,AUDIO=\"a\"\nhd.m3u8\n"));
        QCOMPARE(v.uri, QStringLiteral("hd.m3u8"));
        QCOMPARE(v.audioGroup, QStringLiteral("a"));
        // Only taller ones: the tallest.
        QCOMPARE(video::pickVariant(QStringLiteral("#EXT-X-STREAM-INF:RESOLUTION=1x2160\nuhd.m3u8\n")).uri, QStringLiteral("uhd.m3u8"));
    }

    void savesHlsWithAudioKeysAndRanges() {
        const auto folder = m_dir.filePath(QStringLiteral("ep1"));
        qint64 lastDone = 0, lastTotal = 0;
        const auto saved = video::save(QUrl(QStringLiteral("https://video.test/watch/master.m3u8")), {}, folder,
                                       [&](qint64 done, qint64 total) { lastDone = done, lastTotal = total; });
        QVERIFY(saved.hls);
        QCOMPARE(saved.file, QStringLiteral("video.m3u8"));
        QCOMPARE(lastDone, 4);
        QCOMPARE(lastTotal, 4);
        const auto master = read(folder + QStringLiteral("/video.m3u8"));
        QVERIFY(master.contains(QStringLiteral("URI=\"a.m3u8\"")));
        QVERIFY(master.contains(QStringLiteral("\nv.m3u8\n")));
        const auto video = read(folder + QStringLiteral("/v.m3u8"));
        QVERIFY(video.contains(QStringLiteral("URI=\"v-key.1.key\",IV=0x01")));
        QVERIFY(video.contains(QStringLiteral("\nv-00001.ts\n")));
        QVERIFY(video.contains(QStringLiteral("\nv-00002.ts\n")));
        QCOMPARE(read(folder + QStringLiteral("/v-00002.ts")).mid(1), QStringLiteral("/cdn/high/seg2.jpg"));
        QCOMPARE(m_hits.value(QStringLiteral("/cdn/uhd/index.m3u8")), 0);
        const auto audio = read(folder + QStringLiteral("/a.m3u8"));
        QVERIFY(audio.contains(QStringLiteral("#EXT-X-MAP:URI=\"a-init.1.mp4\"")));
        QVERIFY(!audio.contains(QStringLiteral("BYTERANGE")));
        QVERIFY(audio.contains(QStringLiteral("\na-00001.m4s\n")));
        QVERIFY(read(folder + QStringLiteral("/a-00001.m4s")).endsWith(QStringLiteral("0-99")));
        QVERIFY(read(folder + QStringLiteral("/a-00002.m4s")).endsWith(QStringLiteral("100-149")));

        // Again: what is there is kept, only the playlists are asked again.
        m_hits.clear();
        video::save(QUrl(QStringLiteral("https://video.test/watch/master.m3u8")), {}, folder, [](qint64, qint64) {});
        QCOMPARE(m_hits.value(QStringLiteral("/cdn/high/seg1.jpg")), 0);
        QCOMPARE(m_hits.value(QStringLiteral("/keys/k1")), 0);
        QCOMPARE(m_hits.value(QStringLiteral("/cdn/high/index.m3u8")), 1);
    }

    void savesWrappedPartsUnwrapped() {
        const auto folder = m_dir.filePath(QStringLiteral("wrapped"));
        video::save(QUrl(QStringLiteral("https://video.test/wrapped.m3u8")), {}, folder, [](qint64, qint64) {});
        QFile part(folder + QStringLiteral("/v-00001.ts"));
        QVERIFY(part.open(QIODevice::ReadOnly));
        QCOMPARE(part.readAll(), ts(3));
    }

    // Images read from the cache count as used, so trimming drops the ones
    // nobody looked at first.
    void imageCacheKeepsWhatWasRead() {
        ImageCache::init(m_dir.filePath(QStringLiteral("images")), 1024 * 1024 * 1024);
        const auto read = QStringLiteral("https://video.test/p/1.image?read");
        const auto unread = QStringLiteral("https://video.test/p/1.image?unread");
        const int before = m_hits.value(QStringLiteral("/p/1.image"));
        ImageCache::fetch(read, {});
        ImageCache::fetch(unread, {});
        const auto old = QDateTime::currentDateTime().addDays(-3);
        QStringList files;
        QDirIterator it(m_dir.filePath(QStringLiteral("images")), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files.append(it.next());
        QCOMPARE(files.size(), 2);
        for (const auto &path : files) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(old, QFileDevice::FileModificationTime));
        }
        ImageCache::fetch(read, {});
        ImageCache::setBudget(ImageCache::sizeOnDisk() - 1);
        ImageCache::trim();
        QCOMPARE(m_hits.value(QStringLiteral("/p/1.image")), before + 2);
        // The one read is still there: no new download for it.
        ImageCache::fetch(read, {});
        QCOMPARE(m_hits.value(QStringLiteral("/p/1.image")), before + 2);
        ImageCache::fetch(unread, {});
        QCOMPARE(m_hits.value(QStringLiteral("/p/1.image")), before + 3);
    }

    void findsWhereMediaStarts() {
        QCOMPARE(video::mediaStart(ts(6), false), 0);
        QCOMPARE(video::mediaStart(png() + ts(6), false), png().size());
        // Dressed, too short to tell yet; then the whole of a short part.
        QCOMPARE(video::mediaStart(png() + ts(2), false), -1);
        QCOMPARE(video::mediaStart(png() + ts(2), true), png().size());
        // A GIF starts with 0x47 too.
        QCOMPARE(video::mediaStart("GIF89a" + QByteArray(40, 'g') + ts(6), false), 46);
        const QByteArray moof("\0\0\0\x10moof\0\0\0\0\0\0\0\0", 16);
        QCOMPARE(video::mediaStart(moof, false), 0);
        QCOMPARE(video::mediaStart("\xFF\xD8\xFF\xE0" + QByteArray(100, 'j') + moof, false), 104);
        QCOMPARE(video::mediaStart("WEBVTT\n\n00:00.000 --> 00:01.000\nhi\n", false), 0);
        QCOMPARE(video::mediaStart(QByteArray("\xFF\xF1\x50\x80", 4), false), 0);
        // Something unknown, all of it: left as it is.
        QCOMPARE(video::mediaStart(QByteArray(5000, 'x'), true), 0);
        QCOMPARE(video::mediaStart(QByteArray(10, 'x'), false), -1);
    }

    void rewritesPlaylists() {
        QList<std::tuple<QString, video::Part, QString, bool>> seen;
        const auto address = [&](const QUrl &url, video::Part kind, const QString &suffix, bool subs) {
            seen.append({url.toString(), kind, suffix, subs});
            return "R" + QByteArray::number(seen.size()) + '.' + suffix.toLatin1();
        };
        const auto master = video::rewritePlaylist("#EXTM3U\n"
                                                   "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s\",NAME=\"en\",URI=\"subs/en.txt\"\n"
                                                   "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"ja\",URI=\"audio.m3u8\"\r\n"
                                                   "#EXT-X-STREAM-INF:BANDWIDTH=1,AUDIO=\"a\",SUBTITLES=\"s\"\n"
                                                   "hd/list.php?id=4\n",
                                                   QUrl(QStringLiteral("https://cdn.test/v/master")), false, address);
        QCOMPARE(master, QByteArray("#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s\",NAME=\"en\",URI=\"R1.m3u8\"\n"
                                    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"ja\",URI=\"R2.m3u8\"\n"
                                    "#EXT-X-STREAM-INF:BANDWIDTH=1,AUDIO=\"a\",SUBTITLES=\"s\"\nR3.m3u8\n"));
        QCOMPARE(seen[0], std::make_tuple(QStringLiteral("https://cdn.test/v/subs/en.txt"), video::Part::Playlist, QStringLiteral("m3u8"), true));
        QCOMPARE(seen[2], std::make_tuple(QStringLiteral("https://cdn.test/v/hd/list.php?id=4"), video::Part::Playlist, QStringLiteral("m3u8"), false));

        seen.clear();
        const auto media = video::rewritePlaylist("#EXTM3U\n#EXT-X-TARGETDURATION:6\n"
                                                  "#EXTINF:6,\nhttps://img.test/a.image?x=1\n"
                                                  "#EXT-X-KEY:METHOD=AES-128,URI=\"k.bin\",IV=0x1\n#EXTINF:6,\nb.jpg\n"
                                                  "#EXT-X-KEY:METHOD=NONE\n#EXT-X-BYTERANGE:10@0\n#EXTINF:6,\nc.ts\n"
                                                  "#EXTINF:6,\nskd://not-http\n#EXT-X-ENDLIST\n",
                                                  QUrl(QStringLiteral("https://cdn.test/v/hd/list.php?id=4")), false, address);
        QVERIFY(media.contains("\nR1.ts\n"));
        QVERIFY(media.contains("URI=\"R2.key\",IV=0x1"));
        QVERIFY(media.contains("\nskd://not-http\n"));
        QCOMPARE(std::get<1>(seen[0]), video::Part::Media);
        QCOMPARE(std::get<1>(seen[1]), video::Part::Raw); // the key
        QCOMPARE(std::get<1>(seen[2]), video::Part::Raw); // encrypted
        QCOMPARE(std::get<1>(seen[3]), video::Part::Raw); // a byte range
        QCOMPARE(std::get<0>(seen[2]), QStringLiteral("https://cdn.test/v/hd/b.jpg"));

        seen.clear();
        video::rewritePlaylist("#EXTM3U\n#EXT-X-MAP:URI=\"init.png\"\n#EXTINF:4,\n1.css\n", QUrl(QStringLiteral("https://cdn.test/")), false, address);
        QCOMPARE(std::get<2>(seen[0]), QStringLiteral("mp4"));
        QCOMPARE(std::get<2>(seen[1]), QStringLiteral("m4s"));
        seen.clear();
        video::rewritePlaylist("#EXTM3U\n#EXTINF:4,\n1.txt\n", QUrl(QStringLiteral("https://cdn.test/")), true, address);
        QCOMPARE(std::get<2>(seen[0]), QStringLiteral("vtt"));
    }

    // The relay end to end, between a fixture server and a client.
    void relaysWrappedStreams() {
        Fixture server;
        const auto base = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        server.files.insert("/master", {"text/plain", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nmedia?q=1\n"});
        server.files.insert("/media", {"text/plain", "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4,\nseg.image\n"
                                                     "#EXTINF:4,\nplain.jpg\n#EXT-X-ENDLIST\n"});
        server.files.insert("/seg.image", {"image/png", png() + ts(40)});
        server.files.insert("/plain.jpg", {"image/jpeg", ts(8)});
        server.files.insert("/movie", {"video/mp4", QByteArray(300000, 'm')});
        const Headers headers{{"Referer", "https://site.test/"}};
        QNetworkAccessManager client;
        const auto get = [&](const QUrl &url, const QByteArray &range = {}) {
            QNetworkRequest request(url);
            if (!range.isEmpty())
                request.setRawHeader("Range", range);
            auto *reply = client.get(request);
            QSignalSpy done(reply, &QNetworkReply::finished);
            done.wait(10000);
            reply->deleteLater();
            return reply;
        };

        const auto top = video::StreamRelay::wrap(QUrl(base + QStringLiteral("/master")), headers);
        QCOMPARE(top.host(), QStringLiteral("127.0.0.1"));
        auto *reply = get(top);
        QCOMPARE(reply->rawHeader("Content-Type"), QByteArray("application/vnd.apple.mpegurl"));
        QCOMPARE(server.lastHeaders.value("referer"), QByteArray("https://site.test/"));
        const auto variant = QUrl(QString::fromUtf8(reply->readAll().split('\n').value(2)));
        QVERIFY(variant.path().endsWith(QLatin1String(".m3u8")));
        const auto parts = get(variant)->readAll().split('\n');
        const auto wrapped = QUrl(QString::fromUtf8(parts.value(3)));
        const auto plain = QUrl(QString::fromUtf8(parts.value(5)));
        QVERIFY(wrapped.path().endsWith(QLatin1String(".ts")));
        QCOMPARE(server.lastPath, QByteArray("/media?q=1"));

        // The picture taken off, the length less it; then further in.
        reply = get(wrapped);
        QCOMPARE(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(), ts(40).size());
        QCOMPARE(reply->readAll(), ts(40));
        reply = get(wrapped, "bytes=376-");
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
        QCOMPARE(reply->readAll(), ts(40).mid(376));
        QCOMPARE(reply->rawHeader("Content-Range"), "bytes 376-" + QByteArray::number(ts(40).size() - 1) + '/' + QByteArray::number(ts(40).size()));
        QCOMPARE(get(plain)->readAll(), ts(8));
        // One connection for all of it.
        QCOMPARE(server.connections, 1);

        // Not a playlist after all: passed on as it is, ranges included.
        reply = get(video::StreamRelay::wrap(QUrl(base + QStringLiteral("/movie")), {}), "bytes=1000-1999");
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
        QCOMPARE(reply->readAll().size(), 1000);
        reply = get(video::StreamRelay::wrap(QUrl(base + QStringLiteral("/movie")), {}));
        QCOMPARE(reply->readAll().size(), 300000);

        // A server's refusal goes through; unknown addresses are refused.
        QCOMPARE(get(video::StreamRelay::wrap(QUrl(base + QStringLiteral("/gone")), {}))->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                 404);
        auto unknown = wrapped;
        unknown.setPath(QStringLiteral("/nope/1.ts"));
        QCOMPARE(get(unknown)->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);

        QVERIFY(!video::StreamRelay::wanted(QUrl(QStringLiteral("https://x.test/a.mp4"))));
        QVERIFY(video::StreamRelay::wanted(QUrl(QStringLiteral("https://x.test/a.m3u8"))));
        QVERIFY(video::StreamRelay::wanted(QUrl(QStringLiteral("https://x.test/play?id=1"))));
        QVERIFY(!video::StreamRelay::wanted(QUrl::fromLocalFile(QStringLiteral("/tmp/video.m3u8"))));
    }

    // Against a real server: SCANCORE_VIDEO_URL (HLS or a file) is saved
    // into SCANCORE_VIDEO_DIR; skipped without them.
    // Casting: the TV is given the cast proxy's address for the relay's, so
    // it gets disguised streams undressed (the relay stays on this device).
    void castsThroughTheRelay() {
        Fixture server;
        const auto base = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        server.files.insert("/master", {"text/plain", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nmedia\n"});
        server.files.insert("/media", {"text/plain", "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4,\nseg.image\n#EXT-X-ENDLIST\n"});
        server.files.insert("/seg.image", {"image/png", png() + ts(40)});
        QNetworkAccessManager client;
        const auto get = [&](const QUrl &url) {
            auto *reply = client.get(QNetworkRequest(url));
            QSignalSpy done(reply, &QNetworkReply::finished);
            done.wait(10000);
            reply->deleteLater();
            return reply->readAll();
        };
        const auto relayed = video::StreamRelay::wrap(QUrl(base + QStringLiteral("/master")), {{"Referer", "https://site.test/"}});
        CastProxy proxy;
        const QUrl shared(proxy.share(relayed, {}, QHostAddress(QHostAddress::LocalHost)));
        QVERIFY(shared.isValid());
        QVERIFY(shared.port() != relayed.port());
        const auto variant = QUrl(QString::fromUtf8(get(shared).split('\n').value(2)));
        QCOMPARE(variant.port(), shared.port());
        const auto part = QUrl(QString::fromUtf8(get(variant).split('\n').value(3)));
        QCOMPARE(part.port(), shared.port());
        QCOMPARE(get(part), ts(40));
        QCOMPARE(server.lastHeaders.value("referer"), QByteArray("https://site.test/"));

        // A share relays its own addresses (and its playlists' parts) only:
        // its token is no way to any other address, on this device or not.
        const auto status = [&](const QUrl &url) {
            auto *reply = client.get(QNetworkRequest(url));
            QSignalSpy done(reply, &QNetworkReply::finished);
            done.wait(10000);
            reply->deleteLater();
            return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        };
        const auto token = part.path().split(QLatin1Char('/')).value(2);
        auto forged = part;
        forged.setPath(QStringLiteral("/r/%1/%2/media").arg(
            token, QString::fromLatin1(QUrl(base + QStringLiteral("/media")).toEncoded().toBase64(
                       QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))));
        QCOMPARE(status(forged), 403);

        // A shared file is served, not the rest of its folder.
        QTemporaryDir folder;
        QFile movie(folder.filePath(QStringLiteral("movie.mp4")));
        QVERIFY(movie.open(QIODevice::WriteOnly));
        movie.write("not really a movie");
        movie.close();
        QFile other(folder.filePath(QStringLiteral("notes.mp4")));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write("private");
        other.close();
        const QUrl file(proxy.share(QUrl::fromLocalFile(movie.fileName()), {}, QHostAddress(QHostAddress::LocalHost)));
        QCOMPARE(get(file), QByteArray("not really a movie"));
        auto sibling = file;
        sibling.setPath(file.path().section(QLatin1Char('/'), 0, -2) + QStringLiteral("/notes.mp4"));
        QCOMPARE(status(sibling), 403);
    }

    void savesFromNetwork() {
        const auto url = qEnvironmentVariable("SCANCORE_VIDEO_URL");
        if (url.isEmpty())
            QSKIP("SCANCORE_VIDEO_URL is not set");
        const auto saved = video::save(QUrl(url), {}, qEnvironmentVariable("SCANCORE_VIDEO_DIR"), [](qint64, qint64) {});
        qInfo("saved %s (hls %d)", qPrintable(saved.file), saved.hls);
    }

    // Against a real server: SCANCORE_RELAY_URL plays through the relay in
    // mpv for a few seconds, with SCANCORE_RELAY_HEADERS ({"name": "value"})
    // and mpv's own SCANCORE_RELAY_MPV_ARGS (space separated); skipped
    // without them or mpv.
    void relaysFromNetwork() {
        const auto url = qEnvironmentVariable("SCANCORE_RELAY_URL");
        const auto mpv = QStandardPaths::findExecutable(QStringLiteral("mpv"));
        if (url.isEmpty() || mpv.isEmpty())
            QSKIP("SCANCORE_RELAY_URL is not set or mpv is missing");
        Headers headers;
        const auto json = QJsonDocument::fromJson(qgetenv("SCANCORE_RELAY_HEADERS")).object();
        for (auto it = json.begin(); it != json.end(); ++it)
            headers.append({it.key().toUtf8(), it.value().toString().toUtf8()});
        const auto relayed = video::StreamRelay::wrap(QUrl(url), headers);
        QVERIFY(relayed.host() == QLatin1String("127.0.0.1"));
        QProcess player;
        QStringList args{QStringLiteral("--no-config"), QStringLiteral("--vo=null"), QStringLiteral("--ao=null"),
                         QStringLiteral("--frames=240"), QStringLiteral("--msg-level=all=warn")};
        args += qEnvironmentVariable("SCANCORE_RELAY_MPV_ARGS").split(QLatin1Char(' '), Qt::SkipEmptyParts);
        player.setProcessChannelMode(QProcess::MergedChannels);
        player.start(mpv, args << relayed.toString());
        QVERIFY(player.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(player.state() == QProcess::NotRunning, 90000);
        qInfo("%s", player.readAll().constData());
        QCOMPARE(player.exitCode(), 0);
    }

    void failsOnMissingPart() {
        bool failed = false;
        try {
            video::save(QUrl(QStringLiteral("https://video.test/missing.m3u8")), {}, m_dir.filePath(QStringLiteral("x")), [](qint64, qint64) {});
        } catch (const Error &) {
            failed = true;
        }
        QVERIFY(failed);
    }
};

QTEST_MAIN(Video)
#include "tst_video.moc"
