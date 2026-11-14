#include "Piper.h"

#include "Archive.h"

#include "core/Http.h"
#include "core/Task.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutex>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>
#include <clocale>


#ifdef SCANCORE_HAVE_MPV
#include <mpv/client.h>
#endif

namespace sc {

namespace {

// Piper's last release with programs for every system (the project moved
// on to a Python package), checked against these.
constexpr auto kRelease = "2023.11.14-2";
struct Build {
    const char *asset;
    const char *sha256;
};

Build build() {
#if defined(Q_OS_WIN)
    if (QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64"))
        return {"piper_windows_amd64.zip", "f3c58906402b24f3a96d92145f58acba6d86c9b5db896d207f78dc80811efcea"};
#elif defined(Q_OS_MACOS)
    if (QSysInfo::currentCpuArchitecture() == QLatin1String("arm64"))
        return {"piper_macos_aarch64.tar.gz", "6b1eb03b3735946cb35216e063e7eebcc33a6bbf5dd96ec0217959bf1cdcb0cc"};
    if (QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64"))
        return {"piper_macos_x64.tar.gz", "ced85c0a3df13945b1e623b878a48fdc2854d5c485b4b67f62857cf551deaf8b"};
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64"))
        return {"piper_linux_x86_64.tar.gz", "a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992"};
    if (QSysInfo::currentCpuArchitecture() == QLatin1String("arm64"))
        return {"piper_linux_aarch64.tar.gz", "fea0fd2d87c54dbc7078d0f878289f404bd4d6eea6e7444a77835d1537ab88eb"};
#endif
    return {nullptr, nullptr};
}

// The voices as of the catalogue's first release; each file is checked
// against its md5 there.
constexpr auto kVoices = "https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/";

QString dataDir() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
QString programDir() { return dataDir() + QStringLiteral("/piper"); }
QString voicesDir() { return dataDir() + QStringLiteral("/piper-voices"); }
QString speechDir() { return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/piper-speech"); }

QString program() {
#ifdef Q_OS_WIN
    return programDir() + QStringLiteral("/piper.exe");
#else
    return programDir() + QStringLiteral("/piper");
#endif
}

bool programInstalled() {
    QFile marker(programDir() + QStringLiteral("/scancore-piper.txt"));
    return QFileInfo::exists(program()) && marker.open(QIODevice::ReadOnly) && marker.readAll().trimmed() == kRelease;
}

void report(const QString &status) { Task::report(QVariantMap{{"status", status}}); }

QString megabytes(qint64 bytes) { return QString::number(std::max<qint64>(1, (bytes + 500000) / 1000000)) + QStringLiteral(" MB"); }

QMutex g_catalogueMutex;
QJsonObject g_catalogue;

QJsonObject catalogueNow() {
    {
        QMutexLocker lock(&g_catalogueMutex);
        if (!g_catalogue.isEmpty())
            return g_catalogue;
    }
    const auto json = Http::getJson(QUrl(QString::fromLatin1(kVoices) + QStringLiteral("voices.json")), {}, 30000).object();
    if (json.isEmpty())
        throw Error(QStringLiteral("The list of voices could not be loaded."));
    QMutexLocker lock(&g_catalogueMutex);
    g_catalogue = json;
    return json;
}

QString niceName(const QString &name) {
    auto out = name;
    out.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!out.isEmpty())
        out[0] = out[0].toUpper();
    return out;
}

// The program into programDir(): the archive holds one folder, piper/.
void installProgram() {
    const auto which = build();
    if (!which.asset)
        throw Error(QStringLiteral("There are no neural voices for this computer."));
    report(QStringLiteral("Downloading the voice program…"));
    Request request;
    request.url = QUrl(QStringLiteral("https://github.com/rhasspy/piper/releases/download/%1/%2").arg(QLatin1String(kRelease), QLatin1String(which.asset)));
    request.timeoutMs = 600000;
    request.maxBytes = 100LL * 1024 * 1024;
    const auto archive = Http::send(request);
    if (!archive.ok())
        throw Error(QStringLiteral("The voice program could not be downloaded (HTTP %1).").arg(archive.status), archive.status);
    if (QCryptographicHash::hash(archive.body, QCryptographicHash::Sha256).toHex() != which.sha256)
        throw Error(QStringLiteral("The download was damaged. Try again."));

    report(QStringLiteral("Unpacking…"));
    const auto staging = programDir() + QStringLiteral(".new");
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);
    if (QByteArray(which.asset).endsWith(".zip"))
        archive::unzip(archive.body, staging);
    else
        archive::untarGz(archive.body, staging);
    QFile marker(staging + QStringLiteral("/scancore-piper.txt"));
    if (!marker.open(QIODevice::WriteOnly) || marker.write(kRelease) < 0)
        throw Error(QStringLiteral("Could not write to the app data folder."));
    marker.close();
    QDir(programDir()).removeRecursively();
    if (!QDir().rename(staging, programDir()))
        throw Error(QStringLiteral("The voice program could not be installed."));
}

} // namespace

Piper *Piper::instance() {
    static auto *piper = [] {
        auto *created = new Piper;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return piper;
}

Piper::Piper(QObject *parent) : QObject(parent) {
    scanVoices();
    // What an earlier run left unsaid.
    QDir(speechDir()).removeRecursively();
}

Piper::~Piper() {
#if QT_CONFIG(process)
    if (m_process)
        m_process->kill();
#endif
#ifdef SCANCORE_HAVE_MPV
    if (m_mpv)
        mpv_terminate_destroy(static_cast<mpv_handle *>(m_mpv));
#endif
}

bool Piper::available() const {
#ifdef SCANCORE_HAVE_MPV
    return build().asset != nullptr;
#else
    return false;
#endif
}

void Piper::scanVoices() {
    QVariantList voices;
    const auto files = QDir(voicesDir()).entryInfoList({QStringLiteral("*.onnx")}, QDir::Files, QDir::Name);
    for (const auto &file : files) {
        // <language>-<name>-<quality>.onnx, with its config beside it.
        const auto key = file.completeBaseName();
        if (!QFileInfo::exists(file.absoluteFilePath() + QStringLiteral(".json")))
            continue;
        const auto parts = key.split(QLatin1Char('-'));
        if (parts.size() < 3)
            continue;
        voices.append(QVariantMap{{"id", QStringLiteral("piper:") + key},
                                  {"key", key},
                                  {"name", niceName(parts[1])},
                                  {"language", parts[0]},
                                  {"quality", parts.last()}});
    }
    if (voices != m_voices) {
        m_voices = voices;
        emit voicesChanged();
    }
}

Task *Piper::catalogue() {
    return Task::run([]() -> QVariant {
        const auto json = catalogueNow();
        const auto locale = QLocale().name();
        const auto mine = locale.section(QLatin1Char('_'), 0, 0);
        QVariantList out;
        for (auto it = json.begin(); it != json.end(); ++it) {
            const auto v = it.value().toObject();
            const auto language = v.value(QStringLiteral("language")).toObject();
            qint64 size = 0;
            const auto files = v.value(QStringLiteral("files")).toObject();
            for (auto f = files.begin(); f != files.end(); ++f)
                size += f.value().toObject().value(QStringLiteral("size_bytes")).toInteger();
            const auto country = language.value(QStringLiteral("country_english")).toString();
            out.append(QVariantMap{{"key", it.key()},
                                   {"name", niceName(v.value(QStringLiteral("name")).toString())},
                                   {"language", language.value(QStringLiteral("code")).toString()},
                                   {"family", language.value(QStringLiteral("family")).toString()},
                                   {"languageName", language.value(QStringLiteral("name_english")).toString() +
                                                        (country.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(country))},
                                   {"quality", v.value(QStringLiteral("quality")).toString()},
                                   {"size", megabytes(size)},
                                   {"installed", QFileInfo::exists(voicesDir() + QLatin1Char('/') + it.key() + QStringLiteral(".onnx"))}});
        }
        // The app's language first; better voices before smaller ones.
        static const QStringList qualities{"high", "medium", "low", "x_low"};
        std::stable_sort(out.begin(), out.end(), [&](const QVariant &a, const QVariant &b) {
            const auto ma = a.toMap(), mb = b.toMap();
            const bool ia = ma.value("family") == mine, ib = mb.value("family") == mine;
            if (ia != ib)
                return ia;
            // en_US before en_GB for someone in the US.
            const bool ea = ma.value("language") == locale, eb = mb.value("language") == locale;
            if (ea != eb)
                return ea;
            if (ma.value("languageName") != mb.value("languageName"))
                return ma.value("languageName").toString() < mb.value("languageName").toString();
            const auto qa = qualities.indexOf(ma.value("quality").toString()), qb = qualities.indexOf(mb.value("quality").toString());
            if (qa != qb)
                return qa < qb;
            return ma.value("name").toString() < mb.value("name").toString();
        });
        return out;
    });
}

Task *Piper::install(const QString &key) {
    auto *task = Task::run([key]() -> QVariant {
        const auto entry = catalogueNow().value(key).toObject();
        if (entry.isEmpty())
            throw Error(QStringLiteral("This voice is not in the list."));
        if (!programInstalled())
            installProgram();
        QDir().mkpath(voicesDir());
        const auto files = entry.value(QStringLiteral("files")).toObject();
        for (auto it = files.begin(); it != files.end(); ++it) {
            const auto path = it.key();
            const auto name = path.section(QLatin1Char('/'), -1);
            // The model and its config; the model card says whose voice it
            // is and its licence.
            const bool model = name.endsWith(QLatin1String(".onnx"));
            const bool config = name.endsWith(QLatin1String(".onnx.json"));
            if (!model && !config && name != QLatin1String("MODEL_CARD"))
                continue;
            const auto info = it.value().toObject();
            if (model)
                report(QStringLiteral("Downloading the voice (%1)…").arg(megabytes(info.value(QStringLiteral("size_bytes")).toInteger())));
            Request request;
            request.url = QUrl(QString::fromLatin1(kVoices) + path);
            request.timeoutMs = 900000;
            request.maxBytes = 300LL * 1024 * 1024;
            const auto response = Http::send(request);
            if (!response.ok())
                throw Error(QStringLiteral("The voice could not be downloaded (HTTP %1).").arg(response.status), response.status);
            if (QCryptographicHash::hash(response.body, QCryptographicHash::Md5).toHex() != info.value(QStringLiteral("md5_digest")).toString())
                throw Error(QStringLiteral("The download was damaged. Try again."));
            // The model last, under its final name only once whole: a voice
            // is installed when its .onnx is there.
            const auto target = voicesDir() + QLatin1Char('/') + (name == QLatin1String("MODEL_CARD") ? key + QStringLiteral(".MODEL_CARD") : name);
            QFile out(target + QStringLiteral(".part"));
            if (!out.open(QIODevice::WriteOnly) || out.write(response.body) != response.body.size())
                throw Error(QStringLiteral("Could not write to the app data folder."));
            out.close();
            QFile::remove(target);
            if (!QFile::rename(target + QStringLiteral(".part"), target))
                throw Error(QStringLiteral("Could not write to the app data folder."));
        }
        // The list knows the voice before the task says it is done.
        QMetaObject::invokeMethod(Piper::instance(), &Piper::scanVoices, Qt::BlockingQueuedConnection);
        return true;
    });
    return task;
}

void Piper::remove(const QString &key) {
    if (key.isEmpty() || key.contains(QLatin1Char('/')) || key.contains(QLatin1Char('\\')))
        return;
#if QT_CONFIG(process)
    if (m_processVoice == key && m_process) {
        stop();
        m_process->kill();
    }
#endif
    for (const auto &suffix : {".onnx", ".onnx.json", ".MODEL_CARD"})
        QFile::remove(voicesDir() + QLatin1Char('/') + key + QLatin1String(suffix));
    scanVoices();
}

// ------------------------------------------------------------- speaking

void Piper::setVoice(const QString &key) {
    if (key == m_voice)
        return;
    stop();
    m_voice = key;
}

void Piper::setRate(double rate) {
    m_rate = std::clamp(rate, 0.25, 4.0);
#ifdef SCANCORE_HAVE_MPV
    // mpv keeps the voice's pitch at other speeds (scaletempo2).
    if (m_mpv) {
        double value = m_rate;
        mpv_set_property_async(static_cast<mpv_handle *>(m_mpv), 0, "speed", MPV_FORMAT_DOUBLE, &value);
    }
#endif
}

bool Piper::ensureProcess() {
#if !QT_CONFIG(process)
    // Piper is a program of its own, and there is none to start (iOS).
    return false;
#else
    if (m_process && m_process->state() != QProcess::NotRunning && m_processVoice == m_voice)
        return true;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    const auto model = voicesDir() + QLatin1Char('/') + m_voice + QStringLiteral(".onnx");
    if (m_voice.isEmpty() || !QFileInfo::exists(model) || !programInstalled())
        return false;
    QDir().mkpath(speechDir());
    m_process = new QProcess(this);
    m_processVoice = m_voice;
    m_process->setWorkingDirectory(programDir());
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setStandardErrorFile(QProcess::nullDevice());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Piper::readOutput);
    connect(m_process, &QProcess::finished, this, [this, process = m_process](int code, QProcess::ExitStatus status) {
        if (process != m_process)
            return;
        // Quit on purpose (stop()) leaves nothing to say.
        if (!m_synth.isEmpty())
            fail(QStringLiteral("The neural voice stopped (%1).").arg(status == QProcess::CrashExit ? QStringLiteral("crashed") : QString::number(code)));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, process = m_process](QProcess::ProcessError error) {
        if (process == m_process && error == QProcess::FailedToStart)
            fail(QStringLiteral("The neural voice could not start."));
    });
    m_process->start(program(), {QStringLiteral("--model"), model, QStringLiteral("--json-input"), QStringLiteral("--quiet"),
                                 QStringLiteral("--espeak_data"), programDir() + QStringLiteral("/espeak-ng-data")});
    return true;
#endif
}

bool Piper::ensurePlayer() {
#ifdef SCANCORE_HAVE_MPV
    if (m_mpv)
        return true;
    std::setlocale(LC_NUMERIC, "C");
    auto *mpv = mpv_create();
    if (!mpv)
        return false;
    static const std::pair<const char *, const char *> options[]{
        {"vo", "null"}, {"vid", "no"}, {"audio-display", "no"}, {"idle", "yes"}, {"config", "no"}, {"load-scripts", "no"},
        {"ytdl", "no"}, {"terminal", "no"}, {"input-default-bindings", "no"}, {"keep-open", "no"}, {"cache", "no"},
        {"audio-client-name", "SCANCORE"}};
    for (const auto &[name, value] : options)
        mpv_set_option_string(mpv, name, value);
    // Development hook: SCANCORE_PIPER_AO=null speaks in silence, with the
    // same timing (tests).
    if (qEnvironmentVariableIsSet("SCANCORE_PIPER_AO"))
        mpv_set_option_string(mpv, "ao", qgetenv("SCANCORE_PIPER_AO").constData());
    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv);
        return false;
    }
    m_mpv = mpv;
    mpv_set_wakeup_callback(
        mpv, [](void *self) { QMetaObject::invokeMethod(static_cast<Piper *>(self), &Piper::drainPlayer, Qt::QueuedConnection); }, this);
    setRate(m_rate);
    return true;
#else
    return false;
#endif
}

