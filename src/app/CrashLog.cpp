#include "CrashLog.h"

#include "core/Database.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <csignal>
#include <io.h>
#include <fcntl.h>
#else
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <unwind.h>
#ifdef Q_OS_LINUX
#include <dirent.h>
#include <sys/syscall.h>
#endif
#endif

namespace sc::crashlog {

namespace {

// The recent log: a ring written by every thread, read when crashing.
constexpr size_t kRing = 64 * 1024;
char g_ring[kRing];
std::atomic<size_t> g_written{0};
std::atomic_flag g_ringLock = ATOMIC_FLAG_INIT;

// Prepared at install, so a crash only writes: where the report goes, and
// its first lines.
char g_path[1024];
char g_header[2048];
// A fatal message or an uncaught exception's text, for the report.
char g_reason[2048];
std::atomic<int> g_crashing{0};
QtMessageHandler g_previous = nullptr;

constexpr int kKeep = 10;

QString folder() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/crashes"); }

void ringAppend(const char *data, size_t size) {
    while (g_ringLock.test_and_set(std::memory_order_acquire)) {
    }
    size_t at = g_written.load(std::memory_order_relaxed);
    for (size_t i = 0; i < size; ++i)
        g_ring[(at + i) % kRing] = data[i];
    g_written.store(at + size, std::memory_order_relaxed);
    g_ringLock.clear(std::memory_order_release);
}

// The ring in order, oldest first. While crashing, without waiting for a
// lock another (crashed) thread may hold.
template <typename Out>
void ringCopy(Out &&out, bool locking) {
    if (locking)
        while (g_ringLock.test_and_set(std::memory_order_acquire)) {
        }
    const size_t written = g_written.load(std::memory_order_relaxed);
    const size_t start = written > kRing ? written % kRing : 0;
    const size_t size = written > kRing ? kRing : written;
    if (start + size <= kRing) {
        out(g_ring + start, size);
    } else {
        out(g_ring + start, kRing - start);
        out(g_ring, size - (kRing - start));
    }
    if (locking)
        g_ringLock.clear(std::memory_order_release);
}

void onMessage(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    static const char *const levels[] = {"D", "W", "C", "F", "I"};
    QByteArray line = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")).toLatin1();
    line += ' ';
    line += levels[std::min<int>(type, 4)];
    line += ' ';
    if (context.category && std::strcmp(context.category, "default") != 0) {
        line += context.category;
        line += ": ";
    }
    line += message.toUtf8();
    if (type != QtDebugMsg && type != QtInfoMsg && context.file) {
        line += " (";
        line += QFileInfo(QString::fromUtf8(context.file)).fileName().toUtf8();
        line += ':';
        line += QByteArray::number(context.line);
        line += ')';
    }
    line += '\n';
    ringAppend(line.constData(), size_t(line.size()));
    if (type == QtFatalMsg)
        qstrncpy(g_reason, message.toUtf8().constData(), sizeof g_reason);
    if (g_previous)
        g_previous(type, context, message);
}

// ---------------------------------------------------------------- writing

#ifdef Q_OS_WIN
using Handle = HANDLE;
void put(Handle fd, const char *data, size_t size) {
    DWORD done = 0;
    WriteFile(fd, data, DWORD(size), &done, nullptr);
}
#else
using Handle = int;
void put(Handle fd, const char *data, size_t size) {
    while (size > 0) {
        const auto n = ::write(fd, data, size);
        if (n <= 0)
            return;
        data += n;
        size -= size_t(n);
    }
}
#endif

void put(Handle fd, const char *text) { put(fd, text, std::strlen(text)); }

void putHex(Handle fd, uintptr_t value) {
    char out[2 + 2 * sizeof value + 1];
    int at = int(sizeof out) - 1;
    out[at] = '\0';
    do {
        out[--at] = "0123456789abcdef"[value & 15];
        value >>= 4;
    } while (value);
    out[--at] = 'x';
    out[--at] = '0';
    put(fd, out + at);
}

void putNumber(Handle fd, long value) {
    char out[24];
    int at = int(sizeof out) - 1;
    out[at] = '\0';
    const bool negative = value < 0;
    unsigned long v = negative ? -value : value;
    do {
        out[--at] = char('0' + v % 10);
        v /= 10;
    } while (v);
    if (negative)
        out[--at] = '-';
    put(fd, out + at);
}

void putTail(Handle fd) {
    if (g_reason[0]) {
        put(fd, "Message: ");
        put(fd, g_reason);
        put(fd, "\n");
    }
}

void putLog(Handle fd) {
    put(fd, "\nRecent log:\n");
    ringCopy([fd](const char *data, size_t size) { put(fd, data, size); }, false);
}

#ifndef Q_OS_WIN
struct Walk {
    Handle fd;
    int frame;
};

_Unwind_Reason_Code onFrame(_Unwind_Context *context, void *argument) {
    auto *walk = static_cast<Walk *>(argument);
    const auto pc = uintptr_t(_Unwind_GetIP(context));
    if (!pc || walk->frame > 64)
        return _URC_END_OF_STACK;
    put(walk->fd, "#");
    putNumber(walk->fd, walk->frame++);
    put(walk->fd, " ");
    putHex(walk->fd, pc);
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(pc), &info) && info.dli_fname) {
        const char *name = std::strrchr(info.dli_fname, '/');
        put(walk->fd, " ");
        put(walk->fd, name ? name + 1 : info.dli_fname);
        put(walk->fd, "+");
        putHex(walk->fd, pc - uintptr_t(info.dli_fbase));
        if (info.dli_sname) {
            put(walk->fd, " ");
            put(walk->fd, info.dli_sname);
        }
    }
    put(walk->fd, "\n");
    return _URC_NO_REASON;
}

