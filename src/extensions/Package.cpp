#include "Package.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QVersionNumber>

namespace sc::ext {

QString kindName(Kind kind) {
    switch (kind) {
    case Kind::Mihon: return QStringLiteral("mihon");
    case Kind::Aniyomi: return QStringLiteral("aniyomi");
    case Kind::LNReader: return QStringLiteral("lnreader");
    case Kind::Music: return QStringLiteral("music");
    case Kind::CloudStream: return QStringLiteral("cloudstream");
    case Kind::Echo: return QStringLiteral("echo");
    }
    return {};
}

std::optional<Kind> kindFrom(const QString &name) {
    if (name == QLatin1String("mihon"))
        return Kind::Mihon;
    if (name == QLatin1String("aniyomi"))
        return Kind::Aniyomi;
    if (name == QLatin1String("lnreader"))
        return Kind::LNReader;
    if (name == QLatin1String("music"))
        return Kind::Music;
    if (name == QLatin1String("cloudstream"))
        return Kind::CloudStream;
    if (name == QLatin1String("echo"))
        return Kind::Echo;
    return std::nullopt;
}

QString kindMedia(Kind kind) {
    switch (kind) {
    case Kind::Mihon: return QStringLiteral("comic");
    case Kind::Aniyomi: return QStringLiteral("anime");
    case Kind::LNReader: return QStringLiteral("novel");
    case Kind::Music: return QStringLiteral("music");
    case Kind::CloudStream: return QStringLiteral("anime");
    case Kind::Echo: return QStringLiteral("music");
    }
    return {};
}

static QString hashedSourceId(const QString &name) {
    const auto hash = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Md5);
    quint64 id = 0;
    for (int i = 0; i < 8; ++i)
        id = (id << 8) | quint8(hash[i]);
    return QString::number(id & 0x7fffffffffffffffULL);
}

QString cloudStreamSourceId(const QString &pkg) { return hashedSourceId(QStringLiteral("cloudstream/%1").arg(pkg)); }

QString echoSourceId(const QString &pkg) { return hashedSourceId(QStringLiteral("echo/%1").arg(pkg)); }

QJsonObject Package::toJson() const {
    QJsonArray list;
    for (const auto &s : sources)
        list.append(QJsonObject{{"id", s.id}, {"name", s.name}, {"lang", s.lang}, {"baseUrl", s.baseUrl}});
    return {{"kind", kindName(kind)},
            {"pkg", pkg},
            {"name", name},
            {"versionName", versionName},
            {"versionCode", QString::number(versionCode)},
            {"lib", lib},
            {"lang", lang},
            {"warning", int(warning)},
            {"fileUrl", fileUrl},
            {"jarUrl", jarUrl},
            {"iconUrl", iconUrl},
            {"site", site},
            {"torrent", torrent},
            {"sources", list}};
}

Package Package::fromJson(const QJsonObject &o) {
    Package p;
    p.kind = kindFrom(o.value("kind").toString()).value_or(Kind::Mihon);
    p.pkg = o.value("pkg").toString();
    p.name = o.value("name").toString();
    p.versionName = o.value("versionName").toString();
    p.versionCode = o.value("versionCode").toString().toLongLong();
    p.lib = o.value("lib").toString();
    p.lang = o.value("lang").toString();
    p.warning = ContentWarning(std::clamp(o.value("warning").toInt(), 0, 2));
    p.fileUrl = o.value("fileUrl").toString();
    p.jarUrl = o.value("jarUrl").toString();
    p.iconUrl = o.value("iconUrl").toString();
    p.site = o.value("site").toString();
    p.torrent = o.value("torrent").toBool();
    for (const auto &value : o.value("sources").toArray()) {
        const auto s = value.toObject();
        p.sources.append({s.value("id").toString(), s.value("name").toString(), s.value("lang").toString(),
                          s.value("baseUrl").toString()});
    }
    return p;
}

QStringList Package::languages() const {
    QStringList out;
    for (const auto &s : sources)
        if (!out.contains(s.lang))
            out.append(s.lang);
    return out;
}

QJsonObject Repository::toJson() const {
    return {{"kind", kindName(kind)}, {"url", url},         {"name", name},      {"badge", badge},
            {"website", website},     {"signingKey", signingKey}, {"listUrl", listUrl}};
}

Repository Repository::fromJson(const QJsonObject &o) {
    Repository r;
    r.kind = kindFrom(o.value("kind").toString()).value_or(Kind::Mihon);
    r.url = o.value("url").toString();
    r.name = o.value("name").toString();
    r.badge = o.value("badge").toString();
    r.website = o.value("website").toString();
    r.signingKey = o.value("signingKey").toString();
    r.listUrl = o.value("listUrl").toString();
    return r;
}

QString normalizedFingerprint(const QString &fingerprint) {
    static const QRegularExpression separators(QStringLiteral("[^0-9a-fA-F]"));
    return QString(fingerprint).remove(separators).toLower();
}

QStringList supportedLibs(Kind kind) {
    switch (kind) {
    case Kind::Mihon: return {QStringLiteral("1.4"), QStringLiteral("1.5"), QStringLiteral("1.6")};
    case Kind::Aniyomi: return {QStringLiteral("14"), QStringLiteral("16"), QStringLiteral("17")};
    case Kind::CloudStream:
    case Kind::Echo: return {QStringLiteral("1")};
    case Kind::LNReader:
    case Kind::Music: return {};
    }
    return {};
}

LibSupport libSupport(Kind kind, const QString &lib) {
    if (isPluginKind(kind))
        return LibSupport::Supported;
    const auto supported = supportedLibs(kind);
    const auto wanted = QVersionNumber::fromString(lib);
    if (wanted.isNull())
        return LibSupport::Unknown;
    for (const auto &s : supported)
        if (QVersionNumber::fromString(s).normalized() == wanted.normalized())
            return LibSupport::Supported;
    if (wanted > QVersionNumber::fromString(supported.last()))
        return LibSupport::TooNew;
    if (wanted < QVersionNumber::fromString(supported.first()))
        return LibSupport::TooOld;
    // Between two supported versions but not one of them.
    return LibSupport::Unknown;
}

} // namespace sc::ext
