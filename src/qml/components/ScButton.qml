import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Text button. variant: "primary" | "secondary" | "ghost" | "plain" | "danger".
// iconAfter puts the icon after the text (an arrow on "Continue").
AbstractButton {
    id: control
    property string variant: "secondary"
    property string iconName
    property bool loading: false
    property bool compact: false
    property bool iconAfter: false

    readonly property color fill: variant === "primary" ? Theme.accent
        : variant === "secondary" ? Theme.surface2
        : variant === "danger" ? Theme.alpha(Theme.danger, 0.14) : "transparent"
    readonly property color ink: variant === "primary" ? Theme.accentInk
        : variant === "danger" ? Theme.danger : variant === "plain" ? Theme.textMuted : Theme.text

    implicitHeight: compact ? 30 : 38
    implicitWidth: Math.max(implicitHeight, row.implicitWidth + (compact ? 22 : 30))
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    font.family: Theme.fontSans
    font.pixelSize: compact ? Theme.textSm : Theme.textMd
    font.weight: Font.DemiBold

    background: Rectangle {
        radius: Theme.pill(height)
        color: control.fill
        border.width: control.variant === "ghost" ? 1 : 0
        border.color: Theme.border
        scale: control.pressed ? 0.97 : 1
        Behavior on scale { NumberAnimation { duration: Motion.fast; easing.type: Easing.OutQuad } }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: control.variant === "primary" ? Theme.accentInk : Theme.text
            opacity: control.pressed ? 0.12 : control.hovered ? 0.06 : 0
            Behavior on opacity { NumberAnimation { duration: Motion.fast } }
        }
        // Keyboard focus: a ring just inside the edge, never over what is
        // next to the button.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Theme.pill(height)
            color: "transparent"
            border.width: 1.5
            border.color: control.variant === "primary" ? Theme.alpha(Theme.accentInk, 0.7) : Theme.alpha(Theme.text, 0.55)
            visible: control.visualFocus
        }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.iconAfter ? 6 : 8
            layoutDirection: control.iconAfter ? Qt.RightToLeft : Qt.LeftToRight
            Spinner {
                visible: control.loading
                size: 14
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Icon {
                visible: control.iconName.length > 0 && !control.loading
                name: control.iconName
                size: control.compact ? 14 : 16
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: control.text.length > 0
                text: control.text
                font: control.font
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
