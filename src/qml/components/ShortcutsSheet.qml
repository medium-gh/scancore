import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Every keyboard shortcut on one sheet, as set in Settings → Keyboard. Opens
// with ? or from the command palette.
Popup {
    id: sheet
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, parent.width - 48)
    height: Math.min(columns.implicitHeight + 112, parent.height - 80)
    modal: true
    focus: true
    padding: 0

    // Keys that cannot be changed, listed with the ones that can.
    readonly property var fixed: ({
        app: [["Esc", qsTr("Back, or close the reader")]],
        reader: [["←  →", qsTr("Previous or next page or chapter")]],
        player: [["Esc", qsTr("Leave full screen, then close")]]
    })
    readonly property var groups: Keymap.scopes.map(scope => ({
        title: scope.title,
        keys: Keymap.actions.filter(a => a.scope === scope.scope && Keymap.sequences(a.id).length > 0)
            .map(a => [Keymap.labels(a.id).join("  "), a.label])
            .concat(fixed[scope.scope] || [])
    }))

    Overlay.modal: Rectangle { color: Theme.overlay }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.95; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
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

    contentItem: Item {
        Row {
            id: head
            x: 24
            y: 22
            spacing: 12
            Rectangle {
                width: 36
                height: 36
                radius: 10
                color: Theme.surface2
                Icon { anchors.centerIn: parent; name: "keyboard"; size: 18; color: Theme.textMuted }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Keyboard shortcuts")
                rightPadding: 12
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textXl
                font.weight: Font.DemiBold
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 14
            y: 18
            spacing: 6
            ScButton {
                compact: true
                text: qsTr("Change shortcuts")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    sheet.close()
                    App.setSetting("settingsCategory", "keyboard")
                    Router.go("settings")
                }
            }
            IconButton {
                iconName: "x"
                tip: qsTr("Close (Esc)")
                onClicked: sheet.close()
            }
        }

        Flickable {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            WheelScroll {}
            anchors.fill: parent
            anchors.topMargin: 76
            anchors.bottomMargin: 12
            contentHeight: columns.implicitHeight + 12
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScScrollBar {}

            Flow {
                id: columns
                x: 24
                width: parent.width - 48
                spacing: 28
                readonly property real columnWidth: width >= 620 ? (width - spacing) / 2 : width
                Repeater {
                    model: sheet.groups
                    Column {
                        required property var modelData
                        width: columns.columnWidth
                        spacing: 6
                        Text {
                            bottomPadding: 4
                            text: modelData.title
                            color: Theme.textFaint
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textXs
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 1.1
                        }
                        Repeater {
                            model: modelData.keys
                            Item {
                                required property var modelData
                                width: columns.columnWidth
                                height: 30
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - keycap.width - 12
                                    text: modelData[1]
                                    color: Theme.text
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.textSm
                                    elide: Text.ElideRight
                                }
                                Rectangle {
                                    id: keycap
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 24
                                    width: keyText.implicitWidth + 16
                                    radius: 6
                                    color: Theme.surface2
                                    border.width: 1
                                    border.color: Theme.borderStrong
                                    Text {
                                        id: keyText
                                        anchors.centerIn: parent
                                        text: modelData[0]
                                        color: Theme.textMuted
                                        font.family: Theme.fontSans
                                        font.pixelSize: Theme.textXs + 1
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
