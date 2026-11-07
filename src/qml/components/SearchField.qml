import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Pill search input with icon, clear button and focus glow.
TextField {
    id: field
    property bool large: false
    signal submitted(string text)

    implicitHeight: large ? (Theme.glass ? 48 : 52) : 40
    leftPadding: large ? 48 : 40
    rightPadding: clearButton.visible ? 40 : 16
    font.family: Theme.fontSans
    font.pixelSize: large ? Theme.textLg : Theme.textMd
    color: Theme.text
    placeholderTextColor: Theme.glass ? Theme.textMuted : Theme.textFaint
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    selectedTextColor: Theme.text
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter

    // Android keyboards otherwise show "Next", which moves focus to the
    // clear button instead of searching.
    EnterKey.type: Qt.EnterKeySearch
    onAccepted: {
        submitted(text.trim())
        if (Theme.touch) Qt.inputMethod.hide()
    }
    Keys.onEscapePressed: {
        if (text.length > 0) clear()
        else focus = false
    }

    background: Rectangle {
        radius: Theme.pill(height)
        // Glass: the field is a glass capsule.
        GlassControl { radius: parent.radius; pressed: false; hovered: field.hovered || field.activeFocus }
        color: Theme.glass ? "transparent" : field.activeFocus ? Theme.surface : Theme.surface2
        border.width: Theme.glass && !field.activeFocus ? 0 : 1
        border.color: field.activeFocus ? Theme.alpha(Theme.accent, 0.55) : (field.hovered ? Theme.borderStrong : Theme.border)
        Behavior on color { ColorAnimation { duration: Motion.fast } }
        Behavior on border.color { ColorAnimation { duration: Motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: Theme.pill(height)
            color: "transparent"
            border.width: 4
            border.color: Theme.alpha(Theme.accent, 0.12)
            opacity: field.activeFocus ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        }

        Icon {
            anchors.left: parent.left
            anchors.leftMargin: field.large ? 18 : 14
            anchors.verticalCenter: parent.verticalCenter
            name: "search"
            size: field.large ? 18 : 16
            color: field.activeFocus ? Theme.text : Theme.textFaint
        }
    }

    IconButton {
        id: clearButton
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        diameter: 30
        iconSize: 14
        iconName: "x"
        visible: field.text.length > 0
        onClicked: {
            field.clear()
            field.forceActiveFocus()
        }
    }
}
