#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace sc::ext {

// The extension ecosystems SCANCORE runs: Mihon/Tachiyomi extensions
// (comics), Aniyomi extensions (anime), LNReader plugins (novels),
// SCANCORE's own music plugins (JavaScript like LNReader's; see
// docs/music-plugins.md), CloudStream plugins (.cs3 files, shown as
// anime; run by the host's CloudStream runtime) and Echo extensions (.eapk
// files, shown as music; run by the host's Echo runtime).
enum class Kind { Mihon, Aniyomi, LNReader, Music, CloudStream, Echo };

// JavaScript plugins (no APK, no signing key): LNReader and music.
inline bool isPluginKind(Kind kind) { return kind == Kind::LNReader || kind == Kind::Music; }
// Trusted for the repository the reader added, which has no signing key:
// the JavaScript plugins, CloudStream's and Echo's (each Echo extension is
// signed by its own author: an update must keep the key it came with).
inline bool isUnsignedKind(Kind kind) { return isPluginKind(kind) || kind == Kind::CloudStream || kind == Kind::Echo; }
// Files that are signed APKs: Mihon's, Aniyomi's and Echo's.
inline bool isSignedFile(Kind kind) { return kind == Kind::Mihon || kind == Kind::Aniyomi || kind == Kind::Echo; }
// Music sources: SCANCORE's own plugins and Echo's.
inline bool isMusicKind(Kind kind) { return kind == Kind::Music || kind == Kind::Echo; }

// A CloudStream plugin's source id, as the host gives it (core's
// CloudStream.sourceId): the first 8 bytes of MD5 of "cloudstream/<pkg>".
QString cloudStreamSourceId(const QString &pkg);
// An Echo extension's, the same way (core's Echo.sourceId): "echo/<pkg>".
QString echoSourceId(const QString &pkg);

QString kindName(Kind kind);           // "mihon", "aniyomi", "lnreader", "music"
std::optional<Kind> kindFrom(const QString &name);
QString kindMedia(Kind kind);          // "comic", "anime", "novel", "music"

enum class ContentWarning { Safe = 0, Mixed = 1, Nsfw = 2 };

// One source inside an extension. Mihon and Aniyomi ids are stable hashes
// (the same on every device and in Mihon backups), kept as text because
// they are 64-bit; LNReader plugins are one source named by the plugin id.
struct SourceInfo {
    QString id;
    QString name;
    QString lang;
    QString baseUrl;
};

// An extension as a repository lists it, or as installed.
struct Package {
    Kind kind = Kind::Mihon;
    QString pkg;          // package name, or the LNReader plugin id
    QString name;
    QString versionName;
    qint64 versionCode = 0;
    QString lib;          // extensions-lib version ("1.4", "16"); empty for plugins
    QString lang;         // one language, or "all"
    ContentWarning warning = ContentWarning::Safe;
    QString fileUrl;      // the APK, or the plugin's JavaScript
    QString jarUrl;       // the same extension as a signed Java jar (Keiyoushi's stores)
    QString iconUrl;
    QString site;         // LNReader: the site the plugin reads
    bool torrent = false; // Aniyomi: needs a torrent client, which SCANCORE has not
    QList<SourceInfo> sources;

    QJsonObject toJson() const;
    static Package fromJson(const QJsonObject &o);
    // The languages of its sources.
    QStringList languages() const;
};

// A repository the user added.
struct Repository {
    Kind kind = Kind::Mihon;
    QString url;          // the address it is known by (index or store)
    QString name;
    QString badge;        // short name for badges
    QString website;
    QString signingKey;   // SHA-256 of the signing certificate, lower-case hex
    QString listUrl;      // where its extensions are listed

    QJsonObject toJson() const;
    static Repository fromJson(const QJsonObject &o);
};

// SHA-256 fingerprints in one spelling: lower-case hex without separators.
QString normalizedFingerprint(const QString &fingerprint);

// Lib versions SCANCORE's hosts implement, per kind.
QStringList supportedLibs(Kind kind);
enum class LibSupport { Supported, TooOld, TooNew, Unknown };
LibSupport libSupport(Kind kind, const QString &lib);

} // namespace sc::ext