struct sigaction g_old[32];
const int kSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};

const char *signalName(int signal) {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (aborted)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    }
    return "signal";
}

void onSignal(int signal, siginfo_t *info, void *context) {
    if (g_crashing.fetch_add(1) == 0) {
        const int fd = ::open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            put(fd, g_header);
            put(fd, "What: ");
            put(fd, signalName(signal));
            if (info && (signal == SIGSEGV || signal == SIGBUS)) {
                put(fd, " at ");
                putHex(fd, uintptr_t(info->si_addr));
            }
            put(fd, "\n");
            putTail(fd);
            put(fd, "\nBacktrace:\n");
            Walk walk{fd, 0};
            _Unwind_Backtrace(onFrame, &walk);
            putLog(fd);
            ::close(fd);
        }
    }
    // The system's own handling (Android's crash dump, a core file).
    sigaction(signal, &g_old[signal], nullptr);
    if (g_old[signal].sa_flags & SA_SIGINFO) {
        if (g_old[signal].sa_sigaction)
            g_old[signal].sa_sigaction(signal, info, context);
    } else if (g_old[signal].sa_handler != SIG_DFL && g_old[signal].sa_handler != SIG_IGN) {
        g_old[signal].sa_handler(signal);
    }
    raise(signal);
}
#else
wchar_t g_widePath[1024];

// The exported function at or before `rva` in a loaded module (Qt's and
// the system's functions are exported), read from its export table.
const char *nearestExport(HMODULE module, DWORD rva, DWORD *offset) {
    const auto base = reinterpret_cast<const BYTE *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return nullptr;
    const auto *exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY *>(base + dir.VirtualAddress);
    const auto *names = reinterpret_cast<const DWORD *>(base + exports->AddressOfNames);
    const auto *ordinals = reinterpret_cast<const WORD *>(base + exports->AddressOfNameOrdinals);
    const auto *functions = reinterpret_cast<const DWORD *>(base + exports->AddressOfFunctions);
    const char *best = nullptr;
    DWORD bestRva = 0;
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        const DWORD at = functions[ordinals[i]];
        if (at <= rva && at >= bestRva && (at < dir.VirtualAddress || at >= dir.VirtualAddress + dir.Size)) {
            bestRva = at;
            best = reinterpret_cast<const char *>(base + names[i]);
        }
    }
    *offset = rva - bestRva;
    return best;
}

