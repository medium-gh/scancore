#include "SiteNotifications.h"

#include "Account.h"
#include "App.h"
#include "Notifications.h"
#include "core/Database.h"
#include "core/Http.h"
#include "core/Task.h"

#include <QDateTime>
#include <QGuiApplication>

namespace sc {

namespace {

// The site's notifications already put in the notification area, newest
// last, so each comes in once.
const auto kSeenPref = QStringLiteral("siteNotifications.seen");
constexpr int kSeenKept = 200;
constexpr int kEveryMs = 3 * 60 * 1000;

QString clipped(QString text) {
    text = text.simplified();
    return text.size() > 200 ? text.left(199) + QStringLiteral("…") : text;
}

} // namespace

SiteNotifications::SiteNotifications(QObject *parent) : QObject(parent) {
    m_timer.setInterval(kEveryMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (QGuiApplication::applicationState() == Qt::ApplicationActive)
            check();
    });
    // Back in front, or just signed in: asked at once.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            check();
    });
    connect(Account::instance(), &Account::changed, this, [this] {
        static bool was = false;
        const bool now = Account::instance()->signedIn();
        if (now && !was)
            QTimer::singleShot(3000, this, &SiteNotifications::check);
        was = now;
    });
}

SiteNotifications *SiteNotifications::instance() {
    static auto *notifications = new SiteNotifications(App::instance());
    return notifications;
}

void SiteNotifications::start() {
    m_timer.start();
    check();
}

void SiteNotifications::check() {
    if (m_checking || !Account::instance()->signedIn())
        return;
    m_checking = true;
    const auto site = Account::siteBase();
    runInBackground([this, site] {
        QVariantList rows;
        try {
            rows = Account::call("GET", QStringLiteral("/api/notifications?pageSize=20")).value(QStringLiteral("notifications")).toList();
        } catch (const Error &) {
            // Offline, signed out on the site, or a sync server without
            // comments: asked again later.
        }
        auto seen = Database::pref(kSeenPref).toStringList();
        // Oldest first, so the newest ends up on top.
        for (auto it = rows.crbegin(); it != rows.crend(); ++it) {
            const auto n = it->toMap();
            const auto id = n.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || seen.contains(id) || !n.value(QStringLiteral("readAt")).isNull())
                continue;
            seen.append(id);
            const auto type = n.value(QStringLiteral("type")).toString();
            const auto who = n.value(QStringLiteral("actor")).toMap().value(QStringLiteral("username")).toString();
            QString title;
            bool system = false;
            if (type == QLatin1String("COMMENT_REPLY")) {
                title = tr("%1 replied to your comment").arg(who);
                system = true;
            } else if (type == QLatin1String("COMMENT_MENTION")) {
                title = tr("%1 mentioned you").arg(who);
                system = true;
            } else if (type == QLatin1String("COMMENT_UPVOTE")) {
                title = tr("%1 upvoted your comment").arg(who);
            } else if (type == QLatin1String("COMMENT_DOWNVOTE")) {
                title = tr("%1 downvoted your comment").arg(who);
            } else {
                continue;
            }
            QVariantMap action;
            if (const auto link = n.value(QStringLiteral("deepLink")).toString(); link.startsWith(QLatin1Char('/')))
                action.insert(QStringLiteral("url"), site + link);
            Notifications::post({QStringLiteral("comments"), QStringLiteral("site:") + id, title,
                                 clipped(n.value(QStringLiteral("comment")).toMap().value(QStringLiteral("body")).toString()), action,
                                 system});
        }
        if (seen.size() > kSeenKept)
            seen = seen.mid(seen.size() - kSeenKept);
        Database::setPref(kSeenPref, seen);
        QMetaObject::invokeMethod(this, [this] { m_checking = false; });
    });
}

void SiteNotifications::markRead(const QStringList &ids, bool all) {
    if (!Account::instance()->signedIn() || (ids.isEmpty() && !all))
        return;
    runInBackground([ids, all] {
        try {
            if (all)
                Account::call("PATCH", QStringLiteral("/api/notifications"), {{"action", "mark_all_read"}});
            else
                for (const auto &id : ids)
                    Account::call("PATCH", QStringLiteral("/api/notifications"), {{"action", "mark_read"}, {"id", id}});
        } catch (const Error &) {
            // Still unread on the site; nothing else depends on it.
        }
    });
}

} // namespace sc
