#pragma once

#include "Http.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QMutex>
#include <QThreadPool>
#include <QWaitCondition>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace sc {

QThreadPool &fanoutPool();
// Long batches by site (library updates): a few threads of their own, so
// they never queue ahead of a search on the fan-out pool.
QThreadPool &batchPool();

template <typename T>
struct Outcome {
    std::optional<T> value;
    QString error;
    int status = 0;
    QString verifyUrl;
    bool ok() const { return value.has_value(); }
};

template <typename T>
Outcome<T> runJob(const std::function<T()> &job) {
    Outcome<T> outcome;
    try {
        outcome.value = job();
    } catch (const Error &e) {
        outcome.error = e.message();
        outcome.status = e.status();
        outcome.verifyUrl = e.verifyUrl();
    } catch (const std::exception &e) {
        outcome.error = QString::fromUtf8(e.what());
    }
    return outcome;
}

// Runs jobs concurrently on the fan-out pool and waits for all of them. The
// caller's cancel flag is carried into every job, so cancelling a search
// stops all of its source requests.
template <typename T>
QList<Outcome<T>> parallel(const QList<std::function<T()>> &jobs) {
    const auto cancel = CancelScope::current();
    QList<QFuture<Outcome<T>>> futures;
    futures.reserve(jobs.size());
    for (const auto &job : jobs) {
        futures.append(QtConcurrent::run(&fanoutPool(), [job, cancel]() -> Outcome<T> {
            CancelScope scope(cancel);
            Outcome<T> outcome;
            try {
                outcome.value = job();
            } catch (const Error &e) {
                outcome.error = e.message();
                outcome.status = e.status();
                outcome.verifyUrl = e.verifyUrl();
            } catch (const std::exception &e) {
                outcome.error = QString::fromUtf8(e.what());
            }
            return outcome;
        }));
    }
    QList<Outcome<T>> results;
    results.reserve(futures.size());
    for (auto &future : futures)
        results.append(future.result());
    return results;
}

// How long a site's own pause lasts after it pushes back, and how quick an
// answer has to be for the site to be asked a little more at once.
constexpr int kSitePauseMs = 5000;
constexpr int kSitePauseMostMs = 60000;
constexpr qint64 kQuickAnswerMs = 5000;

// Runs jobs grouped by key (the site they ask), handing each outcome to
// `each(index, outcome)` on the calling thread as soon as it is ready, for
// long batches such as library updates. On batchPool(), so they never queue
// ahead of a search. Per site, politely:
// - one request at a time to begin with, one more (up to `perKey`) after
//   every three quick answers in a row, back to one when it pushes back;
// - pushing back (429, 503, too slow) pauses it, 5 s doubling to a minute,
//   and the job is tried once more after the pause;
// - a browser check the app could not pass ends its turn: the jobs still
//   waiting for that site report 499 ("Skipped") without asking it, since
//   each would only meet the same check.
// Sites take turns for free threads. Once cancelled, jobs not begun report
// 499.
template <typename T>
void parallelByKeyEach(const QList<std::function<T()>> &jobs, const QStringList &keys, int perKey,
                       const std::function<void(int, const Outcome<T> &)> &each) {
    struct Done {
        int index;
        Outcome<T> outcome;
        qint64 ms;
    };
    struct Shared {
        QMutex mutex;
        QWaitCondition ready;
        QList<Done> done;
    };
    struct Site {
        QList<int> waiting;
        int running = 0;
        int limit = 1;
        int quick = 0;
        int pauseMs = 0;
        qint64 pausedUntil = 0;
    };
    const auto shared = std::make_shared<Shared>();
    const auto parent = CancelScope::current();
    QHash<QString, Site> sites;
    QStringList order;
    for (int i = 0; i < jobs.size(); ++i) {
        const auto key = keys.value(i);
        if (!sites.contains(key))
            order.append(key);
        sites[key].waiting.append(i);
    }
    QList<int> tries(jobs.size(), 0);
    int remaining = int(jobs.size());
    int running = 0;
    int turn = 0;
    const int threads = std::max(1, batchPool().maxThreadCount());
    QElapsedTimer clock;
    clock.start();
    const auto report = [&](int index, const Outcome<T> &outcome) {
        --remaining;
        each(index, outcome);
    };
    const auto skip = [&](Site &site, const QString &why) {
        const auto waiting = std::exchange(site.waiting, {});
        for (const int i : waiting) {
            Outcome<T> skipped;
            skipped.error = why;
            skipped.status = 499;
            report(i, skipped);
        }
    };
    while (remaining > 0) {
        if (parent && parent->load()) {
            for (auto &site : sites)
                skip(site, QStringLiteral("Cancelled"));
        } else {
            // Free threads go to the sites in turn.
            for (int pass = 0; running < threads && pass < order.size(); ++pass) {
                auto &site = sites[order[turn]];
                turn = (turn + 1) % int(order.size());
                if (site.waiting.isEmpty() || site.running >= site.limit || site.pausedUntil > clock.elapsed())
                    continue;
                const int i = site.waiting.takeFirst();
                ++site.running;
                ++running;
                pass = -1;
                batchPool().start([job = jobs[i], i, shared, parent] {
                    CancelScope scope(parent);
                    QElapsedTimer took;
                    took.start();
                    auto outcome = runJob(job);
                    QMutexLocker lock(&shared->mutex);
                    shared->done.append({i, std::move(outcome), took.elapsed()});
                    shared->ready.wakeAll();
                });
            }
        }
        if (remaining == 0)
            break;
        QList<Done> batch;
        {
            QMutexLocker lock(&shared->mutex);
            if (shared->done.isEmpty())
                shared->ready.wait(&shared->mutex, 100);
            batch.swap(shared->done);
        }
        for (const auto &done : std::as_const(batch)) {
            --running;
            auto &site = sites[keys.value(done.index)];
            --site.running;
            const auto &outcome = done.outcome;
            if (!outcome.verifyUrl.isEmpty()) {
                report(done.index, outcome);
                skip(site, outcome.error);
                continue;
            }
            const bool pushedBack = outcome.status == 429 || outcome.status == 503 || outcome.status == 504;
            if (pushedBack) {
                site.limit = 1;
                site.quick = 0;
                site.pauseMs = site.pauseMs > 0 ? std::min(site.pauseMs * 2, kSitePauseMostMs) : kSitePauseMs;
                site.pausedUntil = clock.elapsed() + site.pauseMs;
                if (++tries[done.index] < 2 && !(parent && parent->load())) {
                    site.waiting.prepend(done.index);
                    continue;
                }
            } else if (outcome.ok()) {
                site.pauseMs = 0;
                if (done.ms >= kQuickAnswerMs) {
                    site.quick = 0;
                } else if (++site.quick >= 3) {
                    site.limit = std::min(std::max(1, perKey), site.limit + 1);
                    site.quick = 0;
                }
            }
            report(done.index, outcome);
        }
    }
}

