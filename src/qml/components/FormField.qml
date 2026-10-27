import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Labelled text input with a hint or problem line underneath. `action`
// puts a small link at the end of the label row ("Forgot password?").
Column {
    id: field
    property alias text: input.text
    property alias placeholder: input.placeholderText
    property alias echoMode: input.echoMode
    property alias inputMethodHints: input.inputMethodHints
    property string label
    property string problem
    property string hint
    property string action
    signal accepted()
    signal actionClicked()

    function focusInput() { input.forceActiveFocus() }

    spacing: 6
    width: parent ? parent.width : 320

    Item {
        width: parent.width
        height: Math.max(labelText.implicitHeight, actionLink.visible ? actionLink.implicitHeight : 0)
        Text {
            id: labelText
            anchors.verticalCenter: parent.verticalCenter
            text: field.label
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            font.weight: Font.Medium
        }
        Text {
            id: actionLink
            visible: field.action.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: field.action
            color: actionArea.containsMouse || actionArea.activeFocus ? Theme.text : Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            font.weight: Font.Medium
            Behavior on color { ColorAnimation { duration: Motion.fast } }
            MouseArea {
                id: actionArea
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                activeFocusOnTab: true
                Accessible.role: Accessible.Link
                Accessible.name: field.action
                Keys.onReturnPressed: field.actionClicked()
                Keys.onSpacePressed: field.actionClicked()
                onClicked: field.actionClicked()
            }
        }
    }
    TextField {
        id: input
        width: parent.width
        implicitHeight: 46
        leftPadding: 14
        rightPadding: reveal.visible ? 44 : 14
        color: Theme.text
        placeholderTextColor: Theme.textFaint
        selectionColor: Theme.alpha(Theme.accent, 0.35)
        font.family: Theme.fontSans
        font.pixelSize: Theme.textMd
        selectByMouse: true
        onAccepted: field.accepted()
        // Focus brightens and thickens the field's own edge, inward, so
        // nothing around the field is covered.
        background: Rectangle {
            radius: Theme.radiusMd
            color: input.activeFocus ? Theme.surface : Theme.surface2
            border.width: input.activeFocus ? 1.5 : 1
            border.color: field.problem ? Theme.danger
                : input.activeFocus ? Theme.alpha(Theme.text, 0.6)
                : input.hovered ? Theme.borderStrong : Theme.border
            Behavior on border.color { ColorAnimation { duration: Motion.fast } }
            Behavior on color { ColorAnimation { duration: Motion.fast } }
        }
        IconButton {
            id: reveal
            visible: field.echoMode === TextInput.Password || reveal.showing
            property bool showing: false
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            diameter: 32
            iconSize: 16
            iconName: showing ? "eye-off" : "eye"
            tip: showing ? qsTr("Hide password") : qsTr("Show password")
            onClicked: {
                showing = !showing
                input.echoMode = showing ? TextInput.Normal : TextInput.Password
            }
        }
    }
    Text {
        visible: text.length > 0
        width: parent.width
        text: field.problem || field.hint
        color: field.problem ? Theme.danger : Theme.textFaint
        font.family: Theme.fontSans
        font.pixelSize: Theme.textXs + 1
        wrapMode: Text.Wrap
    }
}
