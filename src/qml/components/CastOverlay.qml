import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Over the player while an episode plays on a TV: what plays where, its
// position, and the controls, which go to the TV.
Rectangle {
    id: root
    property string title
    property bool hasNext: false
    signal stopRequested()
    signal nextRequested()

    color: "#0b0b0c"

    function formatTime(s) {
        s = Math.max(0, Math.floor(s || 0))
        const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = String(s % 60).padStart(2, "0")
        return h > 0 ? h + ":" + String(m).padStart(2, "0") + ":" + sec : m + ":" + sec
    }
    readonly property string stateText: Cast.state === "connecting" ? qsTr("Connecting…")
        : Cast.state === "buffering" ? qsTr("Loading…")
        : Cast.state === "paused" ? qsTr("Paused")
        : Cast.state === "ended" ? qsTr("Finished")
        : Cast.state === "error" ? (Cast.error || qsTr("Something went wrong"))
        : qsTr("Playing")

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 520)
        spacing: 14
        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "cast"; size: 34; color: Theme.accent }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Playing on %1").arg(Cast.deviceName)
            color: "white"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.textXl
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.title + " · " + root.stateText
            textFormat: Text.PlainText
            color: Cast.state === "error" ? Theme.danger : "#a1a1aa"
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
        Slider {
            id: bar
            width: parent.width
            from: 0
            to: Math.max(1, Cast.duration)
            enabled: Cast.duration > 0
            Binding on value { when: !bar.pressed; value: Cast.position }
            onPressedChanged: if (!pressed) Cast.seek(value)
            background: Rectangle {
                x: bar.leftPadding
                y: bar.topPadding + bar.availableHeight / 2 - height / 2
                width: bar.availableWidth
                height: 4
                radius: 2
                color: Qt.rgba(1, 1, 1, 0.18)
                Rectangle { width: bar.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
            }
            handle: Rectangle {
                x: bar.leftPadding + bar.visualPosition * (bar.availableWidth - width)
                y: bar.topPadding + bar.availableHeight / 2 - height / 2
                width: 14
                height: 14
                radius: 7
                color: "white"
                visible: bar.enabled
            }
        }
        Item {
            width: parent.width
            height: 18
            Text { text: root.formatTime(Cast.position); color: "#a1a1aa"; font.family: Theme.fontSans; font.pixelSize: Theme.textXs }
            Text { anchors.right: parent.right; text: Cast.duration > 0 ? root.formatTime(Cast.duration) : ""; color: "#a1a1aa"; font.family: Theme.fontSans; font.pixelSize: Theme.textXs }
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12
            IconButton { iconName: "rotate-ccw"; iconColor: "white"; diameter: 44; tip: qsTr("Back 10 seconds"); onClicked: Cast.seek(Math.max(0, Cast.position - 10)) }
            IconButton {
                iconName: Cast.state === "paused" ? "play" : "pause"
                iconColor: "white"
                diameter: 52
                iconSize: 24
                filled: true
                enabled: Cast.state === "playing" || Cast.state === "paused" || Cast.state === "buffering"
                onClicked: Cast.state === "paused" ? Cast.resume() : Cast.pause()
            }
            IconButton { iconName: "skip-forward"; iconColor: "white"; diameter: 44; visible: root.hasNext; tip: qsTr("Next episode"); onClicked: root.nextRequested() }
        }
        ScButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Stop casting")
            iconName: "x"
            onClicked: root.stopRequested()
        }
    }
}