// Like parallel(), but hands each outcome to `each(index, outcome)` on the
// calling thread as soon as it is ready, fastest first. Jobs still running
// after `timeoutMs` are cancelled and reported as timed out (status 504), so
// one slow source cannot hold the rest back.
template <typename T>
void parallelEach(const QList<std::function<T()>> &jobs, int timeoutMs,
                  const std::function<void(int, const Outcome<T> &)> &each) {
    struct Shared {
        QMutex mutex;
        QWaitCondition ready;
        QList<QPair<int, Outcome<T>>> done;
    };
    const auto shared = std::make_shared<Shared>();
    const auto parent = CancelScope::current();
    QList<CancelFlag> flags;
    flags.reserve(jobs.size());
    for (int i = 0; i < jobs.size(); ++i) {
        const auto flag = std::make_shared<std::atomic_bool>(false);
        flags.append(flag);
        fanoutPool().start([job = jobs[i], flag, shared, i] {
            CancelScope scope(flag);
            Outcome<T> outcome;
            try {
                outcome.value = job();
            } catch (const Error &e) {
                outcome.error = e.message();
                outcome.status = e.status();
                outcome.verifyUrl = e.verifyUrl();
            } catch (const std::exception &e) {
                outcome.error = QString::fromUtf8(e.what());
            }
            QMutexLocker lock(&shared->mutex);
            shared->done.append({i, std::move(outcome)});
            shared->ready.wakeAll();
        });
    }
    QList<bool> reported(jobs.size(), false);
    int remaining = int(jobs.size());
    const QDeadlineTimer deadline(timeoutMs);
    while (remaining > 0) {
        QList<QPair<int, Outcome<T>>> batch;
        {
            QMutexLocker lock(&shared->mutex);
            if (shared->done.isEmpty())
                shared->ready.wait(&shared->mutex, QDeadlineTimer(std::min<qint64>(100, deadline.remainingTime())));
            batch.swap(shared->done);
        }
        for (const auto &[index, outcome] : batch) {
            reported[index] = true;
            --remaining;
            each(index, outcome);
        }
        const bool cancelled = parent && parent->load();
        if (remaining > 0 && (cancelled || deadline.hasExpired())) {
            for (int i = 0; i < jobs.size(); ++i) {
                if (reported[i])
                    continue;
                flags[i]->store(true);
                if (cancelled)
                    continue;
                Outcome<T> late;
                late.error = QStringLiteral("It took too long to answer.");
                late.status = 504;
                each(i, late);
            }
            if (cancelled)
                throw Error(QStringLiteral("Cancelled"), 499);
            return;
        }
    }
}

} // namespace sc
