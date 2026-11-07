import QtQuick
import QtQuick.Controls.Basic
import Scancore

// The open-source software inside SCANCORE and the licences it comes under
// (resources/licenses). Choosing one shows its licence text.
Dialog {
    id: dialog
    property var items: []
    // The text being read, or "" for the list.
    property string file: ""
    property string heading: ""
    readonly property string text: file ? App.licenceText(file) : ""

    function start() {
        items = App.licences()
        file = ""
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, (parent ? parent.width : 560) - 32)
    height: (parent ? parent.height : 720) - 64
    modal: true
    focus: true
    padding: 24
    topPadding: 4
    title: ""

    Overlay.modal: Rectangle { color: Theme.overlay }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast }
            NumberAnimation { property: "scale"; to: 0.97; duration: Motion.fast }
        }
    }

    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    header: Item {
        implicitHeight: 64
        IconButton {
            id: backButton
            visible: dialog.file !== ""
            x: 12
            y: 14
            iconName: "arrow-left"
            tip: qsTr("Back")
            onClicked: dialog.file = ""
        }
        Text {
            x: dialog.file ? 56 : 24
            y: 22
            width: parent.width - x - 64
            text: dialog.file ? dialog.heading : qsTr("Licences")
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.textLg
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        IconButton { anchors.right: parent.right; anchors.rightMargin: 14; y: 14; iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
    }

    contentItem: Item {
        Text {
            id: intro
            width: parent.width
            visible: dialog.file === ""
            bottomPadding: 12
            text: qsTr("SCANCORE is built with this open-source software. Choose one to read its licence.")
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
        ListView {
            id: list
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            anchors.fill: parent
            anchors.topMargin: intro.height
            visible: dialog.file === ""
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.items
            WheelScroll {}
            ScrollBar.vertical: ScScrollBar {}
            delegate: AbstractButton {
                id: row
                required property var modelData
                width: list.width
                height: 58
                hoverEnabled: true
                onClicked: {
                    dialog.heading = modelData.name
                    dialog.file = modelData.file
                }
                background: Rectangle {
                    radius: Theme.radiusMd
                    color: row.hovered ? Theme.surface2 : "transparent"
                }
                contentItem: Column {
                    leftPadding: 8
                    rightPadding: 8
                    topPadding: 9
                    spacing: 2
                    Text {
                        width: row.width - 16
                        text: row.modelData.name
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        width: row.width - 16
                        text: row.modelData.licence + " · " + row.modelData.use
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs + 1
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Flickable {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            anchors.fill: parent
            visible: dialog.file !== ""
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            WheelScroll {}
            ScrollBar.vertical: ScScrollBar {}
            TextEdit {
                id: body
                width: parent.width - 8
                text: dialog.text
                readOnly: true
                selectByMouse: true
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: "monospace"
                font.pixelSize: Theme.textXs + 1
            }
        }
    }
}
