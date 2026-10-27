import QtQuick
import QtQuick.Controls.Basic
import Scancore

// A small menu of actions, opened at a point (a right click or a long
// press). items: [{text, icon, danger, enabled, checked, run}]; a falsy
// entry is a divider.
Popup {
    id: menu
    property var items: []

    // Opens with its top-left at (x, y) of `item`, kept inside the window.
    // A list too long for the room below that point scrolls there, unless
    // the room is small; then it moves up.
    property real room: 600
    function openAt(item, px, py, list) {
        items = list
        const p = item.mapToItem(Overlay.overlay, px, py)
        const w = Overlay.overlay.width, h = Overlay.overlay.height
        room = h - p.y - 8 >= Math.min(h / 2, 240) ? h - p.y - 8 : h - 16
        x = Math.max(8, Math.min(p.x, w - implicitWidth - 8))
        y = Math.max(8, Math.min(p.y, h - implicitHeight - 8))
        open()
    }
    // The list is laid out after openAt placed it.
    onImplicitHeightChanged: if (visible && parent) y = Math.max(8, Math.min(y, parent.height - implicitHeight - 8))

    parent: Overlay.overlay
    padding: 6
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    implicitWidth: 240

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.fast }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast } }

    background: GlassPanel {
        radius: Theme.radiusLg
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    // A long list (an episode's servers) scrolls inside the window.
    contentItem: Flickable {
        implicitHeight: Math.min(list.implicitHeight, menu.room - menu.topPadding - menu.bottomPadding)
        contentHeight: list.implicitHeight
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        clip: interactive
        WheelScroll {}
        Column {
            id: list
            width: parent.width
            spacing: 2
            Repeater {
                model: menu.items
                Item {
                    id: entry
                    required property var modelData
                    width: menu.availableWidth
                    height: modelData ? 38 : 9
                    Rectangle {
                        visible: !entry.modelData
                        anchors.centerIn: parent
                        width: parent.width - 12
                        height: 1
                        color: Theme.border
                    }
                    AbstractButton {
                        id: button
                        visible: !!entry.modelData
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !!entry.modelData && entry.modelData.enabled !== false
                        opacity: enabled ? 1 : 0.4
                        onClicked: {
                            const run = entry.modelData.run
                            menu.close()
                            if (run) run()
                        }
                        background: Rectangle {
                            radius: Theme.radiusMd
                            color: button.hovered || button.visualFocus ? Theme.surface3 : "transparent"
                        }
                        contentItem: Item {
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                leftPadding: 10
                                spacing: 10
                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: entry.modelData ? (entry.modelData.icon || "") : ""
                                    visible: name.length > 0
                                    size: 16
                                    color: entry.modelData && entry.modelData.danger ? Theme.danger : Theme.textMuted
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    // Room for the icon before and the check after.
                                    width: Math.min(implicitWidth, button.width - 10 - (entry.modelData && entry.modelData.icon ? 26 : 0) - 36)
                                    elide: Text.ElideRight
                                    text: entry.modelData ? entry.modelData.text : ""
                                    color: entry.modelData && entry.modelData.danger ? Theme.danger : Theme.text
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.textSm
                                    font.weight: entry.modelData && entry.modelData.checked ? Font.DemiBold : Font.Normal
                                }
                            }
                            Icon {
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !!entry.modelData && !!entry.modelData.checked
                                name: "check"
                                size: 16
                                color: Theme.accent
                            }
                        }
                    }
                }
            }
        }
    }
}
