#include "Index.h"

#include "core/Proto.h"
#include "core/Text.h"
#include "sources/local/Local.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QVersionNumber>

namespace sc::ext {

namespace {

QString resolved(const QUrl &from, const QString &link) {
    return link.isEmpty() ? QString() : from.resolved(QUrl(link)).toString();
}

ContentWarning warningFrom(const QJsonValue &value) {
    // Store JSON spells the enum "SAFE" or "CONTENT_WARNING_SAFE"; protobuf
    // and older JSON use 0 unspecified, 1 safe, 2 mixed, 3 NSFW.
    if (value.isString()) {
        const auto name = value.toString();
        if (name.endsWith(QLatin1String("NSFW")))
            return ContentWarning::Nsfw;
        if (name.endsWith(QLatin1String("MIXED")))
            return ContentWarning::Mixed;
        return ContentWarning::Safe;
    }
    const int n = value.toInt();
    return n == 3 ? ContentWarning::Nsfw : n == 2 ? ContentWarning::Mixed : ContentWarning::Safe;
}

QString idText(const QJsonValue &value) {
    // Source ids are 64-bit; JSON numbers above 2^53 lose digits as doubles,
    // so they are read from the text when they come as strings.
    if (value.isString())
        return value.toString();
    return QString::number(value.toInteger());
}

QString languageOf(const QList<SourceInfo> &sources, const QString &fallback) {
    QStringList langs;
    for (const auto &s : sources)
        if (!langs.contains(s.lang))
            langs.append(s.lang);
    if (langs.size() == 1)
        return langs.first();
    return langs.isEmpty() ? fallback : QStringLiteral("all");
}

// ---------------------------------------------------------------- legacy

QList<Package> legacyIndex(const QJsonArray &rows, const QUrl &from, std::optional<Kind> hint) {
    // Links are relative to the folder the index is in.
    const QUrl base = from.resolved(QUrl(QStringLiteral(".")));
    QList<Package> out;
    out.reserve(rows.size());
    for (const auto &value : rows) {
        const auto o = value.toObject();
        Package p;
        p.pkg = o.value("pkg").toString();
        p.versionName = o.value("version").toString();
        p.lib = p.versionName.section(QLatin1Char('.'), 0, -2);
        p.kind = hint.value_or(kindForLib(p.lib));
        auto name = o.value("name").toString();
        for (const char *prefix : {"Tachiyomi: ", "Aniyomi: "})
            if (name.startsWith(QLatin1String(prefix)))
                name = name.mid(int(qstrlen(prefix)));
        p.name = name;
        p.versionCode = o.value("code").toInteger();
        p.lang = o.value("lang").toString();
        p.warning = o.value("nsfw").toInt() == 1 ? ContentWarning::Nsfw : ContentWarning::Safe;
        p.fileUrl = base.resolved(QUrl(QStringLiteral("apk/") + o.value("apk").toString())).toString();
        p.iconUrl = base.resolved(QUrl(QStringLiteral("icon/%1.png").arg(p.pkg))).toString();
        for (const auto &s : o.value("sources").toArray()) {
            const auto so = s.toObject();
            p.sources.append({idText(so.value("id")), so.value("name").toString(), so.value("lang").toString(),
                              so.value("baseUrl").toString()});
        }
        if (p.sources.isEmpty())
            p.sources.append({QStringLiteral("0"), p.name, p.lang, QString()});
        if (!p.pkg.isEmpty() && !o.value("apk").toString().isEmpty())
            out.append(p);
    }
    return out;
}

// The ids in a legacy index are plain JSON numbers; QJsonDocument reads them
// as doubles, which cannot hold every 64-bit id. Quote them before parsing.
QByteArray quoteLargeIds(const QByteArray &json) {
    QByteArray out;
    out.reserve(json.size() + json.size() / 20);
    const QByteArray key = "\"id\":";
    qsizetype at = 0;
    while (true) {
        const auto hit = json.indexOf(key, at);
        if (hit < 0) {
            out.append(json.mid(at));
            break;
        }
        auto i = hit + key.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t'))
            ++i;
        auto end = i;
        if (end < json.size() && json[end] == '-')
            ++end;
        while (end < json.size() && json[end] >= '0' && json[end] <= '9')
            ++end;
        out.append(json.mid(at, i - at));
        if (end > i && (end - i) > 15) {
            out.append('"').append(json.mid(i, end - i)).append('"');
        } else {
            out.append(json.mid(i, end - i));
        }
        at = end;
    }
    return out;
}

// ---------------------------------------------------------------- stores

Package storeExtension(const QJsonObject &o, const QUrl &from, std::optional<Kind> hint) {
    Package p;
    p.name = o.value("name").toString();
    p.pkg = o.value("packageName").toString();
    const auto resources = o.value("resources").toObject();
    p.fileUrl = resolved(from, resources.value("apkUrl").toString());
    if (const auto jar = resources.value("jarUrl").toString(); !jar.isEmpty())
        p.jarUrl = resolved(from, jar);
    p.iconUrl = resolved(from, resources.value("iconUrl").toString());
    p.lib = o.value("extensionLib").toString();
    p.kind = hint.value_or(kindForLib(p.lib));
    p.versionCode = o.value("versionCode").isString() ? o.value("versionCode").toString().toLongLong()
                                                      : o.value("versionCode").toInteger();
    p.versionName = o.value("versionName").toString();
    p.warning = warningFrom(o.value("contentWarning"));
    p.torrent = o.value("isTorrent").toBool();
    for (const auto &s : o.value("sources").toArray()) {
        const auto so = s.toObject();
        p.sources.append({idText(so.value("id")), so.value("name").toString(), so.value("language").toString(),
                          so.value("homeUrl").toString()});
    }
    p.lang = languageOf(p.sources, QString());
    return p;
}

Repository storeRepository(const QJsonObject &o, const QUrl &from) {
    Repository r;
    r.url = from.toString();
    r.name = o.value("name").toString();
    r.badge = o.value("badgeLabel").toString(r.name);
    r.signingKey = normalizedFingerprint(o.value("signingKey").toString());
    r.website = o.value("contact").toObject().value("website").toString();
    r.listUrl = resolved(from, o.value("extensionListUrl").toString());
    return r;
}

// Protobuf messages of the store format. Mihon and Aniyomi number the
// extension's fields alike up to 7; Mihon's sources are field 8, Aniyomi's
// 9 (its 8 is isTorrent), so the wire type tells them apart.
QJsonObject protoSource(QByteArrayView data) {
    QJsonObject s;
    Proto m(data);
    while (m.next()) {
        switch (m.field()) {
        case 1: s.insert("id", QString::number(qint64(m.varint()))); break;
        case 2: s.insert("name", m.text()); break;
        case 3: s.insert("language", m.text()); break;
        case 4: s.insert("homeUrl", m.text()); break;
        default: m.skip();
        }
    }
    return s;
}

QJsonObject protoExtension(QByteArrayView data) {
    QJsonObject e;
    QJsonArray sources;
    Proto m(data);
    while (m.next()) {
        switch (m.field()) {
        case 1: e.insert("name", m.text()); break;
        case 2: e.insert("packageName", m.text()); break;
        case 3: {
            QJsonObject resources;
            Proto r(m.bytes());
            while (r.next()) {
                if (r.field() == 1)
                    resources.insert("apkUrl", r.text());
                else if (r.field() == 2)
                    resources.insert("iconUrl", r.text());
                else if (r.field() == 501) // Keiyoushi's addition
                    resources.insert("jarUrl", r.text());
                else
                    r.skip();
            }
            e.insert("resources", resources);
            break;
        }
        case 4: e.insert("extensionLib", m.text()); break;
        case 5: e.insert("versionCode", QString::number(qint64(m.varint()))); break;
        case 6: e.insert("versionName", m.text()); break;
        case 7: e.insert("contentWarning", int(m.varint())); break;
        case 8:
            if (m.wire() == 0)
                e.insert("isTorrent", m.varint() != 0);
            else
                sources.append(protoSource(m.bytes()));
            break;
        case 9: sources.append(protoSource(m.bytes())); break;
        default: m.skip();
        }
    }
    e.insert("sources", sources);
    return e;
}

QJsonArray protoExtensionList(QByteArrayView data) {
    QJsonArray out;
    Proto m(data);
    while (m.next()) {
        if (m.field() == 1)
            out.append(protoExtension(m.bytes()));
        else
            m.skip();
    }
    return out;
}

QJsonObject protoStore(QByteArrayView data, bool *ok) {
    QJsonObject o;
    Proto m(data);
    while (m.next()) {
        switch (m.field()) {
        case 1: o.insert("name", m.text()); break;
        case 2: o.insert("badgeLabel", m.text()); break;
        case 3: o.insert("signingKey", m.text()); break;
        case 4: {
            QJsonObject contact;
            Proto c(m.bytes());
            while (c.next()) {
                if (c.field() == 1)
                    contact.insert("website", c.text());
                else
                    c.skip();
            }
            o.insert("contact", contact);
            break;
        }
        case 101: o.insert("extensionList", QJsonObject{{"extensions", protoExtensionList(m.bytes())}}); break;
        case 102: o.insert("extensionListUrl", m.text()); break;
        default: m.skip();
        }
    }
    *ok = !m.bad() && !o.value("name").toString().isEmpty();
    return o;
}

// ---------------------------------------------------------------- LNReader

QList<Package> pluginList(const QJsonArray &rows, const QUrl &from) {
    QList<Package> out;
    for (const auto &value : rows) {
        const auto o = value.toObject();
        Package p;
        p.kind = Kind::LNReader;
        p.pkg = o.value("id").toString();
        p.name = o.value("name").toString();
        p.site = o.value("site").toString();
        // Plugin lists name languages ("Español"); SCANCORE uses codes.
        p.lang = languageCode(o.value("lang").toString());
        if (p.lang.isEmpty())
            p.lang = o.value("lang").toString();
        p.versionName = o.value("version").toString();
        // Plugin versions are "1.2.3"; one number orders them.
        const auto version = QVersionNumber::fromString(p.versionName);
        p.versionCode = qint64(version.majorVersion()) * 1000000 + version.minorVersion() * 1000 + version.microVersion();
        p.fileUrl = resolved(from, o.value("url").toString());
        p.iconUrl = resolved(from, o.value("iconUrl").toString());
        p.sources.append({p.pkg, p.name, p.lang, p.site});
        if (!p.pkg.isEmpty() && !p.fileUrl.isEmpty())
            out.append(p);
    }
    return out;
}

// A CloudStream plugins.json: [{internalName, name, url, version,
// apiVersion, language, tvTypes, iconUrl, status, ...}]. Plugins marked down
// (status 0) are left out.
QList<Package> cloudStreamList(const QJsonArray &array, const QUrl &from) {
    QList<Package> out;
    for (const auto &value : array) {
        const auto o = value.toObject();
        Package p;
        p.kind = Kind::CloudStream;
        p.pkg = o.value("internalName").toString();
        if (p.pkg.isEmpty() || o.value("url").toString().isEmpty() || o.value("status").toInt(1) == 0)
            continue;
        p.name = o.value("name").toString(p.pkg);
        p.versionCode = o.value("version").toVariant().toLongLong();
        p.versionName = QString::number(p.versionCode);
        p.lib = QString::number(o.value("apiVersion").toInt(1));
        p.lang = o.value("language").toString(QStringLiteral("all")).toLower();
        if (p.lang.isEmpty())
            p.lang = QStringLiteral("all");
        QStringList types;
        for (const auto &t : o.value("tvTypes").toArray())
            types.append(t.toString());
        p.warning = types.contains(QStringLiteral("NSFW")) ? ContentWarning::Nsfw : ContentWarning::Safe;
        p.torrent = !types.isEmpty() && std::all_of(types.begin(), types.end(), [](const QString &t) { return t == QLatin1String("Torrent"); });
        p.fileUrl = resolved(from, o.value("url").toString());
        p.iconUrl = o.value("iconUrl").toString().replace(QLatin1String("%size%"), QLatin1String("128"));
        p.sources.append({cloudStreamSourceId(p.pkg), p.name, p.lang, QString()});
        out.append(p);
    }
    return out;
}

// An Echo extension list: [{id, name, subtitle, iconUrl, updateUrl}]. The
// file and its version are the latest release updateUrl names (GitHub's
// releases API), looked up when the repository is read (Manager).
QList<Package> echoList(const QJsonArray &array, const QUrl &from) {
    QList<Package> out;
    for (const auto &value : array) {
        const auto o = value.toObject();
        Package p;
        p.kind = Kind::Echo;
        p.pkg = o.value("id").toString();
        if (p.pkg.isEmpty() || o.value("updateUrl").toString().isEmpty())
            continue;
        p.name = o.value("name").toString(p.pkg);
        p.lib = QStringLiteral("1");
        p.lang = QStringLiteral("all");
        p.fileUrl = resolved(from, o.value("updateUrl").toString());
        p.iconUrl = resolved(from, o.value("iconUrl").toString());
        p.site = o.value("subtitle").toString();
        p.sources.append({echoSourceId(p.pkg), p.name, p.lang, QString()});
        out.append(p);
    }
    return out;
}

} // namespace

Kind kindForLib(const QString &lib) {
    return lib.section(QLatin1Char('.'), 0, 0).toInt() >= 10 ? Kind::Aniyomi : Kind::Mihon;
}

IndexDocument parseIndex(QByteArray body, const QUrl &from, std::optional<Kind> hint) {
    IndexDocument doc;
    if (body.startsWith("\x1f\x8b"))
        body = local::gunzip(body);
    const auto trimmed = body.trimmed();
    if (trimmed.startsWith('[')) {
        const auto array = QJsonDocument::fromJson(quoteLargeIds(trimmed)).array();
        const auto first = array.isEmpty() ? QJsonObject() : array.first().toObject();
        if (first.contains("internalName") || (array.isEmpty() && hint == Kind::CloudStream)) {
            doc.type = IndexDocument::Type::PluginList;
            doc.packages = cloudStreamList(array, from);
            doc.hasPackages = true;
            Repository r;
            r.kind = Kind::CloudStream;
            r.url = from.toString();
            r.name = from.host();
            r.badge = r.name;
            r.listUrl = r.url;
            doc.repo = r;
            return doc;
        }
        if (first.contains("updateUrl") || (array.isEmpty() && hint == Kind::Echo)) {
            doc.type = IndexDocument::Type::PluginList;
            doc.packages = echoList(array, from);
            doc.hasPackages = true;
            Repository r;
            r.kind = Kind::Echo;
            r.url = from.toString();
            r.name = from.host();
            r.badge = r.name;
            r.listUrl = r.url;
            doc.repo = r;
            return doc;
        }
        if (first.contains("pkg") || (array.isEmpty() && hint != Kind::LNReader && from.path().endsWith(QLatin1String("index.min.json")))) {
            doc.type = IndexDocument::Type::LegacyIndex;
            doc.packages = legacyIndex(array, from, hint);
            doc.hasPackages = true;
            // Its name and signing key are in repo.json beside it.
            doc.follow = from.resolved(QUrl(QStringLiteral("repo.json"))).toString();
        } else if (first.contains("site") || first.contains("url") || hint == Kind::LNReader) {
            doc.type = IndexDocument::Type::PluginList;
            doc.packages = pluginList(array, from);
            doc.hasPackages = true;
            Repository r;
            r.kind = Kind::LNReader;
            r.url = from.toString();
            r.name = from.host();
            r.badge = r.name;
            r.listUrl = r.url;
            doc.repo = r;
        }
        return doc;
    }
    if (trimmed.startsWith('{')) {
        const auto o = QJsonDocument::fromJson(quoteLargeIds(trimmed)).object();
        // SCANCORE's music plugins: {"name": ..., "music": [...]}.
        if (o.value("music").isArray()) {
            doc = parseIndex(QJsonDocument(o.value("music").toArray()).toJson(QJsonDocument::Compact), from, Kind::LNReader);
            for (auto &p : doc.packages)
                p.kind = Kind::Music;
            if (doc.repo) {
                doc.repo->kind = Kind::Music;
                if (!o.value("name").toString().isEmpty())
                    doc.repo->name = doc.repo->badge = o.value("name").toString();
            }
            return doc;
        }
        // CloudStream's repo.json: its name and the plugin lists it has
        // (the first is read).
        if (o.value("pluginLists").isArray()) {
            doc.type = IndexDocument::Type::CloudStreamRepo;
            Repository r;
            r.kind = Kind::CloudStream;
            r.url = from.toString();
            r.name = o.value("name").toString(from.host());
            r.badge = r.name;
            r.listUrl = resolved(from, o.value("pluginLists").toArray().first().toString());
            doc.repo = r;
            doc.follow = r.listUrl;
            return doc;
        }
        // A plugin list wrapped in an object ({"plugins": [...]}).
        if (o.value("plugins").isArray()) {
            doc = parseIndex(QJsonDocument(o.value("plugins").toArray()).toJson(QJsonDocument::Compact), from, Kind::LNReader);
            if (doc.repo && !o.value("name").toString().isEmpty())
                doc.repo->name = doc.repo->badge = o.value("name").toString();
            return doc;
        }
        if (o.contains("meta")) {
            doc.type = IndexDocument::Type::LegacyRepo;
            const auto meta = o.value("meta").toObject();
            Repository r;
            r.kind = hint.value_or(Kind::Mihon);
            r.url = from.toString();
            r.name = meta.value("name").toString();
            r.badge = meta.value("shortName").toString(r.name);
            r.website = meta.value("website").toString();
            r.signingKey = normalizedFingerprint(meta.value("signingKeyFingerprint").toString());
            r.listUrl = from.resolved(QUrl(QStringLiteral("index.min.json"))).toString();
            doc.repo = r;
            doc.follow = o.value("index_v2").toString().isEmpty() ? r.listUrl : resolved(from, o.value("index_v2").toString());
            return doc;
        }
        if (o.contains("extensions") && !o.contains("name")) {
            doc.type = IndexDocument::Type::ExtensionList;
            for (const auto &e : o.value("extensions").toArray())
                doc.packages.append(storeExtension(e.toObject(), from, hint));
            doc.hasPackages = true;
            return doc;
        }
        if (o.contains("signingKey")) {
            doc.type = IndexDocument::Type::Store;
            doc.repo = storeRepository(o, from);
            if (o.value("extensionList").isObject()) {
                for (const auto &e : o.value("extensionList").toObject().value("extensions").toArray())
                    doc.packages.append(storeExtension(e.toObject(), from, hint));
                doc.hasPackages = true;
            } else {
                doc.follow = doc.repo->listUrl;
            }
        }
        return doc;
    }
    // Protobuf: a whole store, or the extension list it links to. Not
    // trimmed: its first byte is often 0x0a, a newline.
    bool ok = false;
    const auto store = protoStore(body, &ok);
    if (ok && store.contains("signingKey")) {
        doc.type = IndexDocument::Type::Store;
        doc.repo = storeRepository(store, from);
        if (store.value("extensionList").isObject()) {
            for (const auto &e : store.value("extensionList").toObject().value("extensions").toArray())
                doc.packages.append(storeExtension(e.toObject(), from, hint));
            doc.hasPackages = true;
        } else {
            doc.follow = doc.repo->listUrl;
        }
        return doc;
    }
    const auto list = protoExtensionList(body);
    if (!list.isEmpty()) {
        doc.type = IndexDocument::Type::ExtensionList;
        for (const auto &e : list)
            doc.packages.append(storeExtension(e.toObject(), from, hint));
        doc.hasPackages = true;
    }
    return doc;
}

} // namespace sc::ext