// "#n 0xpc module+0xoffset (export+0xoffset)"
void putFrame(Handle fd, int index, uintptr_t pc, bool withExport) {
    put(fd, "#");
    putNumber(fd, index);
    put(fd, " ");
    putHex(fd, pc);
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(pc), &module)) {
        char name[MAX_PATH];
        const DWORD n = GetModuleFileNameA(module, name, MAX_PATH);
        const char *base = name;
        for (DWORD k = 0; k < n; ++k)
            if (name[k] == '\\')
                base = name + k + 1;
        put(fd, " ");
        put(fd, base);
        put(fd, "+");
        putHex(fd, pc - uintptr_t(module));
        DWORD offset = 0;
        // SCANCORE's own frames need its symbols (scancore.pdb); its few
        // exports would only mislead.
        if (const char *function = withExport && module != GetModuleHandleW(nullptr)
                                       ? nearestExport(module, DWORD(pc - uintptr_t(module)), &offset)
                                       : nullptr) {
            put(fd, " (");
            put(fd, function);
            put(fd, "+");
            putHex(fd, offset);
            put(fd, ")");
        }
    }
    put(fd, "\n");
}

void putStack(Handle fd) {
    void *frames[64];
    const USHORT count = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < count; ++i)
        putFrame(fd, i, uintptr_t(frames[i]), false);
}

// Another thread's stack: stopped just long enough to unwind it (nothing
// that could take a lock it holds runs meanwhile), then written out.
void putThreadStack(Handle fd, DWORD threadId) {
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
    if (!thread) {
        put(fd, "(no access)\n");
        return;
    }
    DWORD64 frames[64];
    int count = 0;
    if (SuspendThread(thread) != DWORD(-1)) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
#if defined(_M_X64)
        if (GetThreadContext(thread, &context)) {
            while (count < 64 && context.Rip) {
                frames[count++] = context.Rip;
                DWORD64 imageBase = 0;
                if (auto *entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr)) {
                    void *handlerData = nullptr;
                    DWORD64 establisher = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry, &context, &handlerData, &establisher, nullptr);
                } else {
                    // A leaf function: the return address is on top (read
                    // so that a bad stack ends the walk, not the app).
                    DWORD64 ret = 0;
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(context.Rsp), &ret, sizeof ret, &got) ||
                        got != sizeof ret)
                        break;
                    context.Rip = ret;
                    context.Rsp += 8;
                }
            }
        }
#endif
        ResumeThread(thread);
    }
    CloseHandle(thread);
    for (int i = 0; i < count; ++i)
        putFrame(fd, i, uintptr_t(frames[i]), true);
    if (count == 0)
        put(fd, "(no stack)\n");
}

void writeReport(const char *what, uintptr_t address) {
    if (g_crashing.fetch_add(1) != 0)
        return;
    HANDLE fd = CreateFileW(g_widePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fd == INVALID_HANDLE_VALUE)
        return;
    put(fd, g_header);
    put(fd, "What: ");
    put(fd, what);
    if (address) {
        put(fd, " at ");
        putHex(fd, address);
    }
    put(fd, "\n");
    putTail(fd);
    put(fd, "\nBacktrace:\n");
    putStack(fd);
    putLog(fd);
    CloseHandle(fd);
}

LONG WINAPI onException(EXCEPTION_POINTERS *pointers) {
    char what[64] = "exception ";
    char code[16];
    DWORD value = pointers->ExceptionRecord->ExceptionCode;
    int at = 15;
    code[at] = '\0';
    do {
        code[--at] = "0123456789abcdef"[value & 15];
        value >>= 4;
    } while (value);
    std::strcat(what, "0x");
    std::strcat(what, code + at);
    writeReport(what, uintptr_t(pointers->ExceptionRecord->ExceptionAddress));
    return EXCEPTION_CONTINUE_SEARCH;
}

void onAbort(int) { writeReport("abort", 0); }
#endif

void onTerminate() {
    if (auto current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception &e) {
            qstrncpy(g_reason, (std::string("Uncaught exception: ") + e.what()).c_str(), sizeof g_reason);
        } catch (...) {
            qstrncpy(g_reason, "Uncaught exception of an unknown type", sizeof g_reason);
        }
    }
    std::abort();
}

QStringList seenList() { return Database::pref(QStringLiteral("crash.seen")).toStringList(); }

// ------------------------------------------------------------------ hangs

qint64 monotonicMs() { return QDeadlineTimer::current().deadline(); }

