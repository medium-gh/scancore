#pragma once

#include "core/Task.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

namespace sc {

// The app lock: a PIN (and, on Android, fingerprint or face) asked for when
// SCANCORE starts and after it has been away for a while (setting
// "lockAfter": minutes, or -1 for only at start). The PIN is kept only as a
// salted PBKDF2 hash (pref "lock.pin"), worked out off the GUI thread.
// Wrong PINs make the next try wait longer.
class Lock : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // A PIN is set.
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    // The lock screen is up.
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    // How many digits the PIN has (the lock screen tries it once that many
    // are in).
    Q_PROPERTY(int length READ length NOTIFY changed)
    // Seconds before another try, after several wrong PINs.
    Q_PROPERTY(int wait READ wait NOTIFY changed)
    Q_PROPERTY(int failures READ failures NOTIFY changed)
    // Fingerprint or face can unlock (Android with one enrolled).
    Q_PROPERTY(bool biometricAvailable READ biometricAvailable CONSTANT)

public:
    static Lock *instance();
    static Lock *create(QQmlEngine *, QJSEngine *) { return instance(); }

    bool enabled() const;
    bool locked() const { return m_locked; }
    int length() const;
    int wait() const;
    int failures() const { return m_failures; }
    bool biometricAvailable() const;

    // Tries `pin`; unlocks when right. The task answers true or false.
    Q_INVOKABLE sc::Task *unlock(const QString &pin);
    // Whether `pin` is the PIN, without unlocking (changing or removing it).
    Q_INVOKABLE sc::Task *check(const QString &pin);
    // Sets a new PIN (4 to 8 digits). The task answers true when stored.
    Q_INVOKABLE sc::Task *setPin(const QString &pin);
    Q_INVOKABLE void removePin();
    Q_INVOKABLE void lockNow();
    // Android's fingerprint or face prompt; unlocks when it passes.
    Q_INVOKABLE void useBiometric();

    // From the Android prompt (any thread).
    void biometricResult(bool ok);

signals:
    void changed();
    // The Android prompt was closed without passing.
    void biometricFailed();

private:
    explicit Lock(QObject *parent = nullptr);
    void stateChanged(Qt::ApplicationState state);
    void setLocked(bool locked);
    // Wrong PINs and the wait they earned, kept across restarts (closing the
    // app must not reset the count).
    void setFailures(int failures, qint64 waitUntil);

    // The stored PIN: {salt, hash, iterations, length}.
    QVariantMap m_pin;
    bool m_locked = false;
    qint64 m_leftAt = 0;
    int m_failures = 0;
    qint64 m_waitUntil = 0;
    QTimer m_tick;
};

namespace lock {
// PBKDF2-SHA256 of `pin` with `salt`; exposed for tests.
QByteArray hash(const QString &pin, const QByteArray &salt, int iterations);
// How long to wait after `failures` wrong PINs, in seconds (0 for the
// first four).
int backoff(int failures);
} // namespace lock

} // namespace sc
