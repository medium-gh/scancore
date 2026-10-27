import QtQuick
import QtQuick.Dialogs
import Scancore

SettingsPane {
    id: pane
    title: qsTr("Sources")
    subtitle: qsTr("The sites SCANCORE reads from")
    iconName: "globe"

    property var refreshTask: null
    // Built-in comic sources only (extensions' have their own page), and
    // every key for the sources gone from under saved series.
    readonly property var allComicSources: { App.dependOn(Extensions.revision); return Comics.builtinSources() }
    readonly property var comicKeys: Comics.sourceKeys()
    readonly property var allNovelSources: { App.dependOn(Extensions.novelPackInstalled, Extensions.revision); return Novels.sources() }
    // Built-in sources only: extensions have their own page (above), and a
    // row each for hundreds of extension sources made this page crawl.
    readonly property var comicSources: savedFirst(allComicSources.filter(s => !s.extension), comicCounts)
    readonly property var novelSources: savedFirst(allNovelSources.filter(s => !s.extended && !s.extension), novelCounts)
    readonly property int extendedCount: allNovelSources.filter(s => s.extended).length

    // A few rows per list until the reader asks for all or searches: every
    // row is a few dozen items, all made when the page opens.
    readonly property int collapsedRows: 12
    property string sourceQuery: ""
    property bool allComics: false
    property bool allNovels: false
    // Sources with saved series first, then those switched on, otherwise in
    // the registry's order (the sort here is not stable, hence the index).
    function savedFirst(list, counts) {
        const rank = (s) => (s.pinned ? -4 : 0) + ((counts[s.key] || 0) > 0 ? 0 : 2) + (s.enabled ? 0 : 1)
        return list.map((s, i) => ({ s: s, i: i, r: rank(s) }))
            .sort((a, b) => a.r - b.r || a.i - b.i)
            .map(x => x.s)
    }
    function shown(list, all) {
        const q = sourceQuery.trim().toLowerCase()
        if (q.length)
            return list.filter(s => s.label.toLowerCase().indexOf(q) >= 0 || String(s.base).toLowerCase().indexOf(q) >= 0
                                    || String(s.languageName || "").toLowerCase().indexOf(q) >= 0)
        return all ? list : list.slice(0, collapsedRows)
    }

    // Saved series per source, to move them elsewhere.
    readonly property var comicCounts: { App.dependOn(Library.revision); return Library.sourceCounts("comic") }
    readonly property var novelCounts: { App.dependOn(Library.revision); return Library.sourceCounts("novel") }
    // Sources the library still has series on that are gone (switched off
    // for everyone, or their extension removed).
    readonly property var goneSources: {
        const out = []
        const known = new Set(comicKeys.concat(allNovelSources.map(s => s.key)))
        for (const [media, counts] of [["comic", comicCounts], ["novel", novelCounts]])
            for (const key of Object.keys(counts))
                if (!known.has(key) && key !== "local") {
                    // Named by its site when its links say which.
                    const link = String((Library.seriesOn(media, key)[0] || {}).sourceUrl || "")
                    const host = (link.match(/^https?:\/\/(?:www\.)?([^\/]+)/) || [])[1]
                    out.push({ media: media, key: key, label: host || key, count: counts[key] })
                }
        return out
    }

    function moveFrom(media, key, label) {
        migrate.start(Library.seriesOn(media, key).map(r => Object.assign({ sourceLabel: label }, r)))
    }

    MigrateDialog { id: migrate }

    function refresh() {
        refreshTask = App.refreshSources()
        refreshTask.finished.connect((r) => Router.toast(r.changed ? qsTr("Sources updated to version ") + r.version : qsTr("Sources are up to date"), "success"))
        refreshTask.failed.connect((error) => Router.toast(error, "error"))
    }

    component SourceRow: SettingRow {
        id: sourceRow
        required property var modelData
        property string media: "comic"
        readonly property int saved: (media === "novel" ? pane.novelCounts : pane.comicCounts)[modelData.key] || 0
        title: modelData.label + (modelData.nsfw ? "  · 18+" : "")
        description: modelData.base.replace("https://", "") + (modelData.languageName ? " · " + modelData.languageName : "")
            + (modelData.enabled ? "" : " · " + modelData.disabledReason)
        Row {
            spacing: 4
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: modelData.enabled ? Theme.success : Theme.textFaint
            }
            Item { width: 6; height: 1 }
            ScButton {
                visible: sourceRow.saved > 0
                anchors.verticalCenter: parent.verticalCenter
                compact: true
                variant: "ghost"
                iconName: "arrow-right-left"
                text: qsTr("Move %1").arg(sourceRow.saved)
                onClicked: pane.moveFrom(sourceRow.media, modelData.key, modelData.label)
            }
            // Files on this device have no site to check or open.
            IconButton {
                visible: App.platform !== "android" && modelData.key !== "local"
                iconName: "shield-check"
                tip: qsTr("Pass a browser check for this site")
                onClicked: Router.openSiteCheck(modelData.base + "/")
            }
            IconButton { visible: modelData.key !== "local"; iconName: "external-link"; tip: qsTr("Open site"); onClicked: App.openUrl(modelData.base) }
            IconButton {
                iconName: "pin"
                active: modelData.pinned === true
                tip: modelData.pinned ? qsTr("Unpin") : qsTr("Pin: it comes first")
                onClicked: Extensions.setPinned(modelData.key, modelData.pinned !== true)
            }
        }
    }

    // The reader's own Komga, Kavita, Suwayomi, OPDS and Jellyfin servers.
    ServerDialog { id: serverDialog; onClosed: pane.serversChanged() }
    signal serversChanged()
    SettingsGroup {
        title: qsTr("Your servers")
        note: qsTr("Komga, Kavita and Suwayomi servers you run, and OPDS catalogues: their comics and books are a source like the others, searched with them and saved to the library. A Jellyfin server's shows and films play under Anime.")
        Repeater {
            // A handful, as many as the reader runs.
            model: { App.dependOn(pane.serversChanged); return Comics.servers() }
            SettingRow {
                required property var modelData
                iconName: "server"
                title: modelData.label
                description: (modelData.kind === "opds" ? "OPDS" : modelData.kind.charAt(0).toUpperCase() + modelData.kind.slice(1)) + " · " + modelData.url
                Row {
                    spacing: 4
                    IconButton { iconName: "pencil"; tip: qsTr("Change"); onClicked: serverDialog.start(modelData) }
                    IconButton { iconName: "external-link"; tip: qsTr("Open in the browser"); onClicked: App.openUrl(modelData.url) }
                    IconButton {
                        iconName: "trash"
                        tip: qsTr("Remove")
                        onClicked: {
                            Comics.removeServer(modelData.id)
                            pane.serversChanged()
                        }
                    }
                }
            }
        }
        SettingRow {
            iconName: "plus"
            title: qsTr("Add your server")
            ScButton { text: qsTr("Add"); onClicked: serverDialog.start(null) }
        }
    }

    // Extensions from the repositories the user added.
    readonly property var extensionRepos: { App.dependOn(Extensions.revision); return Extensions.repositories() }
    readonly property int extensionCount: { App.dependOn(Extensions.revision); return Extensions.installedCount() }
    SettingsGroup {
        title: qsTr("Extensions")
        note: qsTr("SCANCORE runs Mihon, Tachiyomi, Aniyomi, CloudStream, Echo and LNReader extensions from repositories you add. It has no repository of its own.")
        SettingRow {
            iconName: "puzzle"
            title: qsTr("Extensions")
            description: pane.extensionCount + qsTr(" installed from ") + pane.extensionRepos.length
                + (pane.extensionRepos.length === 1 ? qsTr(" repository") : qsTr(" repositories"))
                + (Extensions.updateCount > 0 ? " · " + Extensions.updateCount + qsTr(" updates") : "")
            clickable: true
            onClicked: Router.openExtensions()
        }
        SettingRow {
            iconName: "plus"
            title: qsTr("Add a repository")
            description: qsTr("Paste a repository link, or open one from a repository's page with SCANCORE.")
            ScButton { text: qsTr("Add"); onClicked: Router.addRepository("") }
        }
        SettingRow {
            iconName: "gauge"
            title: qsTr("Diagnostics")
            description: qsTr("Whether the extensions runtime answers, a quick test of a source, and the sites each extension contacted.")
            ScButton { text: qsTr("Open"); variant: "ghost"; onClicked: Router.openExtensionsDiagnostics() }
        }
        // For bug reports: what extensions reported (computers).
        SettingRow {
            visible: App.platform !== "android" && pane.hostLog !== ""
            iconName: "file-text"
            title: qsTr("Extension log")
            description: qsTr("What the extensions reported since they last started, errors included. Attach it to a bug report.")
            ScButton { text: qsTr("Open"); variant: "ghost"; onClicked: App.openUrl(pane.hostLog) }
        }
        SettingRow {
            iconName: "refresh-cw"
            title: qsTr("Update extensions automatically")
            description: qsTr("Installs updates signed by keys you already trust when SCANCORE checks its repositories.")
            Toggle { checked: App.settings.autoUpdateExtensions !== false; onToggled: App.setSetting("autoUpdateExtensions", checked) }
        }
    }

    // Comics and books on this device: the folders listed and searched.
    readonly property var folders: Array.from(App.settings.localFolders || [])
    readonly property string hostLog: { App.dependOn(Extensions.revision); return Extensions.hostLog() }
    SettingsGroup {
        title: qsTr("On this device")
        note: qsTr("Each folder or file in these folders is a series: folders of images, CBZ, CBR, 7z and PDF files for comics, and EPUB, FB2 and MOBI books for novels. ")
            + qsTr("Comics and Novels list them under On this device. CBR and 7z files need 7-Zip, unrar or bsdtar installed.")
            + " " + qsTr("Mihon's storage folder works too: the chapters it downloaded open from your library.")
        Repeater {
            model: pane.folders
            SettingRow {
                required property string modelData
                iconName: "folder-open"
                // Android: "content://…/tree/primary%3AMihon" is "Mihon".
                title: (/^content:/.test(modelData) ? decodeURIComponent(modelData.replace(/^.*\/(tree|document)\//, "")).replace(/^[^:]*:/, "")
                                                    : modelData).replace(/^.*[\/\\]/, "") || modelData
                // Mihon's folder: its chapters count as downloads (Downloads.relinkMihon).
                description: (Downloads.mihon.roots || {})[modelData] ? qsTr("Mihon's downloads · ") + modelData : modelData
                IconButton {
                    iconName: "x"
                    tip: qsTr("Stop listing this folder")
                    onClicked: App.setSetting("localFolders", pane.folders.filter(f => f !== modelData))
                }
            }
        }
        SettingRow {
            iconName: "plus"
            title: pane.folders.length ? qsTr("Add another folder") : qsTr("Add a folder")
            description: qsTr("Nothing is copied: SCANCORE reads the files where they are.")
            Row {
                spacing: 8
                ScButton { text: qsTr("Add folder"); iconName: "folder-open"; onClicked: localFolderDialog.open() }
                ScButton { text: qsTr("Open a file"); variant: "ghost"; onClicked: localFileDialog.open() }
            }
        }
    }
    FolderDialog {
        id: localFolderDialog
        onAccepted: {
            const path = App.localPath(selectedFolder)
            if (pane.folders.indexOf(path) < 0) App.setSetting("localFolders", pane.folders.concat([path]))
        }
    }
    FileDialog {
        id: localFileDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Comics and books (*.cbz *.zip *.cbr *.rar *.cb7 *.7z *.pdf *.epub *.fb2 *.mobi *.azw3)"), qsTr("All files (*)")]
        onAccepted: Router.openFile(selectedFile)
    }

    SettingsGroup {
        title: qsTr("Source list")
        note: qsTr("Site addresses and availability come from the SCANCORE repository, so a moved or broken site can be fixed without a new release.")
        SettingRow {
            iconName: "refresh-cw"
            title: qsTr("Version ") + App.manifestVersion
            description: pane.comicSources.length + qsTr(" comic and ") + pane.novelSources.length + qsTr(" novel sources built in.")
            ScButton {
                text: qsTr("Check now")
                loading: pane.refreshTask ? pane.refreshTask.loading : false
                onClicked: pane.refresh()
            }
        }
        SettingRow {
            iconName: "globe"
            title: qsTr("Source languages")
            description: qsTr("Built-in sources in these languages are on. Series you already read keep opening whatever you choose.")
        }
        // Built after the page's first frame: dozens of chips held it back.
        Loader {
            x: 16
            width: parent.width - 32
            asynchronous: true
            sourceComponent: languageChips
        }
        Component {
            id: languageChips
            Flow {
                width: parent ? parent.width : 0
                bottomPadding: 14
                spacing: 8
                Repeater {
                    model: { App.dependOn(App.manifestVersion, App.settings.builtinLanguages); return App.builtinLanguages() }
                    Chip {
                        required property var modelData
                        text: modelData.label + " · " + modelData.count
                        selected: modelData.on
                        onClicked: {
                            const on = App.builtinLanguages().filter(l => l.on).map(l => l.value)
                            const next = modelData.on ? on.filter(v => v !== modelData.value) : on.concat([modelData.value])
                            // At least one language stays on.
                            if (next.length > 0) App.setSetting("builtinLanguages", next)
                        }
                    }
                }
            }
        }
        SettingRow {
            iconName: "languages"
            title: qsTr("Languages for “All sources”")
            description: qsTr("Which comic sources “All sources” searches and finds series on. Each source can still be picked on its own.")
            Select {
                model: [{ value: "", label: qsTr("All languages") }].concat(Comics.languages())
                value: App.settings.sourceLanguages || ""
                onPicked: (v) => App.setSetting("sourceLanguages", v)
            }
        }
        SettingRow {
            iconName: "eye"
            title: qsTr("Show adult sources")
            description: qsTr("Lists sources marked 18+ in the comic source picker.")
            Toggle { checked: App.settings.showNsfwSources === true; onToggled: App.setSetting("showNsfwSources", checked) }
        }
    }

    // The Java runtime extensions run in on a computer (phones run them
    // themselves).
    SettingsGroup {
        visible: App.platform !== "android" && Extensions.runtimeAvailable
        title: qsTr("Extensions")
        SettingRow {
            iconName: "download"
            title: qsTr("Extensions runtime")
            description: Extensions.busyWith === "runtime" ? Extensions.status
                : Extensions.runtimeInstalled ? qsTr("Installed. Extensions from the repositories you add run in it.")
                : qsTr("About 100 MB. Needed to run extensions on this computer; SCANCORE downloads it into the app data folder.")
            Row {
                spacing: 8
                ScButton {
                    visible: !Extensions.runtimeInstalled
                    variant: "primary"
                    text: qsTr("Install")
                    iconName: "download"
                    loading: Extensions.busyWith === "runtime"
                    enabled: !Extensions.busy
                    onClicked: {
                        const task = Extensions.installRuntime()
                        task.finished.connect(() => Router.toast(qsTr("Extensions runtime installed"), "success"))
                        task.failed.connect((error) => Router.toast(error, "error"))
                    }
                }
                ScButton {
                    visible: Extensions.runtimeInstalled
                    text: qsTr("Remove")
                    variant: "ghost"
                    enabled: !Extensions.busy
                    onClicked: Extensions.removeRuntime()
                }
            }
        }
    }

    SettingsGroup {
        visible: App.platform !== "android"
        title: qsTr("Extended novel sources")
        SettingRow {
            iconName: "book-text"
            title: !Extensions.novelPackAvailable ? qsTr("Not available in this build")
                : Extensions.novelPackInstalled ? pane.extendedCount + qsTr(" more novel sites installed")
                : qsTr("About 260 more novel sites")
            description: !Extensions.novelPackAvailable ? qsTr("The downloadable Python novel-source pack is turned off in this build.")
                : Extensions.busyWith === "novels" ? Extensions.status
                : Extensions.novelPackInstalled ? "From lightnovel-crawler. Pick them from “More sources” on the Novels page; pasted book links from any of them open directly."
                : qsTr("Uses lightnovel-crawler. Downloads a private copy of Python and the crawler (about 400 MB) into the app data folder.")
                  + (Extensions.status ? qsTr(" Last attempt: ") + Extensions.status : "")
            Row {
                spacing: 8
                ScButton {
                    visible: Extensions.novelPackAvailable && !Extensions.novelPackInstalled
                    variant: "primary"
                    text: qsTr("Install")
                    iconName: "download"
                    loading: Extensions.busyWith === "novels"
                    enabled: !Extensions.busy
                    onClicked: {
                        const task = Extensions.installNovelPack()
                        task.finished.connect(() => Router.toast(qsTr("Extended novel sources installed"), "success"))
                        task.failed.connect((error) => Router.toast(error, "error"))
                    }
                }
                ScButton {
                    visible: Extensions.novelPackAvailable && Extensions.novelPackInstalled
                    text: qsTr("Remove")
                    variant: "ghost"
                    onClicked: Extensions.removeNovelPack()
                }
            }
        }
    }

    // Series left on sources that are gone.
    SettingsGroup {
        visible: pane.goneSources.length > 0
        title: qsTr("No longer available")
        note: qsTr("Your progress on these series is kept. Move them to a source you have to read new chapters.")
        Repeater {
            model: pane.goneSources
            SettingRow {
                required property var modelData
                iconName: "triangle-alert"
                iconTint: Theme.warning
                title: modelData.label
                description: (modelData.media === "novel" ? qsTr("Novels") : qsTr("Comics")) + " · "
                    + (modelData.count === 1 ? qsTr("1 series") : qsTr("%1 series").arg(modelData.count))
                ScButton {
                    compact: true
                    iconName: "arrow-right-left"
                    text: qsTr("Move them")
                    onClicked: pane.moveFrom(modelData.media, modelData.key, modelData.label)
                }
            }
        }
    }

    SearchField {
        width: parent.width
        placeholderText: qsTr("Find a source")
        text: pane.sourceQuery
        onTextChanged: pane.sourceQuery = text
    }

    SettingsGroup {
        id: comicGroup
        readonly property var visibleSources: pane.shown(pane.comicSources, pane.allComics)
        visible: visibleSources.length > 0
        title: qsTr("Comics")
        note: App.platform !== "android" ? "If a site asks for a browser check, the shield opens it inside SCANCORE once; the app then reuses the clearance." : ""
        Repeater {
            model: comicGroup.visibleSources
            SourceRow {}
        }
        SettingRow {
            visible: !pane.sourceQuery.trim().length && pane.comicSources.length > pane.collapsedRows
            clickable: true
            title: pane.allComics ? qsTr("Show fewer") : qsTr("Show all %1 comic sources").arg(pane.comicSources.length)
            onClicked: pane.allComics = !pane.allComics
        }
    }

    SettingsGroup {
        id: novelGroup
        readonly property var visibleSources: pane.shown(pane.novelSources, pane.allNovels)
        visible: visibleSources.length > 0
        title: qsTr("Novels")
        Repeater {
            model: novelGroup.visibleSources
            SourceRow { media: "novel" }
        }
        SettingRow {
            visible: !pane.sourceQuery.trim().length && pane.novelSources.length > pane.collapsedRows
            clickable: true
            title: pane.allNovels ? qsTr("Show fewer") : qsTr("Show all %1 novel sources").arg(pane.novelSources.length)
            onClicked: pane.allNovels = !pane.allNovels
        }
    }

    Text {
        visible: pane.sourceQuery.trim().length > 0 && !comicGroup.visible && !novelGroup.visible
        width: parent.width
        text: qsTr("No built-in source matches. Extension sources are on the Extensions page.")
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }
}
