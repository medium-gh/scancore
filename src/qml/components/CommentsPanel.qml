import QtQuick
import QtQuick.Controls.Basic
import Scancore

// The comments of the chapter or episode being read or watched, in a panel
// that slides in from the right (from the bottom on phones).
Drawer {
    id: panel
    property string media: "comic"
    property string title
    property string comickHid
    property int anilistId: 0
    property string url
    property string unit
    // "Chapter 12", "Episode 3"
    property string label
    property bool seen: false
    onAboutToShow: seen = true

    edge: Theme.compact ? Qt.BottomEdge : Qt.RightEdge
    width: Theme.compact ? parent.width : Math.min(460, parent.width)
    height: Theme.compact ? parent.height * 0.85 : parent.height
    parent: Overlay.overlay
    modal: true
    dim: true
    Overlay.modal: Rectangle { color: Theme.overlay }
    background: GlassPanel {
        fill: Theme.surface
        radius: Theme.compact ? Theme.radiusXl : 0
        Rectangle {
            visible: !Theme.compact
            width: 1
            height: parent.height
            color: Theme.border
        }
    }

    contentItem: Item {
        Row {
            id: head
            x: 20
            y: 16
            width: parent.width - 40
            spacing: 8
            Column {
                width: parent.width - close.width - 8
                Text {
                    text: panel.label
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textXl
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: panel.title
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
            }
            IconButton {
                id: close
                iconName: "x"
                tip: qsTr("Close")
                onClicked: panel.close()
            }
        }
        SmoothFlickable {
            anchors.fill: parent
            anchors.topMargin: head.y + head.height + 16
            contentHeight: loader.height + 40
            clip: true
            Loader {
                id: loader
                x: 20
                width: parent.width - 40
                // Loaded on first open, so readers make no requests otherwise.
                active: panel.seen
                sourceComponent: Discussion {
                    media: panel.media
                    title: panel.title
                    comickHid: panel.comickHid
                    anilistId: panel.anilistId
                    url: panel.url
                    unit: panel.unit
                }
            }
        }
    }
}