// The interface's thread beats once a second while the app is in use; the
// watchdog reports a gap of kHang, and quitting that takes kQuitHang.
constexpr qint64 kHang = 8000;
constexpr qint64 kQuitHang = 15000;
std::atomic<qint64> g_beat{0};
std::atomic_bool g_watching{false};
std::atomic_bool g_quitting{false};
std::atomic<qint64> g_quitSince{0};
std::atomic<int> g_exitCode{0};
// Kept from start-up: while quitting, the application object may be gone.
const QString *g_hangFolder = nullptr;

// Threads that may wait on something that does not answer (Busy).
struct BusySlot {
    std::atomic<const char *> what{nullptr};
    std::atomic<qint64> since{0};
#ifndef Q_OS_WIN
    pthread_t thread{};
#else
    std::atomic<DWORD> thread{0};
#endif
};
BusySlot g_busy[8];

#ifdef Q_OS_WIN
DWORD g_guiThread = 0;
#endif

#ifndef Q_OS_WIN
pthread_t g_guiThread{};
// Where the stacks of a hang report go, while one is written; the thread
// asked writes its own stack from its signal handler.
std::atomic<int> g_hangFd{-1};
std::atomic_bool g_stackDone{false};
#ifdef SIGRTMIN
const int kStackSignal = SIGRTMIN + 5;
#else
// macOS has no real-time signals.
const int kStackSignal = SIGUSR2;
#endif

void onStackSignal(int) {
    const int fd = g_hangFd.load();
    if (fd >= 0) {
        Walk walk{fd, 0};
        _Unwind_Backtrace(onFrame, &walk);
    }
    g_stackDone = true;
}

// The stack of `thread`, written into fd by the thread itself (it may be
// blocked; the signal interrupts the wait). Nothing if it does not answer.
void putStackOf(int fd, pthread_t thread) {
    g_stackDone = false;
    g_hangFd = fd;
    if (pthread_kill(thread, kStackSignal) == 0)
        for (int i = 0; i < 100 && !g_stackDone; ++i)
            ::usleep(10000);
    if (!g_stackDone)
        put(fd, "(no answer)\n");
    g_hangFd = -1;
}

#ifdef Q_OS_LINUX
// Every other thread's stack (quitting waits on one of them: the render
// thread, a worker), each written by the thread itself through the same
// signal, with its name.
void putOtherThreads(int fd) {
    const pid_t self = pid_t(::syscall(SYS_gettid));
    const pid_t process = ::getpid();
    DIR *dir = ::opendir("/proc/self/task");
    if (!dir)
        return;
    while (const dirent *entry = ::readdir(dir)) {
        const pid_t tid = pid_t(std::atoi(entry->d_name));
        if (tid <= 0 || tid == self || tid == process)
            continue;
        char name[64] = "";
        char path[64];
        std::snprintf(path, sizeof path, "/proc/self/task/%d/comm", int(tid));
        if (const int comm = ::open(path, O_RDONLY); comm >= 0) {
            const auto n = ::read(comm, name, sizeof name - 1);
            name[n > 0 ? n : 0] = '\0';
            if (char *end = std::strchr(name, '\n'))
                *end = '\0';
            ::close(comm);
        }
        put(fd, "\nThread ");
        putNumber(fd, long(tid));
        put(fd, " (");
        put(fd, name);
        put(fd, "):\n");
        g_stackDone = false;
        g_hangFd = fd;
        if (::syscall(SYS_tgkill, process, tid, kStackSignal) == 0)
            for (int i = 0; i < 30 && !g_stackDone; ++i)
                ::usleep(10000);
        if (!g_stackDone)
            put(fd, "(no answer)\n");
        g_hangFd = -1;
    }
    ::closedir(dir);
}
#endif
#endif

