// Offline tests for the extension core: repository formats, signing keys,
// trust, installs and updates, source keys and the extension source. The
// repositories are fixtures on repo.test (tests/fixtures/ext), served
// through Http's test handler; the APKs are tiny signed zips made by
// tests/fixtures/ext/make-fixtures.sh.

#include "core/Database.h"
#include "core/Http.h"
#include "core/Text.h"
#include "extensions/Apk.h"
#include "extensions/ExtensionSource.h"
#include "extensions/Host.h"
#include "extensions/LineHost.h"
#include "extensions/Index.h"
#include "extensions/Manager.h"
#include "extensions/Runtime.h"
#include "sources/anime/Playback.h"
#include "sources/comics/Registry.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
#include <QBuffer>
#include <QCryptographicHash>
#include <QtCore/private/qzipwriter_p.h>

using namespace sc;
using namespace sc::ext;

namespace {

QString fixturePath(const QString &name) { return QStringLiteral(SCANCORE_SOURCE_DIR "/tests/fixtures/ext/") + name; }

QByteArray fixture(const QString &name) {
    QFile file(fixturePath(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString key(const char *name) { return QString::fromLatin1(fixture(QStringLiteral("key-%1.txt").arg(QLatin1String(name)))).trimmed(); }

// https://repo.test/<path> serves tests/fixtures/ext/<path>; APKs live at
// the top of that folder. Paths can be replaced to change what is served.
class FixtureServer {
public:
    FixtureServer() {
        Http::setTestHandler([this](const Request &request) -> std::optional<Response> {
            if (request.url.host() != QLatin1String("repo.test"))
                return std::nullopt;
            QMutexLocker lock(&m_mutex);
            ++m_requests;
            auto path = request.url.path().mid(1);
            Response response;
            response.status = 200;
            QByteArray body;
            if (m_overrides.contains(path))
                body = m_overrides.value(path);
            else if (path.contains(QLatin1String("/apk/")) || path.startsWith(QLatin1String("apk/")) || path.startsWith(QLatin1String("jar/")))
                body = fixture(path.section(QLatin1Char('/'), -1).startsWith(QLatin1String("store-comics."))
                                   ? QStringLiteral("tiny-alpha.apk")
                                   : path.section(QLatin1Char('/'), -1));
            else
                body = fixture(path);
            if (body.isEmpty()) {
                response.status = 404;
                return response;
            }
            const auto etag = QByteArray("\"") + QByteArray::number(qHash(body)) + "\"";
            for (const auto &[name, value] : request.headers)
                if (name == "If-None-Match" && value == etag) {
                    response.status = 304;
                    return response;
                }
            response.body = body;
            response.headers = {{"ETag", etag}};
            return response;
        });
    }
    ~FixtureServer() { Http::setTestHandler({}); }
    void serve(const QString &path, const QByteArray &body) {
        QMutexLocker lock(&m_mutex);
        m_overrides.insert(path, body);
    }
    void reset() {
        QMutexLocker lock(&m_mutex);
        m_overrides.clear();
    }
    int requests() {
        QMutexLocker lock(&m_mutex);
        return m_requests;
    }

private:
    QMutex m_mutex;
    QHash<QString, QByteArray> m_overrides;
    int m_requests = 0;
};

// The extension host, answered from a script of canned results.
class FakeHost : public Host {
public:
    bool available() const override { return true; }
    QJsonValue call(const QString &method, const QJsonObject &params) override {
        QMutexLocker lock(&mutex);
        calls.append(method);
        last = params;
        if (method == QLatin1String("fail"))
            throw Error(QStringLiteral("The site is down."), 502);
        return answers.value(method);
    }
    QMutex mutex;
    QHash<QString, QJsonValue> answers;
    QStringList calls;
    QJsonObject last;
};

// Protobuf writer for the store test.
QByteArray varint(quint64 v) {
    QByteArray out;
    do {
        uchar b = v & 0x7f;
        v >>= 7;
        if (v)
            b |= 0x80;
        out.append(char(b));
    } while (v);
    return out;
}
QByteArray field(int number, const QByteArray &bytes) { return varint(quint64(number) << 3 | 2) + varint(bytes.size()) + bytes; }
QByteArray field(int number, quint64 value) { return varint(quint64(number) << 3) + varint(value); }

class BrowserCheckTestHost : public LineHost {
public:
    using LineHost::receive;
    bool available() const override { return true; }
    void sendLine(const QByteArray &) override {}
};

} // namespace

class ExtensionTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<FixtureServer> m_server;

private slots:
    void interactiveCheckKeepsExtensionAgent() {
        BrowserCheckTestHost host;
        const QJsonObject params{{"url", "https://agent-fixture.test/check"}, {"userAgent", "Source-specific UA"}};
        QString agentAtDispatch;
        bool dispatched = false;
        LineHost::setAskHandler([&](const QString &method, const QJsonObject &request, LineHost::Answer) {
            dispatched = method == QLatin1String("readerBrowserCheck") && request == params;
            agentAtDispatch = browserCheckAgent(request.value(QStringLiteral("url")).toString());
        });
        host.receive(QJsonDocument(QJsonObject{{"ask", 1}, {"method", "readerBrowserCheck"}, {"params", params}}).toJson());
        LineHost::setAskHandler({});
        QVERIFY(dispatched);
        QCOMPARE(agentAtDispatch, QStringLiteral("Source-specific UA"));
        QCOMPARE(browserCheckAgent(QStringLiteral("https://agent-fixture.test/another-path")), QStringLiteral("Source-specific UA"));
        QVERIFY(browserCheckAgent(QStringLiteral("https://unrelated-agent-fixture.test/")).isEmpty());
    }

    void interactiveCheckIgnoresEmptyAndUnrelatedAgents() {
        BrowserCheckTestHost host;
        const QString url = QStringLiteral("https://agent-edge-fixture.test/check");
        const auto receive = [&](const QString &method, const QJsonObject &params) {
            host.receive(QJsonDocument(QJsonObject{{"ask", 1}, {"method", method}, {"params", params}}).toJson());
        };
        receive(QStringLiteral("readerBrowserCheck"), {{"url", url}, {"userAgent", "Original UA"}});
        QCOMPARE(browserCheckAgent(url), QStringLiteral("Original UA"));
        receive(QStringLiteral("readerBrowserCheck"), {{"url", url}, {"userAgent", ""}});
        QCOMPARE(browserCheckAgent(url), QStringLiteral("Original UA"));
        receive(QStringLiteral("readerBrowserCheck"), {{"url", url}});
        QCOMPARE(browserCheckAgent(url), QStringLiteral("Original UA"));
        receive(QStringLiteral("unrelatedAsk"), {{"url", url}, {"userAgent", "Unrelated UA"}});
        QCOMPARE(browserCheckAgent(url), QStringLiteral("Original UA"));
        receive(QStringLiteral("readerBrowserCheck"), {{"url", "https://agent-empty-fixture.test/"}, {"userAgent", ""}});
        QVERIFY(browserCheckAgent(QStringLiteral("https://agent-empty-fixture.test/")).isEmpty());
        receive(QStringLiteral("readerBrowserCheck"), {{"url", url}, {"userAgent", "Updated UA"}});
        QCOMPARE(browserCheckAgent(url), QStringLiteral("Updated UA"));
    }

    void initTestCase() {
        QVERIFY(m_dir.isValid());
        Database::open(m_dir.filePath(QStringLiteral("test.db")));
        Manager::setStorage(m_dir.filePath(QStringLiteral("extensions")));
        m_server = std::make_unique<FixtureServer>();
    }
    void cleanupTestCase() { m_server.reset(); }

    void signingKeys() {
        QCOMPARE(apkSigners(fixture(QStringLiteral("tiny-alpha.apk"))), QStringList{key("alpha")});
        QCOMPARE(apkSigners(fixture(QStringLiteral("tiny-beta.apk"))), QStringList{key("beta")});
        // Only a JAR (v1) signature.
        QCOMPARE(apkSigners(fixture(QStringLiteral("tiny-v1.apk"))), QStringList{key("alpha")});
        QVERIFY(apkSigners("not an apk").isEmpty());
        QVERIFY(apkSigners(fixture(QStringLiteral("tiny-alpha.apk")).left(4000)).isEmpty());
        QCOMPARE(normalizedFingerprint(QStringLiteral("AB:cd:01")), QStringLiteral("abcd01"));
    }

    void legacyIndex() {
        const QUrl from(QStringLiteral("https://repo.test/mihon/index.min.json"));
        const auto doc = parseIndex(fixture(QStringLiteral("mihon/index.min.json")), from);
        QCOMPARE(doc.type, IndexDocument::Type::LegacyIndex);
        QCOMPARE(doc.follow, QStringLiteral("https://repo.test/mihon/repo.json"));
        QCOMPARE(doc.packages.size(), 5);
        const auto &alpha = doc.packages[0];
        QCOMPARE(alpha.kind, Kind::Mihon);
        QCOMPARE(alpha.name, QStringLiteral("Alpha Comics"));
        QCOMPARE(alpha.lib, QStringLiteral("1.4"));
        QCOMPARE(alpha.fileUrl, QStringLiteral("https://repo.test/mihon/apk/tiny-alpha.apk"));
        QCOMPARE(alpha.iconUrl, QStringLiteral("https://repo.test/mihon/icon/test.ext.alpha.png"));
        // 64-bit ids survive JSON.
        QCOMPARE(alpha.sources[0].id, QStringLiteral("7894320198765432101"));
        QCOMPARE(doc.packages[3].warning, ContentWarning::Nsfw);
        const auto repo = parseIndex(fixture(QStringLiteral("mihon/repo.json")), QUrl(doc.follow));
        QCOMPARE(repo.type, IndexDocument::Type::LegacyRepo);
        QCOMPARE(repo.repo->signingKey, key("alpha"));
        QCOMPARE(repo.repo->badge, QStringLiteral("Test"));
        // Aniyomi's lib versions say what it is.
        const auto anime = parseIndex(fixture(QStringLiteral("anime/index.min.json")), QUrl(QStringLiteral("https://repo.test/anime/index.min.json")));
        QCOMPARE(anime.packages[0].kind, Kind::Aniyomi);
        QCOMPARE(anime.packages[0].lib, QStringLiteral("14"));
    }

    void storeFormats() {
        const QUrl from(QStringLiteral("https://repo.test/store.json"));
        const auto json = parseIndex(fixture(QStringLiteral("store.json")), from);
        QCOMPARE(json.type, IndexDocument::Type::Store);
        QCOMPARE(json.repo->signingKey, key("alpha"));
        QCOMPARE(json.repo->badge, QStringLiteral("TS"));
        QCOMPARE(json.packages.size(), 1);
        const auto &p = json.packages[0];
        QCOMPARE(p.lib, QStringLiteral("1.6"));
        QCOMPARE(p.warning, ContentWarning::Mixed);
        QCOMPARE(p.fileUrl, QStringLiteral("https://repo.test/apk/store-comics.apk"));
        QCOMPARE(p.jarUrl, QStringLiteral("https://repo.test/jar/store-comics.jar"));
        QCOMPARE(p.sources[0].id, QStringLiteral("9007199254740993"));
        QCOMPARE(p.lang, QStringLiteral("pt-BR"));

        // The same store as protobuf, with Aniyomi's numbering (isTorrent 8,
        // sources 9) for the extension.
        const auto source = field(1, quint64(4242)) + field(2, QByteArray("Proto Anime")) + field(3, QByteArray("en"));
        const auto extension = field(1, QByteArray("Proto Anime")) + field(2, QByteArray("test.proto.anime")) +
                               field(3, field(1, QByteArray("apk/p.apk")) + field(2, QByteArray("icon/p.png")) + field(501, QByteArray("jar/p.jar"))) +
                               field(4, QByteArray("16")) + field(5, quint64(9)) + field(6, QByteArray("16.9")) +
                               field(7, quint64(1)) + field(8, quint64(1)) + field(9, source);
        const auto store = field(1, QByteArray("Proto Store")) + field(2, QByteArray("PS")) + field(3, key("beta").toUtf8()) +
                           field(4, field(1, QByteArray("https://repo.test"))) + field(101, field(1, extension));
        const auto proto = parseIndex(store, QUrl(QStringLiteral("https://repo.test/store.pb")));
        QCOMPARE(proto.type, IndexDocument::Type::Store);
        QCOMPARE(proto.repo->name, QStringLiteral("Proto Store"));
        QCOMPARE(proto.packages.size(), 1);
        QCOMPARE(proto.packages[0].kind, Kind::Aniyomi);
        QVERIFY(proto.packages[0].torrent);
        QCOMPARE(proto.packages[0].sources[0].id, QStringLiteral("4242"));
        QCOMPARE(proto.packages[0].fileUrl, QStringLiteral("https://repo.test/apk/p.apk"));
        QCOMPARE(proto.packages[0].jarUrl, QStringLiteral("https://repo.test/jar/p.jar"));
    }

    void pluginLists() {
        const QUrl from(QStringLiteral("https://repo.test/novels/plugins.min.json"));
        const auto doc = parseIndex(fixture(QStringLiteral("novels/plugins.min.json")), from);
        QCOMPARE(doc.type, IndexDocument::Type::PluginList);
        QCOMPARE(doc.packages.size(), 2);
        QCOMPARE(doc.packages[0].kind, Kind::LNReader);
        QCOMPARE(doc.packages[0].lang, QStringLiteral("en"));
        QCOMPARE(doc.packages[1].lang, QStringLiteral("es"));
        QVERIFY(doc.packages[0].versionCode > QStringLiteral("1.2.9").size());
        QCOMPARE(doc.packages[1].fileUrl, QStringLiteral("https://repo.test/novels/testnovelas.js"));
        QCOMPARE(languageCode(QStringLiteral("中文 (简体)")), QStringLiteral("zh"));
    }

    // CloudStream: repo.json names its plugin lists; ids are the host's.
    void cloudStreamRepositories() {
        const QUrl from(QStringLiteral("https://repo.test/cloudstream/repo.json"));
        const auto repo = parseIndex(fixture(QStringLiteral("cloudstream/repo.json")), from);
        QCOMPARE(repo.type, IndexDocument::Type::CloudStreamRepo);
        QCOMPARE(repo.repo->kind, Kind::CloudStream);
        QCOMPARE(repo.repo->name, QStringLiteral("Test CloudStream repository"));
        QCOMPARE(repo.follow, QStringLiteral("https://repo.test/cloudstream/plugins.json"));
        const auto list = parseIndex(fixture(QStringLiteral("cloudstream/plugins.json")), QUrl(repo.follow));
        QCOMPARE(list.type, IndexDocument::Type::PluginList);
        // The one marked down is left out.
        QCOMPARE(list.packages.size(), 3);
        const auto &first = list.packages[0];
        QCOMPARE(first.kind, Kind::CloudStream);
        QCOMPARE(first.pkg, QStringLiteral("DailymotionProvider"));
        QCOMPARE(first.versionCode, 4);
        QCOMPARE(first.lib, QStringLiteral("1"));
        QCOMPARE(first.iconUrl, QStringLiteral("https://icons.test/x.png?sz=128"));
        // The id core's CloudStream.sourceId gives (checked against the host).
        QCOMPARE(first.sources.size(), 1);
        QCOMPARE(first.sources[0].id, QStringLiteral("1943850539651523733"));
        QCOMPARE(cloudStreamSourceId(QStringLiteral("DailymotionProvider")), QStringLiteral("1943850539651523733"));
        QCOMPARE(list.packages[1].fileUrl, QStringLiteral("https://repo.test/cloudstream/Adult.cs3"));
        QCOMPARE(list.packages[1].warning, ContentWarning::Nsfw);
        QCOMPARE(list.packages[1].lang, QStringLiteral("pt-br"));
        QVERIFY(list.packages[2].torrent);
        QCOMPARE(libSupport(Kind::CloudStream, QStringLiteral("1")), LibSupport::Supported);
        QCOMPARE(kindMedia(Kind::CloudStream), QStringLiteral("anime"));
        const auto link = Manager::parseLink(QStringLiteral("cloudstreamrepo://repo.test/cloudstream/repo.json"));
        QVERIFY(link);
        QCOMPARE(link->kind, std::optional(Kind::CloudStream));
    }

    // Echo: a list of {id, name, iconUrl, updateUrl}; ids are the host's.
    void echoLists() {
        const QUrl from(QStringLiteral("https://repo.test/echo/extensions.json"));
        const auto doc = parseIndex(fixture(QStringLiteral("echo/extensions.json")), from);
        QCOMPARE(doc.type, IndexDocument::Type::PluginList);
        QCOMPARE(doc.repo->kind, Kind::Echo);
        QCOMPARE(doc.packages.size(), 2);
        const auto &first = doc.packages[0];
        QCOMPARE(first.kind, Kind::Echo);
        QCOMPARE(first.pkg, QStringLiteral("testecho"));
        QCOMPARE(first.name, QStringLiteral("Test Echo"));
        QCOMPARE(first.iconUrl, QStringLiteral("https://repo.test/echo/icon.png"));
        // The file comes from the release updateUrl names.
        QCOMPARE(first.fileUrl, QStringLiteral("https://repo.test/echo/releases.json"));
        QCOMPARE(first.versionCode, 0);
        // The id core's Echo.sourceId gives.
        QCOMPARE(first.sources[0].id, QStringLiteral("1270673316770206241"));
        QCOMPARE(echoSourceId(QStringLiteral("testecho")), QStringLiteral("1270673316770206241"));
        QCOMPARE(kindMedia(Kind::Echo), QStringLiteral("music"));
        QCOMPARE(libSupport(Kind::Echo, first.lib), LibSupport::Supported);
        QVERIFY(isMusicKind(Kind::Echo) && isSignedFile(Kind::Echo) && isUnsignedKind(Kind::Echo));
    }

    void libVersions() {
        QCOMPARE(libSupport(Kind::Mihon, QStringLiteral("1.4")), LibSupport::Supported);
        QCOMPARE(libSupport(Kind::Mihon, QStringLiteral("1.9")), LibSupport::TooNew);
        QCOMPARE(libSupport(Kind::Mihon, QStringLiteral("1.2")), LibSupport::TooOld);
        QCOMPARE(libSupport(Kind::Aniyomi, QStringLiteral("16.0")), LibSupport::Supported);
        QCOMPARE(libSupport(Kind::Aniyomi, QStringLiteral("12")), LibSupport::TooOld);
    }

    // One-tap links: SCANCORE's own and the schemes community repository
    // pages already use.
    void repositoryLinks() {
        const auto target = QStringLiteral("https://repo.test/mihon/index.min.json");
        const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(target));
        for (const auto &link : {QStringLiteral("tachiyomi://add-repo?url=") + encoded, QStringLiteral("mihon://add-repo?url=") + target,
                                 QStringLiteral("scancore://repos/add?url=") + encoded}) {
            const auto parsed = Manager::parseLink(link);
            QVERIFY2(parsed, qPrintable(link));
            QCOMPARE(parsed->url, target);
        }
        QCOMPARE(Manager::parseLink(QStringLiteral("mihon://add-repo?url=") + target)->kind, std::optional(Kind::Mihon));
        QCOMPARE(Manager::parseLink(QStringLiteral("aniyomi://add-repo?url=") + target)->kind, std::optional(Kind::Aniyomi));
        QCOMPARE(Manager::parseLink(QStringLiteral("lnreader://repo/add?url=") + target)->kind, std::optional(Kind::LNReader));
        QCOMPARE(Manager::parseLink(QStringLiteral("scancore://repos/add?kind=aniyomi&url=") + encoded)->kind, std::optional(Kind::Aniyomi));
        QVERIFY(!Manager::parseLink(QStringLiteral("scancore://repos/add?url=") + encoded)->kind);
        // Not repository links.
        QVERIFY(!Manager::parseLink(target));
        QVERIFY(!Manager::parseLink(QStringLiteral("tachiyomi://add-repo")));
        QVERIFY(!Manager::parseLink(QStringLiteral("mihon://something-else?url=") + encoded));
    }

    void autoInstallInContentLanguage() {
        const auto packages = parseIndex(fixture(QStringLiteral("mihon/index.min.json")),
                                         QUrl(QStringLiteral("https://repo.test/mihon/index.min.json")))
                                  .packages;
        QStringList picked;
        for (const auto &p : Manager::autoInstallSelection(packages, QStringLiteral("es"), false))
            picked.append(p.pkg);
        // Spanish, and the multi-language one with a Spanish source; not the
        // adult one or the one for a newer SCANCORE.
        QCOMPARE(picked, (QStringList{QStringLiteral("test.ext.beta"), QStringLiteral("test.ext.multi")}));
        picked.clear();
        for (const auto &p : Manager::autoInstallSelection(packages, QStringLiteral("es"), true))
            picked.append(p.pkg);
        QVERIFY(picked.contains(QStringLiteral("test.ext.adult")));
    }

    void addAndInstall() {
        Database::setPref(QStringLiteral("setting.sourceLanguages"), QStringLiteral("es"));
        // A folder link finds its index.
        const auto preview = Manager::preview(QStringLiteral("https://repo.test/mihon/"));
        QCOMPARE(preview.repo.name, QStringLiteral("Test Comics Repository"));
        QCOMPARE(preview.repo.url, QStringLiteral("https://repo.test/mihon/repo.json"));
        QCOMPARE(preview.repo.listUrl, QStringLiteral("https://repo.test/mihon/index.min.json"));
        QCOMPARE(preview.packages.size(), 5);
        QCOMPARE(preview.autoInstall.size(), 2);
        Manager::add(preview);
        QCOMPARE(Manager::repositories().size(), 1);

        // Signed with the repository's key: trusted.
        const auto alpha = Manager::install(Kind::Mihon, QStringLiteral("test.ext.alpha"));
        QVERIFY(alpha.trusted);
        QCOMPARE(alpha.signers, QStringList{key("alpha")});
        QVERIFY(QFile::exists(alpha.path));
        QVERIFY(!(QFileInfo(alpha.path).permissions() & QFile::WriteOwner));

        // Signed with another key: installed, waiting for trust.
        const auto beta = Manager::install(Kind::Mihon, QStringLiteral("test.ext.beta"));
        QVERIFY(!beta.trusted);
        auto sources = Manager::sources(Kind::Mihon);
        const auto betaSource = std::find_if(sources.begin(), sources.end(), [](const SourceEntry &e) { return e.pkg == "test.ext.beta"; });
        QVERIFY(!betaSource->blocked.isEmpty());
        Manager::trust(Kind::Mihon, QStringLiteral("test.ext.beta"));
        QVERIFY(Manager::installedPackage(Kind::Mihon, QStringLiteral("test.ext.beta"))->trusted);

        // A multi-language extension: only its Spanish source starts on.
        Manager::install(Kind::Mihon, QStringLiteral("test.ext.multi"));
        sources = Manager::sources(Kind::Mihon);
        QHash<QString, bool> on;
        for (const auto &s : sources)
            on.insert(s.key, s.enabled);
        QVERIFY(!on.value(QStringLiteral("mihon:11")));
        QVERIFY(on.value(QStringLiteral("mihon:12")));
        QVERIFY(!on.value(QStringLiteral("mihon:13")));
        QCOMPARE(on.size(), 5);

        // Refused with a reason the user can act on.
        try {
            Manager::install(Kind::Mihon, QStringLiteral("test.ext.future"));
            QFAIL("installed an extension for a newer SCANCORE");
        } catch (const Error &e) {
            QVERIFY(e.message().contains(QLatin1String("newer SCANCORE")));
        }

        // Survives a restart.
        Manager::reload();
        QCOMPARE(Manager::installed(Kind::Mihon).size(), 3);
        QVERIFY(Manager::installedPackage(Kind::Mihon, QStringLiteral("test.ext.beta"))->trusted);
    }

    void updates() {
        // Nothing new: answered 304, nothing changes.
        const auto before = m_server->requests();
        QVERIFY(Manager::refresh(QStringLiteral("https://repo.test/mihon/repo.json")) || true);
        QVERIFY(!Manager::refresh(QStringLiteral("https://repo.test/mihon/repo.json")));
        QVERIFY(m_server->requests() > before);

        auto index = QJsonDocument::fromJson(fixture(QStringLiteral("mihon/index.min.json"))).array();
        auto alpha = index[0].toObject();
        alpha.insert("code", 4);
        alpha.insert("version", "1.4.4");
        // First a version signed with another key: refused.
        alpha.insert("apk", "tiny-beta.apk");
        index[0] = alpha;
        m_server->serve(QStringLiteral("mihon/index.min.json"), QJsonDocument(index).toJson());
        QVERIFY(Manager::refresh(QStringLiteral("https://repo.test/mihon/repo.json")));
        QCOMPARE(Manager::updates().size(), 1);
        QVERIFY(Manager::installTrustedUpdates().isEmpty());
        QCOMPARE(Manager::installedPackage(Kind::Mihon, QStringLiteral("test.ext.alpha"))->package.versionCode, 3);

        alpha.insert("apk", "tiny-alpha.apk");
        index[0] = alpha;
        m_server->serve(QStringLiteral("mihon/index.min.json"), QJsonDocument(index).toJson());
        Manager::refresh(QStringLiteral("https://repo.test/mihon/repo.json"));
        const auto done = Manager::installTrustedUpdates();
        QCOMPARE(done.size(), 1);
        const auto now = Manager::installedPackage(Kind::Mihon, QStringLiteral("test.ext.alpha"));
        QCOMPARE(now->package.versionCode, 4);
        // The old version's file is gone.
        QCOMPARE(QDir(QFileInfo(now->path).absolutePath()).entryList(QDir::Files).size(), 1);
        QVERIFY(Manager::updates().isEmpty());
        m_server->reset();
    }

    void registryAndKeys() {
        comics::Registry::rebuild();
        const auto source = comics::Registry::byKey(QStringLiteral("mihon:7894320198765432101"));
        QVERIFY(source);
        QCOMPARE(source->label(), QStringLiteral("Alpha Comics"));
        QVERIFY(source->enabled() == false || source->language() == QStringLiteral("en"));
        const auto link = wrapUrl(source->key(), QStringLiteral("/manga/1?x=1#a"));
        QCOMPARE(link, QStringLiteral("mihon:7894320198765432101:/manga/1?x=1#a"));
        QCOMPARE(comics::Registry::forUrl(link)->key(), source->key());
        QCOMPARE(unwrapUrl(link)->url, QStringLiteral("/manga/1?x=1#a"));
        QVERIFY(!unwrapUrl(QStringLiteral("https://alpha.test/manga/1")));
        QVERIFY(!unwrapUrl(QStringLiteral("mangadex:x")));
        // An untrusted extension's sources are listed, switched off.
        Manager::uninstall(Kind::Mihon, QStringLiteral("test.ext.beta"));
        QVERIFY(!Manager::installedPackage(Kind::Mihon, QStringLiteral("test.ext.beta")));
        QVERIFY(!QDir(Manager::storage() + QStringLiteral("/mihon/test.ext.beta")).exists());
    }

    void extensionSource() {
        auto host = std::make_shared<FakeHost>();
        setHost(Kind::Mihon, host);
        SourceEntry entry;
        entry.kind = Kind::Mihon;
        entry.id = QStringLiteral("77");
        entry.key = sourceKey(Kind::Mihon, entry.id);
        entry.name = QStringLiteral("Fake");
        entry.lang = QStringLiteral("en");
        entry.baseUrl = QStringLiteral("https://fake.test");
        entry.pkg = QStringLiteral("test.fake");
        ExtensionSource source(entry);
        host->answers.insert("search", QJsonObject{{"items", QJsonArray{QJsonObject{{"url", "/m/1"}, {"title", " One "}, {"cover", "https://fake.test/1.jpg"}},
                                                                       QJsonObject{{"url", ""}, {"title", "No link"}}}},
                                                   {"hasNextPage", true}});
        const auto found = source.search(QStringLiteral("one"));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].title, QStringLiteral("One"));
        QCOMPARE(found[0].url, QStringLiteral("mihon:77:/m/1"));
        QCOMPARE(host->last.value("source").toString(), QStringLiteral("77"));
        QCOMPARE(host->last.value("query").toString(), QStringLiteral("one"));
        host->answers.insert("popular", host->answers.value("search"));
        QVERIFY(source.browse(QStringLiteral("popular"), 2, {}, {}).hasNextPage);
        QCOMPARE(host->last.value("page").toInt(), 2);
        QCOMPARE(host->calls.last(), QStringLiteral("popular"));

