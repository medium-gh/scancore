#pragma once

#include <functional>

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace sc {

// Checks GitHub releases and updates the app in place:
// - Linux AppImage: downloads the new AppImage, swaps it over the running file
//   and restarts.
// - Windows: downloads the installer and runs it silently; it relaunches the app.
// - macOS: downloads the disk image and replaces the app bundle after quitting.
// - Android: downloads the APK and opens the system installer.
// Builds that cannot replace themselves (for example from source or a distro
// package) only offer the release page.
class Updater : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString state READ state NOTIFY changed)        // idle checking current available downloading ready error
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString pageUrl READ pageUrl NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(bool managedByFlatpak READ managedByFlatpak CONSTANT)
    Q_PROPERTY(qint64 size READ size NOTIFY changed)

public:
    static Updater *instance();
    static Updater *create(QQmlEngine *, QJSEngine *) { return instance(); }

    QString state() const { return m_state; }
    QString version() const { return m_version; }
    QString notes() const { return m_notes; }
    QString pageUrl() const { return m_pageUrl; }
    QString error() const { return m_error; }
    double progress() const { return m_progress; }
    bool canInstall() const;
    bool managedByFlatpak() const;
    qint64 size() const { return m_size; }

    // quiet: no "up to date" state change when nothing is newer.
    Q_INVOKABLE void check(bool quiet = false);
    Q_INVOKABLE void download();
    Q_INVOKABLE void install();
    Q_INVOKABLE void dismiss();

    void startAutomatic();

signals:
    void changed();
    void progressChanged();

private:
    // Checks the downloaded file against the release's signature, then
    // `ready`; a file that does not match is deleted.
    void checkSignature(const QString &path, const std::function<void()> &ready);
    // Private so QML uses create(): Qt builds its own instance of a
    // singleton it can default-construct, and C++ and QML would then
    // work on different objects.
    explicit Updater(QObject *parent = nullptr);

    void setState(const QString &state, const QString &error = QString());
    QString assetSuffix() const;
    QString downloadPath() const;
    void installAndroid(const QString &apk);

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile *m_file = nullptr;
    QString m_state = QStringLiteral("idle");
    QString m_version;
    QString m_notes;
    QString m_pageUrl;
    QString m_assetUrl;
    // The asset's Ed25519 signature (<asset>.sig on the release).
    QString m_signatureUrl;
    QString m_error;
    qint64 m_size = 0;
    double m_progress = 0;
    bool m_quiet = false;
    QTimer m_timer;
};

} // namespace sc
