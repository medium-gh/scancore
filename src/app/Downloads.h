#pragma once

#include "core/Http.h"
#include "core/Task.h"

#include <QObject>
#include <QQmlEngine>
#include <QHash>
#include <QSet>
#include <QTimer>

namespace sc {

// Offline copies of comic chapters (images), novel chapters (text) and anime
// episodes (video, with subtitles), kept in the app data folder and
// processed two at a time.
class Downloads : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int pending READ pending NOTIFY changed)
    Q_PROPERTY(QVariantMap mihon READ mihonStatus NOTIFY mihonChanged)

public:
    static Downloads *instance();
    static Downloads *create(QQmlEngine *, QJSEngine *) { return instance(); }

    int revision() const { return m_revision; }
    int pending() const;

    Q_INVOKABLE void enqueue(const QVariantMap &series, const QVariantList &units);
    // Only the queue, for SCANCORE to download when it next runs (Android's
    // check while it is closed); false when nothing could be kept.
    static bool queue(const QVariantMap &series, const QVariantList &units);
    Q_INVOKABLE sc::Task *items();
    Q_INVOKABLE QVariantMap states(const QString &seriesId) const;
    Q_INVOKABLE void remove(int id);
    Q_INVOKABLE void removeUnit(const QString &seriesId, const QString &unitKey);
    // A series' chapters from Mihon's folder stop counting as downloads;
    // the files stay.
    Q_INVOKABLE void unlinkSeries(const QString &seriesId);
    Q_INVOKABLE void retry(int id);
    Q_INVOKABLE void retryFailed();
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE QString folder() const;
    // The novel's downloaded chapters as an EPUB for e-readers: in the
    // computer's Downloads folder ({path}), or on Android shared through the
    // share sheet ({shared: true}). Fails when nothing is downloaded.
    Q_INVOKABLE sc::Task *exportEpub(const QString &seriesId);
    Q_INVOKABLE QString sizeOnDisk() const;

    // A downloaded episode as the player takes a video ({url, subtitles,
    // audio, skips, mpvArgs}), or an empty map.
    Q_INVOKABLE QVariantMap localVideo(const QString &seriesId, const QString &unitKey) const;

    // Chapters in the Mihon downloads folders among the folders chosen in
    // Settings (Mihon's storage folder or its downloads folder) count as
    // downloads of the same chapters of library entries, read where they
    // are; nothing is copied. Runs in the background (also at start and
    // when the folders change), then mihonLinked.
    Q_INVOKABLE void relinkMihon();
    // The same, on this thread: {series, chapters, folders, roots,
    // unmatchedSeries, unmatchedChapters}. Call linked() after it.
    static QVariantMap linkMihonNow();
    void linked(const QVariantMap &result);
    // The last result, with `linking` while one runs.
    QVariantMap mihonStatus() const;
    // Chapters linked to files outside the app's own folder.
    Q_INVOKABLE int linkedCount() const;
    // Whether a download row's files are the user's own (never deleted).
    static bool isExternal(const QString &path);

    static QVariantList localPanels(const QString &seriesId, const QString &unitKey);
    static QVariantMap localNovelChapter(const QString &seriesId, const QString &unitKey);
    static QString unitFolder(const QString &seriesId, const QString &unitKey);

signals:
    void changed();
    void finished(const QString &title);
    void mihonChanged();
    void mihonLinked(const QVariantMap &result);

private:
    // Private so QML uses create(): Qt builds its own instance of a
    // singleton it can default-construct, and C++ and QML would then
    // work on different objects.
    explicit Downloads(QObject *parent = nullptr);

    void pump();
    void runJob(const QVariantMap &row, const CancelFlag &flag);
    void notify();

    int m_revision = 0;
    QSet<int> m_running;
    // Each running download's cancel flag (removing one stops it).
    QHash<int, CancelFlag> m_flags;
    QTimer m_throttle;
    bool m_linking = false;
    bool m_linkAgain = false;
};

} // namespace sc
