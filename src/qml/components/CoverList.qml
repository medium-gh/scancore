import QtQuick
import QtQuick.Controls.Basic
import Scancore

// The list view of a library: one row per series with a small cover, title,
// subtitle, progress and a badge. Like CoverGrid, it lives inside a
// scrolling page and only the rows on screen (and a few either side) exist,
// reused in a ring as the page scrolls.
Item {
    id: list
    property var items: []
    property var subtitleFor: (item) => ""
    property var badgeFor: (item) => ""
    property var progressFor: (item) => -1
    // Whether a row is picked in a selection.
    property var markedFor: (item) => false
    property real rowHeight: Theme.compact ? 72 : 80
    signal opened(var item, Item coverItem)

    width: parent ? parent.width - 2 * Theme.gutter : 800
    x: Theme.gutter
    height: items.length * rowHeight

    readonly property Flickable view: {
        for (let p = list.parent; p; p = p.parent)
            if (p instanceof Flickable) return p as Flickable
        return null
    }
    readonly property int bufferRows: 4
    readonly property int windowRows: view ? Math.ceil(view.height / rowHeight) + 1 + 2 * bufferRows : items.length
    readonly property int slots: Math.min(items.length, windowRows)
    readonly property real offsetInView: {
        if (!view) return 0
        // A bare read would be dropped by the QML compiler (no update).
        App.dependOn(list.y, view.contentHeight)
        return list.mapToItem(view.contentItem, 0, 0).y
    }
    readonly property int firstIndex: {
        if (!view || items.length === 0) return 0
        const top = view.contentY - offsetInView
        return Math.max(0, Math.min(items.length - windowRows, Math.floor(top / rowHeight) - bufferRows))
    }

    Repeater {
        model: list.slots
        delegate: AbstractButton {
            id: row
            required property int index
            readonly property int itemIndex: {
                const offset = ((row.index - list.firstIndex) % list.slots + list.slots) % list.slots
                return list.firstIndex + offset
            }
            readonly property var entry: row.itemIndex < list.items.length ? list.items[row.itemIndex] : null
            readonly property real progress: row.entry ? list.progressFor(row.entry) : -1
            readonly property string badge: row.entry ? list.badgeFor(row.entry) : ""
            readonly property bool marked: row.entry ? list.markedFor(row.entry) : false
            readonly property string referer: !row.entry ? ""
                : row.entry.referer || (row.entry.sourceUrl && !row.entry.sourceUrl.startsWith("http")
                    ? (row.entry.media === "comic" ? Comics.imageReferer(row.entry.sourceUrl)
                       : row.entry.media === "novel" ? Novels.imageReferer(row.entry.sourceUrl) : "") : "")
            visible: row.entry !== null
            y: row.itemIndex * list.rowHeight
            width: list.width
            height: list.rowHeight
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            onClicked: list.opened(row.entry, cover)
            // Its options, as on a cover card.
            onPressAndHold: if (row.entry) Router.seriesMenu(row.entry, row, row.width / 2, row.height / 2)
            TapHandler {
                acceptedButtons: Qt.RightButton
                // Mice only: touch taps ignore acceptedButtons (a tap would open it).
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onTapped: (point) => { if (row.entry) Router.seriesMenu(row.entry, row, point.position.x, point.position.y) }
            }

            background: Rectangle {
                x: -10
                width: parent.width + 20
                height: parent.height - 4
                y: 2
                radius: Theme.radiusLg
                color: row.marked ? Theme.alpha(Theme.accent, 0.16) : row.hovered || row.visualFocus ? Theme.surface2 : "transparent"
                border.width: row.marked ? 1 : 0
                border.color: Theme.alpha(Theme.accent, 0.5)
                Behavior on color { ColorAnimation { duration: Motion.fast } }
            }
            contentItem: Item {
                Cover {
                    id: cover
                    anchors.verticalCenter: parent.verticalCenter
                    height: list.rowHeight - 16
                    width: Math.round(height / 1.45)
                    radius: 7
                    source: row.entry ? (row.entry.cover || "") : ""
                    referer: row.referer
                    title: row.entry ? (row.entry.title || "") : ""
                }
                Column {
                    anchors.left: cover.right
                    anchors.leftMargin: 14
                    anchors.right: badgeBox.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        width: parent.width
                        text: row.entry ? (row.entry.title || "") : ""
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: row.entry ? list.subtitleFor(row.entry) : ""
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: row.progress >= 0
                        width: Math.min(parent.width, 220)
                        height: 3
                        radius: 1.5
                        color: Theme.alpha(Theme.text, 0.12)
                        Rectangle {
                            width: parent.width * Math.max(0.04, Math.min(1, row.progress))
                            height: parent.height
                            radius: 1.5
                            color: Theme.text
                        }
                    }
                }
                Rectangle {
                    id: badgeBox
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.badge.length > 0
                    height: 22
                    width: visible ? badgeText.implicitWidth + 14 : 0
                    radius: 11
                    color: Theme.accent
                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: row.badge
                        color: Theme.accentInk
                        font.family: Theme.fontSans
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                }
            }
        }
    }
}
