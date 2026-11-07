import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Opens a title from the chosen tracker's catalogue: finds it on the
// built-in sources (under any of its names) and opens it there. It opens
// straight away when exactly one source in the content language has it, and
// otherwise asks, content language first. Installed extensions are only
// asked when the reader picks one under "My extensions": one tap must not
// wait on every extension. On phones it is a sheet from the bottom.
Dialog {
    id: dialog
    property var item: ({})
    // "sources": the built-in sources' matches; "pick": choosing an
    // installed extension; "extension": the picked extension's matches.
    property string mode: "sources"
    property var task: null
    // The built-in search, kept so Back does not search again.
    property var sourcesTask: null
    // The extension picked: {key, name, languageName, icon}.
    property var picked: ({})
    // Opened to read it from an extension (a title found on a built-in
    // source): there is no built-in list to go back to.
    property bool extensionsOnly: false
    property string query: ""
    readonly property bool novel: item.media === "novel"
    readonly property bool sheet: Theme.compact
    // While it looks, the matches found so far (sources answer one by one).
    readonly property var matches: mode === "pick" || !task ? []
        : task.ok ? task.result.matches
        : task.loading && task.partial ? task.partial.matches || [] : []
    // Another language to look in (comics), when the user's own has nothing.
    property string language: ""
    property bool choosing: false
    readonly property bool hasExtensions: novel ? Novels.hasExtensions() : Comics.hasExtensions()
    readonly property var otherLanguages: novel || mode !== "sources" ? [] : Comics.otherLanguages().filter(l => l.value !== language)
    readonly property bool showLanguages: mode === "sources" && !!task && task.ok && (matches.length === 0 || choosing) && otherLanguages.length > 0

    // Extensions picked before, most recent first (per media).
    readonly property string recentSetting: "readFromExtensions." + (novel ? "novel" : "comic")
    function recent() {
        try {
            const value = JSON.parse(App.settings[recentSetting] || "[]")
            return Array.isArray(value) ? value : []
        } catch (e) {
            return []
        }
    }
    // Installed, enabled extension sources for this media: recently picked
    // first, then the content language, then by name.
    readonly property var extensionList: {
        if (mode !== "pick") return []
        App.dependOn(Extensions.revision)
        const recentKeys = recent()
        const out = []
        for (const ext of Extensions.installed(novel ? "lnreader" : "mihon"))
            for (const s of ext.sources || [])
                if (s.enabled && !s.blocked)
                    out.push({ key: s.key, name: s.name || ext.name, languageName: s.languageName || "", icon: ext.icon || "",
                               recent: recentKeys.indexOf(s.key), own: s.preferred === true })
        out.sort((a, b) => {
            const ra = a.recent < 0 ? 1e6 : a.recent, rb = b.recent < 0 ? 1e6 : b.recent
            if (ra !== rb) return ra - rb
            if (a.own !== b.own) return a.own ? -1 : 1
            return a.name.localeCompare(b.name)
        })
        return out
    }
    readonly property var shownExtensions: {
        const q = query.trim().toLowerCase()
        return q.length ? extensionList.filter(e => e.name.toLowerCase().indexOf(q) >= 0 || e.languageName.toLowerCase().indexOf(q) >= 0)
                        : extensionList
    }

    function start(entry, onlyExtensions) {
        item = entry
        language = ""
        choosing = false
        query = ""
        picked = ({})
        extensionsOnly = onlyExtensions === true
        if (extensionsOnly) {
            sourcesTask = null
            task = null
            mode = "pick"
        } else {
            mode = "sources"
            sourcesTask = entry.media === "novel" ? Novels.resolve(entry) : Comics.resolve(entry)
            task = sourcesTask
        }
        open()
    }

    function pickExtension() {
        query = ""
        choosing = false
        mode = "pick"
        task = null
    }

    function lookOn(ext) {
        picked = ext
        mode = "extension"
        const keys = [ext.key].concat(recent().filter(k => k !== ext.key)).slice(0, 8)
        App.setSetting(recentSetting, JSON.stringify(keys))
        task = novel ? Novels.resolve(item, ext.key) : Comics.resolve(item, "", ext.key)
    }

    function back() {
        if (mode === "extension") {
            pickExtension()
        } else if (mode === "pick" && sourcesTask) {
            mode = "sources"
            task = sourcesTask
        }
    }

    function lookIn(code) {
        language = code
        choosing = false
        sourcesTask = Comics.resolve(item, code)
        task = sourcesTask
    }

    function show(match) {
        close()
        // The catalogue entry travels with the series (and is stored with it),
        // so comments, reviews and tracking know which title this is.
        const it = dialog.item
        const ids = it.ids || {}
        const extra = { catalog: it.catalog || "comick", catalogId: it.catalogId || it.hid || "", hid: it.hid || "",
                        catalogTitle: it.title || "", titles: (it.titles || []).slice(0, 20), year: it.year || 0 }
        if (ids.anilist) extra.anilist = ids.anilist
        if (ids.mal) extra.mal = ids.mal
        Router.openSeries({ media: dialog.novel ? "novel" : "comic", source: match.source, sourceUrl: match.url, title: match.title,
                            cover: match.cover || it.cover, description: it.description || "",
                            status: it.status || "", type: it.type || "", rating: it.rating || "",
                            hid: it.hid || "", extra: extra }, null)
    }

    Connections {
        target: dialog.task
        function onFinished(result) {
            const preferred = result.matches.filter(m => m.preferred)
            if (dialog.mode === "extension" && result.matches.length === 1) dialog.show(result.matches[0])
            else if (dialog.mode !== "sources") return
            else if (preferred.length === 1) dialog.show(preferred[0])
            else if (result.matches.length === 1) dialog.show(result.matches[0])
        }
    }

    readonly property real safeBottom: sheet && parent ? parent.SafeArea.margins.bottom : 0
    parent: Overlay.overlay
    width: sheet ? (parent ? parent.width : 400) : Math.min(460, (parent ? parent.width : 460) - 32)
    height: Math.min(implicitHeight, parent ? (sheet ? parent.height * 0.85 : parent.height - 48) : 640)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (sheet ? parent.height - height : (parent.height - height) / 2) : 0
    modal: true
    focus: true
    padding: sheet ? 18 : 24
    topPadding: sheet ? 30 : 24
    bottomPadding: (sheet ? 14 : 24) + safeBottom
    title: ""

    Overlay.modal: Rectangle { color: Theme.overlay }

    enter: dialog.sheet ? riseIn : popIn
    exit: dialog.sheet ? riseOut : popOut
    property Transition popIn: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
        }
    }
    property Transition popOut: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast }
            NumberAnimation { property: "scale"; to: 0.97; duration: Motion.fast }
        }
    }
    property Transition riseIn: Transition {
        NumberAnimation { property: "y"; from: dialog.parent ? dialog.parent.height : 800; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
    }
    property Transition riseOut: Transition {
        NumberAnimation { property: "y"; to: dialog.parent ? dialog.parent.height : 800; duration: Motion.normal; easing.type: Easing.InCubic }
    }

    background: SurfacePanel {
        radius: Theme.radiusXl
        bottomLeftRadius: dialog.sheet ? 0 : Theme.radiusXl
        bottomRightRadius: dialog.sheet ? 0 : Theme.radiusXl
        fill: Theme.surface
        border.width: dialog.sheet ? 0 : 1
        border.color: Theme.borderStrong
        Rectangle {
            visible: dialog.sheet
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            width: 36; height: 4; radius: 2
            color: Theme.alpha(Theme.text, 0.2)
        }
    }

    component Option: AbstractButton {
        id: option
        property string label
        property string detail
        property bool first: false
        property string iconSource
        width: ListView.view ? ListView.view.width : 300
        height: 54
        hoverEnabled: true
        background: Rectangle {
            radius: Theme.radiusMd
            color: option.hovered || option.pressed ? Theme.surface3 : Theme.surface2
            border.width: 1
            border.color: option.first ? Theme.alpha(Theme.accent, 0.4) : Theme.border
        }
        contentItem: Item {
            Cover {
                id: optionIcon
                visible: option.iconSource.length > 0
                x: 12
                width: 30
                height: 30
                radius: 8
                anchors.verticalCenter: parent.verticalCenter
                tint: Theme.surface3
                source: option.iconSource
            }
            Column {
                x: optionIcon.visible ? 54 : 14
                width: parent.width - x - 38
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    width: parent.width
                    text: option.label
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: option.detail
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    elide: Text.ElideRight
                }
            }
            Icon {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-right"
                size: 16
                color: Theme.textFaint
            }
        }
    }

    contentItem: Item {
        id: body
        // The header and the buttons stay put; the list between them
        // scrolls once the dialog reaches the window's height.
        implicitHeight: header.height + 16 + list.wanted + 16 + footer.height

        Column {
            id: header
            width: parent.width
            spacing: 12

            Row {
                width: parent.width
                spacing: 14
                Cover {
                    width: 54
                    height: 78
                    radius: 8
                    source: dialog.item.cover || ""
                }
                Column {
                    width: parent.width - 68
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        width: parent.width
                        text: dialog.item.title || ""
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.textLg
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: dialog.mode === "pick" ? (dialog.extensionList.length ? qsTr("Pick an extension to look in") : qsTr("No extension for this is installed and on."))
                            : !dialog.task || dialog.task.loading ? (dialog.mode === "extension" ? qsTr("Looking for it on %1…").arg(dialog.picked.name || "")
                                                                                                 : qsTr("Looking for it on your sources…"))
                            : !dialog.task.ok ? dialog.task.error
                            : dialog.matches.length === 0 && dialog.mode === "extension" ? qsTr("%1 does not have it.").arg(dialog.picked.name || "")
                            : dialog.matches.length === 0 && dialog.language ? qsTr("No source in that language has it either.")
                            : dialog.matches.length === 0 ? qsTr("None of your sources has it yet.")
                            : dialog.mode === "extension" ? qsTr("Read it from %1").arg(dialog.picked.name || "")
                            : qsTr("Read it from")
                        color: dialog.task && !dialog.task.loading && !dialog.task.ok ? Theme.danger : Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                }
            }

            SearchField {
                visible: dialog.mode === "pick" && dialog.extensionList.length > 6
                width: parent.width
                placeholderText: qsTr("Find an extension")
                text: dialog.query
                onTextChanged: dialog.query = text
            }

            Spinner {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: dialog.mode !== "pick" && !!dialog.task && dialog.task.loading
            }
        }

        ListView {
            id: list
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            // Its whole height when it fits.
            readonly property real wanted: contentHeight
            y: header.height + 16
            width: parent.width
            height: Math.max(0, parent.height - y - 16 - footer.height)
            clip: true
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.mode === "pick" ? dialog.shownExtensions : dialog.matches
            WheelScroll {}
            ScrollBar.vertical: ScScrollBar {}
            delegate: Option {
                required property var modelData
                required property int index
                first: index === 0 && dialog.mode !== "pick"
                label: dialog.mode === "pick" ? modelData.name
                    // The language, when it is not the user's.
                    : modelData.sourceLabel + (modelData.language && dialog.task
                        && modelData.language.split("-")[0] !== ((dialog.task.ok ? dialog.task.result : dialog.task.partial || {}).contentLanguage || "en")
                        ? " · " + modelData.language.toUpperCase() : "")
                detail: dialog.mode === "pick" ? modelData.languageName : (modelData.title || "")
                iconSource: dialog.mode === "pick" ? modelData.icon : ""
                onClicked: dialog.mode === "pick" ? dialog.lookOn(modelData) : dialog.show(modelData)
            }

            // Other languages to look in.
            footer: Column {
                width: ListView.view ? ListView.view.width : 0
                visible: dialog.showLanguages
                height: visible ? implicitHeight : 0
                topPadding: dialog.matches.length ? 10 : 0
                spacing: 10
                Text {
                    width: parent.width
                    text: qsTr("Look for it in another language:")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                }
                Flow {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: dialog.showLanguages ? dialog.otherLanguages : []
                        Chip {
                            required property var modelData
                            text: modelData.label
                            onClicked: dialog.lookIn(modelData.value)
                        }
                    }
                }
            }
        }

        Flow {
            id: footer
            anchors.bottom: parent.bottom
            width: parent.width
            layoutDirection: Qt.RightToLeft
            spacing: 8
            ScButton {
                text: qsTr("Close")
                onClicked: dialog.close()
            }
            ScButton {
                visible: dialog.mode === "extension" || (dialog.mode === "pick" && !!dialog.sourcesTask)
                text: qsTr("Back")
                iconName: "arrow-left"
                variant: "ghost"
                onClicked: dialog.back()
            }
            ScButton {
                visible: dialog.mode === "sources" && dialog.hasExtensions
                text: qsTr("My extensions")
                iconName: "puzzle"
                variant: "ghost"
                onClicked: dialog.pickExtension()
            }
            ScButton {
                visible: dialog.mode === "sources" && !dialog.novel && !!dialog.task && dialog.task.ok && dialog.matches.length > 0
                         && !dialog.choosing && dialog.otherLanguages.length > 0
                text: qsTr("Other languages")
                iconName: "languages"
                variant: "ghost"
                onClicked: dialog.choosing = true
            }
            ScButton {
                visible: dialog.mode === "sources" && !!dialog.item.catalogUrl
                text: dialog.item.catalog === "anilist" ? "AniList" : dialog.item.catalog === "mal" ? "MyAnimeList" : qsTr("Comick")
                iconName: "external-link"
                variant: "ghost"
                onClicked: App.openUrl(dialog.item.catalogUrl)
            }
        }
    }
}
