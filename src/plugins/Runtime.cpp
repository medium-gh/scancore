#include "Runtime.h"

#include "Engine.h"
#include "core/Cookies.h"
#include "core/Database.h"
#include "core/Parallel.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QMutex>
#include <QStringDecoder>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QWaitCondition>

namespace sc::plugins {

namespace {

constexpr qint64 kIdleUnloadMs = 15 * 60 * 1000;
// JavaScript itself (not waiting for a site) may run this long per turn.
constexpr qint64 kBusyLimitMs = 10000;

QString resourceText(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// QJSEngine does not know the regular expression flag "s" (dot matches
// line breaks), which a few plugins use. Their regex literals with it are
// rewritten: every "." outside a character class becomes [\s\S]. Strings,
// comments and template literals are skipped; a "/" starts a regex where
// an expression may begin.
QString withoutDotAll(const QString &code) {
    QString out;
    out.reserve(code.size() + 64);
    const auto n = code.size();
    QChar lastSignificant;
    QString lastWord;
    const auto regexMayStart = [&] {
        if (lastSignificant.isNull())
            return true;
        static const QString punct = QStringLiteral("(,=:[!&|?{};+-*%<>~^");
        if (punct.contains(lastSignificant))
            return true;
        static const QStringList words{"return", "typeof", "case", "do", "else", "in", "of", "new", "delete", "void", "throw", "yield", "await"};
        return lastSignificant.isLetter() && words.contains(lastWord);
    };
    qsizetype i = 0;
    while (i < n) {
        const QChar c = code.at(i);
        if (c == u'"' || c == u'\'' || c == u'`') {
            const qsizetype start = i++;
            int braces = 0;
            while (i < n) {
                const QChar d = code.at(i);
                if (d == u'\\') {
                    i += 2;
                    continue;
                }
                if (c == u'`' && d == u'$' && i + 1 < n && code.at(i + 1) == u'{') {
                    ++braces;
                    i += 2;
                    continue;
                }
                if (c == u'`' && braces > 0 && d == u'}') {
                    --braces;
                    ++i;
                    continue;
                }
                ++i;
                if (d == c && braces == 0)
                    break;
            }
            out += code.mid(start, i - start);
            lastSignificant = c;
            continue;
        }
        if (c == u'/' && i + 1 < n && code.at(i + 1) == u'/') {
            const auto end = code.indexOf(u'\n', i);
            const auto stop = end < 0 ? n : end;
            out += code.mid(i, stop - i);
            i = stop;
            continue;
        }
        if (c == u'/' && i + 1 < n && code.at(i + 1) == u'*') {
            const auto end = code.indexOf(QStringLiteral("*/"), i + 2);
            const auto stop = end < 0 ? n : end + 2;
            out += code.mid(i, stop - i);
            i = stop;
            continue;
        }
        if (c == u'/' && regexMayStart()) {
            qsizetype j = i + 1;
            bool inClass = false;
            QString body;
            bool closed = false;
            while (j < n) {
                const QChar d = code.at(j);
                if (d == u'\n')
                    break;
                if (d == u'\\' && j + 1 < n) {
                    body += d;
                    body += code.at(j + 1);
                    j += 2;
                    continue;
                }
                if (d == u'[')
                    inClass = true;
                else if (d == u']')
                    inClass = false;
                else if (d == u'/' && !inClass) {
                    closed = true;
                    break;
                }
                body += d;
                ++j;
            }
            if (closed) {
                qsizetype k = j + 1;
                while (k < n && code.at(k).isLetter())
                    ++k;
                QString flags = code.mid(j + 1, k - j - 1);
                if (flags.contains(u's')) {
                    QString rewritten;
                    bool cls = false;
                    for (qsizetype m = 0; m < body.size(); ++m) {
                        const QChar d = body.at(m);
                        if (d == u'\\' && m + 1 < body.size()) {
                            rewritten += d;
                            rewritten += body.at(++m);
                            continue;
                        }
                        if (d == u'[')
                            cls = true;
                        else if (d == u']')
                            cls = false;
                        rewritten += (d == u'.' && !cls) ? QStringLiteral("[\\s\\S]") : QString(d);
                    }
                    body = rewritten;
                    flags.remove(u's');
                }
                out += u'/' + body + u'/' + flags;
                i = k;
                lastSignificant = u')';
                continue;
            }
        }
        if (!c.isSpace()) {
            if (c.isLetterOrNumber() || c == u'_' || c == u'$') {
                if (!(lastSignificant.isLetterOrNumber() || lastSignificant == u'_' || lastSignificant == u'$'))
                    lastWord.clear();
                lastWord += c;
            } else {
                lastWord.clear();
            }
            lastSignificant = c;
        } else if (!lastWord.isEmpty()) {
            // A word ends at a space; the next letter starts a new one.
            lastSignificant = lastWord.back();
        }
        out += c;
        ++i;
    }
    return out;
}

// Plain JSON from a JavaScript value (dates as ISO text).
QJsonValue toJson(QJSEngine &js, const QJSValue &value) {
    if (value.isUndefined() || value.isNull())
        return QJsonValue();
    return QJsonValue::fromVariant(js.fromScriptValue<QVariant>(value));
}

bool loopbackAllowed() {
    static const bool allowed = qEnvironmentVariableIsSet("SCANCORE_PLUGIN_TEST_LOOPBACK");
    return allowed;
}

// Plugins may reach sites on the internet only (not this device or its
// network), over http(s).
QString refusal(const QUrl &url) {
    if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
        return QStringLiteral("Plugins can only open web links.");
    const auto host = url.host().toLower();
    // The session there is the user's account.
    if (CookieJar::isProtected(url))
        return QStringLiteral("Plugins cannot reach SCANCORE's own site or your sync server.");
    if (host.endsWith(QLatin1String(".test")) || loopbackAllowed())
        return {};
    static const QRegularExpression privateHost(
        QStringLiteral(R"(^(localhost|.*\.local|127\.\d+\.\d+\.\d+|10\.\d+\.\d+\.\d+|192\.168\.\d+\.\d+|169\.254\.\d+\.\d+|)"
                       R"(172\.(1[6-9]|2\d|3[01])\.\d+\.\d+|0\.0\.0\.0|\[?::1\]?|\[?f[cd][0-9a-f:]*\]?)$)"));
    if (privateHost.match(host).hasMatch())
        return QStringLiteral("Plugins cannot reach this device or its network.");
    return {};
}

// refusal(), and where the name leads: an address written another way
// (2130706433, ::ffff:127.0.0.1) or a public name that resolves to this
// device or its network is refused too. Asked for every redirect as well
// (Request::guard), so a site cannot send a plugin on to such places, or to
// SCANCORE's own site with the account's session.
QString reachRefusal(const QUrl &url) {
    if (const auto refused = refusal(url); !refused.isEmpty())
        return refused;
    const auto host = url.host().toLower();
    if (host.endsWith(QLatin1String(".test")) || loopbackAllowed())
        return {};
    QList<QHostAddress> addresses;
    if (QHostAddress literal(host); !literal.isNull())
        addresses.append(literal);
    else
        addresses = QHostInfo::fromName(host).addresses();
    for (const auto &address : std::as_const(addresses))
        if (Http::isLocalAddress(address))
            return QStringLiteral("Plugins cannot reach this device or its network.");
    return {};
}

QByteArray multipart(const QJSValue &list, QByteArray &contentType) {
    const auto boundary = "----scancore" + QUuid::createUuid().toByteArray(QUuid::Id128);
    contentType = "multipart/form-data; boundary=" + boundary;
    QByteArray body;
    const int length = list.property(QStringLiteral("length")).toInt();
    for (int i = 0; i < length; ++i) {
        const auto pair = list.property(quint32(i));
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" +
                pair.property(0).toString().toUtf8().replace('"', "%22") + "\"\r\n\r\n" + pair.property(1).toString().toUtf8() + "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return body;
}

QString charsetOf(const QByteArray &contentType) {
    const auto at = contentType.toLower().indexOf("charset=");
    if (at < 0)
        return {};
    auto value = contentType.mid(at + 8);
    const auto end = value.indexOf(';');
    if (end >= 0)
        value.truncate(end);
    return QString::fromLatin1(value.trimmed().replace('"', ""));
}

QString decodeText(const QByteArray &bytes, const QString &label) {
    if (label.isEmpty() || label.compare(QLatin1String("utf-8"), Qt::CaseInsensitive) == 0 ||
        label.compare(QLatin1String("utf8"), Qt::CaseInsensitive) == 0)
        return QString::fromUtf8(bytes);
    QStringDecoder decoder(label.toLatin1().constData());
    if (!decoder.isValid())
        return QString::fromUtf8(bytes);
    return decoder.decode(bytes);
}

// ---------------------------------------------------------------- threads
// The minified libraries make QJSEngine's compiler note every function used
// before its declaration (legal JavaScript), once per engine.
QLoggingCategory::CategoryFilter g_previousFilter = nullptr;
void quietCompiler(QLoggingCategory *category) {
    if (g_previousFilter)
        g_previousFilter(category);
    if (qstrcmp(category->categoryName(), "qt.qml.usedbeforedeclared") == 0)
        category->setEnabled(QtWarningMsg, false);
}

struct Pool {
    QList<QThread *> threads;
    QList<Worker *> workers;
    Pool() {
        g_previousFilter = QLoggingCategory::installFilter(quietCompiler);
        const int count = std::clamp(QThread::idealThreadCount() / 2, 1, 3);
        for (int i = 0; i < count; ++i) {
            auto *thread = new QThread;
            thread->setObjectName(QStringLiteral("plugins-%1").arg(i));
            auto *worker = new Worker;
            worker->moveToThread(thread);
            QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
            thread->start();
            threads.append(thread);
            workers.append(worker);
        }
        if (auto *app = QCoreApplication::instance())
            QObject::connect(app, &QCoreApplication::aboutToQuit, app, [this] {
                for (auto *thread : threads) {
                    thread->quit();
                    thread->wait(2000);
                }
            });
    }
    Worker *workerFor(const QString &key) { return workers.at(int(qHash(key) % uint(workers.size()))); }
};

Pool &pool() {
    static Pool *instance = new Pool;
    return *instance;
}

// Runs `work` on the worker's thread and waits for it.
template <typename T>
T onWorker(Worker *worker, const std::function<T(Worker *)> &work) {
    if (QThread::currentThread() == worker->thread())
        return work(worker);
    std::optional<T> result;
    QString error;
    int status = 0;
    QMetaObject::invokeMethod(
        worker,
        [&] {
            try {
                result = work(worker);
            } catch (const Error &e) {
                error = e.message();
                status = e.status();
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
        },
        Qt::BlockingQueuedConnection);
    if (!result)
        throw Error(error.isEmpty() ? QStringLiteral("The plugin failed.") : error, status);
    return *result;
}

} // namespace

void Pending::finish(bool success, const QJsonValue &result, const QString &message, int code) {
    QMutexLocker lock(&mutex);
    if (finished)
        return;
    finished = true;
    ok = success;
    value = result;
    error = message;
    status = code;
    done.wakeAll();
}

// ---------------------------------------------------------------- Native
Native::Native(Engine *engine) : QObject(engine), m_engine(engine) {}

Native::~Native() {
    for (auto &flag : m_fetches)
        flag->store(true);
}

int Native::setTimeout(int ms) {
    const int id = ++m_nextTimer;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id, timer] {
        m_timers.remove(id);
        timer->deleteLater();
        m_engine->run([this, id] { m_engine->js().globalObject().property(QStringLiteral("__fireTimer")).call({id}); });
    });
    m_timers.insert(id, timer);
    timer->start(ms);
    return id;
}

void Native::clearTimeout(int id) {
    if (auto *timer = m_timers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }
}

QJSValue Native::parseUrl(const QString &input, const QString &base) {
    QUrl url(input);
    if (!base.isEmpty() && url.isRelative())
        url = QUrl(base).resolved(url);
    if (!url.isValid() || url.scheme().isEmpty() || (url.host().isEmpty() && url.scheme().startsWith(QLatin1String("http"))))
        return QJSValue(QJSValue::NullValue);
    auto &js = m_engine->js();
    auto parts = js.newObject();
    parts.setProperty(QStringLiteral("protocol"), url.scheme() + QLatin1Char(':'));
    parts.setProperty(QStringLiteral("username"), url.userName(QUrl::FullyEncoded));
    parts.setProperty(QStringLiteral("password"), url.password(QUrl::FullyEncoded));
    parts.setProperty(QStringLiteral("hostname"), url.host(QUrl::FullyEncoded));
    parts.setProperty(QStringLiteral("port"), url.port() < 0 ? QString() : QString::number(url.port()));
    const auto path = url.path(QUrl::FullyEncoded);
    parts.setProperty(QStringLiteral("pathname"), path.isEmpty() ? QStringLiteral("/") : path);
    parts.setProperty(QStringLiteral("search"), url.query(QUrl::FullyEncoded));
    parts.setProperty(QStringLiteral("hash"), url.hasFragment() ? QLatin1Char('#') + url.fragment(QUrl::FullyEncoded) : QString());
    return parts;
}

int Native::fetch(const QString &url, const QString &method, const QVariantMap &headers, const QString &kind,
                  const QJSValue &data, const QString &encoding) {
    const int id = ++m_nextFetch;
    Request request;
    request.url = QUrl(url);
    request.method = method.toLatin1();
    request.timeoutMs = 30000;
    QByteArray contentType;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (it.key() == QLatin1String("content-type")) {
            contentType = it.value().toString().toUtf8();
            continue;
        }
        request.headers.append({it.key().toUtf8(), it.value().toString().toUtf8()});
    }
    if (kind == QLatin1String("form")) {
        // The browser picks the multipart boundary; a plugin's own
        // content type for a form would lack it.
        request.body = multipart(data, contentType);
    } else if (kind == QLatin1String("urlencoded")) {
        request.body = data.toString().toUtf8();
        if (contentType.isEmpty())
            contentType = "application/x-www-form-urlencoded;charset=UTF-8";
    } else if (kind == QLatin1String("bytes")) {
        request.body = m_engine->js().fromScriptValue<QByteArray>(data);
    } else if (kind == QLatin1String("text")) {
        request.body = data.toString().toUtf8();
        if (contentType.isEmpty())
            contentType = "text/plain;charset=UTF-8";
    }
    if (!contentType.isEmpty())
        request.headers.append({"Content-Type", contentType});

