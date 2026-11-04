#pragma once

#include "core/Http.h"

#include <QHash>
#include <QSet>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QUrl>

class QNetworkAccessManager;

namespace sc {

// A small web server on the local network for casting: a TV or Chromecast
// fetches the video itself and cannot send the headers an extension's
// video needs, nor read SCANCORE's downloads. Each cast gets a share (a
// random token) that relays one video's address with its headers, HLS
// playlists rewritten so their parts come through here too, or serves a
// downloaded episode's folder. Responses allow any origin (Chromecast
// fetches HLS and subtitles from a web page). Runs on the GUI thread;
// bodies stream through as they arrive.
class CastProxy : public QObject {
    Q_OBJECT

public:
    explicit CastProxy(QObject *parent = nullptr);

    // An address the device at `device` can fetch `url` (http(s) or a local
    // file) from, with `headers`; empty when no server could start.
    QString share(const QUrl &url, const Headers &headers, const QHostAddress &device);
    // Ends every share (casting stopped).
    void clear();

    // The rewritten playlist (for tests): every address in it, absolute
    // against `base`, becomes `wrap(address)`.
    static QByteArray rewritePlaylist(const QByteArray &playlist, const QUrl &base, const std::function<QString(const QUrl &)> &wrap);

private:
    struct Share {
        Headers headers;
        QString folder; // a download's folder, for local files
        QString file;   // the shared file in it
        // What /r/ may relay for this share: the shared address and those
        // in the playlists relayed for it (two generations, so a live
        // stream's new parts keep coming while old ones age out). Nothing
        // else, so the token is not a way to anywhere.
        QSet<QByteArray> allowed, older;
        bool allows(const QUrl &url) const;
        void allow(const QUrl &url);
    };
    bool ensureListening();
    void serve(QTcpSocket *socket);
    void relay(QTcpSocket *socket, const QString &token, const QUrl &target, const QByteArray &range, bool head);
    void sendFile(QTcpSocket *socket, const QString &path, const QByteArray &range, bool head);
    QString base(const QHostAddress &device) const;
    QString wrap(const QString &token, const QUrl &target) const;

    QTcpServer m_server;
    QNetworkAccessManager *m_network = nullptr;
    QHash<QString, Share> m_shares;
    QString m_host;
};

} // namespace sc
