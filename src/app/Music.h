#pragma once

#include "core/Task.h"

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

namespace sc {

// Songs: anime openings, endings and inserts from AnimeThemes
// (api.animethemes.moe, matched by AniList or MyAnimeList id), and lyrics
// from LRCLIB (lrclib.net, synced when it has them). Every call is a Task;
// nothing blocks the interface.
//
// A song is {id, type ("OP", "ED", "IN"), sequence, title, artists: [names],
// episodes, audio (Ogg), video (WebM), anime, cover, anilistId, malId}; a
// file of yours has {local: true, file, lrc, album, track, disc, duration}.
class Music : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // AnimeThemes plays from its own host; the sources manifest can turn it
    // off (music.animethemes = false) like the built-in sources.
    Q_PROPERTY(bool animeSongsAvailable READ animeSongsAvailable CONSTANT)
    // Raised whenever songs are saved to or removed from the library.
    Q_PROPERTY(int libraryRevision READ libraryRevision NOTIFY libraryChanged)

public:
    static Music *instance();
    static Music *create(QQmlEngine *, QJSEngine *) { return instance(); }

    bool animeSongsAvailable() const;

    // An anime's songs, by its AniList id (else its MyAnimeList id).
    Q_INVOKABLE sc::Task *animeSongs(int anilistId, int malId = 0);
    // Songs by title, artist or anime.
    Q_INVOKABLE sc::Task *searchSongs(const QString &query);
    // This season's anime and their songs: [{anime, cover, anilistId,
    // songs: [song]}].
    Q_INVOKABLE sc::Task *seasonSongs();
    // Songs in the folders chosen on the Music page (the musicFolders
    // setting), album by album. Tags are read once per version of a file;
    // a .lrc beside a file is its `lrc`.
    Q_INVOKABLE sc::Task *localSongs();
    static QStringList musicFolders();
    // A song's lyrics: {synced: [{time, text}], plain, instrumental, found}.
    // A file of yours (its .lrc, then lyrics kept in it) is asked first,
    // then the music plugin a song came from, then LRCLIB; `duration` in
    // seconds helps LRCLIB pick the right version.
    Q_INVOKABLE sc::Task *lyrics(const QVariantMap &song, double duration);

    // Music plugins (extensions of kind "music", see docs/music-plugins.md)
    // that are installed, on and trusted: [{key, name, site}].
    Q_INVOKABLE QVariantList sources() const;
    // One plugin's songs for a search, or its popular ones (empty when it
    // has none). Its songs carry {source, sourceName, raw} and no audio:
    // streamOf says where they play.
    Q_INVOKABLE sc::Task *searchSource(const QString &key, const QString &query);
    Q_INVOKABLE sc::Task *popularSource(const QString &key);
    // {url, headers} for a plugin's song.
    Q_INVOKABLE sc::Task *streamOf(const QVariantMap &song);
    // A plugin's answer as a song (public for the tests).
    static QVariantMap songOfPlugin(const QString &key, const QString &name, const QJsonObject &o);

    // Songs saved to the library, the latest first: openings and endings,
    // files of yours and plugins' songs alike, each as it was when saved.
    int libraryRevision() const { return m_libraryRevision; }
    Q_INVOKABLE QVariantList library() const;
    Q_INVOKABLE bool isSaved(const QString &id) const;
    Q_INVOKABLE void setSaved(const QVariantMap &song, bool saved);

    // Parsers, public for the tests.
    // "[mm:ss.xx] text" lines into [{time, text}] by time; tags and empty
    // stamps are left out, several stamps on a line repeat it.
    static QVariantList parseLrc(const QString &lrc);
    static QVariantList songsOfAnime(const QJsonObject &anime);

signals:
    void libraryChanged();

private:
    explicit Music(QObject *parent = nullptr);
    int m_libraryRevision = 0;
};

} // namespace sc