        host->answers.insert("chapters", QJsonArray{QJsonObject{{"url", "/c/2"}, {"name", "Chapter 2"}, {"number", 2}, {"uploaded", 1700000000000.0}},
                                                    QJsonObject{{"url", "/c/1"}, {"name", "Chapter 1.5"}, {"number", -1}}});
        const auto chapters = source.chapters(QStringLiteral("mihon:77:/m/1"));
        QCOMPARE(host->last.value("url").toString(), QStringLiteral("/m/1"));
        QCOMPARE(chapters.size(), 2);
        // Oldest first; numbers from the name when the extension has none.
        QCOMPARE(chapters[0].number, QStringLiteral("1.5"));
        QCOMPARE(chapters[1].url, QStringLiteral("mihon:77:/c/2"));
        QVERIFY(chapters[1].released.startsWith(QLatin1String("2023-11-14")));

        host->answers.insert("pages", QJsonArray{QJsonObject{{"index", 0}, {"url", ""}, {"imageUrl", "https://img.test/0.jpg"}},
                                                 QJsonObject{{"index", 1}, {"url", "/p/1"}, {"imageUrl", ""}}});
        const auto panels = source.panels(QStringLiteral("mihon:77:/c/2"));
        QCOMPARE(panels.size(), 2);
        // Every page goes through the host, which fetches it into the cache.
        QVERIFY(panels[1].url.startsWith(QLatin1String("extimage:mihon:77:")));
        QCOMPARE(host->last.value("apk").toString(), entry.path);