void writeHangReport(qint64 stuckMs, bool quitting) {
    const auto dir = g_hangFolder ? *g_hangFolder : folder();
    QDir().mkpath(dir);
    const auto path = dir + QStringLiteral("/crash-%1-%2-hang.txt")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
                                .arg(QCoreApplication::applicationPid());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QByteArray head = QByteArray(g_header).replace("crash report", "hang report") +
                      (quitting ? "What: quitting took more than " : "What: the app stopped answering for ") +
                      QByteArray::number(stuckMs / 1000) + " s\n";
    const qint64 now = monotonicMs();
    for (const auto &slot : g_busy)
        if (const char *what = slot.what.load())
            head += "Busy: " + QByteArray(what) + " for " + QByteArray::number((now - slot.since.load()) / 1000.0, 'f', 1) + " s\n";
    file.write(head);
    file.flush();
#ifndef Q_OS_WIN
    const int fd = file.handle();
    put(fd, "\nInterface thread:\n");
    putStackOf(fd, g_guiThread);
    for (const auto &slot : g_busy)
        if (const char *what = slot.what.load()) {
            put(fd, "\nBusy thread (");
            put(fd, what);
            put(fd, "):\n");
            putStackOf(fd, slot.thread);
        }
#ifdef Q_OS_LINUX
    if (quitting)
        putOtherThreads(fd);
#endif
    putLog(fd);
#else
    file.close();
    const HANDLE fd = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fd == INVALID_HANDLE_VALUE)
        return;
    put(fd, "\nInterface thread:\n");
    putThreadStack(fd, g_guiThread);
    for (const auto &slot : g_busy)
        if (const char *what = slot.what.load())
            if (const DWORD thread = slot.thread.load(); thread && thread != g_guiThread) {
                put(fd, "\nBusy thread (");
                put(fd, what);
                put(fd, "):\n");
                putThreadStack(fd, thread);
            }
    QByteArray log;
    ringCopy([&log](const char *data, size_t size) { log.append(data, qsizetype(size)); }, true);
    put(fd, "\nRecent log:\n");
    put(fd, log.constData(), size_t(log.size()));
    CloseHandle(fd);
#endif
}

} // namespace

Busy::Busy(const char *what) {
    for (int i = 0; i < int(std::size(g_busy)); ++i) {
        const char *none = nullptr;
        auto &slot = g_busy[i];
        if (!slot.what.compare_exchange_strong(none, what))
            continue;
#ifndef Q_OS_WIN
        slot.thread = pthread_self();
#else
        slot.thread = GetCurrentThreadId();
#endif
        slot.since = monotonicMs();
        m_slot = i;
        return;
    }
}

Busy::~Busy() {
    if (m_slot >= 0)
        g_busy[m_slot].what = nullptr;
}

void watchHangs() {
#ifndef Q_OS_WIN
    g_guiThread = pthread_self();
    struct sigaction action{};
    action.sa_handler = onStackSignal;
    action.sa_flags = SA_RESTART | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    sigaction(kStackSignal, &action, nullptr);
#else
    g_guiThread = GetCurrentThreadId();
#endif
    g_hangFolder = new QString(folder());
    g_beat = monotonicMs();
    auto *beat = new QTimer(QCoreApplication::instance());
    beat->setInterval(1000);
    QObject::connect(beat, &QTimer::timeout, [] { g_beat = monotonicMs(); });
    beat->start();
    // Only while in use: Android stops the interface's thread in the
    // background. On a computer, also while another window is in front: a
    // freeze that starts then is still one when the user comes back (and a
    // frozen app never hears that it is in front again).
    const auto follow = [](Qt::ApplicationState state) {
        g_beat = monotonicMs();
#ifdef Q_OS_ANDROID
        g_watching = !g_quitting && state == Qt::ApplicationActive;
#else
        g_watching = !g_quitting && state != Qt::ApplicationSuspended;
#endif
    };
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, qGuiApp, follow);
    follow(qGuiApp->applicationState());
    // Quitting waits a bounded time for work still running (a page
    // loading): not a hang, unless it takes longer than kQuitHang (an
    // installer updating SCANCORE waits for it to end).
    QObject::connect(qGuiApp, &QCoreApplication::aboutToQuit, [] {
        g_quitSince = monotonicMs();
        g_quitting = true;
        g_watching = false;
    });
    std::thread([] {
        bool reported = false, quitReported = false;
        qint64 last = monotonicMs();
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const qint64 now = monotonicMs();
            // The computer slept: every clock jumped, nothing hung.
            if (now - last > 5000)
                g_beat = now;
            last = now;
            if (g_quitting) {
                if (!quitReported && now - g_quitSince.load() > kQuitHang) {
                    quitReported = true;
                    writeHangReport(now - g_quitSince.load(), true);
                    // Everything is saved by now (aboutToQuit ran long ago);
                    // what is left is tearing down, which hangs (the window
                    // waiting for its render thread): end here rather than
                    // stay running with no window.
                    std::fflush(nullptr);
                    std::_Exit(g_exitCode.load());
                }
                continue;
            }
            const qint64 stuck = now - g_beat.load();
            if (!g_watching || stuck < kHang) {
                reported = false;
                continue;
            }
            if (!reported) {
                reported = true;
                qWarning("The interface has not answered for %lld s; writing a hang report", stuck / 1000);
                writeHangReport(stuck, false);
            }
        }
    }).detach();
}

