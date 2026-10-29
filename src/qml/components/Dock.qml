import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Navigation as a floating dock along the top or bottom of the window: every
// section as an icon that grows under the pointer, with its name above (or
// below) it, a highlight that slides to the current one, and search and
// settings at the ends.
Rectangle {
    id: dock
    property string current: "home"
    property string side: "bottom"
    signal navigate(string section)
    signal openPalette()

    readonly property var sections: [
        { key: "search", label: qsTr("Search"), icon: "search" },
        { key: "divider" },
        { key: "home", label: qsTr("Home"), icon: "house" },
        { key: "comics", label: qsTr("Comics"), icon: "book-open" },
        { key: "novels", label: qsTr("Novels"), icon: "book-text" },
        { key: "anime", label: qsTr("Anime"), icon: "tv" },
        { key: "library", label: qsTr("Library"), icon: "library" },
        { key: "history", label: qsTr("History"), icon: "history" },
        { key: "activity", label: qsTr("Activity"), icon: "activity" },
        { key: "latest", label: qsTr("Latest"), icon: "flame" },
        { key: "downloads", label: qsTr("Downloads"), icon: "download" },
        { key: "notifications", label: qsTr("Notifications"), icon: "bell" },
        { key: "divider" },
        { key: "settings", label: qsTr("Settings"), icon: "settings" }
    ]

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 60
    radius: height / 2
    color: Theme.layered ? Theme.sidebar : Theme.alpha(Theme.surface, 0.97)
    border.width: 1
    border.color: Theme.borderStrong

    // Sliding highlight behind the current section.
    Rectangle {
        id: highlight
        readonly property Item target: {
            for (let i = 0; i < buttons.count; ++i) {
                const it = buttons.itemAt(i)
                if (it && it.key === dock.current) return it
            }
            return null
        }
        visible: target !== null
        width: 46
        height: 46
        radius: 23
        y: (dock.height - height) / 2
        x: target ? row.x + target.x + (target.width - width) / 2 : 0
        color: Theme.alpha(Theme.text, Theme.dark ? 0.1 : 0.08)
        Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: dock.side === "top" ? -5 : parent.height + 2
            width: 16
            height: 3
            radius: 2
            color: Theme.accent
        }
    }

    Row {
        id: row
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Repeater {
            id: buttons
            model: dock.sections
            delegate: Item {
                id: slot
                required property var modelData
                readonly property string key: modelData.key
                width: key === "divider" ? 13 : 48
                height: 48

                Rectangle {
                    visible: slot.key === "divider"
                    anchors.centerIn: parent
                    width: 1
                    height: 26
                    color: Theme.border
                }

                AbstractButton {
                    id: button
                    visible: slot.key !== "divider"
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: slot.key === "search" ? dock.openPalette() : dock.navigate(slot.key)
                    background: null
                    contentItem: Item {
                        Icon {
                            anchors.centerIn: parent
                            name: slot.modelData.icon || ""
                            size: 20
                            color: dock.current === slot.key ? Theme.text : button.hovered ? Theme.text : Theme.textMuted
                            scale: button.pressed ? 0.86 : button.hovered ? 1.18 : 1
                            Behavior on scale { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutBack } }
                        }
                        Rectangle {
                            readonly property int count: slot.key === "library" ? Library.unreadUpdates
                                : slot.key === "downloads" ? Downloads.pending : 0
                            visible: count > 0
                            x: parent.width / 2 + 7
                            y: 9
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.accent
                        }
                    }
                }

                // The section's name, floating clear of the dock.
                Rectangle {
                    visible: opacity > 0
                    opacity: button.hovered ? 1 : 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: dock.side === "top" ? parent.height + 14 + (button.hovered ? 0 : -4) : -height - 14 + (button.hovered ? 0 : 4)
                    width: tipText.implicitWidth + 16
                    height: 26
                    radius: 8
                    color: Theme.surface3
                    border.width: 1
                    border.color: Theme.borderStrong
                    Behavior on opacity { NumberAnimation { duration: Motion.fast } }
                    Behavior on y { NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic } }
                    Text {
                        id: tipText
                        anchors.centerIn: parent
                        text: slot.modelData.label || ""
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs + 1
                        font.weight: Font.Medium
                    }
                }
            }
        }
    }
}
