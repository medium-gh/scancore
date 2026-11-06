#include "Task.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThreadPool>

namespace sc {

namespace {

QThreadPool &pool() {
    static QThreadPool *instance = [] {
        auto *p = new QThreadPool(QCoreApplication::instance());
        // Workers mostly wait on the network, so allow more than the core count.
        p->setMaxThreadCount(std::max(16, QThread::idealThreadCount() * 2));
        p->setExpiryTimeout(60000);
        return p;
    }();
    return *instance;
}

// The task whose work runs on this thread, for report().
thread_local QPointer<Task> *t_current = nullptr;

} // namespace

// Lists this long get a digest (Task::digest).
constexpr qsizetype kDigestFrom = 50;

// Set once the app quits: work asked for after that never starts.
static std::atomic_bool g_finished = false;

void Task::finishAll(int msecs) {
    g_finished = true;
    pool().clear();
    pool().waitForDone(msecs);
}

bool Task::waitAll(int msecs) { return pool().waitForDone(msecs); }

Task::Task(QObject *parent) : QObject(parent), m_cancel(std::make_shared<std::atomic_bool>(false)) {}

Task::~Task() { m_cancel->store(true); }

Task *Task::run(Work work) {
    auto *task = new Task;
    QQmlEngine::setObjectOwnership(task, QQmlEngine::JavaScriptOwnership);
    QPointer<Task> guard(task);
    auto cancel = task->m_cancel;
    if (g_finished) {
        cancel->store(true);
        return task;
    }
    pool().start([guard, cancel, work = std::move(work)] {
        if (cancel->load())
            return;
        CancelScope scope(cancel);
        auto current = guard;
        t_current = &current;
        QVariant value;
        QString error;
        QString verifyUrl;
        int status = 0;
        try {
            value = work();
        } catch (const Error &e) {
            error = e.message();
            status = e.status();
            verifyUrl = e.verifyUrl();
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        t_current = nullptr;
        if (cancel->load())
            return;
        if (!error.isEmpty() && status != 499)
            qWarning().noquote() << "task failed:" << error;
        QString digest;
        if (value.metaType() == QMetaType::fromType<QVariantList>()) {
            const auto list = value.toList();
            if (list.size() >= kDigestFrom)
                digest = QString::fromLatin1(
                    QCryptographicHash::hash(QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact),
                                             QCryptographicHash::Sha1)
                        .toHex());
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, value, error, status, verifyUrl, digest] {
                if (guard)
                    guard->complete(value, error, status, verifyUrl, digest);
            },
            Qt::QueuedConnection);
    });
    return task;
}

Task *Task::resolved(const QVariant &value) {
    auto *task = new Task;
    QQmlEngine::setObjectOwnership(task, QQmlEngine::JavaScriptOwnership);
    task->m_loading = false;
    task->m_result = value;
    return task;
}

void Task::report(const QVariant &value) {
    if (!t_current)
        return;
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [guard = *t_current, value] {
            if (guard && guard->m_loading) {
                guard->m_partial = value;
                emit guard->progressed(value);
            }
        },
        Qt::QueuedConnection);
}

void Task::cancel() {
    if (!m_loading)
        return;
    m_cancel->store(true);
    complete({}, QStringLiteral("Cancelled"), 499);
}

void Task::then(QObject *context, std::function<void(const QVariant &)> fn) {
    if (!m_loading) {
        if (m_error.isEmpty())
            fn(m_result);
        return;
    }
    connect(this, &Task::finished, context, [fn](const QVariant &value) { fn(value); });
}

void Task::complete(const QVariant &value, const QString &error, int status, const QString &verifyUrl, const QString &digest) {
    if (!m_loading)
        return;
    m_loading = false;
    m_verifyUrl = verifyUrl;
    m_digest = digest;
    m_result = value;
    m_error = error;
    m_status = status;
    emit changed();
    if (error.isEmpty())
        emit finished(value);
    else
        emit failed(error);
}

void runInBackground(std::function<void()> work) {
    pool().start([work = std::move(work)] {
        try {
            work();
        } catch (const std::exception &e) {
            qWarning().noquote() << "background work failed:" << e.what();
        }
    });
}

} // namespace sc
