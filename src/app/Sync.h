#pragma once

#include "core/Task.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantMap>

#include <memory>

namespace sc {

// Sync without an account: each device writes a snapshot of its library,
// reading and watching progress and activity to a shared place, and merges
// the others' snapshots into its own. The place is a WebDAV folder
// (Nextcloud, a NAS, …) or a folder another program keeps in step (Syncthing,
// Dropbox, OneDrive). A SCANCORE website that keeps device snapshots
// (scancore.onl, or one you host) is used the same way, alongside its
// account sync (see Account).
//
// Merging keeps, for every series, its latest change (saved, re-categorised
// or removed), and for every chapter or episode its latest progress.
class Sync : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // "", "webdav" or "folder".
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    // The WebDAV address or folder path.
    Q_PROPERTY(QString location READ location NOTIFY changed)
    Q_PROPERTY(QString user READ user NOTIFY changed)
    Q_PROPERTY(bool syncing READ syncing NOTIFY changed)
    Q_PROPERTY(QString lastSync READ lastSync NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // [{id, name, platform, updatedAt, self}] for every device seen.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)

public:
    static Sync *instance();
    static Sync *create(QQmlEngine *, QJSEngine *) { return instance(); }

    QString kind() const;
    QString location() const;
    QString user() const;
    bool syncing() const { return m_syncing; }
    QString lastSync() const;
    QString error() const { return m_error; }
    QVariantList devices() const;
    QString deviceName() const;

    // Checks the address and credentials, then syncs and keeps syncing there.
    Q_INVOKABLE sc::Task *useWebDav(const QString &url, const QString &user, const QString &password);
    Q_INVOKABLE sc::Task *useFolder(const QUrl &folder);
    // Stops syncing; the snapshots already written stay where they are.
    Q_INVOKABLE void stop();
    // {merged, devices}. While a sync runs, returns that one.
    Q_INVOKABLE sc::Task *sync();

    // One place that holds the snapshots: a name ("<device>.json") and a
    // version tag for each, so unchanged ones are not read again.
    struct Entry {
        QString name;
        QString tag;
        QByteArray inlineData;
    };
    class Store {
    public:
        virtual ~Store() = default;
        virtual void ensure() = 0;
        virtual QList<Entry> list() = 0;
        virtual QByteArray read(const QString &name) = 0;
        virtual void write(const QString &name, const QByteArray &data) = 0;
    };
    // Reads the snapshots that changed since last time (by `seenKey`, a
    // preference), merges them, and writes this device's.
    static QVariantMap exchange(Store &store, const QString &seenKey);
    static std::unique_ptr<Store> serverStore(const QString &site);

    // This device's library, progress and activity as a snapshot, and
    // another device's merged into the local database.
    static QJsonObject snapshot();
    // `own`: this device's own copy, read back after its library was taken
    // off it (signed out); otherwise skipped.
    static int merge(const QJsonObject &snapshot, bool own = false);
    // One chapter's or episode's progress with its series, as live sync
    // sends it ({seriesId, series, unitKey, unit} in the snapshot's shapes),
    // or empty when there is none; and one such merged here when newer.
    static QJsonObject unitPayload(const QString &seriesId, const QString &unitKey);
    static bool mergeUnit(const QJsonObject &payload);
    // "android", "windows", "macos" or "linux".
    static QString platform();

signals:
    void changed();

private:
    explicit Sync(QObject *parent = nullptr);
    void schedule();
    std::unique_ptr<Store> currentStore() const;

    bool m_syncing = false;
    bool m_dirty = false;
    qint64 m_lastEnded = 0;
    QString m_error;
    QTimer m_soon;
    QTimer m_periodic;
    QPointer<Task> m_task;
};

} // namespace sc