void Piper::speak(int chunk, const QString &text) {
    m_waiting.append({chunk, text});
    m_outstanding.insert(chunk);
    pump();
}

// A few paragraphs ahead of the voice, not a whole chapter.
void Piper::pump() {
    while (!m_waiting.isEmpty() && m_synth.size() + m_entries.size() < 3) {
        if (!ensurePlayer()) {
            fail(QStringLiteral("The neural voice has no way to play sound here."));
            return;
        }
        if (!ensureProcess()) {
            if (!m_process)
                fail(QStringLiteral("This neural voice is not installed. Download it again in Settings → Reading."));
            return;
        }
        const auto line = m_waiting.takeFirst();
        const auto file = speechDir() + QStringLiteral("/%1-%2.wav").arg(m_generation).arg(line.chunk);
        const auto json = QJsonDocument(QJsonObject{{"text", line.text}, {"output_file", QDir::toNativeSeparators(file)}}).toJson(QJsonDocument::Compact);
        m_synth.append(line.chunk);
#if QT_CONFIG(process)
        m_process->write(json + '\n');
#endif
    }
}

void Piper::readOutput() {
#if QT_CONFIG(process)
    while (m_process && m_process->canReadLine()) {
        const auto path = QString::fromUtf8(m_process->readLine()).trimmed();
        if (path.isEmpty() || m_synth.isEmpty())
            continue;
        const int chunk = m_synth.takeFirst();
        // Said for an earlier generation (stop() came first): not wanted.
        if (!QFileInfo(path).fileName().startsWith(QString::number(m_generation) + QLatin1Char('-'))) {
            QFile::remove(path);
            continue;
        }
#ifdef SCANCORE_HAVE_MPV
        const auto utf8 = path.toUtf8();
        const char *args[] = {"loadfile", utf8.constData(), "append-play", nullptr};
        mpv_command_async(static_cast<mpv_handle *>(m_mpv), quint64(chunk), args);
        m_entries.insert(-chunk, chunk); // placeholder until the playlist entry is known
#endif
    }
#endif
}

