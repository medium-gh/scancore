#include "Notifications.h"

#include "App.h"
#include "Platform.h"
#include "SiteNotifications.h"
#include "core/Database.h"

#include <QGuiApplication>
#include <QJsonDocument>

namespace sc {

namespace {

constexpr int kKept = 300;

QVariantMap rowMap(const QVariantMap &row) {
    return {{"id", row.value("id")},
            {"kind", row.value("kind")},
            {"title", row.value("title")},
            {"body", row.value("body")},
            {"action", QJsonDocument::fromJson(row.value("action").toString().toUtf8()).toVariant()},
            {"createdAt", row.value("created_at").toLongLong() * 1000},
            {"read", !row.value("read_at").isNull()}};
}

} // namespace

Notifications::Notifications(QObject *parent) : QObject(parent) {
    runInBackground([this] {
        const int unread = Database::scalar(QStringLiteral("SELECT COUNT(*) FROM notifications WHERE read_at IS NULL")).toInt();
        QMetaObject::invokeMethod(this, [this, unread] {
            m_unread = unread;
            bump();
        });
    });
}

Notifications *Notifications::instance() {
    static auto *notifications = [] {
        auto *created = new Notifications;
        QJSEngine::setObjectOwnership(created, QJSEngine::CppOwnership);
        return created;
    }();
    return notifications;
}

void Notifications::bump() {
    ++m_revision;
    emit changed();
}

void Notifications::refreshUnread() {
    runInBackground([this] {
        const int unread = Database::scalar(QStringLiteral("SELECT COUNT(*) FROM notifications WHERE read_at IS NULL")).toInt();
        QMetaObject::invokeMethod(this, [this, unread] {
            m_unread = unread;
            bump();
        });
    });
}

Task *Notifications::list(int limit) {
    return Task::run([limit]() -> QVariant {
        QVariantList out;
        for (const auto &value : Database::query(
                 QStringLiteral("SELECT id, kind, title, body, action, created_at, read_at FROM notifications "
                                "ORDER BY created_at DESC, id DESC LIMIT ?"),
                 {limit}))
            out.append(rowMap(value.toMap()));
        return out;
    });
}

void Notifications::markRead(qint64 id) {
    runInBackground([this, id] {
        // One from the site: read there too.
        if (const auto key = Database::scalar(QStringLiteral("SELECT key FROM notifications WHERE id = ? AND read_at IS NULL"), {id}).toString();
            key.startsWith(QLatin1String("site:")))
            SiteNotifications::markRead({key.mid(5)});
        Database::exec(QStringLiteral("UPDATE notifications SET read_at = ? WHERE id = ? AND read_at IS NULL"), {Database::now(), id});
        refreshUnread();
    });
}

void Notifications::markAllRead() {
    m_unread = 0;
    bump();
    runInBackground([this] {
        if (Database::scalar(QStringLiteral("SELECT 1 FROM notifications WHERE read_at IS NULL AND key LIKE 'site:%'")).isValid())
            SiteNotifications::markRead({}, true);
        Database::exec(QStringLiteral("UPDATE notifications SET read_at = ? WHERE read_at IS NULL"), {Database::now()});
        refreshUnread();
    });
}

void Notifications::remove(qint64 id) {
    runInBackground([this, id] {
        Database::exec(QStringLiteral("DELETE FROM notifications WHERE id = ?"), {id});
        refreshUnread();
    });
}

void Notifications::clear() {
    m_unread = 0;
    bump();
    runInBackground([this] {
        Database::exec(QStringLiteral("DELETE FROM notifications"));
        refreshUnread();
    });
}

void Notifications::add(const QString &kind, const QString &title, const QString &body, bool system) {
    post({kind, QString(), title, body, {}, system});
}

void Notifications::post(const Item &item) {
    runInBackground([item] {
        const auto action = QString::fromUtf8(QJsonDocument::fromVariant(item.action).toJson(QJsonDocument::Compact));
        Database::transaction([&] {
            if (!item.key.isEmpty())
                Database::exec(QStringLiteral("DELETE FROM notifications WHERE key = ?"), {item.key});
            Database::exec(QStringLiteral("INSERT INTO notifications(kind, key, title, body, action, created_at) VALUES(?, ?, ?, ?, ?, ?)"),
                           {item.kind, item.key.isEmpty() ? QVariant() : QVariant(item.key), item.title, item.body, action, Database::now()});
            Database::exec(QStringLiteral("DELETE FROM notifications WHERE id NOT IN "
                                          "(SELECT id FROM notifications ORDER BY created_at DESC, id DESC LIMIT ?)"),
                           {kKept});
        });
        QMetaObject::invokeMethod(instance(), [item] {
            auto *self = instance();
            self->refreshUnread();
            emit self->arrived(item.title, item.body);
            const bool onScreen = QGuiApplication::applicationState() == Qt::ApplicationActive;
            if (item.system && (item.urgent || !onScreen) && !platform::notify(item.title, item.body, item.key.isEmpty() ? item.kind : item.key))
                App::instance()->notify(item.title, item.body);
        });
    });
}

void Notifications::withdraw(const QString &key) {
    runInBackground([key] {
        Database::exec(QStringLiteral("DELETE FROM notifications WHERE key = ?"), {key});
        instance()->refreshUnread();
    });
}

void Notifications::askPermission() { platform::askNotificationPermission(); }

bool Notifications::systemAllowed() const { return platform::notificationsAllowed(); }

} // namespace sc