    const auto refused = refusal(request.url);
    request.guard = reachRefusal;
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    m_fetches.insert(id, cancel);
    QPointer<Native> self(this);
    const auto deliver = [self, id](const Response &response, const QString &error) {
        QMetaObject::invokeMethod(
            self.data(),
            [self, id, response, error] {
                if (!self || !self->m_fetches.remove(id)) {
                    if (self)
                        self->m_encodings.remove(id);
                    return;
                }
                self->m_engine->run([&] {
                    auto &js = self->m_engine->js();
                    QJSValue result = QJSValue(QJSValue::NullValue);
                    if (error.isEmpty()) {
                        result = js.newObject();
                        result.setProperty(QStringLiteral("status"), response.status);
                        result.setProperty(QStringLiteral("url"), response.finalUrl.toString());
                        auto map = js.newObject();
                        for (const auto &[name, value] : response.headers)
                            map.setProperty(QString::fromLatin1(name).toLower(), QString::fromUtf8(value));
                        result.setProperty(QStringLiteral("headers"), map);
                        const auto label = self->m_encodings.take(id);
                        result.setProperty(QStringLiteral("body"),
                                           decodeText(response.body, label.isEmpty() ? charsetOf(response.contentType) : label));
                    }
                    js.globalObject().property(QStringLiteral("__fetchDone")).call({id, result, error});
                });
            },
            Qt::QueuedConnection);
    };
    if (!refused.isEmpty()) {
        deliver({}, refused);
        return id;
    }
    m_encodings.insert(id, encoding);
    // Waiting for the site holds a pool thread, not the plugin thread.
    fanoutPool().start([request, cancel, deliver, id = m_engine->plugin().id] {
        CancelScope scope(cancel);
        // Development: SCANCORE_PLUGIN_TRACE=1 logs every plugin request.
        static const bool trace = qEnvironmentVariableIsSet("SCANCORE_PLUGIN_TRACE");
        QElapsedTimer timer;
        timer.start();
        try {
            const auto response = Http::send(request);
            if (trace)
                qInfo().noquote() << "plugin" << id << request.method << request.url.toString() << "->" << response.status
                                  << response.body.size() << "bytes" << timer.elapsed() << "ms";
            deliver(response, {});
        } catch (const Error &e) {
            if (trace)
                qInfo().noquote() << "plugin" << id << request.method << request.url.toString() << "failed:" << e.message();
            deliver({}, e.message());
        }
    });
    return id;
}

void Native::abortFetch(int id) {
    if (auto flag = m_fetches.take(id))
        flag->store(true);
}

QString Native::storageGet(const QString &key) {
    return Database::scalar(QStringLiteral("SELECT value FROM ext_prefs WHERE source_key = ? AND key = ?"),
                            {m_engine->plugin().key, QStringLiteral("store:") + key})
        .toString();
}

void Native::storageSet(const QString &key, const QString &value) {
    if (value.isEmpty())
        Database::exec(QStringLiteral("DELETE FROM ext_prefs WHERE source_key = ? AND key = ?"),
                       {m_engine->plugin().key, QStringLiteral("store:") + key});
    else
        Database::exec(QStringLiteral("INSERT INTO ext_prefs(source_key, key, value) VALUES(?, ?, ?) "
                                      "ON CONFLICT(source_key, key) DO UPDATE SET value = excluded.value"),
                       {m_engine->plugin().key, QStringLiteral("store:") + key, value});
}

QStringList Native::storageKeys() {
    QStringList keys;
    for (const auto &row : Database::query(QStringLiteral("SELECT key FROM ext_prefs WHERE source_key = ? AND key LIKE 'store:%'"),
                                           {m_engine->plugin().key}))
        keys.append(row.toMap().value(QStringLiteral("key")).toString().mid(6));
    return keys;
}

void Native::storageClear() {
    Database::exec(QStringLiteral("DELETE FROM ext_prefs WHERE source_key = ? AND key LIKE 'store:%'"), {m_engine->plugin().key});
}

QByteArray Native::utf8Encode(const QString &text) { return text.toUtf8(); }

QString Native::decode(const QByteArray &bytes, const QString &label) { return decodeText(bytes, label); }

QString Native::atob(const QString &text) {
    return QString::fromLatin1(QByteArray::fromBase64(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors));
}

QString Native::btoa(const QString &text) { return QString::fromLatin1(text.toLatin1().toBase64()); }

QString Native::btoaBytes(const QByteArray &bytes) { return QString::fromLatin1(bytes.toBase64()); }

void Native::settle(int id, bool ok, const QJSValue &value, const QString &error) {
    m_engine->settled(id, ok, ok ? toJson(m_engine->js(), value) : QJsonValue(), error);
}

// ---------------------------------------------------------------- Engine
void Guard::interruptIfBusy(qint64 limitMs) {
    QMutexLocker lock(&mutex);
    const qint64 since = busySince;
    if (js && since >= 0 && clock.elapsed() - since > limitMs)
        js->setInterrupted(true);
}

void GuardList::add(const std::shared_ptr<Guard> &guard) {
    QMutexLocker lock(&mutex);
    guards.removeIf([](const std::weak_ptr<Guard> &g) { return g.expired(); });
    guards.append(guard);
}

void GuardList::interruptIfBusy(qint64 limitMs) {
    QList<std::shared_ptr<Guard>> live;
    {
        QMutexLocker lock(&mutex);
        for (const auto &g : guards)
            if (auto guard = g.lock())
                live.append(guard);
    }
    for (const auto &guard : live)
        guard->interruptIfBusy(limitMs);
}

Engine::Engine(const PluginRef &plugin, const std::shared_ptr<GuardList> &guards, QObject *parent) : QObject(parent), m_plugin(plugin) {
    m_lastUse.start();
    m_guard->clock.start();
    guards->add(m_guard);
}

Engine::~Engine() {
    for (auto &pending : m_pending)
        pending->finish(false, {}, QStringLiteral("The plugin was closed."));
    // Native's timers and requests go with the engine.
    delete m_native;
    m_native = nullptr;
    m_instance = QJSValue();
    {
        QMutexLocker lock(&m_guard->mutex);
        m_guard->js = nullptr;
    }
    m_js.reset();
}

QString Engine::failure(const QJSValue &error) const {
    auto message = error.property(QStringLiteral("message")).toString();
    if (message.isEmpty())
        message = error.toString();
    return message;
}

void Engine::run(const std::function<void()> &work) {
    m_lastUse.restart();
    m_guard->busySince = m_guard->clock.elapsed();
    work();
    m_guard->busySince = -1;
    if (m_js && m_js->isInterrupted())
        m_js->setInterrupted(false);
    if (m_js && m_js->hasError())
        qWarning().noquote() << "plugin" << m_plugin.id << ":" << failure(m_js->catchError());
}

void Engine::load() {
    if (m_loaded)
        return;
    QFile file(m_plugin.path);
    if (!file.open(QIODevice::ReadOnly))
        throw Error(QStringLiteral("The plugin's file is missing. Install it again."), 404);
    auto code = QString::fromUtf8(file.readAll());

    m_js = std::make_unique<QJSEngine>();
    {
        QMutexLocker lock(&m_guard->mutex);
        m_guard->js = m_js.get();
    }
    m_js->installExtensions(QJSEngine::ConsoleExtension);
    m_native = new Native(this);
    QJSEngine::setObjectOwnership(m_native, QJSEngine::CppOwnership);
    auto global = m_js->globalObject();
    global.setProperty(QStringLiteral("__native"), m_js->newQObject(m_native));
    m_guard->busySince = m_guard->clock.elapsed();
    // Names the libraries look for and newer built-ins QJSEngine lacks.
    const auto prelude = m_js->evaluate(resourceText(QStringLiteral(":/plugins/prelude.js")), QStringLiteral("prelude.js"));
    if (prelude.isError())
        qWarning().noquote() << "plugin prelude:" << failure(prelude);
    const auto vendor = resourceText(QStringLiteral(":/plugins/vendor.js"));
    if (!vendor.isEmpty()) {
        const auto result = m_js->evaluate(vendor, QStringLiteral("vendor.js"));
        if (result.isError())
            qWarning().noquote() << "plugin libraries:" << failure(result);
    }
    const auto runtime = m_js->evaluate(resourceText(QStringLiteral(":/plugins/runtime.js")), QStringLiteral("runtime.js"));
    if (runtime.isError()) {
        m_guard->busySince = -1;
        throw Error(QStringLiteral("SCANCORE's plugin runtime failed: %1").arg(failure(runtime)));
    }
    const auto wrap = [](const QString &body) {
        return QStringLiteral("(function (require, module) { var exports = module.exports = {};\n") + body +
               QStringLiteral("\n;return exports.default || module.exports.default || module.exports; })");
    };
    auto factory = m_js->evaluate(wrap(code), m_plugin.path);
    if (factory.isError() && failure(factory).contains(QLatin1String("flag 's'")))
        factory = m_js->evaluate(wrap(withoutDotAll(code)), m_plugin.path);
    if (factory.isError()) {
        m_guard->busySince = -1;
        throw Error(QStringLiteral("This plugin does not run in SCANCORE: %1").arg(failure(factory)), 501);
    }
    auto module = m_js->newObject();
    m_instance = factory.call({global.property(QStringLiteral("__require")), module});
    m_guard->busySince = -1;
    if (m_instance.isError() || m_js->hasError()) {
        const auto error = m_instance.isError() ? m_instance : m_js->catchError();
        throw Error(QStringLiteral("The plugin failed to start: %1").arg(failure(error)), 501);
    }
    // Music plugins (music:<id>) search songs and say where each plays.
    const bool music = m_plugin.key.startsWith(QLatin1String("music:"));
    const auto needs = music ? QStringList{QStringLiteral("searchSongs"), QStringLiteral("streamOf")} : QStringList{QStringLiteral("parseNovel")};
    for (const auto &method : needs)
        if (!m_instance.isObject() || !m_instance.property(method).isCallable())
            throw Error(music ? QStringLiteral("That file is not a SCANCORE music plugin.") : QStringLiteral("That file is not an LNReader plugin."), 501);
    m_loaded = true;
}

QJsonObject Engine::info() {
    load();
    m_lastUse.restart();
    auto &js = *m_js;
    const auto value = [&](const char *name) { return toJson(js, m_instance.property(QString::fromLatin1(name))); };
    return QJsonObject{{"id", value("id")},
                       {"name", value("name")},
                       {"version", value("version")},
                       {"site", value("site")},
                       {"lang", value("lang")},
                       {"icon", value("icon")},
                       {"filters", value("filters")},
                       {"pluginSettings", value("pluginSettings")},
                       {"imageRequestInit", value("imageRequestInit")},
                       {"hasParsePage", m_instance.property(QStringLiteral("parsePage")).isCallable()},
                       {"hasResolveUrl", m_instance.property(QStringLiteral("resolveUrl")).isCallable()}};
}

void Engine::start(const QString &method, const QJsonArray &args, const std::shared_ptr<Pending> &pending) {
    try {
        load();
    } catch (const Error &e) {
        pending->finish(false, {}, e.message());
        return;
    }
    const auto fn = m_instance.property(method);
    if (!fn.isCallable()) {
        pending->finish(false, {}, QStringLiteral("The plugin cannot do that (%1).").arg(method));
        return;
    }
    const int id = ++m_nextCall;
    m_pending.insert(id, pending);
    run([&] {
        QJSValueList values;
        for (const auto &arg : args)
            values.append(m_js->toScriptValue(arg.toVariant()));
        auto result = fn.callWithInstance(m_instance, values);
        if (m_js->isInterrupted()) {
            if (m_js->hasError())
                m_js->catchError();
            settled(id, false, {}, QStringLiteral("The plugin ran too long and was stopped."), 504);
            return;
        }
        if (result.isError() || m_js->hasError()) {
            const auto error = result.isError() ? result : m_js->catchError();
            settled(id, false, {}, failure(error));
            return;
        }
        m_js->globalObject().property(QStringLiteral("__settle")).call({id, result});
    });
}

void Engine::settled(int id, bool ok, const QJsonValue &value, const QString &error, int status) {
    if (auto pending = m_pending.take(id))
        pending->finish(ok, value, error, status);
}

// ---------------------------------------------------------------- Worker
Worker::Worker(QObject *parent) : QObject(parent), m_sweep(this) {
    // Engines nobody used for a while are dropped; busy JavaScript is
    // interrupted past its limit.
    m_sweep.setInterval(60000);
    connect(&m_sweep, &QTimer::timeout, this, [this] {
        for (auto it = m_engines.begin(); it != m_engines.end();) {
            if (it.value()->idleMs() > kIdleUnloadMs) {
                delete it.value();
                it = m_engines.erase(it);
            } else {
                ++it;
            }
        }
    });
    QMetaObject::invokeMethod(&m_sweep, qOverload<>(&QTimer::start), Qt::QueuedConnection);
}

Engine *Worker::engine(const PluginRef &plugin) {
    auto *engine = m_engines.value(plugin.key);
    if (engine && engine->plugin().path != plugin.path) {
        delete engine;
        engine = nullptr;
    }
    if (!engine) {
        engine = new Engine(plugin, guards, this);
        m_engines.insert(plugin.key, engine);
    }
    return engine;
}

void Worker::unload(const QString &key) { delete m_engines.take(key); }

// ---------------------------------------------------------------- Runtime
QJsonValue Runtime::call(const PluginRef &plugin, const QString &method, const QJsonArray &args, int timeoutMs) {
    auto *worker = pool().workerFor(plugin.key);
    auto pending = std::make_shared<Pending>();
    QMetaObject::invokeMethod(
        worker, [worker, plugin, method, args, pending] { worker->engine(plugin)->start(method, args, pending); }, Qt::QueuedConnection);
    QElapsedTimer clock;
    clock.start();
    QMutexLocker lock(&pending->mutex);
    // JavaScript itself (not a request) running past the limit is stopped,
    // whichever plugin on this thread it belongs to.
    const qint64 busyLimit = std::min<qint64>(timeoutMs, kBusyLimitMs);
    while (!pending->finished) {
        pending->done.wait(&pending->mutex, 100);
        if (pending->finished)
            break;
        worker->guards->interruptIfBusy(busyLimit);
        if (CancelScope::cancelled())
            throw Error(QStringLiteral("Cancelled"), 499);
        if (clock.elapsed() > timeoutMs)
            throw Error(QStringLiteral("The plugin took too long to answer."), 504);
    }
    if (!pending->ok)
        throw Error(pending->error.isEmpty() ? QStringLiteral("The plugin failed.") : pending->error, pending->status);
    return pending->value;
}

QJsonObject Runtime::info(const PluginRef &plugin) {
    return onWorker<QJsonObject>(pool().workerFor(plugin.key), [plugin](Worker *worker) { return worker->engine(plugin)->info(); });
}

void Runtime::unload(const QString &key) {
    auto *worker = pool().workerFor(key);
    QMetaObject::invokeMethod(worker, [worker, key] { worker->unload(key); }, Qt::QueuedConnection);
}

QStringList Runtime::missingLibraries() {
    QStringList missing;
    if (!QFile::exists(QStringLiteral(":/plugins/vendor.js")))
        missing << QStringLiteral("cheerio") << QStringLiteral("htmlparser2") << QStringLiteral("dayjs");
    return missing;
}

} // namespace sc::plugins
