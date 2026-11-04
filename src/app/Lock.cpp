#include "Lock.h"

#include "App.h"
#include "core/Database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QGuiApplication>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace sc {

namespace {

const QString kPinPref = QStringLiteral("lock.pin");
const QString kFailuresPref = QStringLiteral("lock.failures");
constexpr int kIterations = 60000;

#ifdef Q_OS_ANDROID
constexpr auto kBiometricClass = "com/mediumdev/scancore/Biometric";

void jniBiometricResult(JNIEnv *, jclass, jboolean ok) { Lock::instance()->biometricResult(ok); }

bool registerBiometric() {
    static const bool registered = [] {
        const JNINativeMethod methods[] = {
            {"nativeResult", "(Z)V", reinterpret_cast<void *>(jniBiometricResult)},
        };
        QJniEnvironment env;
        return env.registerNativeMethods(kBiometricClass, methods, 1);
    }();
    return registered;
}
#endif

} // namespace

namespace lock {

QByteArray hash(const QString &pin, const QByteArray &salt, int iterations) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, pin.toUtf8(), salt, iterations, 32);
}

int backoff(int failures) {
    if (failures < 5)
        return 0;
    // 30 s after the fifth, doubling, at most an hour.
    return std::min(3600, 30 << std::min(7, failures - 5));
}

} // namespace lock

Lock *Lock::instance() {
    static Lock *lock = new Lock;
    return lock;
}

Lock::Lock(QObject *parent) : QObject(parent) {
    m_pin = Database::pref(kPinPref).toMap();
    const auto failures = Database::pref(kFailuresPref).toMap();
    m_failures = failures.value(QStringLiteral("count")).toInt();
    // A clock set back cannot stretch the wait past its longest.
    m_waitUntil = std::min(failures.value(QStringLiteral("until")).toLongLong(),
                           QDateTime::currentMSecsSinceEpoch() + lock::backoff(1000) * 1000LL);
    m_locked = enabled();
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (wait() == 0)
            m_tick.stop();
        emit changed();
    });
    // A wait earned before the app closed goes on counting down.
    if (wait() > 0)
        m_tick.start();
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, &Lock::stateChanged);
}

bool Lock::enabled() const { return !m_pin.value(QStringLiteral("hash")).toString().isEmpty(); }

int Lock::length() const { return m_pin.value(QStringLiteral("length")).toInt(); }

int Lock::wait() const {
    const auto left = m_waitUntil - QDateTime::currentMSecsSinceEpoch();
    return left > 0 ? int((left + 999) / 1000) : 0;
}

void Lock::setFailures(int failures, qint64 waitUntil) {
    m_failures = failures;
    m_waitUntil = waitUntil;
    Database::setPref(kFailuresPref, failures ? QVariant(QVariantMap{{"count", failures}, {"until", waitUntil}}) : QVariant());
}

void Lock::setLocked(bool locked) {
    if (m_locked == locked)
        return;
    m_locked = locked;
    emit changed();
}

void Lock::stateChanged(Qt::ApplicationState state) {
    if (!enabled())
        return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (state != Qt::ApplicationActive) {
        if (m_leftAt == 0 && !m_locked)
            m_leftAt = now;
        return;
    }
    if (m_leftAt == 0)
        return;
    const int minutes = App::instance()->settings().value(QStringLiteral("lockAfter"), 1).toInt();
    if (minutes >= 0 && now - m_leftAt >= qint64(minutes) * 60000)
        setLocked(true);
    m_leftAt = 0;
}

Task *Lock::check(const QString &pin) {
    const auto pref = m_pin;
    const auto salt = QByteArray::fromBase64(pref.value(QStringLiteral("salt")).toByteArray());
    const auto expected = QByteArray::fromBase64(pref.value(QStringLiteral("hash")).toByteArray());
    const int iterations = pref.value(QStringLiteral("iterations"), kIterations).toInt();
    return Task::run([pin, salt, expected, iterations]() -> QVariant {
        if (expected.isEmpty())
            return false;
        // Compared in full, not stopping at the first difference.
        const auto actual = lock::hash(pin, salt, iterations);
        int diff = actual.size() ^ expected.size();
        for (int i = 0; i < std::min(actual.size(), expected.size()); ++i)
            diff |= actual.at(i) ^ expected.at(i);
        return diff == 0;
    });
}

Task *Lock::unlock(const QString &pin) {
    if (wait() > 0)
        return Task::resolved(false);
    auto *task = check(pin);
    connect(task, &Task::changed, this, [this, task] {
        if (task->loading())
            return;
        if (task->result().toBool()) {
            setFailures(0, 0);
            setLocked(false);
            return;
        }
        const int failures = m_failures + 1;
        const int seconds = lock::backoff(failures);
        setFailures(failures, seconds ? QDateTime::currentMSecsSinceEpoch() + seconds * 1000LL : 0);
        if (seconds)
            m_tick.start();
        emit changed();
    });
    return task;
}

Task *Lock::setPin(const QString &pin) {
    static const QRegularExpression digits(QStringLiteral("^[0-9]{4,8}$"));
    if (!digits.match(pin).hasMatch())
        return Task::resolved(false);
    QByteArray salt(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), salt.size() / 4);
    auto *task = Task::run([pin, salt]() -> QVariant { return lock::hash(pin, salt, kIterations).toBase64(); });
    const int length = int(pin.size());
    connect(task, &Task::changed, this, [this, task, salt, length] {
        if (task->loading() || !task->ok())
            return;
        m_pin = QVariantMap{{"salt", QString::fromLatin1(salt.toBase64())},
                            {"hash", task->result().toString()},
                            {"iterations", kIterations},
                            {"length", length}};
        Database::setPref(kPinPref, m_pin);
        setFailures(0, 0);
        emit changed();
    });
    return task;
}

void Lock::removePin() {
    m_pin.clear();
    Database::setPref(kPinPref, QVariant());
    setFailures(0, 0);
    m_leftAt = 0;
    m_locked = false;
    emit changed();
}

void Lock::lockNow() {
    if (enabled())
        setLocked(true);
}

bool Lock::biometricAvailable() const {
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kBiometricClass, "available", "(Landroid/content/Context;)Z",
                                                  QNativeInterface::QAndroidApplication::context().object());
#else
    return false;
#endif
}

void Lock::useBiometric() {
#ifdef Q_OS_ANDROID
    if (!m_locked || !registerBiometric())
        return;
    QJniObject::callStaticMethod<void>(kBiometricClass, "prompt", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
                                       QNativeInterface::QAndroidApplication::context().object(),
                                       QJniObject::fromString(tr("Unlock SCANCORE")).object<jstring>(),
                                       QJniObject::fromString(tr("Use PIN")).object<jstring>());
#endif
}

void Lock::biometricResult(bool ok) {
    QMetaObject::invokeMethod(this, [this, ok] {
        if (ok) {
            setFailures(0, 0);
            setLocked(false);
        } else {
            emit biometricFailed();
        }
    });
}

} // namespace sc
