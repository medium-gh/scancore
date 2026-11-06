#include "ProcessHost.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSemaphore>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace sc::ext {

namespace {

constexpr auto kMainClass = "com.mediumdev.scancore.host.desktop.MainKt";

} // namespace

struct ProcessHost::State {
    ProcessHost *owner = nullptr;
    mutable std::mutex launchMutex;
    Launch launch;
    std::atomic<int> idleMs{10 * 60 * 1000};

    // Everything below lives on the host's own thread.
    std::once_flag started;
    QThread *thread = nullptr;
    QObject *context = nullptr;
    QProcess *process = nullptr;
    QTimer *idle = nullptr;
    QByteArray buffer;
    QByteArray errors;
    // Everything the host wrote to stderr since it started (extensions'
    // stack traces), for Extensions → Diagnostics: <root>/../host.log, at
    // most about 1 MB.
    std::unique_ptr<QFile> log;

    void ensureThread() {
        std::call_once(started, [this] {
            thread = new QThread;
            thread->setObjectName(QStringLiteral("extension-host"));
            context = new QObject;
            context->moveToThread(thread);
            QObject::connect(thread, &QThread::finished, context, &QObject::deleteLater);
            thread->start();
        });
    }

    void failAll(const QString &message) { owner->failAll(message); }

    // On the host thread.
    void start() {
        Launch launch;
        {
            std::lock_guard lock(launchMutex);
            launch = this->launch;
        }
        process = new QProcess(context);
        buffer.clear();
        errors.clear();
        log = std::make_unique<QFile>(QDir(launch.root).filePath(QStringLiteral("../host.log")));
        if (!log->open(QIODevice::WriteOnly | QIODevice::Truncate))
            log.reset();
        QStringList args = launch.jvmArgs;
        args << QStringLiteral("-cp") << QDir(launch.libDir).filePath(QStringLiteral("*")) << QString::fromLatin1(kMainClass)
             << QStringLiteral("--root") << launch.root;
        if (!launch.userAgent.isEmpty())
            args << QStringLiteral("--user-agent") << launch.userAgent;
        if (!launch.appVersion.isEmpty())
            args << QStringLiteral("--app-version") << launch.appVersion;
        process->setProgram(launch.java);
        process->setArguments(args);
        QObject::connect(process, &QProcess::readyReadStandardOutput, context, [this, p = process] {
            buffer += p->readAllStandardOutput();
            qsizetype newline;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                const auto line = buffer.left(newline);
                buffer.remove(0, newline + 1);
                owner->receive(line);
            }
        });
        QObject::connect(process, &QProcess::readyReadStandardError, context, [this, p = process] {
            const auto chunk = p->readAllStandardError();
            errors += chunk;
            if (log && log->size() < 1024 * 1024) {
                log->write(chunk);
                log->flush();
            }
            if (errors.size() > 8192)
                errors = errors.right(4096);
        });
        QObject::connect(process, &QProcess::finished, context, [this, p = process](int code, QProcess::ExitStatus) {
            if (process == p)
                process = nullptr;
            p->deleteLater();
            // What went wrong, not where: the deepest "Caused by", else the
            // last line of a Java stack trace that is not a frame.
            QString tail;
            const auto lines = QString::fromUtf8(errors).trimmed().split(QLatin1Char('\n'));
            for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
                const auto line = it->trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1String("at ")) || line.startsWith(QLatin1String("...")))
                    continue;
                if (tail.isEmpty())
                    tail = line;
                if (line.startsWith(QLatin1String("Caused by:"))) {
                    tail = line;
                    break;
                }
            }
            failAll(code == 0 ? QStringLiteral("The extension host stopped.")
                              : QStringLiteral("The extension host stopped (%1).").arg(tail.isEmpty() ? QString::number(code) : tail.left(200)));
        });
        process->start();
        if (!process->waitForStarted(15000)) {
            const auto reason = process->errorString();
            process->deleteLater();
            process = nullptr;
            failAll(QStringLiteral("The extension host could not start (%1).").arg(reason));
        }
        if (!idle) {
            idle = new QTimer(context);
            idle->setSingleShot(true);
            QObject::connect(idle, &QTimer::timeout, context, [this] {
                if (owner->busy() || hostHeld())
                    idle->start(idleMs);
                else
                    shutdown();
            });
        }
    }

    void send(const QByteArray &line) {
        const bool running = process && process->state() != QProcess::NotRunning;
        // No host to stop a call on.
        if (!running && line.contains("\"method\":\"cancel\""))
            return;
        if (!running)
            start();
        if (!process)
            return;
        process->write(line);
        idle->start(idleMs);
    }

    void shutdown() {
        if (!process)
            return;
        auto *p = process;
        p->write("{\"id\":0,\"method\":\"shutdown\"}\n");
        p->closeWriteChannel();
        if (!p->waitForFinished(5000))
            p->kill();
    }
};