void Piper::drainPlayer() {
#ifdef SCANCORE_HAVE_MPV
    auto *mpv = static_cast<mpv_handle *>(m_mpv);
    if (!mpv)
        return;
    while (true) {
        auto *event = mpv_wait_event(mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        switch (event->event_id) {
        case MPV_EVENT_COMMAND_REPLY: {
            const int chunk = int(event->reply_userdata);
            if (!m_entries.remove(-chunk))
                break; // stopped meanwhile
            const auto *result = static_cast<mpv_event_command *>(event->data);
            qint64 entry = 0;
            if (event->error >= 0 && result && result->result.format == MPV_FORMAT_NODE_MAP)
                for (int i = 0; i < result->result.u.list->num; ++i)
                    if (qstrcmp(result->result.u.list->keys[i], "playlist_entry_id") == 0)
                        entry = result->result.u.list->values[i].u.int64;
            if (entry <= 0) {
                fail(QStringLiteral("The neural voice could not be played."));
                break;
            }
            m_entries.insert(entry, chunk);
            if (m_startedEarly.remove(entry))
                emit started(chunk);
            break;
        }
        case MPV_EVENT_START_FILE: {
            const auto entry = static_cast<mpv_event_start_file *>(event->data)->playlist_entry_id;
            if (m_entries.contains(entry))
                emit started(m_entries.value(entry));
            else
                m_startedEarly.insert(entry);
            break;
        }
        case MPV_EVENT_END_FILE: {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            m_startedEarly.remove(end->playlist_entry_id);
            if (!m_entries.contains(end->playlist_entry_id))
                break;
            const int chunk = m_entries.take(end->playlist_entry_id);
            QFile::remove(speechDir() + QStringLiteral("/%1-%2.wav").arg(m_generation).arg(chunk));
            if (end->reason == MPV_END_FILE_REASON_EOF || end->reason == MPV_END_FILE_REASON_ERROR) {
                m_outstanding.remove(chunk);
                emit finished(chunk);
            }
            pump();
            break;
        }
        default: break;
        }
    }
#endif
}

void Piper::stop() {
    ++m_generation;
    m_waiting.clear();
    m_entries.clear();
    m_startedEarly.clear();
    m_outstanding.clear();
    // Piper would go on with paragraphs nobody wants: it starts afresh.
#if QT_CONFIG(process)
    if (!m_synth.isEmpty() && m_process) {
        m_synth.clear();
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
#endif
#ifdef SCANCORE_HAVE_MPV
    if (m_mpv) {
        const char *args[] = {"stop", nullptr};
        mpv_command_async(static_cast<mpv_handle *>(m_mpv), 0, args);
    }
#endif
}

void Piper::fail(const QString &message) {
    const int chunk = m_outstanding.isEmpty() ? 0 : *m_outstanding.cbegin();
    stop();
    emit failed(chunk, message);
}

} // namespace sc
