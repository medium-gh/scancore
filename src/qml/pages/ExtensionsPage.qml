import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Repositories the user added, the extensions installed from them (trust,
// updates, their sources, settings and health) and the ones they offer.
Item {
    id: page
    // "installed", "available" or "repositories"
    property string tab: "installed"
    property string query: ""
    property string language: ""
    // Available: one kind (comic, novel, anime) and one repository, or all.
    property string media: ""
    property string repo: ""
    property var expanded: ({})
    property var runtimeTask: null
    property var updateTask: null
    property var refreshTask: null

    readonly property var repositories: { App.dependOn(Extensions.revision); return Extensions.repositories() }
    readonly property int installedCount: { App.dependOn(Extensions.revision); return Extensions.installedCount() }
    // Available: how many and in which languages (the rows come from rows()).
    readonly property var availableInfo: {
        App.dependOn(Extensions.revision, App.settings.showNsfwSources)
        return page.tab === "available" ? Extensions.availableSummary() : ({ count: 0, languages: [] })
    }
    readonly property var availableLanguages: [{ value: "", label: qsTr("All languages") }]
        .concat(Array.from(availableInfo.languages).map(l => ({ value: l, label: l })))
    // The kinds the added repositories offer, for the kind chips.
    readonly property var repoMedia: {
        const seen = []
        for (const r of repositories) if (seen.indexOf(r.media) < 0) seen.push(r.media)
        return seen
    }
    readonly property var repoChoices: [{ value: "", label: qsTr("All repositories") }].concat(repositories.map(r => ({ value: r.url, label: r.name })))
    // A repository's own page: what it offers.
    function openRepository(url) {
        repo = url
        media = ""
        language = ""
        tab = "available"
    }
    // The list's rows, straight from Extensions to the ListView: hundreds
    // of them (installed, or what the repositories offer), and kept in a
    // property they would be converted at every step. Installed ones are
    // found by the extension's name, its sources' names or their sites.
    function rows() {
        if (tab === "installed") return Extensions.installedBrief("", query)
        if (tab === "available") return Extensions.availableRows({ query: query, language: language, media: media, repo: repo })
        return Extensions.repositories()
    }
    // New rows (an extension installed or updated, a search) on the same
    // tab keep the list where it was; another tab starts at the top.
    property string shownTab: ""
    readonly property var rowsWanted: [tab, query, language, media, repo, Extensions.revision, App.settings.showNsfwSources]
    function showRows() {
        const keep = shownTab === tab
        const y = list.contentY
        shownTab = tab
        list.model = rows()
        if (keep) Qt.callLater(() => list.contentY = Math.max(list.originY, Math.min(y, list.originY + list.contentHeight - list.height)))
    }
    onRowsWantedChanged: Qt.callLater(showRows)
    Component.onCompleted: showRows()
    readonly property bool needsRuntime: App.platform !== "android" && Extensions.runtimeAvailable && !Extensions.runtimeInstalled

    function installing(id) { return Extensions.installing.indexOf(id) >= 0 }
    function toggle(id) {
        const e = Object.assign({}, expanded)
        e[id] = !e[id]
        expanded = e
    }
    function install(item) {
        const task = Extensions.install(item.kind, item.pkg)
        task.finished.connect((r) => Router.toast(r.name + (r.trusted ? qsTr(" installed") : qsTr(" installed. Trust it under Installed to use it.")), "success"))
        task.failed.connect((e) => Router.toast(e, "error"))
    }

    ConfirmDialog {
        id: removeDialog
        property var item: null
        heading: item ? qsTr("Remove ") + item.name + "?" : ""
        message: qsTr("Its sources disappear from SCANCORE. Series in your library stay, and come back when you install it again.")
        confirmText: qsTr("Remove")
        destructive: true
        onConfirmed: Extensions.uninstall(item.kind, item.pkg)
    }
    ConfirmDialog {
        id: removeRepoDialog
        property var repo: null
        heading: repo ? qsTr("Remove “") + repo.name + qsTr("”?") : ""
        message: qsTr("Installed extensions stay installed, but they no longer get updates from it, and SCANCORE asks before trusting them again.")
        confirmText: qsTr("Remove")
        destructive: true
        onConfirmed: Extensions.removeRepository(repo.url)
    }
    ConfirmDialog {
        id: trustDialog
        property var item: null
        heading: item ? qsTr("Trust ") + item.name + "?" : ""
        message: item ? qsTr("It is not signed by a repository you added (key ") + item.signer + qsTr("…). Extensions run code from the internet on this device; trust only ones you know.") : ""
        confirmText: qsTr("Trust")
        onConfirmed: Extensions.trust(item.kind, item.pkg)
    }
    SourceSettingsDialog { id: settingsDialog }

    Connections {
        target: page.updateTask
        function onFinished(result) { Router.toast(result.installed + (result.installed === 1 ? qsTr(" extension updated") : qsTr(" extensions updated")), "success") }
        function onFailed(error) { Router.toast(error, "error") }
    }
    Connections {
        target: page.refreshTask
        function onFinished(count) { Router.toast(count > 0 ? count + (count === 1 ? qsTr(" update available") : qsTr(" updates available")) : qsTr("Everything is up to date"), "success") }
        function onFailed(error) { Router.toast(error, "error") }
    }

    ListView {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        WheelScroll {}
        id: list
        anchors.fill: parent
        clip: true
        bottomMargin: 40
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        cacheBuffer: 400
        ScrollBar.vertical: ScScrollBar {}
        header: Column {
            width: list.width
            spacing: 14
            PageHeader {
                title: qsTr("Extensions")
                showBack: true
                subtitle: page.installedCount + (page.installedCount === 1 ? qsTr(" installed") : qsTr(" installed"))
                    + " · " + page.repositories.length + (page.repositories.length === 1 ? qsTr(" repository") : qsTr(" repositories"))
                ScButton { text: qsTr("Add repository"); iconName: "plus"; variant: "primary"; onClicked: Router.addRepository("") }
                IconButton { iconName: "gauge"; filled: true; tip: qsTr("Diagnostics"); onClicked: Router.openExtensionsDiagnostics() }
            }

            // Updates, where they are looked for: what is ready, or when
            // the repositories were last checked, and a labelled button.
            Rectangle {
                visible: page.tab === "installed" && page.installedCount > 0
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                height: visible ? updatesRow.implicitHeight + 24 : 0
                radius: Theme.radiusLg
                color: Extensions.updateCount > 0 ? Theme.alpha(Theme.accent, 0.08) : Theme.surface
                border.width: 1
                border.color: Extensions.updateCount > 0 ? Theme.alpha(Theme.accent, 0.3) : Theme.border
                Row {
                    id: updatesRow
                    x: 14
                    y: 12
                    width: parent.width - 28
                    spacing: 12
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: Extensions.updateCount > 0 ? "download" : "check"
                        size: 20
                        color: Extensions.updateCount > 0 ? Theme.accent : Theme.success
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 32 - updatesButton.width - 12
                        spacing: 2
                        Text {
                            width: parent.width
                            text: Extensions.updateCount > 0
                                ? (Extensions.updateCount === 1 ? qsTr("1 update ready") : qsTr("%1 updates ready").arg(Extensions.updateCount))
                                : qsTr("Extensions are up to date")
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textSm
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }
                        Text {
                            width: parent.width
                            text: (page.refreshTask && page.refreshTask.loading ? qsTr("Checking the repositories…")
                                    : Extensions.checkedAt > 0 ? qsTr("Checked %1").arg(App.relativeTime(Extensions.checkedAt)) : qsTr("Not checked yet"))
                                + (App.settings.autoUpdateExtensions !== false ? " · " + qsTr("trusted ones update by themselves") : "")
                            color: Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textXs
                            wrapMode: Text.Wrap
                        }
                    }
                    ScButton {
                        id: updatesButton
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        variant: Extensions.updateCount > 0 ? "primary" : "secondary"
                        text: Extensions.updateCount > 0 ? qsTr("Update all") : qsTr("Check for updates")
                        iconName: Extensions.updateCount > 0 ? "download" : "refresh-cw"
                        loading: Extensions.updateCount > 0 ? (page.updateTask ? page.updateTask.loading : false)
                                                            : (page.refreshTask ? page.refreshTask.loading : false)
                        onClicked: {
                            if (Extensions.updateCount > 0) page.updateTask = Extensions.updateAll()
                            else page.refreshTask = Extensions.refreshRepositories()
                        }
                    }
                }
            }

            // Desktop: extensions run in a separate download.
            Rectangle {
                visible: page.needsRuntime
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                height: runtimeRow.implicitHeight + 28
                radius: Theme.radiusLg
                color: Theme.alpha(Theme.accent, 0.08)
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.3)
                Row {
                    id: runtimeRow
                    x: 16
                    y: 14
                    width: parent.width - 32
                    spacing: 14
                    Icon { name: "package"; size: 20; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        width: parent.width - 34 - runtimeButton.width - 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: Extensions.busyWith === "runtime" ? Extensions.status
                            : qsTr("Extensions run in the Extensions runtime, a separate download of about 100 MB.")
                              + (Extensions.status ? " " + Extensions.status : "")
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    ScButton {
                        id: runtimeButton
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Install")
                        variant: "primary"
                        loading: Extensions.busyWith === "runtime"
                        enabled: !Extensions.busy
                        onClicked: page.runtimeTask = Extensions.installRuntime()
                    }
                }
            }

            Segmented {
                x: Theme.gutter
                visible: page.repositories.length > 0 || page.installedCount > 0
                options: [
                    { value: "installed", label: qsTr("Installed") + (Extensions.updateCount ? " · " + Extensions.updateCount : "") },
                    { value: "available", label: qsTr("Available") },
                    { value: "repositories", label: qsTr("Repositories") }
                ]
                value: page.tab
                onPicked: (v) => page.tab = v
            }

            OutsideList {
                view: list
                outside: page
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                height: 40
                visible: (page.tab === "available" && page.availableInfo.count > 0) || (page.tab === "installed" && page.installedCount > 0)
                Row {
                    width: parent.width
                    spacing: 10
                    SearchField {
                        width: parent.width - (languagePick.visible ? languagePick.width + 10 : 0)
                        placeholderText: page.tab === "installed" ? qsTr("Find an installed extension or site") : qsTr("Find an extension")
                        onTextChanged: page.query = text
                    }
                    Select {
                        id: languagePick
                        visible: page.tab === "available"
                        model: page.availableLanguages
                        value: page.language
                        onPicked: (v) => page.language = v
                    }
                }
            }

            // Available: by kind, repository and 18+.
            ChipRow {
                visible: page.tab === "available" && page.repositories.length > 0
                width: list.width
                Chip {
                    visible: page.repoMedia.length > 1
                    text: qsTr("All kinds")
                    selected: page.media === ""
                    onClicked: page.media = ""
                }
                Chip {
                    visible: page.repoMedia.length > 1 && page.repoMedia.indexOf("comic") >= 0
                    text: qsTr("Comics")
                    iconName: "book-open"
                    selected: page.media === "comic"
                    onClicked: page.media = "comic"
                }
                Chip {
                    visible: page.repoMedia.length > 1 && page.repoMedia.indexOf("novel") >= 0
                    text: qsTr("Novels")
                    iconName: "book-text"
                    selected: page.media === "novel"
                    onClicked: page.media = "novel"
                }
                Chip {
                    visible: page.repoMedia.length > 1 && page.repoMedia.indexOf("anime") >= 0
                    text: qsTr("Anime")
                    iconName: "tv"
                    selected: page.media === "anime"
                    onClicked: page.media = "anime"
                }
                Chip {
                    visible: page.repoMedia.length > 1 && page.repoMedia.indexOf("music") >= 0
                    text: qsTr("Music")
                    iconName: "volume-2"
                    selected: page.media === "music"
                    onClicked: page.media = "music"
                }
                Select {
                    visible: page.repositories.length > 1
                    model: page.repoChoices
                    value: page.repo
                    title: qsTr("Repository")
                    onPicked: (v) => page.repo = v
                }
                Chip {
                    text: qsTr("18+")
                    selected: App.settings.showNsfwSources === true
                    onClicked: App.setSetting("showNsfwSources", !selected)
                }
            }

            // Extensions installing in the background, with a way to stop.
            Row {
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                spacing: 12
                visible: Extensions.queueTotal > 0
                Column {
                    width: parent.width - stopButton.width - 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Text {
                        text: qsTr("Installing %1 of %2").arg(Math.min(Extensions.queueDone + 1, Extensions.queueTotal)).arg(Extensions.queueTotal)
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                    }
                    Rectangle {
                        width: parent.width
                        height: 4
                        radius: 2
                        color: Theme.surface3
                        Rectangle {
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                            width: parent.width * Extensions.queueDone / Math.max(1, Extensions.queueTotal)
                            Behavior on width { NumberAnimation { duration: Motion.normal } }
                        }
                    }
                }
                ScButton { id: stopButton; text: qsTr("Stop"); compact: true; onClicked: Extensions.stopInstalls() }
            }

            // Nothing added yet: a place to paste a link, and nothing else.
            Column {
                visible: page.repositories.length === 0 && page.installedCount === 0
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                spacing: 14
                topPadding: 24
                Icon { name: "puzzle"; size: 34; color: Theme.textFaint; anchors.horizontalCenter: parent.horizontalCenter }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No repositories yet")
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                }
                Text {
                    width: Math.min(parent.width, 460)
                    anchors.horizontalCenter: parent.horizontalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Extensions come from repositories that other people run. Have a repository link? Paste it here, or open it from the repository's page with SCANCORE.")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    InlineField {
                        id: pasteField
                        width: Math.min(340, list.width - 2 * Theme.gutter - addButton.width - 8)
                        placeholderText: qsTr("Repository link")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                        onAccepted: Router.addRepository(text)
                    }
                    ScButton { id: addButton; text: qsTr("Add"); variant: "primary"; onClicked: Router.addRepository(pasteField.text) }
                }
            }

            Text {
                visible: list.count === 0 && (page.repositories.length > 0 || page.installedCount > 0)
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                topPadding: 20
                text: page.tab === "installed" ? qsTr("Nothing installed yet. Pick extensions under Available.")
                    : page.tab === "available" ? (page.availableInfo.count ? qsTr("No extension matches.") : qsTr("Every extension the repositories list is installed."))
                    : ""
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                wrapMode: Text.Wrap
            }
            Item { width: 1; height: 2 }
        }

        delegate: Loader {
            id: rowLoader
            required property var modelData
            required property int index
            width: list.width
            sourceComponent: page.tab === "installed" ? installedRow : page.tab === "available" ? availableRow : repositoryRow
        }
    }

    // ---------------------------------------------------------------- rows

    component Badge: Rectangle {
        property string text
        property color tint: Theme.textMuted
        width: badgeText.implicitWidth + 12
        height: 20
        radius: 10
        color: Theme.alpha(tint, 0.14)
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: parent.text
            color: parent.tint
            font.family: Theme.fontSans
            font.pixelSize: Theme.textXs
            font.weight: Font.DemiBold
        }
    }

    component ExtensionIcon: Cover {
        width: 40
        height: 40
        radius: 10
        tint: Theme.surface2
    }

    Component {
        id: installedRow
        Column {
            id: row
            readonly property var item: parent ? parent.modelData : ({})
            readonly property bool open: page.expanded[item.id] === true
            readonly property int firstSources: 12
            property bool allSources: false
            readonly property int broken: item.brokenCount || 0
            readonly property var sources: { App.dependOn(Extensions.revision); return open ? Extensions.installedSources(item.id) : [] }
            width: parent ? parent.width : 0
            bottomPadding: 6

            Item {
                width: parent.width
                height: 64
                Rectangle {
                    x: Theme.gutter - 12
                    width: parent.width - 2 * Theme.gutter + 24
                    height: parent.height - 4
                    radius: Theme.radiusLg
                    color: hover.hovered || row.open ? Theme.surface2 : "transparent"
                }
                HoverHandler { id: hover }
                // Under the buttons, which take their own clicks.
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.toggle(row.item.id)
                }
                ExtensionIcon {
                    id: icon
                    x: Theme.gutter
                    anchors.verticalCenter: parent.verticalCenter
                    source: row.item.icon || ""
                    title: (row.item.name || "?").slice(0, 1)
                }
                Column {
                    anchors.left: icon.right
                    anchors.leftMargin: 14
                    anchors.right: actions.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Row {
                        spacing: 8
                        width: parent.width
                        Text {
                            text: row.item.name || ""
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textMd
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, parent.width - 120)
                        }
                        Badge { visible: row.item.nsfw === true; text: "18+"; tint: Theme.danger }
                        Badge { visible: !row.item.trusted; text: qsTr("Not trusted"); tint: Theme.warning }
                        Badge { visible: row.broken > 0; text: qsTr("Error"); tint: Theme.danger }
                    }
                    Text {
                        width: parent.width
                        text: (row.item.update ? row.item.version + " → " + row.item.update : row.item.version)
                            + " · " + row.item.languageName
                            + ((row.item.sourceCount || 0) > 1 ? " · " + row.item.sourceCount + qsTr(" sources") : "")
                            + (row.item.problem ? " · " + row.item.problem : "")
                        color: row.item.problem ? Theme.danger : Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        elide: Text.ElideRight
                    }
                }
                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.gutter - 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Spinner { visible: page.installing(row.item.id); size: 16; anchors.verticalCenter: parent.verticalCenter }
                    ScButton {
                        visible: !row.item.trusted
                        compact: true
                        text: qsTr("Trust")
                        onClicked: { trustDialog.item = row.item; trustDialog.open() }
                    }
                    ScButton {
                        visible: !!row.item.update && !page.installing(row.item.id)
                        compact: true
                        text: qsTr("Update")
                        variant: "primary"
                        onClicked: page.install(row.item)
                    }
                    // Pinned: its sources come first in Comics, Anime, Novels
                    // and their search, and it comes first here.
                    IconButton {
                        iconName: "pin"
                        active: row.item.pinned === true
                        tip: row.item.pinned ? qsTr("Unpin") : qsTr("Pin: its sources come first")
                        onClicked: Extensions.setExtensionPinned(row.item.id, row.item.pinned !== true)
                    }
                    IconButton {
                        iconName: row.open ? "chevron-up" : "chevron-down"
                        tip: row.open ? qsTr("Hide sources") : qsTr("Show sources")
                        onClicked: page.toggle(row.item.id)
                    }
                    IconButton {
                        iconName: "trash"
                        tip: qsTr("Remove")
                        onClicked: { removeDialog.item = row.item; removeDialog.open() }
                    }
                }
            }

            // Its sources: on/off, settings, health.
            Repeater {
                model: row.open ? (row.allSources ? row.sources : row.sources.slice(0, row.firstSources)) : []
                delegate: Item {
                    id: sourceRow
                    required property var modelData
                    width: row.width
                    height: sourceColumn.implicitHeight + 16
                    Rectangle {
                        x: Theme.gutter + 54
                        y: 0
                        width: 2
                        height: parent.height
                        color: Theme.border
                    }
                    Column {
                        id: sourceColumn
                        x: Theme.gutter + 70
                        y: 8
                        width: parent.width - x - sourceActions.width - Theme.gutter
                        spacing: 3
                        Row {
                            spacing: 8
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8
                                height: 8
                                radius: 4
                                color: sourceRow.modelData.error ? Theme.danger : sourceRow.modelData.enabled && !sourceRow.modelData.blocked ? Theme.success : Theme.textFaint
                            }
                            Text {
                                text: sourceRow.modelData.name + " · " + sourceRow.modelData.languageName
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                font.weight: Font.Medium
                            }
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            text: sourceRow.modelData.blocked || sourceRow.modelData.error || (sourceRow.modelData.baseUrl || "").replace(/^https?:\/\//, "")
                            color: sourceRow.modelData.error || sourceRow.modelData.blocked ? Theme.danger : Theme.textFaint
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textXs
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                    }
                    Row {
                        id: sourceActions
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.gutter
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        IconButton {
                            // As Mihon's WebView: sign in, or pass a check the hidden one could not.
                            visible: !!sourceRow.modelData.baseUrl
                            iconName: "shield-check"
                            tip: qsTr("Open the site (browser check, sign in)")
                            onClicked: Router.openSiteCheck(sourceRow.modelData.baseUrl + "/")
                        }
                        IconButton {
                            visible: (row.item.sourceCount || 0) > 1
                            iconName: "pin"
                            active: sourceRow.modelData.pinned === true
                            tip: sourceRow.modelData.pinned ? qsTr("Unpin") : qsTr("Pin: it comes first")
                            onClicked: Extensions.setPinned(sourceRow.modelData.key, sourceRow.modelData.pinned !== true)
                        }
                        IconButton {
                            iconName: "compass"
                            tip: qsTr("Browse this source")
                            enabled: sourceRow.modelData.enabled && !sourceRow.modelData.blocked
                            onClicked: Router.openSource(sourceRow.modelData.key)
                        }
                        IconButton {
                            iconName: "sliders-horizontal"
                            tip: qsTr("Source settings")
                            enabled: !sourceRow.modelData.blocked
                            onClicked: settingsDialog.start(sourceRow.modelData.key, sourceRow.modelData.name)
                        }
                        Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: sourceRow.modelData.enabled
                            onToggled: Extensions.setSourceEnabled(sourceRow.modelData.key, checked)
                        }
                    }
                }
            }
            // A multi-language extension has dozens: twelve, then all.
            ScButton {
                visible: row.open && (row.item.sourceCount || 0) > row.firstSources && !row.allSources
                x: Theme.gutter + 54
                variant: "ghost"
                text: qsTr("Show all %1 sources").arg(row.item.sourceCount || 0)
                onClicked: row.allSources = true
            }
        }
    }

    Component {
        id: availableRow
        Item {
            id: row
            readonly property var item: parent ? parent.modelData : ({})
            // A language heading above the first extension in each language.
            readonly property bool heading: item.heading === true
            readonly property int headingSpace: heading ? 40 : 0
            width: parent ? parent.width : 0
            height: 60 + headingSpace
            Text {
                visible: row.heading
                x: Theme.gutter
                y: 18
                text: row.item.languageName || ""
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
            }
            Rectangle {
                x: Theme.gutter - 12
                y: row.headingSpace
                width: parent.width - 2 * Theme.gutter + 24
                height: 56
                radius: Theme.radiusLg
                color: hover.hovered ? Theme.surface2 : "transparent"
            }
            HoverHandler { id: hover }
            ExtensionIcon {
                id: icon
                x: Theme.gutter
                y: row.headingSpace + 10
                source: row.item.icon || ""
                title: (row.item.name || "?").slice(0, 1)
            }
            Column {
                anchors.left: icon.right
                anchors.leftMargin: 14
                anchors.right: action.left
                anchors.rightMargin: 12
                anchors.verticalCenter: icon.verticalCenter
                spacing: 4
                Row {
                    spacing: 8
                    width: parent.width
                    Text {
                        text: row.item.name || ""
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, parent.width - 50)
                    }
                    Badge { visible: row.item.nsfw === true; text: "18+"; tint: Theme.danger }
                }
                Text {
                    width: parent.width
                    text: row.item.version + (row.item.problem ? " · " + row.item.problem : "")
                    color: row.item.problem ? Theme.danger : Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    elide: Text.ElideRight
                }
            }
            Item {
                id: action
                anchors.right: parent.right
                anchors.rightMargin: Theme.gutter
                anchors.verticalCenter: icon.verticalCenter
                width: installButton.width
                height: installButton.height
                ScButton {
                    id: installButton
                    compact: true
                    text: qsTr("Install")
                    enabled: !row.item.problem
                    loading: page.installing(row.item.id)
                    onClicked: page.install(row.item)
                }
            }
        }
    }

    Component {
        id: repositoryRow
        Item {
            id: row
            readonly property var repo: parent ? parent.modelData : ({})
            width: parent ? parent.width : 0
            height: 68
            Rectangle {
                x: Theme.gutter - 12
                width: parent.width - 2 * Theme.gutter + 24
                height: parent.height - 4
                radius: Theme.radiusLg
                color: hover.hovered ? Theme.surface2 : "transparent"
            }
            HoverHandler { id: hover }
            // Its own page: the extensions it offers.
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: page.openRepository(row.repo.url)
            }
            Rectangle {
                id: badge
                x: Theme.gutter
                anchors.verticalCenter: parent.verticalCenter
                width: 40
                height: 40
                radius: 10
                color: Theme.alpha(row.repo.error ? Theme.danger : Theme.accent, 0.12)
                Icon { anchors.centerIn: parent; name: row.repo.media === "anime" ? "tv" : row.repo.media === "novel" ? "book-text" : row.repo.media === "music" ? "volume-2" : "book-open"; size: 18; color: row.repo.error ? Theme.danger : Theme.accent }
            }
            Column {
                anchors.left: badge.right
                anchors.leftMargin: 14
                anchors.right: repoActions.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Text {
                    width: parent.width
                    text: row.repo.name || ""
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: row.repo.error ? row.repo.error
                        : row.repo.domain + " · " + row.repo.count + (row.repo.count === 1 ? qsTr(" extension") : qsTr(" extensions"))
                    color: row.repo.error ? Theme.danger : Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    elide: Text.ElideRight
                }
            }
            Row {
                id: repoActions
                anchors.right: parent.right
                anchors.rightMargin: Theme.gutter - 6
                anchors.verticalCenter: parent.verticalCenter
                IconButton {
                    iconName: "chevron-right"
                    tip: qsTr("Its extensions")
                    onClicked: page.openRepository(row.repo.url)
                }
                IconButton {
                    iconName: "copy"
                    tip: qsTr("Copy link")
                    onClicked: { App.copy(row.repo.url); Router.toast(qsTr("Link copied"), "success") }
                }
                IconButton {
                    iconName: "trash"
                    tip: qsTr("Remove")
                    onClicked: { removeRepoDialog.repo = row.repo; removeRepoDialog.open() }
                }
            }
        }
    }

    BackToTop { flick: list }
}
