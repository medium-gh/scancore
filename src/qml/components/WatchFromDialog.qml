import QtQuick
import QtQuick.Controls.Basic
import Scancore

// "Watch from": where an AniList anime plays. Finds it on the installed
// anime sources (under any of its names), content language first, and
// remembers the one picked for this anime. With `quiet`, it picks by itself
// when exactly one source in the content language has it. A source that
// splits the anime into seasons asks which one.
Dialog {
    id: dialog
    property var media: ({})
    property bool quiet: false
    property var task: null
    // A source's seasons, when the match picked has them.
    property var seasonsTask: null
    property var seasonsOf: null
    readonly property var matches: task && task.ok ? task.result.matches : []
    readonly property var seasons: seasonsTask && seasonsTask.ok ? seasonsTask.result.seasons : []
    readonly property bool builtIn: Anime.playerEngine !== ""
    readonly property var current: Anime.provider(media.anilistId || 0)
    // The extensions looked in: ticked chips, none = all (starts from
    // Settings → Anime).
    property var ticked: []
    readonly property var extensionSources: { App.dependOn(Extensions.revision); return Anime.sources().filter(s => s.enabled).map(s => ({ key: s.key, label: s.label, language: s.language })) }
    function search() { task = Anime.watchFrom(media, ticked) }
    function tick(key) {
        if (key === "all") ticked = []
        else ticked = ticked.indexOf(key) >= 0 ? ticked.filter(k => k !== key) : ticked.concat([key])
        search()
    }

    // The provider picked ({source, url, title, sourceLabel}; empty for the
    // built-in player).
    signal chosen(var provider)
    // Closed without a choice.
    signal dismissed()
    property bool picked: false

    function start(entry, quietly) {
        media = entry
        quiet = !!quietly
        picked = false
        seasonsTask = null
        seasonsOf = null
        ticked = Array.from(App.settings.animeMatchSources || []).filter(k => extensionSources.some(s => s.key === k))
        search()
        // Quietly, it only shows when there is a choice to make.
        if (!quiet) open()
        else if (task.done) task.ok ? decide(task.result) : open()
    }

    // Looks only in `keys` and plays the match when there is one to pick
    // (the source chips on the watch page); asks otherwise.
    function startWith(entry, keys) {
        media = entry
        quiet = true
        picked = false
        seasonsTask = null
        seasonsOf = null
        ticked = Array.from(keys)
        search()
        if (task.done) task.ok ? decide(task.result) : open()
    }

    function choose(match) {
        if (match.seasons) {
            seasonsOf = match
            seasonsTask = Anime.sourceAnime(match.url, { title: match.title, seasons: true })
            if (!visible) open()
            return
        }
        const provider = match.source ? { source: match.source, url: match.url, title: match.title, sourceLabel: match.sourceLabel } : {}
        Anime.setProvider(media.anilistId || 0, provider)
        picked = true
        close()
        chosen(provider)
    }

    function decide(result) {
        if (!quiet) return
        const preferred = result.matches.filter(m => m.preferred)
        const only = preferred.length === 1 ? preferred[0] : result.matches.length === 1 ? result.matches[0] : null
        if (only) choose(only)
        else if (!visible) open()
    }

    Connections {
        target: dialog.task
        function onFinished(result) { dialog.decide(result) }
        function onFailed() { if (dialog.quiet && !dialog.visible) dialog.open() }
    }
    Connections {
        target: dialog.seasonsTask
        function onFinished(result) {
            // One season: nothing to ask.
            if (result.seasons.length === 1) dialog.choose(Object.assign({}, dialog.seasonsOf, result.seasons[0], { seasons: false, sourceLabel: dialog.seasonsOf.sourceLabel }))
        }
    }
    onClosed: if (!picked) dismissed()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, (parent ? parent.width : 480) - 32)
    modal: true
    focus: true
    padding: 24

    Overlay.modal: Rectangle { color: Theme.overlay }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast }
            NumberAnimation { property: "scale"; to: 0.97; duration: Motion.fast }
        }
    }
    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    component Option: AbstractButton {
        id: option
        property string heading
        property string detail
        property bool highlighted: false
        width: parent ? parent.width : 300
        height: 54
        hoverEnabled: true
        background: Rectangle {
            radius: Theme.radiusMd
            color: option.hovered ? Theme.surface3 : Theme.surface2
            border.width: 1
            border.color: option.highlighted ? Theme.alpha(Theme.accent, 0.5) : Theme.border
        }
        contentItem: Item {
            Column {
                x: 14
                width: parent.width - 52
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    width: parent.width
                    text: option.heading
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
                name: option.highlighted ? "check" : "chevron-right"
                size: 16
                color: option.highlighted ? Theme.accent : Theme.textFaint
            }
        }
    }

    contentItem: Column {
        spacing: 16

        Column {
            width: parent.width
            spacing: 4
            Text {
                width: parent.width
                text: dialog.seasonsOf ? qsTr("Which season?") : qsTr("Watch from")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: dialog.seasonsOf ? dialog.seasonsOf.sourceLabel + " · " + dialog.seasonsOf.title
                    : !dialog.task || dialog.task.loading ? qsTr("Looking for %1 on your anime sources…").arg(dialog.media.title || "")
                    : !dialog.task.ok ? dialog.task.error
                    : dialog.matches.length === 0 ? qsTr("None of your anime sources has %1 yet.").arg(dialog.media.title || "")
                    : dialog.media.title || ""
                color: dialog.task && !dialog.task.loading && !dialog.task.ok ? Theme.danger : Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                wrapMode: Text.Wrap
            }
        }

        // Which extensions to look in.
        SourceChips {
            visible: !dialog.seasonsOf && dialog.extensionSources.length > 1
            width: parent.width
            sources: dialog.extensionSources
            ticked: dialog.ticked
            allLabel: qsTr("All extensions")
            inset: 0
            showLanguage: true
            onPicked: (key) => dialog.tick(key)
        }

        Spinner {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: (!!dialog.task && dialog.task.loading && !dialog.seasonsOf) || (!!dialog.seasonsTask && dialog.seasonsTask.loading)
        }

        // The anime's seasons on the source picked.
        Column {
            width: parent.width
            spacing: 6
            visible: !!dialog.seasonsOf
            Repeater {
                model: dialog.seasons
                Option {
                    required property var modelData
                    heading: modelData.title
                    detail: modelData.season >= 0 ? qsTr("Season %1").arg(modelData.season) : ""
                    onClicked: dialog.choose(Object.assign({}, dialog.seasonsOf, modelData, { seasons: false, sourceLabel: dialog.seasonsOf.sourceLabel }))
                }
            }
        }

        Flickable {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            width: parent.width
            height: Math.min(contentHeight, 340)
            contentHeight: list.height
            clip: true
            visible: !dialog.seasonsOf
            Column {
                id: list
                width: parent.width
                spacing: 6
                Repeater {
                    model: dialog.matches
                    Option {
                        required property var modelData
                        // The language, when it is not English.
                        heading: modelData.sourceLabel + (modelData.language && modelData.language !== "en" ? " · " + modelData.language.toUpperCase() : "")
                        detail: modelData.title + (modelData.seasons ? " · " + qsTr("in seasons") : "")
                        highlighted: dialog.current.source === modelData.source
                        onClicked: dialog.choose(modelData)
                    }
                }
                Option {
                    visible: dialog.builtIn
                    heading: qsTr("Built-in servers")
                    detail: qsTr("The player SCANCORE has without extensions")
                    highlighted: !!dialog.current.builtIn
                    onClicked: dialog.choose({})
                }
            }
        }

        Row {
            anchors.right: parent.right
            spacing: 8
            ScButton {
                visible: !!dialog.seasonsOf
                text: qsTr("Back")
                variant: "ghost"
                onClicked: {
                    dialog.seasonsOf = null
                    dialog.seasonsTask = null
                }
            }
            ScButton {
                text: qsTr("Close")
                onClicked: dialog.close()
            }
        }
    }
}