ProcessHost::ProcessHost(Launch launch) : m_state(std::make_shared<State>()) {
    m_state->owner = this;
    m_state->launch = std::move(launch);
}

ProcessHost::~ProcessHost() {
    stop();
    if (m_state->thread) {
        m_state->thread->quit();
        m_state->thread->wait(3000);
        delete m_state->thread;
        m_state->thread = nullptr;
    }
}

bool ProcessHost::available() const {
    std::lock_guard lock(m_state->launchMutex);
    return !m_state->launch.java.isEmpty() && QFileInfo(m_state->launch.libDir).isDir();
}

void ProcessHost::setLaunch(Launch launch) {
    stop();
    std::lock_guard lock(m_state->launchMutex);
    m_state->launch = std::move(launch);
}

QString ProcessHost::unavailableReason() const {
    return QStringLiteral("Extensions need the Extensions runtime. Install it in Settings → Extensions.");
}

void ProcessHost::setIdleTimeout(int ms) { m_state->idleMs = ms; }

void ProcessHost::sendLine(const QByteArray &line) {
    auto state = m_state;
    state->ensureThread();
    QMetaObject::invokeMethod(state->context, [state, line] { state->send(line); }, Qt::QueuedConnection);
}

void ProcessHost::stop() {
    auto state = m_state;
    if (!state->thread)
        return;
    if (QThread::currentThread() == state->thread) {
        state->shutdown();
    } else {
        // A bounded wait: the host's thread may be starting Java (up to
        // 15 s) when the interface asks, and quitting must not hang on it.
        auto done = std::make_shared<QSemaphore>();
        QMetaObject::invokeMethod(state->context, [state, done] {
            state->shutdown();
            done->release();
        }, Qt::QueuedConnection);
        if (!done->tryAcquire(1, 7000))
            qWarning("The extension host did not stop in time");
    }
    state->failAll(QStringLiteral("The extension host was stopped."));
}

ProcessHost::Launch ProcessHost::find(const QString &runtimeDir, const QString &developmentLibDir) {
    Launch launch;
#ifdef Q_OS_WIN
    const QString javaName = QStringLiteral("java.exe");
#else
    const QString javaName = QStringLiteral("java");
#endif
    // The Extensions runtime SCANCORE downloads: a trimmed Java and the
    // host's jars, with a class-data archive it builds on first start.
    if (!runtimeDir.isEmpty() && QFileInfo::exists(runtimeDir + QStringLiteral("/bin/") + javaName) &&
        QFileInfo(runtimeDir + QStringLiteral("/lib/host")).isDir()) {
        launch.java = runtimeDir + QStringLiteral("/bin/") + javaName;
        launch.libDir = runtimeDir + QStringLiteral("/lib/host");
        // Java 19+ keeps a class-data archive of the host after its first
        // start, which roughly halves later starts.
        QFile info(runtimeDir + QStringLiteral("/scancore-runtime.json"));
        if (info.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(info.readAll()).object().value("java").toString().toInt() >= 19)
            launch.jvmArgs = {QStringLiteral("-XX:+AutoCreateSharedArchive"),
                              QStringLiteral("-XX:SharedArchiveFile=%1/host.jsa").arg(runtimeDir)};
    } else if (!developmentLibDir.isEmpty() && QFileInfo(developmentLibDir).isDir()) {
        // A development build: any Java 17 or newer.
        auto java = qEnvironmentVariable("JAVA_HOME");
        java = java.isEmpty() ? QStandardPaths::findExecutable(javaName) : java + QStringLiteral("/bin/") + javaName;
        if (QFileInfo::exists(java)) {
            launch.java = java;
            launch.libDir = developmentLibDir;
        }
    }
    // Quick to start, small, enough for any extension's work. Thread
    // stacks are large because Java's regular expressions recurse per
    // character where Android's do not, and some extensions run theirs over
    // whole pages (AniZone); only what is used is taken.
    launch.jvmArgs << QStringLiteral("-Xss16m") << QStringLiteral("-Xmx768m") << QStringLiteral("-XX:+UseSerialGC")
                   << QStringLiteral("-XX:TieredStopAtLevel=1") << QStringLiteral("-Dfile.encoding=UTF-8");
    return launch;
}

} // namespace sc::ext
