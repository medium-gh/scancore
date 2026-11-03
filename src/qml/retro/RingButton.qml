import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Windows Phone's round button: an icon in a thin ring that fills when
// pressed (or when `checked`), with its name under it when there is one.
AbstractButton {
    id: button
    property string iconName
    property string label: ""
    property int diameter: 48
    property color ink: Theme.text
    // Drawn filled, as a toggle that is on.
    property bool lit: false
    readonly property bool filled: pressed || lit

    implicitWidth: Math.max(diameter, label.length ? caption.implicitWidth : 0)
    implicitHeight: diameter + (label.length ? caption.implicitHeight + 6 : 0)
    focusPolicy: Qt.TabFocus
    Accessible.name: label || iconName
    opacity: enabled ? 1 : 0.4
    background: null
    contentItem: Item {
        Rectangle {
            id: ring
            anchors.horizontalCenter: parent.horizontalCenter
            width: button.diameter
            height: button.diameter
            radius: width / 2
            color: button.filled ? button.ink : "transparent"
            border.width: Math.max(2, Math.round(button.diameter / 24))
            border.color: button.activeFocus && !button.filled ? Theme.tile : button.ink
            Icon {
                anchors.centerIn: parent
                name: button.iconName
                size: Math.round(button.diameter * 0.42)
                color: button.filled ? Theme.base : button.ink
            }
        }
        Text {
            id: caption
            visible: button.label.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: ring.height + 6
            text: button.label
            color: button.ink
            font.family: Theme.fontSans
            font.pixelSize: Theme.textXs + 1
        }
    }
}
