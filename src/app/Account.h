#pragma once

#include "core/Task.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantMap>

namespace sc {

// Optional link to a scancore.onl account. Signing in stores the site's
// session cookie in the app's cookie jar; the password is never saved.
// Sync brings the website library and comic reading history into the local
// library, adds local saves to the website when it knows the series (and
// removes the ones removed here), and keeps anime watch progress and comic
// reading positions in step both ways.
class Account : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(QVariantMap user READ user NOTIFY changed)
    Q_PROPERTY(bool syncing READ syncing NOTIFY changed)
    Q_PROPERTY(QString lastSync READ lastSync NOTIFY changed)
    // scancore.onl, or a SCANCORE website hosted elsewhere.
    Q_PROPERTY(QString site READ site NOTIFY changed)
    Q_PROPERTY(bool customSite READ customSite NOTIFY changed)
    // Signed in to an account while another account's library is still on
    // this device (kept at sign-out, or a session that ended elsewhere):
    // that account's email, and nothing syncs until keepOtherData() says
    // whether it goes into this one. Empty otherwise.
    Q_PROPERTY(QString otherData READ otherData NOTIFY changed)

public:
    static Account *instance();
    // The website the app talks to: scancore.onl, the user's own, or
    // SCANCORE_SITE (development).
    static QString siteBase();
    static Account *create(QQmlEngine *, QJSEngine *) { return instance(); }

    bool signedIn() const { return !m_user.isEmpty(); }
    QVariantMap user() const { return m_user; }
    bool syncing() const { return m_syncing; }
    QString lastSync() const;
    QString site() const;
    bool customSite() const;
    QString otherData() const { return m_otherData; }
    // While signed out: the website to sign in to; empty for scancore.onl.
    // Returns what is wrong with the address, or nothing.
    Q_INVOKABLE QString setSite(const QString &address);

    Q_INVOKABLE sc::Task *signIn(const QString &email, const QString &password);
    // Creates an account on the site and signs in. `accessCode` is sent when
    // the site asks for one (signupOptions().accessCode).
    Q_INVOKABLE sc::Task *signUp(const QString &email, const QString &username, const QString &password,
                                 const QString &accessCode = QString());
    // Emails a link to choose a new password, when the address has an
    // account; the link opens the site's account/recovery page.
    Q_INVOKABLE sc::Task *requestPasswordReset(const QString &email);
    // The 6-digit code from that email: resolves to a one-time token (the
    // emailed link stops working), which completePasswordReset() spends.
    Q_INVOKABLE sc::Task *exchangeResetCode(const QString &email, const QString &code);
    // Sets the new password and signs the account out everywhere.
    Q_INVOKABLE sc::Task *completePasswordReset(const QString &token, const QString &password);
    // Who may create an account on the current site: {open, accessCode,
    // codeLength, help, recovery}. scancore.onl may need an access code; a SCANCORE
    // sync server answers open or closed.
    Q_INVOKABLE sc::Task *signupOptions();
    // A JSON request to the account's site with its session (comments and
    // reviews). Resolves to the response body; fails with the site's message.
    Q_INVOKABLE sc::Task *request(const QString &method, const QString &path, const QVariantMap &body = {});
    // The same, blocking (on a worker thread); throws Error.
    static QVariantMap call(const QByteArray &method, const QString &path, const QVariantMap &body = {});
    // Website rules, so the form can explain problems before submitting.
    Q_INVOKABLE QVariantList passwordChecks(const QString &password) const;
    Q_INVOKABLE QString usernameProblem(const QString &username) const;
    // Signs out. The library, progress, bookmarks, lists and activity are
    // the account's: they leave this device unless `keepData` (sync first,
    // so nothing unsent is lost). Files from the device's own folders stay.
    Q_INVOKABLE void signOut(bool keepData = false);
    // See otherData: true adds that library to this account, false
    // replaces it with this account's.
    Q_INVOKABLE void keepOtherData(bool keep);
    // {libraryImported, historyImported, pushed, morePending}. While a sync
    // runs, returns that one.
    Q_INVOKABLE sc::Task *sync();
    // Account sync rests while WebDAV or folder sync is on (see Sync).
    bool active() const;

signals:
    void changed();

private:
    // Private so QML uses create(): Qt builds its own instance of a
    // singleton it can default-construct, and C++ and QML would then
    // work on different objects.
    explicit Account(QObject *parent = nullptr);

    sc::Task *authenticate(const QString &path, const QJsonObject &body);
    void setUser(const QVariantMap &user);
    // A sync soon after a change in the library or progress, but not more
    // than about once a minute.
    void scheduleSync();
    static QVariantMap importAll(const QJsonObject &session);
    // Who the library on this device belongs to: "<site>|<email>".
    QString ownerKey() const;
    // Removes what belongs to an account from this device.
    static void clearAccountData();
    static bool hasAccountData();

    QVariantMap m_user;
    QString m_otherData;
    bool m_syncing = false;
    // Something changed during a sync, so another follows it.
    bool m_dirty = false;
    qint64 m_lastSyncEnded = 0;
    QTimer m_soon;
    QPointer<Task> m_task;
};

} // namespace sc
