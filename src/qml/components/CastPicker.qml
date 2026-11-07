import QtQuick
import QtQuick.Controls.Basic
import Scancore

// The TVs and Chromecasts on this network, to cast an episode to. Looks for
// them while open; picking one emits picked(device).
Dialog {
    id: dialog
    signal picked(var device)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(400, (parent ? parent.width : 400) - 32)
    modal: true
    focus: true
    padding: 20
    onOpened: Cast.search()

    Overlay.modal: Rectangle { color: Theme.overlay }
    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    contentItem: Column {
        spacing: 12
        Row {
            spacing: 10
            Icon { name: "cast"; size: 20; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Cast to a TV")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
            }
            Spinner { visible: Cast.searching; size: 16; anchors.verticalCenter: parent.verticalCenter }
        }
        Repeater {
            model: Cast.devices
            AbstractButton {
                id: row
                required property var modelData
                width: parent.width
                height: 48
                hoverEnabled: true
                onClicked: {
                    dialog.close()
                    dialog.picked(modelData)
                }
                background: Rectangle {
                    radius: Theme.radiusMd
                    color: row.hovered ? Theme.alpha(Theme.text, 0.07) : Theme.surface2
                }
                contentItem: Row {
                    leftPadding: 12
                    spacing: 12
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: row.modelData.kind === "chromecast" ? "cast" : "tv"; size: 18; color: Theme.textMuted }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { text: row.modelData.name; color: Theme.text; font.family: Theme.fontSans; font.pixelSize: Theme.textMd; font.weight: Font.Medium }
                        Text {
                            text: row.modelData.kind === "chromecast" ? qsTr("Chromecast") : qsTr("TV (DLNA)")
                            color: Theme.textFaint
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textXs
                        }
                    }
                }
            }
        }
        Text {
            width: parent.width
            visible: Cast.devices.length === 0
            text: Cast.searching ? qsTr("Looking on this network…")
                : qsTr("Nothing found. The TV or Chromecast needs to be on and on the same Wi-Fi as this device.")
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
        Row {
            anchors.right: parent.right
            spacing: 8
            ScButton { text: qsTr("Look again"); iconName: "refresh-cw"; enabled: !Cast.searching; onClicked: Cast.search() }
            ScButton { text: qsTr("Close"); variant: "ghost"; onClicked: dialog.close() }
        }
    }
}
