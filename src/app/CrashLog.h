#pragma once

#include <QString>
#include <QVariantList>

namespace sc::crashlog {

// Crash reports people can send: the last stretch of SCANCORE's log is kept
// in memory (a ring, no disk writes while running), and a crash (a fatal
// signal, a Qt fatal error or an uncaught exception) writes it to a report
// in <app data>/crashes with what happened and where: version, system and a
// backtrace (module + offset for each frame, names where the system has
// them). The next start offers to share it.
//
// install() goes first in main(), once the application's name is set.
void install();

// The reports, newest first: [{file, when, summary, seen}].
QVariantList reports();
// One report's text.
QString read(const QString &file);
// Reports written since the last start that nobody has looked at yet.
QStringList unseen();
void markSeen(const QString &file);
void remove(const QString &file);
// Version, system and the recent log, for problems that are not crashes.
QString diagnostics();

// Hangs: when the interface's thread has not come back to its event loop
// for 8 s while the app is in use, a report ("…-hang.txt", offered like a
// crash's) says so, with that thread's stack and the stacks of threads that
// are Busy at the time. Call once from the interface's thread, after
// install(). Quitting that takes over 15 s leaves a report too (on Linux
// with every thread's stack) and then ends the process: everything is saved
// by then, and a SCANCORE that never ends blocks the next one starting.
void watchHangs();
// What the process ends with if quitting has to be cut short.
void setExitCode(int code);

// A thread about to wait on something that may never answer (mpv's
// renderer): a hang report taken meanwhile names it and shows its stack.
class Busy {
public:
    explicit Busy(const char *what);
    ~Busy();
    Busy(const Busy &) = delete;
    Busy &operator=(const Busy &) = delete;

private:
    int m_slot = -1;
};

} // namespace sc::crashlog
