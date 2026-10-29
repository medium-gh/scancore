#pragma once

#include <QObject>
#include <QTimer>

namespace sc {

// What people did with the reader's comments on the account's site
// (scancore.onl): replies, mentions and votes, from its /api/notifications,
// into the notification area (replies and mentions also as system
// notifications when SCANCORE is not on screen). Asked every few minutes
// while signed in and the app is in front; one read here is read on the
// site too.
class SiteNotifications : public QObject {
    Q_OBJECT

public:
    static SiteNotifications *instance();
    void start();
    // A notification area entry read here ("site:<id>" keys): the same on
    // the site. Empty `ids` with `all` marks every one read.
    static void markRead(const QStringList &ids, bool all = false);

private:
    explicit SiteNotifications(QObject *parent = nullptr);
    void check();

    QTimer m_timer;
    bool m_checking = false;
};

} // namespace sc
