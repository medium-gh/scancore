import QtQuick
import Scancore

// A short code typed into one slot per character (two groups of three for
// six, as on the website). One hidden input takes the typing (and pasting),
// so backspace and phone keyboards just work. The slots share the width they
// are given and never grow past 52 px, so they always fit.
// `digits`: numbers only (a reset code). `invalid` shakes the slots once.
Item {
    id: boxes
    property int length: 6
    property bool digits: false
    property bool invalid: false
    property string label: qsTr("Access code")
    property alias text: input.text
    readonly property bool complete: input.text.length === length
    signal accepted()
    // Every slot filled by typing or pasting.
    signal completed(string code)

    function focusInput() { input.forceActiveFocus() }
    function clear() { input.text = "" }

    readonly property bool grouped: length === 6
    readonly property int gap: 8
    readonly property int dashRoom: grouped ? 22 : 0
    readonly property real slotWidth: Math.max(30, Math.min(52, (width - (length - 1) * gap - dashRoom) / length))

    width: parent ? parent.width : implicitWidth
    implicitWidth: length * 52 + (length - 1) * gap + dashRoom
    implicitHeight: 56

    onInvalidChanged: if (invalid) shake.restart()

    TextInput {
        id: input
        width: 1
        height: 1
        opacity: 0
        // Room for a pasted dash or spaces, removed below.
        maximumLength: boxes.length + 4
        inputMethodHints: boxes.digits ? Qt.ImhDigitsOnly
            : Qt.ImhPreferUppercase | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhLatinOnly
        validator: RegularExpressionValidator { regularExpression: boxes.digits ? /[0-9 -]*/ : /[A-Za-z0-9 -]*/ }
        // Upper case, without the dash or spaces people paste with it.
        onTextEdited: {
            const clean = (boxes.digits ? text.replace(/\D/g, "") : text.toUpperCase().replace(/[^A-Z0-9]/g, ""))
                .slice(0, boxes.length)
            if (clean !== text) text = clean
            if (clean.length === boxes.length) boxes.completed(clean)
        }
        onAccepted: boxes.accepted()
        Accessible.name: boxes.label
    }

    Row {
        id: row
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: boxes.gap
        transform: Translate { id: nudge }
        Repeater {
            model: boxes.length
            Item {
                id: cell
                required property int index
                readonly property bool filled: index < input.text.length
                readonly property bool current: input.activeFocus
                    && index === Math.min(input.text.length, boxes.length - 1)
                // The dash between the groups lives in front of the fourth slot.
                readonly property bool afterDash: boxes.grouped && index === 3
                width: boxes.slotWidth + (afterDash ? boxes.dashRoom : 0)
                height: 56
                Rectangle {
                    visible: cell.afterDash
                    x: (boxes.dashRoom - width) / 2 - boxes.gap / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 2
                    radius: 1
                    color: Theme.borderStrong
                }
                Rectangle {
                    x: cell.afterDash ? boxes.dashRoom : 0
                    width: boxes.slotWidth
                    height: parent.height
                    radius: Theme.radiusMd + 2
                    color: cell.filled || cell.current ? Theme.surface : Theme.surface2
                    border.width: cell.current ? 2 : 1
                    border.color: boxes.invalid ? Theme.danger
                        : cell.current ? Theme.alpha(Theme.text, 0.7)
                        : cell.filled ? Theme.alpha(Theme.text, 0.3) : Theme.borderStrong
                    scale: cell.filled ? 1 : 0.98
                    Behavior on border.color { ColorAnimation { duration: Motion.fast } }
                    Behavior on color { ColorAnimation { duration: Motion.fast } }
                    Behavior on scale { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutBack } }
                    Text {
                        anchors.centerIn: parent
                        text: input.text.charAt(cell.index)
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                    // The caret, in the slot where the next character goes.
                    Rectangle {
                        visible: cell.current && input.text.length <= cell.index && input.cursorVisible
                        anchors.centerIn: parent
                        width: 2
                        height: 22
                        radius: 1
                        color: Theme.text
                    }
                }
            }
        }
    }

    SequentialAnimation {
        id: shake
        loops: 1
        NumberAnimation { target: nudge; property: "x"; to: -6; duration: Motion.ms(50) }
        NumberAnimation { target: nudge; property: "x"; to: 6; duration: Motion.ms(80) }
        NumberAnimation { target: nudge; property: "x"; to: -4; duration: Motion.ms(70) }
        NumberAnimation { target: nudge; property: "x"; to: 0; duration: Motion.ms(60) }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.IBeamCursor
        onClicked: input.forceActiveFocus()
    }
}