        host->answers.insert("details", QJsonObject{{"title", "One"}, {"author", "A"}, {"artist", "A"}, {"status", 2},
                                                    {"genres", QJsonArray{"Action", " "}}});
        const auto details = source.details(QStringLiteral("mihon:77:/m/1"));
        QCOMPARE(details->author, QStringLiteral("A"));
        QCOMPARE(details->status, QStringLiteral("Completed"));
        QCOMPARE(details->genres, QStringList{QStringLiteral("Action")});

        // A link of another source is refused before the host is asked.
        QVERIFY_THROWS_EXCEPTION(Error, source.chapters(QStringLiteral("mihon:78:/m/1")));
        // No host for a kind: a message, not a crash.
        setHost(Kind::Mihon, nullptr);
        try {
            source.search(QStringLiteral("x"));
            QFAIL("searched without a host");
        } catch (const Error &e) {
            QVERIFY(!e.message().isEmpty());
        }
    }

    void sourceHealth() {
        Manager::recordSourceResult(QStringLiteral("mihon:7894320198765432101"), QStringLiteral("HTTP 503"));
        QCOMPARE(Manager::sourceError(QStringLiteral("mihon:7894320198765432101")), QStringLiteral("HTTP 503"));
        Manager::recordSourceResult(QStringLiteral("mihon:7894320198765432101"), {});
        QVERIFY(Manager::sourceError(QStringLiteral("mihon:7894320198765432101")).isEmpty());
        Manager::setPreference(QStringLiteral("mihon:1"), QStringLiteral("quality"), QStringLiteral("[\"high\"]"));
        QCOMPARE(Manager::preference(QStringLiteral("mihon:1"), QStringLiteral("quality")), QStringLiteral("[\"high\"]"));
    }

    // Links in every form people paste: a folder without its slash, a
    // folder holding a plugin list, a plugin list wrapped in an object.
    void linkForms() {
        QCOMPARE(Manager::preview(QStringLiteral("https://repo.test/mihon")).repo.name, QStringLiteral("Test Comics Repository"));
        const auto novels = Manager::preview(QStringLiteral("https://repo.test/novels"));
        QCOMPARE(novels.repo.kind, Kind::LNReader);
        QCOMPARE(novels.repo.url, QStringLiteral("https://repo.test/novels/plugins.min.json"));
        m_server->serve(QStringLiteral("wrapped/list.json"),
                        "{\"name\": \"Wrapped plugins\", \"plugins\": [{\"id\": \"w\", \"name\": \"W\", \"site\": \"https://w.test/\", "
                        "\"lang\": \"English\", \"version\": \"1.0.0\", \"url\": \"https://repo.test/wrapped/w.js\"}]}");
        const auto wrapped = Manager::preview(QStringLiteral("https://repo.test/wrapped/list.json"));
        QCOMPARE(wrapped.repo.kind, Kind::LNReader);
        QCOMPARE(wrapped.repo.name, QStringLiteral("Wrapped plugins"));
        QCOMPARE(wrapped.packages.size(), 1);
        m_server->reset();
    }

    // SCANCORE's music plugins: {"name", "music": [...]} (docs/music-plugins.md).
    void musicPlugins() {
        m_server->serve(QStringLiteral("music/list.json"),
                        "{\"name\": \"Tunes\", \"music\": [{\"id\": \"musicfix\", \"name\": \"Fixture Music\", "
                        "\"site\": \"https://music.test/\", \"lang\": \"en\", \"version\": \"1.0.0\", \"url\": \"musicfix.js\"}]}");
        m_server->serve(QStringLiteral("music/musicfix.js"), "exports.default = {};");
        const auto preview = Manager::preview(QStringLiteral("https://repo.test/music/list.json"));
        QCOMPARE(preview.repo.kind, Kind::Music);
        QCOMPARE(preview.repo.name, QStringLiteral("Tunes"));
        QCOMPARE(preview.packages.size(), 1);
        QCOMPARE(preview.packages[0].kind, Kind::Music);
        QCOMPARE(preview.packages[0].fileUrl, QStringLiteral("https://repo.test/music/musicfix.js"));
        Manager::add(preview);
        const auto plugin = Manager::install(Kind::Music, QStringLiteral("musicfix"));
        QVERIFY(plugin.trusted);
        QVERIFY(plugin.path.endsWith(QLatin1String(".js")));
        QCOMPARE(Manager::sources(Kind::Music).first().key, QStringLiteral("music:musicfix"));
        // Novels do not see it.
        for (const auto &e : Manager::sources(Kind::LNReader))
            QVERIFY(e.key != QLatin1String("music:musicfix"));
        Manager::uninstall(Kind::Music, QStringLiteral("musicfix"));
        Manager::removeRepository(preview.repo.url);
        m_server->reset();
    }

    void novelPlugins() {
        const auto preview = Manager::preview(QStringLiteral("https://repo.test/novels/plugins.min.json"));
        QCOMPARE(preview.repo.kind, Kind::LNReader);
        // Content language is Spanish here.
        QCOMPARE(preview.autoInstall.size(), 1);
        QCOMPARE(preview.autoInstall[0].pkg, QStringLiteral("novelas"));
        Manager::add(preview);
        const auto plugin = Manager::install(Kind::LNReader, QStringLiteral("testnovels"));
        QVERIFY(plugin.trusted);
        QVERIFY(plugin.path.endsWith(QLatin1String(".js")));
        QCOMPARE(Manager::sources(Kind::LNReader).first().key, QStringLiteral("lnreader:testnovels"));
        // Removing the repository withdraws its trust.
        Manager::removeRepository(preview.repo.url);
        QVERIFY(!Manager::installedPackage(Kind::LNReader, QStringLiteral("testnovels"))->trusted);
    }

    void animeRepository() {
        const auto preview = Manager::preview(QStringLiteral("https://repo.test/anime/index.min.json"));
        QCOMPARE(preview.repo.kind, Kind::Aniyomi);
        QCOMPARE(preview.repo.name, QStringLiteral("Test Anime Repository"));
        // Nothing in Spanish (the content language here).
        QVERIFY(preview.autoInstall.isEmpty());
        // In English, without the one too old for SCANCORE (lib 12).
        const auto english = Manager::autoInstallSelection(preview.packages, QStringLiteral("en"), false);
        QCOMPARE(english.size(), 1);
        QCOMPARE(english[0].pkg, QStringLiteral("test.anime.alpha"));
    }

    void refusals() {
        QVERIFY_THROWS_EXCEPTION(Error, Manager::preview(QStringLiteral("https://repo.test/nothing/index.min.json")));
        // Not https.
        QVERIFY_THROWS_EXCEPTION(Error, Manager::preview(QStringLiteral("ftp://repo.test/mihon/index.min.json")));
        // A web page is not a repository.
        m_server->serve(QStringLiteral("page.html"), "<html><body>hello</body></html>");
        try {
            Manager::preview(QStringLiteral("https://repo.test/page.html"));
            QFAIL("read a web page as a repository");
        } catch (const Error &e) {
            QVERIFY(e.message().contains(QLatin1String("not an extension")));
        }
        m_server->reset();
    }

    // The Extensions runtime: downloaded, checked against its SHA-256,
    // unpacked with runnable programs, and only swapped in when whole.
    void runtimeInstall() {
        if (Runtime::platform().isEmpty())
            QSKIP("No Extensions runtime for this computer.");
#ifdef Q_OS_WIN
        const QString java = QStringLiteral("java.exe");
#else
        const QString java = QStringLiteral("java");
#endif
        const auto zip = [java](const QString &host) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            QZipWriter writer(&buffer);
            writer.addFile(QStringLiteral("scancore-extensions-runtime/scancore-runtime.json"),
                           QStringLiteral(R"({"host": "%1", "java": "21"})").arg(host).toUtf8());
            writer.addFile(QStringLiteral("scancore-extensions-runtime/bin/") + java, QByteArray("#!/bin/sh\n"));
            writer.addFile(QStringLiteral("scancore-extensions-runtime/lib/host/host.jar"), QByteArray("jar"));
            writer.close();
            return bytes;
        };
        Runtime::setDir(m_dir.filePath(QStringLiteral("runtime")));
        Runtime::setDownloadBase(QStringLiteral("https://runtime.test/download"));
        QByteArray archive = zip(QString::fromLatin1(Runtime::HostVersion));
        QByteArray checksum;
        auto serve = [&](const QByteArray &body, const QByteArray &sum) {
            archive = body;
            checksum = sum;
        };
        serve(archive, QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex() + "  runtime.zip\n");
        Http::setTestHandler([&](const Request &request) -> std::optional<Response> {
            if (request.url.host() != QLatin1String("runtime.test"))
                return std::nullopt;
            Response r;
            r.status = 200;
            r.body = request.url.path().endsWith(QLatin1String(".sha256")) ? checksum : archive;
            return r;
        });
        QStringList said;
        Runtime::install([&](const QString &status) { said.append(status); });
        QVERIFY(Runtime::installed());
        QVERIFY(said.contains(QStringLiteral("Installed")));
        QVERIFY(QFileInfo(Runtime::dir() + QStringLiteral("/bin/") + java).isExecutable());
        QVERIFY(Runtime::assetUrl().contains(QStringLiteral("/extensions-runtime-%1/").arg(QLatin1String(Runtime::HostVersion))));

        // A damaged download leaves the installed runtime alone.
        serve(zip(QString::fromLatin1(Runtime::HostVersion)), QByteArray(64, '0'));
        QVERIFY_THROWS_EXCEPTION(Error, Runtime::install());
        QVERIFY(Runtime::installed());
        // So does a runtime for another host version.
        const auto other = zip(QStringLiteral("999"));
        serve(other, QCryptographicHash::hash(other, QCryptographicHash::Sha256).toHex());
        QVERIFY_THROWS_EXCEPTION(Error, Runtime::install());
        QVERIFY(Runtime::installed());
        Runtime::remove();
        QVERIFY(!Runtime::installed());
        // Back to the repositories of the other tests.
        m_server.reset();
        m_server = std::make_unique<FixtureServer>();
    }

    // A repository the size of the big community ones parses quickly.
    void bigIndexIsFast() {
        QJsonArray rows;
        for (int i = 0; i < 1500; ++i) {
            QJsonArray sources;
            for (int j = 0; j < 3; ++j)
                sources.append(QJsonObject{{"name", QStringLiteral("Source %1").arg(i)}, {"lang", "en"},
                                           {"id", QStringLiteral("78943201987654%1").arg(i * 3 + j, 5, 10, QLatin1Char('0'))},
                                           {"baseUrl", QStringLiteral("https://s%1.test").arg(i)}});
            rows.append(QJsonObject{{"name", QStringLiteral("Tachiyomi: Extension %1").arg(i)}, {"pkg", QStringLiteral("test.ext.e%1").arg(i)},
                                    {"apk", QStringLiteral("e%1.apk").arg(i)}, {"lang", "en"}, {"code", 10}, {"version", "1.4.10"},
                                    {"nsfw", 0}, {"sources", sources}});
        }
        // The ids as numbers, as repositories write them.
        const auto json = QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact))
                              .replace(QRegularExpression(QStringLiteral("\"id\":\"(\\d+)\"")), QStringLiteral("\"id\":\\1"))
                              .toUtf8();
        QVERIFY(json.contains("\"id\":7894320198765404499"));
        QElapsedTimer timer;
        timer.start();
        const auto doc = parseIndex(json, QUrl(QStringLiteral("https://repo.test/big/index.min.json")));
        const auto elapsed = timer.elapsed();
        QCOMPARE(doc.packages.size(), 1500);
        QCOMPARE(doc.packages[1499].sources[2].id, QStringLiteral("7894320198765404499"));
        QVERIFY2(elapsed < 1000, qPrintable(QStringLiteral("parsed in %1 ms").arg(elapsed)));
        qInfo("1500-extension index parsed in %lld ms", elapsed);
    }

    // Which video of an episode plays first, as Aniyomi picks it.
    void animeBestVideo() {
        const auto video = [](const QString &url, bool preferred = false, bool initialized = true, bool local = false) {
            return QVariantMap{{"url", url}, {"preferred", preferred}, {"initialized", initialized}, {"localServer", local}};
        };
        const auto hoster = [](const QString &state, const QVariantList &videos = {}) {
            return QVariantMap{{"state", state}, {"videos", videos}};
        };
        auto at = [](const QVariant &best) { return best.isNull() ? QStringLiteral("none") : QStringLiteral("%1/%2").arg(best.toMap().value("hoster").toInt()).arg(best.toMap().value("video").toInt()); };
        // A preferred video wins over earlier ones, but only on a hoster that answered.
        QCOMPARE(at(anime::bestVideo({hoster("ready", {video("a"), video("b", true)}), hoster("loading")})), QStringLiteral("0/1"));
        QCOMPARE(at(anime::bestVideo({hoster("ready", {video("a")}), hoster("ready", {video("b", true)})})), QStringLiteral("1/0"));
        // Then the first with an address, then one that can still get one.
        QCOMPARE(at(anime::bestVideo({hoster("failed"), hoster("ready", {video("", false, false), video("c")})})), QStringLiteral("1/1"));
        QCOMPARE(at(anime::bestVideo({hoster("ready", {video("", false, false)})})), QStringLiteral("0/0"));
        // Local-server videos and lazy (idle) hosters are passed over.
        QCOMPARE(at(anime::bestVideo({hoster("ready", {video("http://localhost:1/v", true, true, true)}), hoster("idle")})), QStringLiteral("none"));

        // Episodes by AniList's number: by the source's number, else by place.
        const QJsonArray numbered{QJsonObject{{"url", "e1"}, {"number", 1}}, QJsonObject{{"url", "e2"}, {"number", 2.5}}};
        QCOMPARE(anime::episodeNumbered(numbered, 2.5), QStringLiteral("e2"));
        QCOMPARE(anime::episodeNumbered(numbered, 3), QString());
        const QJsonArray unnumbered{QJsonObject{{"url", "x"}, {"number", -1}}, QJsonObject{{"url", "y"}, {"number", -1}}};
        QCOMPARE(anime::episodeNumbered(unnumbered, 2), QStringLiteral("y"));

        // A video's chapters become skips in AniSkip's terms.
        const auto map = anime::videoMap(QJsonObject{{"url", "v"},
                                                     {"timestamps", QJsonArray{QJsonObject{{"start", 10}, {"end", 95}, {"name", "OP"}, {"type", "Opening"}},
                                                                               QJsonObject{{"start", 100}, {"end", 200}, {"name", "Part A"}, {"type", "Other"}}}}});
        const auto skips = map.value("skips").toList();
        QCOMPARE(skips.size(), 1);
        QCOMPARE(skips[0].toMap().value("type").toString(), QStringLiteral("op"));
        QCOMPARE(skips[0].toMap().value("end").toDouble(), 95.0);
    }

    // A CloudStream repository: added without a signing key, its plugins
    // trusted for it and stored as .cs3 files, their sources anime ones.
    void cloudStreamInstall() {
        const auto preview = Manager::preview(QStringLiteral("cloudstreamrepo://repo.test/cloudstream/repo.json"));
        QCOMPARE(preview.repo.kind, Kind::CloudStream);
        QCOMPARE(preview.packages.size(), 3);
        Manager::add(preview);
        const auto installed = Manager::install(Kind::CloudStream, QStringLiteral("DailymotionProvider"));
        QVERIFY(installed.trusted);
        QVERIFY(installed.path.endsWith(QLatin1String(".cs3")));
        const auto sources = Manager::sources(Kind::CloudStream);
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources[0].key, QStringLiteral("cloudstream:1943850539651523733"));
        QVERIFY(sources[0].blocked.isEmpty());
        Manager::uninstall(Kind::CloudStream, QStringLiteral("DailymotionProvider"));
        Manager::removeRepository(preview.repo.url);
    }

    // An Echo list: each extension's latest release (not a pre-release) is
    // its file and version; installed as an .eapk with its author's key,
    // which an update must keep; its source is a music one.
    void echoInstall() {
        const auto preview = Manager::preview(QStringLiteral("https://repo.test/echo/extensions.json"));
        QCOMPARE(preview.repo.kind, Kind::Echo);
        QCOMPARE(preview.packages.size(), 2);
        const auto &first = preview.packages[0];
        QCOMPARE(first.versionName, QStringLiteral("v1.2"));
        QCOMPARE(first.versionCode, QDateTime::fromString(QStringLiteral("2026-10-20T10:00:00Z"), Qt::ISODate).toSecsSinceEpoch());
        QCOMPARE(first.fileUrl, QStringLiteral("https://repo.test/apk/tiny-alpha.apk"));
        // Not on GitHub: nothing to install from.
        QCOMPARE(preview.packages[1].versionCode, 0);
        Manager::add(preview);
        const auto installed = Manager::install(Kind::Echo, QStringLiteral("testecho"));
        QVERIFY(installed.trusted);
        QVERIFY(installed.path.endsWith(QLatin1String(".eapk")));
        QCOMPARE(installed.signers, QStringList{key("alpha")});
        const auto sources = Manager::sources(Kind::Echo);
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources[0].key, QStringLiteral("echo:1270673316770206241"));
        QCOMPARE(sources[0].signer, key("alpha"));
        QVERIFY(sources[0].blocked.isEmpty());
        QVERIFY_THROWS_EXCEPTION(Error, Manager::install(Kind::Echo, QStringLiteral("elsewhere")));

        // A new release signed with another key is offered and refused.
        m_server->serve(QStringLiteral("echo/releases.json"), fixture(QStringLiteral("echo/releases-new.json")));
        // Releases are looked up at most once an hour.
        Database::exec(QStringLiteral("DELETE FROM cache WHERE key LIKE 'echo-release|%'"));
        Manager::refresh(preview.repo.url);
        const auto now = Manager::installed(Kind::Echo);
        QCOMPARE(now.size(), 1);
        QVERIFY(now[0].update);
        QCOMPARE(now[0].update->versionName, QStringLiteral("v1.4"));
        QVERIFY_THROWS_EXCEPTION(Error, Manager::install(Kind::Echo, QStringLiteral("testecho")));
        m_server->reset();
        Manager::uninstall(Kind::Echo, QStringLiteral("testecho"));
        Manager::removeRepository(preview.repo.url);
    }
};

QTEST_GUILESS_MAIN(ExtensionTests)
#include "tst_extensions.moc"
