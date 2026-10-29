import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Scancore

// Other services: lists on AniList and MyAnimeList that follow what you
// finish, Discord showing what you read, and libraries brought in from
// Mihon, AniList or MyAnimeList.
SettingsPane {
    id: pane
    title: qsTr("Connections")
    subtitle: qsTr("Tracking and imports")
    iconName: "link"

    property var importTask: null
    property string importing: ""
    property var matchTask: null
    property var exportTask: null
    property bool showAllWaiting: false
    readonly property var waiting: Importer.unmatched

    function imported(task, what) {
        pane.importTask = task
        pane.importing = what
        task.finished.connect((r) => {
            const parts = [r.imported === 1 ? qsTr("1 series added") : r.imported + qsTr(" series added")]
            if (r.progress > 0) parts.push(r.progress + (r.progress === 1 ? qsTr(" chapter or episode") : qsTr(" chapters and episodes")) + qsTr(" marked"))
            if (r.unmatched > 0) parts.push(r.unmatched + qsTr(" waiting to be found"))
            if (r.downloadsLinked > 0) parts.push(qsTr("%1 downloaded chapters read from Mihon's folder").arg(r.downloadsLinked))
            if (r.extensionsInstalled > 0) parts.push(qsTr("%1 extensions installed").arg(r.extensionsInstalled))
            if (r.inSeveral > 0) parts.push(qsTr("%1 were in several lists and are in the first").arg(r.inSeveral))
            if (r.hiddenLists > 0) parts.push(qsTr("%1 hidden lists stay hidden").arg(r.hiddenLists))
            if (r.merged > 0) parts.push(qsTr("%1 merged entries came in as their main source").arg(r.merged))
            // Komikku / TachiyomiSY extras without a place here.
            if (r.customInfo > 0) parts.push(qsTr("edited titles, covers and descriptions of %1 series stay in the other app").arg(r.customInfo))
            if (r.savedSearches > 0) parts.push(qsTr("%1 saved searches stay in the other app").arg(r.savedSearches))
            Router.toast(parts.join(" · "), "success")
        })
        task.failed.connect((error) => Router.toast(error, "error"))
    }

    // ------------------------------------------------------------ tracking

    SettingsGroup {
        title: qsTr("Tracking")
        note: qsTr("Finishing a chapter or episode moves the series forward on every tracker you connect. Anime are matched by their AniList entry, comics and novels by their catalogue entry or title; link one by hand from its page.")

        // The catalogues the Comics, Novels and Anime pages browse and search.
        SettingRow {
            iconName: "compass"
            title: qsTr("Comics catalogue")
            description: qsTr("What the Comics page shows and searches. None searches your sources directly.")
            CataloguePicker { media: "comic" }
        }
        SettingRow {
            iconName: "compass"
            title: qsTr("Novels catalogue")
            description: qsTr("What the Novels page shows and searches.")
            CataloguePicker { media: "novel" }
        }
        SettingRow {
            visible: Trackers.malAvailable
            iconName: "compass"
            title: qsTr("Anime catalogue")
            description: qsTr("Whose lists the Anime page shows. Episodes, the schedule and search come from AniList either way.")
            CataloguePicker { media: "anime" }
        }
        Repeater {
            model: Trackers.services
            SettingRow {
                id: trackerRow
                required property var modelData
                readonly property var look: ({
                    anilist: ["list", Theme.dark ? "#3db4f2" : "#0b7fc0", qsTr("Keep your AniList lists up to date.")],
                    mal: ["bookmark", Theme.dark ? "#6d8fe0" : "#2e51a2", qsTr("Keep your MyAnimeList lists up to date.")],
                    comick: ["book-open", Theme.dark ? "#a78bfa" : "#7c3aed", qsTr("Keep your Comick library up to date, and bring it into SCANCORE.")],
                    kitsu: ["heart", Theme.dark ? "#f87171" : "#e0533b", qsTr("Comics, novels and anime on Kitsu.")],
                    mangaupdates: ["book-open", Theme.dark ? "#fb923c" : "#c2410c", qsTr("Your MangaUpdates reading list, for comics and novels.")],
                    shikimori: ["list", Theme.dark ? "#94a3b8" : "#475569", qsTr("Comics, novels and anime on Shikimori.")],
                    bangumi: ["list", Theme.dark ? "#f472b6" : "#db2777", qsTr("Comics, novels and anime on Bangumi.")],
                    simkl: ["play", Theme.dark ? "#a3a3a3" : "#262626", qsTr("Anime on Simkl.")]
                }[modelData.key] || (modelData.auth === "server"
                    ? ["hard-drive", Theme.textMuted, qsTr("Your server: series read from other sources move forward on it too. It is managed in Settings → Sources → Your servers.")]
                    : ["link", Theme.textMuted, ""]))
                iconName: look[0]
                iconTint: look[1]
                title: modelData.key === "comick" ? qsTr("Comick") : modelData.label
                description: modelData.auth === "server" ? look[2] : connector.status || look[2]
                TrackerConnect { id: connector; visible: trackerRow.modelData.auth !== "server"; service: trackerRow.modelData.key }
            }
        }
    }

    // ------------------------------------------------------------ Discord

    SettingsGroup {
        visible: Presence.available
        title: qsTr("Discord")
        note: qsTr("Shown while a reader or the player is open, through the Discord app on this computer.")
        SettingRow {
            iconName: "message-circle"
            iconTint: Theme.dark ? "#8c9eff" : "#5865f2"
            title: qsTr("Show what you read and watch on Discord")
            description: App.settings.discordPresence !== true ? qsTr("Off") : Presence.connected ? qsTr("Connected to Discord") : qsTr("Discord is not running")
            Toggle { checked: App.settings.discordPresence === true; onToggled: App.setSetting("discordPresence", checked) }
        }
        SettingRow {
            visible: App.settings.discordPresence === true
            iconName: "eye-off"
            title: qsTr("Keep titles private")
            description: qsTr("Shows “Reading a comic” without saying which.")
            Toggle { checked: App.settings.discordHideTitles === true; onToggled: App.setSetting("discordHideTitles", checked) }
        }
    }

    // ------------------------------------------------------------ imports

    SettingsGroup {
        title: qsTr("Bring your library")
        note: qsTr("Anime come in with the episodes you watched. Comics and novels come in with the chapters you read when one of your sources has them; the others wait below until they are found.")
        SettingRow {
            iconName: "book-open"
            title: qsTr("Mihon, Komikku, TachiyomiSY, Tachiyomi or Aniyomi backup")
            description: qsTr("A .tachibk file from More → Backup and restore (Komikku: More → Data and storage). Series from an extension you have here come back exactly as they were.")
            ScButton {
                text: qsTr("Choose file")
                iconName: "folder-open"
                loading: pane.importing === "mihon" && pane.importTask !== null && pane.importTask.loading
                enabled: !(pane.importTask && pane.importTask.loading)
                onClicked: mihonDialog.open()
            }
        }
        SettingRow {
            iconName: "download"
            title: qsTr("Chapters Mihon downloaded")
            description: mihonFolder.summary
            Row {
                spacing: 8
                IconButton {
                    visible: mihonFolder.linked
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "refresh-cw"
                    tip: qsTr("Look again")
                    enabled: !mihonFolder.linking
                    onClicked: Downloads.relinkMihon()
                }
                ScButton {
                    text: mihonFolder.linked ? qsTr("Add folder") : qsTr("Choose folder")
                    iconName: "folder-open"
                    loading: mihonFolder.pending !== "" && mihonFolder.linking
                    onClicked: mihonFolder.open()
                }
            }
        }
        SettingRow {
            iconName: "book-text"
            title: qsTr("LNReader backup")
            description: qsTr("A backup .zip from LNReader's More → Backup. Novels from a plugin you have here come back exactly as they were.")
            ScButton {
                text: qsTr("Choose file")
                iconName: "folder-open"
                loading: pane.importing === "lnreader" && pane.importTask !== null && pane.importTask.loading
                enabled: !(pane.importTask && pane.importTask.loading)
                onClicked: lnreaderDialog.open()
            }
        }
        SettingRow {
            iconName: "download"
            title: qsTr("Back up for Mihon")
            description: qsTr("Saves the comics you read from extensions as a .tachibk file Mihon or Tachiyomi can restore.")
            ScButton {
                text: qsTr("Save file")
                iconName: "download"
                loading: pane.exportTask !== null && pane.exportTask.loading
                onClicked: mihonSaveDialog.open()
            }
        }
        SettingRow {
            iconName: "list"
            title: "AniList"
            description: qsTr("Anime, comics and novels from a public AniList list.")
            Row {
                spacing: 8
                InlineField {
                    id: anilistUser
                    width: Theme.compact ? 160 : 190
                    placeholderText: qsTr("User name")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onAccepted: anilistImport.clicked()
                }
                ScButton {
                    id: anilistImport
                    text: qsTr("Import")
                    loading: pane.importing === "anilist" && pane.importTask !== null && pane.importTask.loading
                    enabled: anilistUser.text.trim().length > 0 && !(pane.importTask && pane.importTask.loading)
                    onClicked: pane.imported(Importer.importAniList(anilistUser.text), "anilist")
                }
            }
        }
        SettingRow {
            visible: Trackers.comickAvailable
            iconName: "book-open"
            title: qsTr("Comick")
            description: Trackers.comick.name ? qsTr("Everything ") + Trackers.comick.name + qsTr(" follows on Comick.")
                : qsTr("Connect Comick above to bring in what you follow there.")
            ScButton {
                text: qsTr("Import")
                enabled: !!Trackers.comick.name && !(pane.importTask && pane.importTask.loading)
                loading: pane.importing === "comick" && pane.importTask !== null && pane.importTask.loading
                onClicked: pane.imported(Importer.importComick(), "comick")
            }
        }
        SettingRow {
            iconName: "bookmark"
            title: "MyAnimeList"
            description: qsTr("The anime or manga list file from MyAnimeList's export page.")
            Row {
                spacing: 8
                ScButton {
                    text: qsTr("Export page")
                    iconName: "external-link"
                    variant: "ghost"
                    onClicked: App.openUrl("https://myanimelist.net/panel.php?go=export")
                }
                ScButton {
                    text: qsTr("Choose file")
                    iconName: "folder-open"
                    loading: pane.importing === "mal" && pane.importTask !== null && pane.importTask.loading
                    enabled: !(pane.importTask && pane.importTask.loading)
                    onClicked: malDialog.open()
                }
            }
        }
    }

    FileDialog {
        id: mihonDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Mihon, Komikku, TachiyomiSY, Tachiyomi or Aniyomi backup (*.tachibk *.proto.gz)"), qsTr("All files (*)")]
        onAccepted: {
            mihonImport.start(selectedFile)
            pane.importTask = mihonImport.task
            pane.importing = "mihon"
        }
    }
    MihonFolderPicker { id: mihonFolder }
    MihonImportDialog {
        id: mihonImport
        onImportStarted: (task) => pane.imported(task, "mihon")
    }
    FileDialog {
        id: lnreaderDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("LNReader backup (*.zip)"), qsTr("All files (*)")]
        onAccepted: pane.imported(Importer.importLNReader(selectedFile), "lnreader")
    }
    FileDialog {
        id: mihonSaveDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "tachibk"
        currentFile: "scancore.tachibk"
        nameFilters: [qsTr("Mihon or Tachiyomi backup (*.tachibk *.proto.gz)")]
        onAccepted: {
            pane.exportTask = Importer.exportMihon(selectedFile)
            pane.exportTask.finished.connect((r) => Router.toast(qsTr("Saved %1 series").arg(r.exported)
                + (r.skipped > 0 ? qsTr(" · %1 from built-in sources left out").arg(r.skipped) : ""), "success"))
            pane.exportTask.failed.connect((error) => Router.toast(error, "error"))
        }
    }
    FileDialog {
        id: malDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("MyAnimeList export (*.xml *.xml.gz *.gz)"), qsTr("All files (*)")]
        onAccepted: pane.imported(Importer.importMal(selectedFile), "mal")
    }

    // Titles from an import that no source had yet.
    SettingsGroup {
        visible: pane.waiting.length > 0
        title: qsTr("Waiting to be found · ") + pane.waiting.length
        note: qsTr("Adding a source in Settings → Sources, or a site coming back, can find more of them later.")
        SettingRow {
            iconName: "search"
            title: qsTr("Look for them on every source")
            description: pane.matchTask && pane.matchTask.loading ? qsTr("Searching… this can take a few minutes.")
                : qsTr("Adds the ones a source has under exactly the same title.")
            ScButton {
                variant: "primary"
                text: pane.matchTask && pane.matchTask.loading ? qsTr("Stop") : qsTr("Find them")
                onClicked: {
                    if (pane.matchTask && pane.matchTask.loading) {
                        pane.matchTask.cancel()
                        return
                    }
                    pane.matchTask = Importer.matchAll()
                    pane.matchTask.finished.connect((r) => Router.toast(r.matched > 0
                        ? qsTr("Found ") + r.matched + (r.left > 0 ? " · " + r.left + qsTr(" still waiting") : "") : qsTr("None found yet"), r.matched > 0 ? "success" : "info"))
                    pane.matchTask.failed.connect((error) => { if (error !== "Cancelled") Router.toast(error, "error") })
                }
            }
        }
        Repeater {
            model: pane.showAllWaiting ? pane.waiting : pane.waiting.slice(0, 6)
            SettingRow {
                required property var modelData
                iconName: modelData.media === "novel" ? "book-text" : modelData.media === "anime" ? "tv" : "book-open"
                title: modelData.title
                description: (modelData.media === "novel" ? qsTr("Novel") : modelData.media === "anime" ? qsTr("Anime") : qsTr("Comic"))
                    + (modelData.read > 0 ? " · " + modelData.read + (modelData.media === "anime" ? qsTr(" watched") : qsTr(" read")) : "")
                    + (modelData.source ? qsTr(" · from ") + modelData.source : "")
                IconButton {
                    iconName: "x"
                    tip: qsTr("Forget")
                    onClicked: Importer.forget(modelData.title)
                }
            }
        }
        Item {
            width: parent.width
            height: 56
            Rectangle { x: 16; width: parent.width - 16; height: 1; color: Theme.border }
            ScButton {
                visible: pane.waiting.length > 6 && !pane.showAllWaiting
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Show all ") + pane.waiting.length
                variant: "ghost"
                compact: true
                onClicked: pane.showAllWaiting = true
            }
            ScButton {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Forget all")
                variant: "ghost"
                compact: true
                onClicked: forgetConfirm.open()
            }
        }
    }

    ConfirmDialog {
        id: forgetConfirm
        heading: qsTr("Forget these titles?")
        message: "They stay wherever you imported them from; SCANCORE stops looking for them."
        confirmText: qsTr("Forget all")
        destructive: true
        onConfirmed: Importer.forgetAll()
    }
}
