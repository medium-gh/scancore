import QtQuick
import QtQuick.Controls.Basic
import Scancore

// The player's one settings panel (Quality, Audio, Subtitles, Speed, Fit,
// Server, Episodes and the player's actions), in the player's own dark look
// and inside its bounds: a sheet from the bottom on phones, a panel above
// the controls on desktop. A row with choices opens them in place; Back or
// a tap outside closes it.
//   sections: [{key, icon, label, value, choices: [{label, detail, selected, pick}]}]
//   actions:  [{icon, label, run, enabled}]
Item {
    id: panel
    property var sections: []
    property var actions: []
    property bool sheet: App.mobile
    // The section whose choices show ("" for the main list).
    property string page: ""
    readonly property bool opened: visible
    readonly property var current: sections.find(s => s.key === page) || null

    anchors.fill: parent
    visible: false
    z: 20

    function open(section) {
        page = section || ""
        visible = true
        box.forceActiveFocus()
    }
    function close() { visible = false; page = "" }
    // Back: out of a section first, then closed. True when it did something.
    function back() {
        if (!visible) return false
        if (page !== "") page = ""
        else close()
        return true
    }

    // Outside the panel: a tap closes it, and nothing reaches the video.
    MouseArea {
        anchors.fill: parent
        onClicked: panel.close()
        onWheel: (wheel) => wheel.accepted = true
        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, panel.sheet ? 0.45 : 0.15) }
    }

    Rectangle {
        id: box
        focus: true
        Keys.onEscapePressed: panel.back()
        Keys.onBackPressed: (event) => event.accepted = panel.back()
        readonly property real maxHeight: panel.sheet ? Math.min(panel.height - 24, Math.max(240, panel.height * (panel.height > panel.width ? 0.75 : 0.9)))
                                                      : panel.height - 96
        width: panel.sheet ? Math.min(panel.width, 560) : Math.min(320, panel.width - 24)
        height: Math.min(maxHeight, header.height + (panel.current ? choiceList.contentHeight : mainColumn.implicitHeight) + 12)
        x: panel.sheet ? (panel.width - width) / 2 : panel.width - width - 16
        y: panel.sheet ? panel.height - height : panel.height - height - 84
        radius: Theme.radiusLg
        color: "#141416"
        border.width: 1
        border.color: "#2a2a30"
        // A sheet's lower corners sit on the screen's edge.
        Rectangle {
            visible: panel.sheet
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.radius
            color: parent.color
        }
        // Taps inside stay inside.
        MouseArea { anchors.fill: parent; onWheel: (wheel) => wheel.accepted = true }

        Item {
            id: header
            width: parent.width
            height: 48
            Rectangle {
                visible: panel.sheet && !panel.current
                anchors.horizontalCenter: parent.horizontalCenter
                y: 8
                width: 36
                height: 4
                radius: 2
                color: "#3f3f46"
            }
            IconButton {
                id: backButton
                visible: panel.current !== null
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                iconName: "arrow-left"
                iconColor: "white"
                tip: qsTr("Back")
                onClicked: panel.page = ""
            }
            Text {
                anchors.left: backButton.visible ? backButton.right : parent.left
                anchors.leftMargin: backButton.visible ? 4 : 18
                anchors.right: closeButton.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: panel.sheet && !panel.current ? 4 : 0
                text: panel.current ? panel.current.label : qsTr("Settings")
                color: "white"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            IconButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                iconName: "x"
                iconColor: "#a1a1aa"
                tip: qsTr("Close")
                onClicked: panel.close()
            }
        }

        // The main list: one row per setting, then the actions.
        Flickable {
            id: mainFlick
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            visible: !panel.current
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            width: parent.width
            contentHeight: mainColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScScrollBar {}
            Column {
                id: mainColumn
                width: parent.width
                Repeater {
                    model: panel.sections
                    PanelRow {
                        required property var modelData
                        iconName: modelData.icon
                        label: modelData.label
                        value: modelData.value || ""
                        chevron: true
                        onClicked: panel.page = modelData.key
                    }
                }
                Rectangle {
                    visible: panel.sections.length > 0 && panel.actions.length > 0
                    x: 16
                    width: parent.width - 32
                    height: 1
                    color: "#26262b"
                }
                Repeater {
                    model: panel.actions
                    PanelRow {
                        required property var modelData
                        iconName: modelData.icon
                        label: modelData.label
                        enabled: modelData.enabled !== false
                        onClicked: {
                            panel.close()
                            modelData.run()
                        }
                    }
                }
            }
        }

        // One setting's choices (a list: servers and episodes can be many).
        ListView {
            id: choiceList
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            visible: panel.current !== null
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            width: parent.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScScrollBar {}
            model: panel.current ? panel.current.choices : []
            onModelChanged: {
                // The chosen one in view.
                const list = Array.from(model || [])
                const at = list.findIndex(c => c.selected)
                if (at > 3) Qt.callLater(() => choiceList.positionViewAtIndex(at, ListView.Center))
            }
            delegate: PanelRow {
                required property var modelData
                width: choiceList.width
                label: modelData.label
                value: modelData.detail || ""
                chosen: modelData.selected === true
                choice: true
                onClicked: {
                    modelData.pick()
                    // Choices that act at once close the panel; a list one
                    // goes back to the main list.
                    if (panel.current && panel.current.stay) panel.page = ""
                    else panel.close()
                }
            }
        }
    }

    component PanelRow: AbstractButton {
        id: row
        property string iconName
        property string label
        property string value
        property bool chevron: false
        // One of a list: a check when it is the one chosen.
        property bool choice: false
        property bool chosen: false
        width: parent ? parent.width : 300
        height: 48
        hoverEnabled: true
        opacity: enabled ? 1 : 0.4
        background: Rectangle {
            x: 6
            width: row.width - 12
            height: row.height
            radius: Theme.radiusMd
            color: row.pressed ? "#26262b" : row.hovered ? "#1e1e22" : "transparent"
        }
        contentItem: Item {
            Icon {
                id: rowIcon
                visible: row.iconName.length > 0 || row.choice
                x: 18
                anchors.verticalCenter: parent.verticalCenter
                name: row.choice ? "check" : row.iconName
                size: 18
                color: "white"
                opacity: row.choice && !row.chosen ? 0 : 1
            }
            Text {
                anchors.left: rowIcon.visible ? rowIcon.right : parent.left
                anchors.leftMargin: rowIcon.visible ? 14 : 18
                anchors.right: valueText.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: row.label
                color: "white"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm + 1
                elide: Text.ElideRight
            }
            Text {
                id: valueText
                anchors.right: chevronIcon.visible ? chevronIcon.left : parent.right
                anchors.rightMargin: chevronIcon.visible ? 6 : 18
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, row.width * 0.45)
                text: row.value
                color: "#a1a1aa"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
            }
            Icon {
                id: chevronIcon
                visible: row.chevron
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-right"
                size: 16
                color: "#71717a"
            }
        }
    }
}
