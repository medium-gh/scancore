import QtQuick
import QtQuick.Controls.Basic
import Scancore

// The notification area: new chapters, announcements, extension and app
// updates, newest first. A sheet from the right on desktop, from the bottom
// on phones. Opening one goes where it points and marks it read.
Popup {
    id: panel
    property var task: null
    property var items: []

    function reload() { task = Notifications.list(100) }
    function act(item) {
        if (!item.read) Notifications.markRead(item.id)
        const a = item.action || {}
        close()
        if (a.seriesId) {
            const row = Library.seriesRow(a.seriesId)
            if (row && row.id) Router.openSeries(row, null)
        } else if (a.url) {
            App.openUrl(a.url)
        } else if (a.page === "extensions") {
            Router.openExtensions()
        } else if (a.settings) {
            Router.openSettings(a.settings)
        } else if (a.section) {
            Router.go(a.section)
        }
    }
    function iconFor(kind) {
        return kind === "chapters" ? "book-open" : kind === "announcement" ? "megaphone"
            : kind === "extensions" ? "puzzle" : kind === "app" ? "download" : kind === "comments" ? "message-circle" : "bell"
    }

    onOpened: reload()
    Connections {
        target: Notifications
        function onRevisionChanged() { if (panel.opened) refresh.restart() }
    }
    Timer { id: refresh; interval: 150; onTriggered: panel.reload() }
    Connections {
        target: panel.task
        function onFinished(result) { panel.items = result }
    }

    parent: Overlay.overlay
    modal: true
    focus: true
    padding: 0
    readonly property bool phone: Theme.compact
    width: phone ? parent.width : Math.min(420, parent.width - 32)
    height: phone ? Math.min(parent.height * 0.8, parent.height - 48) : parent.height - 24
    x: phone ? 0 : parent.width - width - 12
    y: phone ? parent.height - height : 12

    Overlay.modal: Rectangle { color: Theme.overlay }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation {
                property: panel.phone ? "y" : "x"
                from: panel.phone ? panel.parent.height : panel.parent.width
                to: panel.phone ? panel.parent.height - panel.height : panel.parent.width - panel.width - 12
                duration: Motion.slow
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Motion.decelerate
            }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast }
    }

    background: GlassPanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    contentItem: Item {
        Item {
            id: head
            width: parent.width
            height: 64
            Text {
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Notifications")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textXl
                font.weight: Font.DemiBold
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                IconButton {
                    visible: Notifications.unread > 0
                    iconName: "list-checks"
                    tip: qsTr("Mark all as read")
                    onClicked: Notifications.markAllRead()
                }
                IconButton {
                    visible: panel.items.length > 0
                    iconName: "trash"
                    tip: qsTr("Clear all")
                    onClicked: {
                        Notifications.clear()
                        panel.items = []
                    }
                }
                IconButton { iconName: "x"; tip: qsTr("Close"); onClicked: panel.close() }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        ListView {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            WheelScroll {}
            id: list
            anchors.top: head.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            clip: true
            model: panel.items
            bottomMargin: 12 + panel.SafeArea.margins.bottom
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScScrollBar {}
            delegate: AbstractButton {
                id: row
                required property var modelData
                width: list.width
                height: Math.max(64, texts.implicitHeight + 24)
                hoverEnabled: true
                onClicked: panel.act(modelData)
                background: Rectangle {
                    color: row.hovered ? Theme.alpha(Theme.text, 0.04) : "transparent"
                    Rectangle { anchors.bottom: parent.bottom; x: 20; width: parent.width - 40; height: 1; color: Theme.border; opacity: 0.6 }
                }
                contentItem: Item {
                    Rectangle {
                        id: badge
                        x: 18
                        y: 14
                        width: 34
                        height: 34
                        radius: 17
                        color: row.modelData.kind === "announcement" ? Theme.alpha(Theme.accent, 0.18) : Theme.surface2
                        Icon {
                            anchors.centerIn: parent
                            name: panel.iconFor(row.modelData.kind)
                            size: 16
                            color: row.modelData.kind === "announcement" ? Theme.accent : Theme.textMuted
                        }
                    }
                    Column {
                        id: texts
                        anchors.left: badge.right
                        anchors.leftMargin: 12
                        anchors.right: dot.left
                        anchors.rightMargin: 10
                        y: 12
                        spacing: 3
                        Text {
                            width: parent.width
                            text: row.modelData.title || ""
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textMd
                            font.weight: row.modelData.read ? Font.Medium : Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            text: row.modelData.body || ""
                            color: Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textSm
                            wrapMode: Text.Wrap
                            maximumLineCount: row.modelData.kind === "announcement" ? 6 : 2
                            elide: Text.ElideRight
                        }
                        Text {
                            text: App.relativeTime(new Date(row.modelData.createdAt).toISOString())
                            color: Theme.textFaint
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textXs
                        }
                    }
                    Rectangle {
                        id: dot
                        anchors.right: parent.right
                        anchors.rightMargin: 18
                        y: 22
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.accent
                        visible: !row.modelData.read
                    }
                }
            }
        }

        StateView {
            anchors.top: head.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            visible: panel.items.length === 0
            task: panel.items.length === 0 && panel.task && panel.task.loading ? panel.task : null
            empty: panel.task !== null && panel.task.ok && panel.items.length === 0
            emptyTitle: qsTr("Nothing new")
            emptyText: qsTr("New chapters in your library, announcements and updates show up here.")
            emptyIcon: "bell"
        }
    }
}
