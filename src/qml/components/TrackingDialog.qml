import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Which AniList and MyAnimeList entry a series moves forward when you finish
// a chapter or episode. Comics and novels are matched by title on their own;
// this picks or changes the entry by hand.
Dialog {
    id: dialog
    property string seriesId
    property string media: "comic"
    property string seriesTitle

    // The service being linked (search shown), or "".
    property string picking: ""
    property var searchTask: null
    readonly property var links: { App.dependOn(Trackers.services); return Trackers.links(seriesId) }
    // Connected services that list this kind of series (Comick has no
    // novels, MangaUpdates no anime, Simkl only anime).
    readonly property var services: Trackers.services.filter(s => s.connected && s.media.indexOf(media) >= 0)

    // The name to search by: the catalogue's title when the series came
    // from one, which trackers know better than a site's.
    readonly property string lookupTitle: Trackers.lookupTitle(seriesId, seriesTitle)
    readonly property var misses: { App.dependOn(Trackers.services); return Trackers.misses(seriesId) }
    property string pendingPick: ""

    function search(service, text) {
        picking = service
        query.text = text
        searchTask = Trackers.search(service, media, text)
    }
    // Opens straight on one service's search.
    function pick(service) {
        pendingPick = service
        open()
    }

    onOpened: {
        picking = ""
        if (pendingPick) search(pendingPick, lookupTitle)
        pendingPick = ""
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, (parent ? parent.width : 520) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 640) - 48)
    modal: true
    focus: true
    padding: 24
    title: ""

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

    contentItem: Column {
        spacing: 16

        Row {
            width: parent.width
            spacing: 10
            Text {
                width: parent.width - close.width - 10
                anchors.verticalCenter: parent.verticalCenter
                text: dialog.picking ? qsTr("Find it on ") + (Trackers.services.find(s => s.key === dialog.picking) || { label: "" }).label : qsTr("Tracking")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            IconButton {
                id: close
                iconName: dialog.picking ? "arrow-left" : "x"
                tip: dialog.picking ? qsTr("Back") : qsTr("Close")
                onClicked: dialog.picking ? dialog.picking = "" : dialog.close()
            }
        }

        // The services and what each is linked to.
        Column {
            visible: !dialog.picking
            width: parent.width
            spacing: 10
            Text {
                visible: dialog.services.length === 0
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Connect a tracker such as AniList, MyAnimeList or Kitsu in Settings → Connections to keep your lists there up to date.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            ScButton {
                visible: dialog.services.length === 0
                text: qsTr("Open Connections")
                iconName: "link"
                onClicked: {
                    dialog.close()
                    Router.openSettings("connections")
                }
            }
            Repeater {
                model: dialog.services
                Rectangle {
                    id: card
                    required property var modelData
                    readonly property var link: dialog.links[modelData.key] || null
                    width: parent.width
                    height: cardRow.height + 28
                    radius: Theme.radiusLg
                    color: Theme.surface2
                    border.width: 1
                    border.color: Theme.border
                    Item {
                        id: cardRow
                        x: 16
                        y: 14
                        width: parent.width - 32
                        height: Math.max(texts.height, actions.height)
                        Column {
                            id: texts
                            width: parent.width - actions.width - 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text {
                                text: card.modelData.label
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textMd
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                readonly property bool missed: !card.link && dialog.misses.indexOf(card.modelData.key) >= 0
                                text: card.link ? (card.link.title || qsTr("Entry ") + card.link.id) + (card.link.auto ? qsTr(" · from this anime") : "")
                                    : missed ? qsTr("Not found by its title; pick the entry")
                                    : dialog.media === "anime" ? qsTr("Linked when you finish an episode") : qsTr("Found by title when you finish a chapter")
                                color: missed ? Theme.warning : Theme.textMuted
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                            }
                        }
                        Row {
                            id: actions
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            IconButton {
                                visible: !!card.link
                                iconName: "external-link"
                                tip: qsTr("Open on ") + card.modelData.label
                                onClicked: App.openUrl(Trackers.entryUrl(card.modelData.key, dialog.media, card.link.id))
                            }
                            ScButton {
                                compact: true
                                text: card.link ? qsTr("Change") : qsTr("Link")
                                variant: !card.link && dialog.misses.indexOf(card.modelData.key) >= 0 ? "primary" : "secondary"
                                onClicked: dialog.search(card.modelData.key, dialog.lookupTitle)
                            }
                            ScButton {
                                visible: !!card.link && !card.link.auto
                                compact: true
                                variant: "ghost"
                                text: qsTr("Unlink")
                                onClicked: Trackers.unlink(dialog.seriesId, card.modelData.key)
                            }
                        }
                    }
                }
            }
        }

        // Picking an entry.
        Column {
            visible: !!dialog.picking
            width: parent.width
            spacing: 12
            SearchField {
                id: query
                width: parent.width
                text: dialog.lookupTitle
                placeholderText: qsTr("Title")
                onSubmitted: (text) => dialog.search(dialog.picking, text)
            }
            Item {
                width: parent.width
                height: 120
                visible: !dialog.searchTask || dialog.searchTask.loading || !dialog.searchTask.ok || dialog.searchTask.result.length === 0
                Spinner { anchors.centerIn: parent; visible: !!dialog.searchTask && dialog.searchTask.loading }
                Text {
                    anchors.centerIn: parent
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: !!dialog.searchTask && !dialog.searchTask.loading
                    text: dialog.searchTask && !dialog.searchTask.ok ? dialog.searchTask.error : qsTr("Nothing by that title")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
            }
            ListView {
                maximumFlickVelocity: Motion.flickVelocity
                flickDeceleration: Motion.flickDeceleration
                WheelScroll {}
                id: results
                width: parent.width
                height: Math.min(contentHeight, 380)
                visible: !!dialog.searchTask && dialog.searchTask.ok && dialog.searchTask.result.length > 0
                clip: true
                spacing: 4
                model: dialog.searchTask && dialog.searchTask.ok ? dialog.searchTask.result : []
                ScrollBar.vertical: ScScrollBar {}
                delegate: AbstractButton {
                    id: result
                    required property var modelData
                    width: results.width
                    height: 72
                    hoverEnabled: true
                    onClicked: {
                        Trackers.link(dialog.seriesId, dialog.picking, modelData.id, modelData.title)
                        Router.toast(qsTr("Linked to ") + modelData.title, "success")
                        dialog.picking = ""
                    }
                    background: Rectangle {
                        radius: Theme.radiusMd
                        color: result.hovered ? Theme.alpha(Theme.text, 0.05) : "transparent"
                    }
                    contentItem: Row {
                        leftPadding: 6
                        spacing: 12
                        Cover {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 42
                            height: 60
                            radius: 6
                            source: result.modelData.cover || ""
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: results.width - 72
                            spacing: 3
                            Text {
                                width: parent.width
                                text: result.modelData.title
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textMd
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: [result.modelData.detail, result.modelData.total > 0 ? result.modelData.total + (dialog.media === "anime" ? qsTr(" episodes") : qsTr(" chapters")) : ""]
                                    .filter(s => s).join(" · ")
                                color: Theme.textMuted
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