void setExitCode(int code) { g_exitCode = code; }

void install() {
    QDir().mkpath(folder());
    // The oldest go once there are more than kKeep.
    auto files = QDir(folder()).entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time);
    for (int i = kKeep; i < files.size(); ++i)
        QFile::remove(files[i].absoluteFilePath());

    const auto stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const auto path = QDir::toNativeSeparators(folder() + QStringLiteral("/crash-%1-%2.txt").arg(stamp).arg(QCoreApplication::applicationPid()));
    qstrncpy(g_path, path.toLocal8Bit().constData(), sizeof g_path);
#ifdef Q_OS_WIN
    path.toWCharArray(g_widePath);
    g_widePath[std::min<qsizetype>(path.size(), 1023)] = 0;
#endif
    const auto header = QStringLiteral("SCANCORE crash report\nVersion: %1\nSystem: %2 (%3, %4)\nQt: %5\nStarted: %6\n")
                            .arg(QCoreApplication::applicationVersion(), QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                                 QSysInfo::kernelVersion(), QString::fromLatin1(qVersion()),
                                 QDateTime::currentDateTime().toString(Qt::ISODate))
                            .toUtf8();
    qstrncpy(g_header, header.constData(), sizeof g_header);

    g_previous = qInstallMessageHandler(onMessage);
    std::set_terminate(onTerminate);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(onException);
    std::signal(SIGABRT, onAbort);
#else
    // Room to report a stack overflow from.
    static char altStack[64 * 1024];
    stack_t stack{};
    stack.ss_sp = altStack;
    stack.ss_size = sizeof altStack;
    sigaltstack(&stack, nullptr);
    for (const int signal : kSignals) {
        struct sigaction action{};
        action.sa_sigaction = onSignal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        sigaction(signal, &action, &g_old[signal]);
    }
#endif
}

QVariantList reports() {
    QVariantList out;
    const auto seen = seenList();
    for (const auto &file : QDir(folder()).entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time)) {
        QFile f(file.absoluteFilePath());
        QString summary;
        if (f.open(QIODevice::ReadOnly))
            for (const auto &line : QString::fromUtf8(f.read(8192)).split(QLatin1Char('\n')))
                if (line.startsWith(QLatin1String("What: ")) || line.startsWith(QLatin1String("Message: ")))
                    summary += (summary.isEmpty() ? QString() : QStringLiteral(" · ")) + line.section(QLatin1String(": "), 1);
        out.append(QVariantMap{{"file", file.fileName()},
                               {"when", file.lastModified().toSecsSinceEpoch()},
                               {"summary", summary},
                               {"seen", seen.contains(file.fileName())}});
    }
    return out;
}

QString read(const QString &file) {
    if (file.contains(QLatin1Char('/')) || file.contains(QLatin1Char('\\')))
        return {};
    QFile f(folder() + QLatin1Char('/') + file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList unseen() {
    QStringList out;
    for (const auto &value : reports())
        if (!value.toMap().value(QStringLiteral("seen")).toBool())
            out.append(value.toMap().value(QStringLiteral("file")).toString());
    return out;
}

void markSeen(const QString &file) {
    auto seen = seenList();
    if (!seen.contains(file)) {
        seen.append(file);
        while (seen.size() > 50)
            seen.removeFirst();
        Database::setPref(QStringLiteral("crash.seen"), seen);
    }
}

void remove(const QString &file) {
    if (!file.contains(QLatin1Char('/')) && !file.contains(QLatin1Char('\\')))
        QFile::remove(folder() + QLatin1Char('/') + file);
}

QString diagnostics() {
    QByteArray log;
    ringCopy([&log](const char *data, size_t size) { log.append(data, qsizetype(size)); }, true);
    return QString::fromUtf8(g_header).replace(QLatin1String("crash report"), QLatin1String("diagnostics")) +
           QStringLiteral("Now: %1\n\nRecent log:\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate)) + QString::fromUtf8(log);
}

} // namespace sc::crashlog
