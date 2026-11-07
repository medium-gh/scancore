import QtQuick
import QtQuick.Controls.Basic
import Scancore

// One movable, hideable part of a page (a shelf, the search bar, the
// spotlight…). Outside edit mode it is just its content; in edit mode it
// gets a bar to drag it, move it, hide it and change its look.
//
//   PageSection { arranger: arranger; key: "continue"; label: qsTr("Continue reading")
//       Shelf { … } }
//
// A Shelf inside follows the section's look (row or grid, size, count,
// subtitles, progress). `shown` says whether the content has anything to
// show; it reads the content's `hasContent` when it has one.
Item {
    id: section
    readonly property bool isPageSection: true
    property PageArranger arranger
    property string key
    property string label
    default property alias content: holder.data
    readonly property Item body: holder.children.length > 0 ? holder.children[0] : null
    property bool shown: body && body.hasContent !== undefined ? body.hasContent : true
    // Whether the Look menu applies (shelves).
    readonly property bool hasLook: !!body && body.look !== undefined

    readonly property bool editing: !!arranger && arranger.editing
    // Taken off the page: not shown even while arranging, and the page
    // loads nothing for it.
    readonly property bool removed: !!arranger && (PageLayout.revision, PageLayout.isRemoved(arranger.page, key))
    // A source's listing the user added (removing it deletes it).
    readonly property bool extra: key.startsWith("src:")
    readonly property bool userHidden: !!arranger && (PageLayout.revision, PageLayout.isHidden(arranger.page, key))
    readonly property var look: arranger ? (PageLayout.revision, PageLayout.look(arranger.page, key)) : PageLayout.defaultLook
    readonly property bool dragged: !!arranger && arranger.dragging === key

    component LookRow: Column {
        property alias title: caption.text
        width: parent ? parent.width : 300
        spacing: 6
        Text {
            id: caption
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
        }
    }

    width: parent ? parent.width : 800
    visible: !removed && (editing || (!userHidden && shown))
    height: removed ? 0 : (editing ? bar.height + (userHidden || !shown ? 0 : 10) : 0)
            + (userHidden && editing ? 0 : (shown ? holder.childrenRect.height : 0))
    z: dragged ? 10 : 0
    opacity: editing && userHidden ? 0.55 : 1

    onBodyChanged: if (body && body.look !== undefined) body.look = Qt.binding(() => section.look)

    // How far the section is dragged (edit mode), and how far it steps
    // aside to show where a dragged one would land.
    property real dragOffset: 0
    readonly property real roomShift: {
        if (!arranger || !arranger.dragging || arranger.dragging === key || arranger.dropIndex < 0) return 0
        const order = (PageLayout.revision, PageLayout.order(arranger.page, arranger.declared))
        const i = order.indexOf(key)
        const from = order.indexOf(arranger.dragging)
        const after = i > from ? i - 1 : i
        const lands = after >= arranger.dropIndex ? after + 1 : after
        return lands > i ? arranger.dragHeight : lands < i ? -arranger.dragHeight : 0
    }

    Item {
        id: mover
        y: section.dragOffset + section.roomShift
        Behavior on y { enabled: !section.dragged; NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } }
        width: parent.width
        height: parent.height

    // ------------------------------------------------ edit bar
    Rectangle {
        id: frame
        visible: section.editing
        x: Theme.gutter - 10
        width: parent.width - 2 * Theme.gutter + 20
        height: parent.height
        radius: Theme.radiusLg
        color: section.dragged ? Theme.alpha(Theme.accent, 0.08) : "transparent"
        border.width: 1
        border.color: section.dragged ? Theme.accent : Theme.border
    }
    Item {
        id: bar
        visible: section.editing
        x: Theme.gutter
        width: parent.width - 2 * Theme.gutter
        height: section.editing ? 48 : 0

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Item {
                width: 28
                height: 36
                visible: !Theme.touch
                Icon {
                    anchors.centerIn: parent
                    name: "grip-vertical"
                    size: 18
                    color: grip.active ? Theme.accent : Theme.textMuted
                }
                HoverHandler { cursorShape: grip.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
                DragHandler {
                    id: grip
                    target: null
                    xAxis.enabled: false
                    onActiveChanged: {
                        if (active) {
                            section.arranger.dragHeight = section.height + (section.parent ? section.parent.spacing || 0 : 0)
                            section.arranger.dragging = section.key
                        } else {
                            section.dragOffset = 0
                            section.arranger.drop(section.key)
                        }
                    }
                    onTranslationChanged: {
                        if (!active) return
                        section.dragOffset = translation.y
                        section.arranger.dropIndex = section.arranger.indexAt(section.key, section.y + translation.y + section.height / 2)
                    }
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: bar.width - tools.width - 48
                spacing: 1
                Text {
                    width: parent.width
                    text: section.label
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    visible: section.userHidden || !section.shown
                    text: section.userHidden ? qsTr("Hidden") : qsTr("Nothing to show right now")
                    elide: Text.ElideRight
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs
                }
            }
        }
        Row {
            id: tools
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            IconButton {
                visible: section.hasLook && !section.userHidden
                iconName: "sliders-horizontal"
                tip: qsTr("Look")
                active: lookPanel.opened
                onClicked: lookPanel.opened ? lookPanel.close() : lookPanel.open()
            }
            IconButton {
                iconName: "chevron-up"
                tip: qsTr("Move up")
                onClicked: section.arranger.move(section.key, -1)
            }
            IconButton {
                iconName: "chevron-down"
                tip: qsTr("Move down")
                onClicked: section.arranger.move(section.key, 1)
            }
            IconButton {
                visible: !section.extra
                iconName: section.userHidden ? "eye-off" : "eye"
                tip: section.userHidden ? qsTr("Show") : qsTr("Hide")
                onClicked: section.arranger.setHidden(section.key, !section.userHidden)
            }
            IconButton {
                visible: section.arranger && section.arranger.removable
                iconName: "trash"
                tip: qsTr("Remove")
                onClicked: section.extra ? PageLayout.removeExtra(section.arranger.page, section.key)
                                         : PageLayout.setRemoved(section.arranger.page, section.key, true)
            }
        }
    }

    // ------------------------------------------------ content
    Item {
        id: holder
        y: section.editing ? bar.height + 4 : 0
        width: parent.width
        height: childrenRect.height
        visible: !(section.editing && section.userHidden)
        // Arranging, not using: taps go to the edit bar.
        enabled: !section.editing
    }

    }

    // ------------------------------------------------ look
    Popup {
        id: lookPanel
        x: Math.max(Theme.gutter, parent.width - width - Theme.gutter)
        y: bar.height
        width: Math.min(360, section.width - 2 * Theme.gutter)
        padding: 16
        // Kept inside the window (it moves up near the bottom edge).
        margins: 12
        modal: false
        background: SurfacePanel {
            radius: Theme.radiusLg
            fill: Theme.surface
            border.width: 1
            border.color: Theme.border
        }
        contentItem: Column {
            spacing: 14
            LookRow {
                title: qsTr("Show as")
                Segmented {
                    options: [{ value: "row", label: qsTr("Row") }, { value: "grid", label: qsTr("Grid") }]
                    value: section.look.style
                    onPicked: (v) => PageLayout.setLook(section.arranger.page, section.key, "style", v)
                }
            }
            LookRow {
                title: qsTr("Card size")
                Segmented {
                    options: [{ value: "small", label: qsTr("Small") }, { value: "medium", label: qsTr("Medium") }, { value: "large", label: qsTr("Large") }]
                    value: section.look.size
                    onPicked: (v) => PageLayout.setLook(section.arranger.page, section.key, "size", v)
                }
            }
            LookRow {
                title: qsTr("How many")
                Segmented {
                    options: [{ value: 0, label: qsTr("All") }, { value: 6, label: "6" }, { value: 12, label: "12" }, { value: 24, label: "24" }]
                    value: section.look.count
                    onPicked: (v) => PageLayout.setLook(section.arranger.page, section.key, "count", v)
                }
            }
            Row {
                spacing: 10
                Toggle {
                    checked: section.look.subtitles
                    onToggled: PageLayout.setLook(section.arranger.page, section.key, "subtitles", checked)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Details under covers")
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                }
            }
            Row {
                spacing: 10
                Toggle {
                    checked: section.look.progress
                    onToggled: PageLayout.setLook(section.arranger.page, section.key, "progress", checked)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Progress bars")
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                }
            }
        }
    }
}
