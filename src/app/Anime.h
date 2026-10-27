#pragma once

#include "core/Task.h"

#include <QObject>
#include <QQmlEngine>

namespace sc {

// AniList metadata, ani.zip episode details, AniSkip ranges, ReAnime
// player links, and anime from Aniyomi extensions. AniList series are
// stored as "anime:anilist:<id>"; an extension's anime as
// "anime:aniyomi:<id>:<link>" (Library::idFor).
class Anime : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool webPlayerAvailable READ webPlayerAvailable CONSTANT)
    Q_PROPERTY(bool nativePlayerAvailable READ nativePlayerAvailable CONSTANT)
    // Which embedded browser shows the player: "system" (the platform's own
    // web view: Android WebView, WebView2 on Windows, WKWebView on macOS),
    // "webengine" (Qt WebEngine) or "" when there is none.
    Q_PROPERTY(QString playerEngine READ playerEngine CONSTANT)

public:
    explicit Anime(QObject *parent = nullptr) : QObject(parent) {}

    // An extension anime's episodes as library units (worker threads; for
    // restoring backups). Throws when its source is missing or off.
    static QVariantList fetchUnits(const QString &url);

    // SCANCORE's MyAnimeList client id (empty in builds without one).
    static QString malClientId();

    // AniList's covers for these anime as they are now (worker threads):
    // {id: cover}. A replaced cover gets a new address and the old one
    // stops working.
    static QHash<int, QString> currentCovers(const QList<int> &ids);

    bool webPlayerAvailable() const;
    bool nativePlayerAvailable() const;
    QString playerEngine() const;

    Q_INVOKABLE QString seriesId(int anilistId) const;
    Q_INVOKABLE QStringList genres() const;

    // {trending, popular, season, top, seasonName, catalog}: AniList's
    // lists, or MyAnimeList's rankings when that is the anime catalogue
    // (setting "catalogAnime"). MyAnimeList's titles carry idMal and no
    // anilistId (see anilistIdFor).
    Q_INVOKABLE sc::Task *home();
    // The AniList entry of a MyAnimeList anime: its AniList id.
    Q_INVOKABLE sc::Task *anilistIdFor(int malId);
    // {results, hasNextPage}
    Q_INVOKABLE sc::Task *search(const QVariantMap &filters);
    Q_INVOKABLE sc::Task *media(int anilistId);
    Q_INVOKABLE sc::Task *episodes(int anilistId);
    Q_INVOKABLE sc::Task *recommendations(int anilistId);
    Q_INVOKABLE sc::Task *schedule(qint64 start, qint64 end);
    // {streams: [{url, quality, type}], message}
    Q_INVOKABLE sc::Task *streams(int anilistId, int episode);
    Q_INVOKABLE sc::Task *skipTimes(int anilistId, int episode, int duration);

    // Library series map for an AniList media map.
    Q_INVOKABLE QVariantMap seriesFor(const QVariantMap &media) const;
    Q_INVOKABLE bool isEmbedAllowed(const QString &url) const;

    // ---- anime extensions (Aniyomi)
    // Installed anime sources, shaped like Comics.sources(): [{key, label,
    // enabled, disabledReason, nsfw, language, languageName, base,
    // browseLatest, canBrowse, extension}]
    Q_INVOKABLE QVariantList sources() const;
    Q_INVOKABLE QString sourceLabel(const QString &key) const;
    // One source a page at a time ("popular", "latest" or "search"):
    // {items: [{id, media, source, sourceUrl, sourceLabel, title, cover,
    // referer, seasons}], hasNextPage}
    Q_INVOKABLE sc::Task *browse(const QString &source, const QString &listing, int page,
                                 const QString &query = QString(), const QVariantList &filters = {});
    Q_INVOKABLE sc::Task *filters(const QString &source);
    // An extension's anime: {series: {id, media, source, sourceLabel,
    // sourceUrl, title, cover, description, genres, status, extra:
    // {background, author, seasons}}, episodes: [{key, number, title, url,
    // released, filler, overview, image}], seasons: [items as browse()]}
    Q_INVOKABLE sc::Task *sourceAnime(const QString &url, const QVariantMap &summary = {});
    // "Watch from": an AniList anime on the installed sources, under any
    // of its names, content language first: {matches: [{source,
    // sourceLabel, language, preferred, url, title, cover, referer,
    // seasons}], contentLanguage}
    // Only `keys` (anime source keys) when given, else every enabled one.
    Q_INVOKABLE sc::Task *watchFrom(const QVariantMap &media, const QStringList &keys = {});
    // The source an AniList anime plays from ({source, url, title,
    // sourceLabel}), {builtIn: true} when the built-in servers were picked,
    // or an empty map when nothing was (or the source is gone). An empty
    // match picks the built-in servers.
    Q_INVOKABLE QVariantMap provider(int anilistId) const;
    Q_INVOKABLE void setProvider(int anilistId, const QVariantMap &match);
    // One episode's videos on a source, as its hosters answer (partial
    // results stream in): {episodeUrl, hosters: [{id, name, lazy, state:
    // loading|ready|failed|idle, error, videos: [video]}], best: {hoster,
    // video} or null, message}. The episode is `episodeUrl`, or the one
    // numbered `number` of the anime at `animeUrl` (Watch from). A video is
    // as the host gives it (AnimeSource.h); `skips` holds its chapters in
    // AniSkip's form.
    Q_INVOKABLE sc::Task *videos(const QString &source, const QString &episodeUrl, const QString &animeUrl = QString(),
                                 double number = -1);
    // A lazy hoster's videos: [video]
    Q_INVOKABLE sc::Task *hosterVideos(const QString &source, const QString &hosterId);
    // A video's final address, for the one about to play: video
    Q_INVOKABLE sc::Task *resolveVideo(const QString &source, const QString &videoId);

    // Other players for anime from extensions: [{id, name}]: mpv, VLC and
    // IINA where installed (not inside Flatpak), or the system's chooser of
    // player apps on Android.
    Q_INVOKABLE QVariantList externalPlayers() const;
    // Opens `video` (as the player has it: url, headers, subtitles, audio,
    // mpvArgs) there, from `startAt` seconds, with what each player takes
    // of its headers and tracks.
    // Whether openExternally hands the other player SCANCORE's relay (which
    // stops when SCANCORE closes) rather than the stream itself.
    Q_INVOKABLE bool relayedExternally(const QVariantMap &video) const;
    Q_INVOKABLE bool openExternally(const QString &player, const QVariantMap &video, const QString &title, double startAt) const;

    // For a download (blocking, on a worker): the episode's video as the
    // player would pick it (the server used last, else the source's best),
    // with its final address. Throws when there is none.
    static QVariantMap videoForDownload(const QString &source, const QString &episodeUrl, const QString &animeUrl, double number);
};

} // namespace sc
