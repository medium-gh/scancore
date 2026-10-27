import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Selectable pill used for filters and source pickers.
AbstractButton {
    id: chip
    property bool selected: checked
    property string iconName
    property string detail
    // After the label, as a menu's chevron.
    property string trailingIcon
    // A colour dot before the label (theme presets).
    property color dot: "transparent"

    checkable: false
    hoverEnabled: true
    implicitHeight: 32
    implicitWidth: row.implicitWidth + 26
    font.family: Theme.fontSans
    font.pixelSize: Theme.textSm
    font.weight: Font.Medium
    opacity: enabled ? 1 : 0.45

    background: Rectangle {
        radius: Theme.pill(height)
        color: chip.selected ? Theme.accent : chip.hovered ? Theme.surface3 : Theme.surface2
        border.width: chip.selected ? 0 : 1
        border.color: Theme.border
        scale: chip.pressed ? 0.96 : 1
        Behavior on color { ColorAnimation { duration: Motion.fast } }
        Behavior on scale { NumberAnimation { duration: Motion.fast } }
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            Rectangle {
                visible: chip.dot.a > 0
                width: 12
                height: 12
                radius: 6
                color: chip.dot
                border.width: 1
                border.color: Theme.alpha(chip.selected ? Theme.accentInk : Theme.text, 0.3)
                anchors.verticalCenter: parent.verticalCenter
            }
            Icon {
                visible: chip.iconName.length > 0
                name: chip.iconName
                size: 14
                color: chip.selected ? Theme.accentInk : Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: chip.text
                font: chip.font
                color: chip.selected ? Theme.accentInk : Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: chip.detail.length > 0
                text: chip.detail
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs
                color: chip.selected ? Theme.alpha(Theme.accentInk, 0.7) : Theme.textFaint
                anchors.verticalCenter: parent.verticalCenter
            }
            Icon {
                visible: chip.trailingIcon.length > 0
                name: chip.trailingIcon
                size: 14
                color: chip.selected ? Theme.accentInk : Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
