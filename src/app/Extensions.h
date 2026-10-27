#pragma once

#include "core/Task.h"

#include <QObject>
#include <QQmlEngine>
#include <QSet>

#include <atomic>

namespace sc::ext {
struct Package;
}

namespace sc {

// Optional add-ons downloaded on request: the Extensions runtime (desktop),
// which runs Mihon extensions, and the extended novel source pack, a
// private Python and lightnovel-crawler (about 120 MB). Both go into the app
// data folder; nothing is installed system-wide.
class Extensions : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool novelPackAvailable READ novelPackAvailable CONSTANT)
    Q_PROPERTY(bool novelPackInstalled READ novelPackInstalled NOTIFY changed)
    // A runtime exists for this computer (desktop, x64 or Apple arm64).
    Q_PROPERTY(bool runtimeAvailable READ runtimeAvailable CONSTANT)
    // Extensions can run: the runtime is installed (or a development host
    // is built in).
    Q_PROPERTY(bool runtimeInstalled READ runtimeInstalled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // What is being installed: "runtime" or "novels".
    Q_PROPERTY(QString busyWith READ busyWith NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // Bumped whenever repositories, extensions or their sources change, so
    // bindings that list them read again.
    Q_PROPERTY(int revision READ revision NOTIFY extensionsChanged)
    // Extensions being installed or updated right now ("<kind>:<pkg>").
    Q_PROPERTY(QStringList installing READ installing NOTIFY extensionsChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY extensionsChanged)
    // When the repositories were last read for updates (seconds), or 0.
    Q_PROPERTY(qint64 checkedAt READ checkedAt NOTIFY extensionsChanged)
    // Extensions installing in the background (after adding a repository):
    // how many in all and how many are done; both 0 when none are.
    Q_PROPERTY(int queueTotal READ queueTotal NOTIFY queueChanged)
    Q_PROPERTY(int queueDone READ queueDone NOTIFY queueChanged)

public:
    static Extensions *instance();
    static Extensions *create(QQmlEngine *, QJSEngine *) { return instance(); }

    bool novelPackAvailable() const;
    bool novelPackInstalled() const;
    bool runtimeAvailable() const;
    bool runtimeInstalled() const;
    bool busy() const { return m_busy; }
    QString busyWith() const { return m_busy ? m_busyWith : QString(); }
    QString status() const { return m_status; }

    Q_INVOKABLE sc::Task *installNovelPack();
    Q_INVOKABLE void removeNovelPack();
    // Downloads (or updates) the Extensions runtime, about 100 MB.
    Q_INVOKABLE sc::Task *installRuntime();
    Q_INVOKABLE void removeRuntime();
    // Computers: what the extension host and its extensions wrote since it
    // last started (stack traces), as a file:// address, or "" before then.
    Q_INVOKABLE QString hostLog() const;
    // Extensions → Diagnostics: the runtime and host (its version, the
    // extension libraries it takes), the sites each extension contacted
    // this session ({package: [{host, requests, last}]}), the end of the
    // host's log and the system, as one report that can be copied.
    Q_INVOKABLE sc::Task *diagnostics();

    int revision() const { return m_revision; }
    QStringList installing() const;
    int updateCount() const;
    qint64 checkedAt() const;

    // ---- repositories and extensions (the Extensions page)
    // [{url, name, kind, media, website, domain, count, error}]
    Q_INVOKABLE QVariantList repositories() const;
    // The repository a link names: a one-tap link (scancore://, tachiyomi://,
    // mihon://, aniyomi://, lnreader://) or a plain address. {url, kind}, or
    // an empty map when `text` is no link at all.
    Q_INVOKABLE QVariantMap repositoryLink(const QString &text) const;
    // Reads a repository without adding it: {url, name, domain, kind, media,
    // count, languages: [{value, label, count}], autoInstall: [names],
    // language, languageName}.
    Q_INVOKABLE sc::Task *previewRepository(const QString &link, const QString &kind = QString());
    // Adds a previewed repository: {name, media, queued}. With `install`
    // its extensions in the content language install in the background
    // (queueTotal, installsFinished), so the app can be used meanwhile.
    Q_INVOKABLE sc::Task *addRepository(const QString &url, bool install = false);
    // The background installs still waiting are dropped.
    Q_INVOKABLE void stopInstalls();
    int queueTotal() const { return m_queueTotal; }
    int queueDone() const { return m_queueDone; }
    Q_INVOKABLE void removeRepository(const QString &url);
    Q_INVOKABLE sc::Task *refreshRepositories();

    // Installed extensions of one kind ("mihon", "aniyomi", "lnreader"; empty
    // for all): [{id, kind, media, pkg, name, version, lang, languageName,
    // icon, nsfw, trusted, problem, update, repo, signer, sources: [{key,
    // name, lang, languageName, preferred, baseUrl, enabled, error}]}]
    Q_INVOKABLE QVariantList installed(const QString &kind = QString()) const;
    // The same without each one's sources: their count (sourceCount), how
    // many fail (brokenCount) and their names and sites (sourceText, lower
    // case); one's sources by its id ("mihon:<pkg>") when it is opened.
    // With `query`: those whose name, sources' names or sites hold it. Pages
    // hand the list to a ListView as it comes: kept in a QML property, its
    // hundreds of rows are converted at every step.
    Q_INVOKABLE QVariantList installedBrief(const QString &kind = QString(), const QString &query = QString()) const;
    Q_INVOKABLE QVariantList installedSources(const QString &id) const;
    Q_INVOKABLE int installedCount(const QString &kind = QString()) const;
    // Extensions the repositories list that are not installed, content
    // language first: [{id, kind, media, pkg, name, version, lang,
    // languageName, icon, nsfw, problem, preferred}]
    Q_INVOKABLE QVariantList available(const QString &kind = QString()) const;
    // The same chosen by {query, language (its name), media, repo}; each
    // row says whether it starts its language (heading).
    Q_INVOKABLE QVariantList availableRows(const QVariantMap &filter) const;
    // How many are available and their languages: {count, languages}.
    Q_INVOKABLE QVariantMap availableSummary() const;
    Q_INVOKABLE sc::Task *install(const QString &kind, const QString &pkg);
    Q_INVOKABLE void uninstall(const QString &kind, const QString &pkg);
    Q_INVOKABLE void trust(const QString &kind, const QString &pkg);
    // Installs every update (trusted signers only): {installed}.
    Q_INVOKABLE sc::Task *updateAll();

    Q_INVOKABLE void setSourceEnabled(const QString &key, bool enabled);
    // Pinned sources come first wherever sources are listed (App::
    // pinnedFirst); an extension's pin is all of its sources'.
    Q_INVOKABLE void setPinned(const QString &key, bool pinned);
    Q_INVOKABLE void setExtensionPinned(const QString &id, bool pinned);
    // A source's own settings, as its extension describes them: [{key,
    // title, summary, type (switch, list, multiselect, text, info), entries,
    // values, value, enabled}]
    Q_INVOKABLE sc::Task *sourceSettings(const QString &key);
    Q_INVOKABLE sc::Task *setSourceSetting(const QString &key, const QString &setting, const QVariant &value);

    // The registries were rebuilt after extensions changed (main()).
    void notifyExtensionsChanged();

    // Sets up the extension host at start-up (main()); stopHost() at exit.
    void startHost();
    void stopHost();

signals:
    void changed();
    void extensionsChanged();
    void statusChanged(const QString &status);
    void queueChanged();
    // The background installs are done (or stopped).
    void installsFinished(int installed, int failed, const QString &firstError);

private:
    QVariantList installedRows(const QString &kind, bool brief, const QString &query = QString()) const;
    // Private so QML uses create(): Qt builds its own instance of a
    // singleton it can default-construct, and C++ and QML would then
    // work on different objects.
    explicit Extensions(QObject *parent = nullptr);

    void setStatus(const QString &status);
    void setInstalling(const QString &id, bool on);
    QSet<QString> m_installing;
    void installInBackground(const QList<ext::Package> &packages);
    void queueProgress();
    std::atomic<int> m_queueTotal{0};
    std::atomic<int> m_queueDone{0};
    std::atomic<int> m_queueBatches{0};
    std::atomic<bool> m_stopQueue{false};
    std::atomic<bool> m_progressPending{false};
    int m_revision = 0;
    mutable int m_updateCount = 0;
    mutable int m_updateCountAt = -1;
    bool m_busy = false;
    QString m_busyWith;
    QString m_status;
};

} // namespace sc
