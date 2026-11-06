#pragma once

#include "Http.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
#include <QVariant>

#include <functional>

namespace sc {

// One background operation exposed to QML. Services return a Task; pages bind
// to loading/error/result. The work runs on the shared pool and observes the
// task's cancel flag, which is set when QML drops the task or calls cancel().
class Task : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Tasks are returned by services")
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool done READ done NOTIFY changed)
    Q_PROPERTY(bool ok READ ok NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int status READ status NOTIFY changed)
    Q_PROPERTY(QVariant result READ result NOTIFY changed)
    Q_PROPERTY(QString verifyUrl READ verifyUrl NOTIFY changed)
    // What the work has so far, while it is still loading (see report()).
    Q_PROPERTY(QVariant partial READ partial NOTIFY progressed)
    // For long lists (a library): a fingerprint of the result, made on the
    // worker, so a page refreshing it can tell nothing changed without
    // comparing it item by item (Router.keep). Empty for short results.
    Q_PROPERTY(QString digest READ digest NOTIFY changed)

public:
    using Work = std::function<QVariant()>;

    // Starts `work` on the background pool. The task is owned by QML's
    // garbage collector once returned to it.
    static Task *run(Work work);
    // A task that has already finished with `value`.
    static Task *resolved(const QVariant &value);
    // When the app quits: drops tasks not started yet and waits up to
    // `msecs` for running ones, so none is left reading the database while
    // it and the SQLite driver are torn down. Tasks asked for after this
    // never start. False when some is still running after that.
    static bool finishAll(int msecs);
    // Waits up to `msecs` for all work, started or not, to end.
    static bool waitAll(int msecs);

    ~Task() override;

    bool loading() const { return m_loading; }
    bool done() const { return !m_loading; }
    bool ok() const { return !m_loading && m_error.isEmpty(); }
    QString error() const { return m_error; }
    int status() const { return m_status; }
    QVariant result() const { return m_result; }
    QString verifyUrl() const { return m_verifyUrl; }
    QVariant partial() const { return m_partial; }
    QString digest() const { return m_digest; }

    // From inside a task's work: hands QML what is ready so far (a search
    // with some sources answered). Ignored outside a task.
    static void report(const QVariant &value);

    Q_INVOKABLE void cancel();
    // Calls fn(result) on success, now if already finished.
    void then(QObject *context, std::function<void(const QVariant &)> fn);

signals:
    void changed();
    void finished(const QVariant &result);
    void failed(const QString &error);
    void progressed(const QVariant &partial);

private:
    explicit Task(QObject *parent = nullptr);
    void complete(const QVariant &value, const QString &error, int status, const QString &verifyUrl = QString(),
                  const QString &digest = QString());

    bool m_loading = true;
    QString m_error;
    int m_status = 0;
    QVariant m_result;
    QString m_verifyUrl;
    QVariant m_partial;
    QString m_digest;
    CancelFlag m_cancel;
};

// Runs work on the background pool without a Task (fire and forget).
void runInBackground(std::function<void()> work);

} // namespace sc
