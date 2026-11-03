#include "Music.h"

#include "core/Database.h"
#include "core/Http.h"
#include "extensions/Host.h"
#include "extensions/Manager.h"
#include "plugins/Runtime.h"
#include "sources/Manifest.h"
#include "sources/local/AudioTags.h"
#include "sources/local/Local.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace sc {

namespace {

const QString kThemes = QStringLiteral("https://api.animethemes.moe");
const QString kLrclib = QStringLiteral("https://lrclib.net/api");
// Both services ask callers to say who they are.
const Headers kHeaders{{"Accept", "application/json"},
                       {"User-Agent", "SCANCORE (https://github.com/medium-gh/scancore)"}};
const QString kInclude = QStringLiteral("images,resources,animethemes.song.artists,animethemes.animethemeentries.videos.audio");

QJsonObject getJson(const QUrl &url, const QString &cacheKey, qint64 ttl) {
    if (!cacheKey.isEmpty())
        if (const auto hit = Database::cacheGet(cacheKey))
            return QJsonDocument::fromJson(*hit).object();
    const auto response = Http::get(url, kHeaders, 15000);
    if (!response.ok()) {
        if (!cacheKey.isEmpty())
            if (const auto stale = Database::cacheGet(cacheKey, true))
                return QJsonDocument::fromJson(*stale).object();
        if (response.status == 404)
            return {};
        throw Error(QStringLiteral("The songs service is unavailable right now (%1).").arg(response.status), response.status);
    }
    const auto object = response.json().object();
    if (!cacheKey.isEmpty())
        Database::cachePut(cacheKey, QJsonDocument(object).toJson(QJsonDocument::Compact), ttl);
    return object;
}

QString coverOf(const QJsonObject &anime) {
    QString small;
    for (const auto &value : anime.value("images").toArray()) {
        const auto image = value.toObject();
        if (image.value("facet").toString() == QLatin1String("Large Cover"))
            return image.value("link").toString();
        small = image.value("link").toString();
    }
    return small;
}

// Your files ---------------------------------------------------------------

const QString kLocalIndex = QStringLiteral("music|local-index");

QString addressOf(const QString &path) {
    return path.startsWith(QLatin1String("content:")) ? path : QUrl::fromLocalFile(path).toString();
}

QString stampOf(const local::Entry &e) {
    return QStringLiteral("%1|%2").arg(e.size).arg(e.modified);
}

// The embedded picture, written once per version of the file.
QString saveArt(const QString &path, const QString &stamp, const QByteArray &art) {
    if (art.size() < 64)
        return {};
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/music-art");
    QDir().mkpath(dir);
    const auto name = QString::fromLatin1(QCryptographicHash::hash((path + stamp).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    const auto file = dir + QLatin1Char('/') + name + (art.startsWith("\x89PNG") ? QStringLiteral(".png") : QStringLiteral(".jpg"));
    if (!QFileInfo::exists(file)) {
        QFile out(file);
        if (!out.open(QIODevice::WriteOnly))
            return {};
        out.write(art);
    }
    return QUrl::fromLocalFile(file).toString();
}

// A folder's own picture (cover.jpg, folder.png, ...), for files without one.
bool isFolderArt(const QString &name) {
    static const QRegularExpression re(QStringLiteral("^(cover|folder|front|album|albumart[^.]*)\\.(jpe?g|png|webp)$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}

QString baseName(const QString &name) {
    const auto dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.left(dot) : name;
}

QVariantMap songOfFile(const local::Entry &e, const QString &folder, const local::AudioTags &tags, const QString &art) {
    auto title = tags.title;
    auto artists = tags.artists;
    // Untagged files are often named "Artist - Title".
    if (title.isEmpty()) {
        title = baseName(e.name).replace(QLatin1Char('_'), QLatin1Char(' ')).trimmed();
        static const QRegularExpression track(QStringLiteral("^\\d{1,3}[ .\\-]+"));
        title.remove(track);
        const auto dash = title.indexOf(QStringLiteral(" - "));
        if (dash > 0 && artists.isEmpty()) {
            artists = {title.left(dash).trimmed()};
            title = title.mid(dash + 3).trimmed();
        }
    }
    return {{"id", QStringLiteral("local:") + e.path},
            {"type", QString()},
            {"label", QString()},
            {"title", title},
            {"artists", artists},
            {"album", tags.album.isEmpty() ? folder : tags.album},
            {"albumArtist", tags.albumArtist},
            {"track", tags.track},
            {"disc", tags.disc},
            {"duration", tags.duration},
            {"audio", addressOf(e.path)},
            {"file", e.path},
            {"cover", art},
            {"anime", QString()},
            {"local", true}};
}

// Music plugins ------------------------------------------------------------

// Music sources: SCANCORE's own plugins (the JavaScript runtime) and Echo
// extensions (the extension host).
QList<ext::SourceEntry> musicEntries() { return ext::Manager::sources(ext::Kind::Music) + ext::Manager::sources(ext::Kind::Echo); }

std::optional<ext::SourceEntry> musicSource(const QString &key) {
    for (const auto &e : musicEntries())
        if (e.key == key && e.enabled && e.blocked.isEmpty())
            return e;
    return std::nullopt;
}

ext::SourceEntry requireSource(const QString &key) {
    if (const auto source = musicSource(key))
        return *source;
    throw Error(QStringLiteral("That music extension is not installed or is turned off."), 404);
}

// One of the music plugin contract's calls (docs/music-plugins.md):
// searchSongs(query, page), popularSongs(page), streamOf(song),
// lyricsOf(song). Echo's go to the host with the same names.
QJsonValue musicCall(const ext::SourceEntry &e, const QString &method, const QJsonArray &args, int timeoutMs) {
    if (e.kind != ext::Kind::Echo)
        return plugins::Runtime::call(plugins::PluginRef{e.key, e.id, e.path}, method, args, timeoutMs);
    QJsonObject params{{"source", e.id}, {"pkg", e.pkg}, {"apk", e.path}};
    if (!e.signer.isEmpty())
        params.insert(QStringLiteral("signer"), e.signer);
    if (method == QLatin1String("searchSongs")) {
        params.insert(QStringLiteral("query"), args.at(0));
        params.insert(QStringLiteral("page"), args.at(1));
    } else if (method == QLatin1String("popularSongs")) {
        params.insert(QStringLiteral("page"), args.at(0));
    } else {
        params.insert(QStringLiteral("song"), args.at(0));
    }
    return ext::requireHost(ext::Kind::Echo)->call(method, params);
}

QString sourceName(const QString &key) {
    for (const auto &e : musicEntries())
        if (e.key == key)
            return e.name;
    return key;
}

void ensureAvailable() {
    if (!Music::instance()->animeSongsAvailable())
        throw Error(QStringLiteral("Anime songs are turned off for now."));
}

} // namespace

Music::Music(QObject *parent) : QObject(parent) {}

Music *Music::instance() {
    static auto *music = new Music();
    return music;
}

namespace {
const QString kLibrary = QStringLiteral("music.library");
} // namespace

QVariantList Music::library() const { return Database::pref(kLibrary).toList(); }

bool Music::isSaved(const QString &id) const {
    if (id.isEmpty())
        return false;
    const auto all = library();
    return std::any_of(all.begin(), all.end(), [&](const QVariant &v) { return v.toMap().value("id").toString() == id; });
}

void Music::setSaved(const QVariantMap &song, bool saved) {
    const auto id = song.value("id").toString();
    if (id.isEmpty())
        return;
    auto all = library();
    all.erase(std::remove_if(all.begin(), all.end(), [&](const QVariant &v) { return v.toMap().value("id").toString() == id; }), all.end());
    if (saved) {
        auto kept = song;
        // What only this run of the app needs (a plugin's stream, the
        // queue's place) is not kept.
        for (const auto *key : {"stream", "headers", "queueIndex"})
            kept.remove(QLatin1String(key));
        kept.insert("savedAt", QDateTime::currentMSecsSinceEpoch());
        all.prepend(kept);
    }
    Database::setPref(kLibrary, all);
    ++m_libraryRevision;
    emit libraryChanged();
}

bool Music::animeSongsAvailable() const {
    return Manifest::current().value("music").toObject().value("animethemes").toBool(true);
}

QVariantList Music::songsOfAnime(const QJsonObject &anime) {
    int anilistId = 0, malId = 0;
    for (const auto &value : anime.value("resources").toArray()) {
        const auto resource = value.toObject();
        const auto site = resource.value("site").toString();
        if (site == QLatin1String("AniList"))
            anilistId = resource.value("external_id").toInt();
        else if (site == QLatin1String("MyAnimeList"))
            malId = resource.value("external_id").toInt();
    }
    const auto cover = coverOf(anime);
    const auto animeName = anime.value("name").toString();
    QVariantList out;
    for (const auto &value : anime.value("animethemes").toArray()) {
        const auto theme = value.toObject();
        const auto song = theme.value("song").toObject();
        QStringList artists;
        for (const auto &artist : song.value("artists").toArray())
            artists.append(artist.toObject().value("name").toString());
        // The first entry is the version the episodes start with; its
        // first video the creditless one where there is one.
        QString audio, video, episodes;
        for (const auto &entryValue : theme.value("animethemeentries").toArray()) {
            const auto entry = entryValue.toObject();
            for (const auto &videoValue : entry.value("videos").toArray()) {
                const auto v = videoValue.toObject();
                if (video.isEmpty() || v.value("nc").toBool()) {
                    video = v.value("link").toString();
                    audio = v.value("audio").toObject().value("link").toString();
                }
                if (v.value("nc").toBool())
                    break;
            }
            episodes = entry.value("episodes").toString();
            if (!video.isEmpty())
                break;
        }
        if (audio.isEmpty() && video.isEmpty())
            continue;
        const auto type = theme.value("type").toString();
        const int sequence = theme.value("sequence").toInt();
        out.append(QVariantMap{{"id", QStringLiteral("animethemes:%1").arg(theme.value("id").toInt())},
                               {"type", type},
                               {"sequence", sequence},
                               {"label", sequence > 0 ? type + QString::number(sequence) : type},
                               {"title", song.value("title").toString()},
                               {"artists", artists},
                               {"episodes", episodes},
                               {"audio", audio},
                               {"video", video},
                               {"anime", animeName},
                               {"cover", cover},
                               {"anilistId", anilistId},
                               {"malId", malId}});
    }
    return out;
}

Task *Music::animeSongs(int anilistId, int malId) {
    return Task::run([anilistId, malId]() -> QVariant {
        ensureAvailable();
        for (const auto &[site, id] : {std::pair{QStringLiteral("AniList"), anilistId}, std::pair{QStringLiteral("MyAnimeList"), malId}}) {
            if (id <= 0)
                continue;
            QUrl url(kThemes + QStringLiteral("/anime"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("filter[has]"), QStringLiteral("resources"));
            query.addQueryItem(QStringLiteral("filter[site]"), site);
            query.addQueryItem(QStringLiteral("filter[external_id]"), QString::number(id));
            query.addQueryItem(QStringLiteral("include"), kInclude);
            url.setQuery(query);
            const auto data = getJson(url, QStringLiteral("animesongs|%1|%2").arg(site).arg(id), 86400);
            QVariantList songs;
            for (const auto &anime : data.value("anime").toArray())
                songs += songsOfAnime(anime.toObject());
            if (!songs.isEmpty())
                return songs;
        }
        return QVariantList();
    });
}

Task *Music::searchSongs(const QString &query) {
    const auto q = query.simplified().left(100);
    return Task::run([q]() -> QVariant {
        ensureAvailable();
        if (q.isEmpty())
            return QVariantList();
        // Anime matching the words, with their songs; then songs whose own
        // title matches, on anime the first list missed.
        QUrl url(kThemes + QStringLiteral("/search"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("q"), q);
        query.addQueryItem(QStringLiteral("fields[search]"), QStringLiteral("anime,animethemes"));
        query.addQueryItem(QStringLiteral("include[anime]"), kInclude);
        query.addQueryItem(QStringLiteral("include[animetheme]"),
                           QStringLiteral("anime.images,song.artists,animethemeentries.videos.audio"));
        url.setQuery(query);
        const auto search = getJson(url, QString(), 0).value("search").toObject();
        QVariantList out;
        QSet<QString> seen;
        const auto add = [&](const QVariantList &songs) {
            for (const auto &song : songs) {
                const auto id = song.toMap().value("id").toString();
                if (!seen.contains(id)) {
                    seen.insert(id);
                    out.append(song);
                }
            }
        };
        for (const auto &value : search.value("animethemes").toArray()) {
            auto theme = value.toObject();
            auto anime = theme.value("anime").toObject();
            anime.insert("animethemes", QJsonArray{theme});
            add(songsOfAnime(anime));
        }
        for (const auto &anime : search.value("anime").toArray())
            add(songsOfAnime(anime.toObject()));
        return out;
    });
}

QStringList Music::musicFolders() {
    QStringList out;
    for (const auto &value : Database::pref(QStringLiteral("setting.musicFolders")).toList())
        if (!value.toString().isEmpty())
            out.append(value.toString());
    return out;
}

Task *Music::localSongs() {
    return Task::run([]() -> QVariant {
        QJsonObject before;
        if (const auto hit = Database::cacheGet(kLocalIndex, true))
            before = QJsonDocument::fromJson(*hit).object();
        QJsonObject index;
        QVariantList songs;
        // Folder by folder: the files' own pictures and lyrics sit beside them.
        QStringList pending = musicFolders();
        QSet<QString> seen;
        while (!pending.isEmpty() && songs.size() < 50000) {
            const auto dir = pending.takeFirst();
            if (seen.contains(dir))
                continue;
            seen.insert(dir);
            const auto list = local::entries(dir);
            QString folderArt;
            QHash<QString, QString> lrcs;
            for (const auto &e : list) {
                if (e.dir)
                    pending.append(e.path);
                else if (e.name.endsWith(QLatin1String(".lrc"), Qt::CaseInsensitive))
                    lrcs.insert(baseName(e.name).toLower(), e.path);
                else if (folderArt.isEmpty() && isFolderArt(e.name))
                    folderArt = addressOf(e.path);
            }
            // "content://…/tree/primary%3AMusic%2FAlbum" is "Album".
            const auto folder = QUrl::fromPercentEncoding(dir.toUtf8()).section(QRegularExpression(QStringLiteral("[/\\\\:]")), -1);
            for (const auto &e : list) {
                if (e.dir || !local::isAudio(e.name))
                    continue;
                const auto stamp = stampOf(e);
                QVariantMap song;
                const auto known = before.value(e.path).toObject();
                if (e.size >= 0 && known.value("stamp").toString() == stamp) {
                    song = known.value("song").toObject().toVariantMap();
                } else {
                    const auto tags = local::readAudioTags(e.path, e.name, true);
                    song = songOfFile(e, folder, tags, saveArt(e.path, stamp, tags.art));
                }
                if (song.value("cover").toString().isEmpty())
                    song.insert("cover", folderArt);
                song.insert("lrc", lrcs.value(baseName(e.name).toLower()));
                index.insert(e.path, QJsonObject{{"stamp", stamp}, {"song", QJsonObject::fromVariantMap(song)}});
                songs.append(song);
            }
        }
        Database::cachePut(kLocalIndex, QJsonDocument(index).toJson(QJsonDocument::Compact), 365 * 86400);
        // Album by album, in disc and track order; loose files by name.
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(songs.begin(), songs.end(), [&](const QVariant &a, const QVariant &b) {
            const auto x = a.toMap(), y = b.toMap();
            if (const int c = collator.compare(x.value("album").toString(), y.value("album").toString()))
                return c < 0;
            if (x.value("disc").toInt() != y.value("disc").toInt())
                return x.value("disc").toInt() < y.value("disc").toInt();
            if (x.value("track").toInt() != y.value("track").toInt())
                return x.value("track").toInt() < y.value("track").toInt();
            return collator.compare(x.value("file").toString(), y.value("file").toString()) < 0;
        });
        return songs;
    });
}

Task *Music::seasonSongs() {
    return Task::run([]() -> QVariant {
        ensureAvailable();
        const auto today = QDate::currentDate();
        static const QStringList seasons{QStringLiteral("Winter"), QStringLiteral("Spring"), QStringLiteral("Summer"), QStringLiteral("Fall")};
        const auto season = seasons.value((today.month() - 1) / 3);
        QUrl url(kThemes + QStringLiteral("/anime"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("filter[year]"), QString::number(today.year()));
        query.addQueryItem(QStringLiteral("filter[season]"), season);
        query.addQueryItem(QStringLiteral("include"), kInclude);
        query.addQueryItem(QStringLiteral("page[size]"), QStringLiteral("40"));
        url.setQuery(query);
        const auto data = getJson(url, QStringLiteral("seasonsongs|%1|%2").arg(today.year()).arg(season), 6 * 3600);
        QVariantList out;
        for (const auto &value : data.value("anime").toArray()) {
            const auto anime = value.toObject();
            const auto songs = songsOfAnime(anime);
            if (songs.isEmpty())
                continue;
            const auto first = songs.first().toMap();
            out.append(QVariantMap{{"anime", anime.value("name").toString()},
                                   {"cover", first.value("cover")},
                                   {"anilistId", first.value("anilistId")},
                                   {"songs", songs}});
        }
        return out;
    });
}

QVariantList Music::parseLrc(const QString &lrc) {
    static const QRegularExpression stamp(QStringLiteral(R"(\[(\d+):(\d+(?:[.:]\d+)?)\])"));
    QList<QPair<double, QString>> lines;
    for (const auto &raw : lrc.split(QLatin1Char('\n'))) {
        auto it = stamp.globalMatch(raw);
        QList<double> times;
        qsizetype end = 0;
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedStart() != end)
                break;  // stamps only at the start of the line
            QString seconds = m.captured(2);
            seconds.replace(QLatin1Char(':'), QLatin1Char('.'));
            times.append(m.captured(1).toInt() * 60 + seconds.toDouble());
            end = m.capturedEnd();
        }
        if (times.isEmpty())
            continue;
        const auto text = raw.mid(end).trimmed();
        for (const double t : times)
            lines.append({t, text});
    }
    std::stable_sort(lines.begin(), lines.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QVariantList out;
    for (const auto &[time, text] : lines)
        out.append(QVariantMap{{"time", time}, {"text", text}});
    return out;
}

Task *Music::lyrics(const QVariantMap &song, double duration) {
    const auto title = song.value("title").toString();
    const auto artist = song.value("artists").toStringList().value(0);
    const auto album = song.value("album").toString();
    const auto file = song.value("file").toString();
    const auto lrc = song.value("lrc").toString();
    const auto source = song.value("source").toString();
    const auto raw = song.value("raw").toMap();
    return Task::run([title, artist, album, duration, file, lrc, source, raw]() -> QVariant {
        // Your own first: the .lrc beside the file, then lyrics kept in it.
        const auto ownText = [&]() -> QString {
            if (!lrc.isEmpty()) {
                QFile f(lrc);
                if (f.open(QIODevice::ReadOnly))
                    return QString::fromUtf8(f.readAll());
            }
            if (!file.isEmpty())
                return local::readAudioTags(file, file.section(QLatin1Char('/'), -1), false).lyrics;
            return {};
        }();
        if (!ownText.trimmed().isEmpty()) {
            const auto synced = parseLrc(ownText);
            return QVariantMap{{"found", true}, {"synced", synced}, {"plain", synced.isEmpty() ? ownText.trimmed() : QString()},
                               {"instrumental", false}, {"own", true}};
        }
        // The plugin the song came from, when it has lyrics.
        if (!source.isEmpty()) {
            if (const auto plugin = musicSource(source)) {
                try {
                    const auto o = musicCall(*plugin, QStringLiteral("lyricsOf"), {QJsonObject::fromVariantMap(raw)}, 20000).toObject();
                    const auto synced = parseLrc(o.value("synced").toString());
                    const auto plain = o.value("plain").toString().trimmed();
                    if (!synced.isEmpty() || !plain.isEmpty())
                        return QVariantMap{{"found", true}, {"synced", synced}, {"plain", synced.isEmpty() ? plain : QString()},
                                           {"instrumental", o.value("instrumental").toBool()}};
                } catch (const Error &) {
                    // No lyricsOf, or it failed: LRCLIB next.
                }
            }
        }
        // A file without a singer would match any song of that name.
        if (title.trimmed().isEmpty() || (!file.isEmpty() && artist.trimmed().isEmpty()))
            return QVariantMap{{"found", false}};
        const auto key = QStringLiteral("lyrics|%1|%2|%3").arg(title.toLower(), artist.toLower()).arg(qRound(duration));
        if (const auto hit = Database::cacheGet(key))
            return QJsonDocument::fromJson(*hit).object().toVariantMap();
        const auto shape = [](const QJsonObject &l) {
            const auto synced = l.value("syncedLyrics").toString();
            return QVariantMap{{"found", true},
                               {"synced", parseLrc(synced)},
                               {"plain", l.value("plainLyrics").toString()},
                               {"instrumental", l.value("instrumental").toBool()}};
        };
        QVariantMap result{{"found", false}};
        // The exact match first (title, artist, album, duration), then a
        // search by title and artist, preferring synced lyrics of about the
        // same length.
        QUrl get(kLrclib + QStringLiteral("/get"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("track_name"), title);
        query.addQueryItem(QStringLiteral("artist_name"), artist);
        if (!album.isEmpty())
            query.addQueryItem(QStringLiteral("album_name"), album);
        if (duration > 0)
            query.addQueryItem(QStringLiteral("duration"), QString::number(qRound(duration)));
        get.setQuery(query);
        const auto exact = Http::get(get, kHeaders, 12000);
        if (exact.ok()) {
            result = shape(exact.json().object());
        } else {
            QUrl search(kLrclib + QStringLiteral("/search"));
            QUrlQuery sq;
            sq.addQueryItem(QStringLiteral("track_name"), title);
            if (!artist.isEmpty())
                sq.addQueryItem(QStringLiteral("artist_name"), artist);
            search.setQuery(sq);
            const auto found = Http::get(search, kHeaders, 12000);
            if (!found.ok() && found.status != 404)
                throw Error(QStringLiteral("Lyrics are unavailable right now (%1).").arg(found.status), found.status);
            QJsonObject best;
            double bestScore = -1;
            for (const auto &value : found.json().array()) {
                const auto l = value.toObject();
                double score = l.value("syncedLyrics").toString().isEmpty() ? 0 : 2;
                if (duration > 0)
                    score += std::max(0.0, 1 - std::abs(l.value("duration").toDouble() - duration) / 10);
                if (score > bestScore) {
                    bestScore = score;
                    best = l;
                }
            }
            if (!best.isEmpty())
                result = shape(best);
        }
        Database::cachePut(key, QJsonDocument(QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Compact),
                           result.value("found").toBool() ? 30 * 86400 : 86400);
        return result;
    });
}

QVariantList Music::sources() const {
    QVariantList out;
    for (const auto &e : musicEntries())
        if (e.enabled && e.blocked.isEmpty())
            out.append(QVariantMap{{"key", e.key}, {"name", e.name}, {"site", e.baseUrl}});
    return out;
}

QVariantMap Music::songOfPlugin(const QString &key, const QString &name, const QJsonObject &o) {
    QStringList artists;
    const auto a = o.value("artists");
    if (a.isArray())
        for (const auto &v : a.toArray())
            artists.append(v.toString());
    else if (!a.toString().isEmpty())
        artists = a.toString().split(QStringLiteral(", "), Qt::SkipEmptyParts);
    else if (!o.value("artist").toString().isEmpty())
        artists = {o.value("artist").toString()};
    return {{"id", key + QLatin1Char('|') + o.value("id").toVariant().toString()},
            {"type", QString()},
            {"label", QString()},
            {"title", o.value("title").toString()},
            {"artists", artists},
            {"album", o.value("album").toString()},
            {"cover", o.value("cover").toString()},
            {"duration", o.value("duration").toDouble()},
            {"anime", QString()},
            // Asked for when it plays (streamOf).
            {"audio", QString()},
            {"source", key},
            {"sourceName", name},
            {"raw", o.toVariantMap()}};
}

namespace {
QVariantList pluginSongs(const QString &key, const QString &method, const QJsonArray &args) {
    const auto plugin = requireSource(key);
    const auto name = sourceName(key);
    QJsonValue result;
    try {
        result = musicCall(plugin, method, args, 30000);
    } catch (const Error &e) {
        // A plugin without popular songs simply has none.
        if (method == QLatin1String("popularSongs") && e.message().contains(QLatin1String("cannot do that")))
            return {};
        ext::Manager::recordSourceResult(key, e.message());
        throw;
    }
    ext::Manager::recordSourceResult(key, {});
    QVariantList out;
    for (const auto &value : result.toArray()) {
        const auto o = value.toObject();
        if (!o.value("id").toVariant().toString().isEmpty() && !o.value("title").toString().isEmpty())
            out.append(Music::songOfPlugin(key, name, o));
        if (out.size() >= 200)
            break;
    }
    return out;
}
} // namespace

Task *Music::searchSource(const QString &key, const QString &query) {
    const auto q = query.simplified().left(200);
    return Task::run([key, q]() -> QVariant { return q.isEmpty() ? QVariantList() : pluginSongs(key, QStringLiteral("searchSongs"), {q, 1}); });
}

Task *Music::popularSource(const QString &key) {
    return Task::run([key]() -> QVariant { return pluginSongs(key, QStringLiteral("popularSongs"), {1}); });
}

Task *Music::streamOf(const QVariantMap &song) {
    const auto key = song.value("source").toString();
    const auto raw = song.value("raw").toMap();
    return Task::run([key, raw]() -> QVariant {
        const auto result = musicCall(requireSource(key), QStringLiteral("streamOf"), {QJsonObject::fromVariantMap(raw)}, 30000);
        const auto o = result.isString() ? QJsonObject{{"url", result.toString()}} : result.toObject();
        const QUrl url(o.value("url").toString());
        if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")))
            throw Error(QStringLiteral("The extension gave no address to play this song from."));
        return QVariantMap{{"url", url.toString()}, {"headers", o.value("headers").toObject().toVariantMap()}};
    });
}

} // namespace sc
