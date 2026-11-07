import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Dropdown. model: [{value, label}] ; bind `value`, handle `picked`.
// On phones the choices open as a sheet from the bottom, titled `title`
// (or the accessible name).
ComboBox {
    id: control
    property var value
    property string title: Accessible.name
    readonly property bool sheet: Theme.compact
    signal picked(var value)

    textRole: "label"
    valueRole: "value"
    implicitHeight: 36
    implicitWidth: Math.max(140, contentItem.implicitWidth + 48)
    font.family: Theme.fontSans
    font.pixelSize: Theme.textSm
    hoverEnabled: true

    Component.onCompleted: currentIndex = Math.max(0, indexOfValue(value))
    onValueChanged: currentIndex = Math.max(0, indexOfValue(value))
    onModelChanged: currentIndex = Math.max(0, indexOfValue(value))
    onActivated: (index) => picked(valueAt(index))

    // A TV's remote: while the list is closed the D-pad moves on to the next
    // control (a ComboBox would step through its choices instead, and the
    // focus got stuck there), and Select opens the list; in it the D-pad
    // picks and Select chooses.
    property bool openedByRemote: false
    Keys.onPressed: (event) => {
        if (!App.tv || popup.visible) return
        const dirs = { [Qt.Key_Left]: "left", [Qt.Key_Right]: "right", [Qt.Key_Up]: "up", [Qt.Key_Down]: "down" }
        if (dirs[event.key] !== undefined) {
            event.accepted = true
            if (Router.shell) Router.shell.moveFocus(dirs[event.key])
        } else if (event.key === Qt.Key_Select || event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
            event.accepted = true
            if (event.isAutoRepeat) return
            openedByRemote = true
            popup.open()
        }
    }
    // The release of the press that opened the list would choose at once.
    // ComboBox chooses on Enter and Return only; a remote's Select chooses too.
    Keys.onReleased: (event) => {
        if (!App.tv || (event.key !== Qt.Key_Select && event.key !== Qt.Key_Enter && event.key !== Qt.Key_Return)) return
        if (openedByRemote) {
            event.accepted = true
            openedByRemote = false
        } else if (event.key === Qt.Key_Select && popup.visible) {
            event.accepted = true
            const index = highlightedIndex
            popup.close()
            if (index >= 0 && index !== currentIndex) {
                currentIndex = index
                picked(valueAt(index))
            }
        }
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.hovered || control.popup.visible ? Theme.surface3 : Theme.surface2
        border.width: 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: Motion.fast } }
    }
    contentItem: Text {
        leftPadding: 14
        rightPadding: 30
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: control.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: 14
        color: Theme.textMuted
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } }
    }
    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        readonly property bool chosen: control.currentIndex === option.index
        width: ListView.view ? ListView.view.width : 200
        height: control.sheet ? 48 : 34
        hoverEnabled: true
        contentItem: Item {
            Text {
                anchors.left: parent.left
                anchors.right: tick.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: option.modelData.label
                color: Theme.text
                font.family: Theme.fontSans
                font.pixelSize: control.sheet ? Theme.textMd : Theme.textSm
                font.weight: option.chosen ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
            Icon {
                id: tick
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: control.sheet && option.chosen
                width: visible ? 18 : 0
                name: "check"
                size: 18
                color: Theme.accent
            }
        }
        leftPadding: control.sheet ? 16 : 12
        rightPadding: control.sheet ? 16 : 12
        // The choice the arrow keys or a TV's remote are on (the mouse
        // moves it too), so a remote can see what Select will pick.
        readonly property bool keyed: control.highlightedIndex === option.index
        background: Rectangle {
            radius: control.sheet ? Theme.radiusMd : Theme.radiusSm
            color: option.pressed ? Theme.surface3 : option.keyed || option.hovered ? Theme.surface3
                : option.chosen && control.sheet ? Theme.surface2 : "transparent"
            border.width: option.keyed && App.tv ? 2 : 0
            border.color: Theme.accentIsMono ? Theme.text : Theme.accent
        }
    }
    popup: Popup {
        id: menu
        readonly property real safeBottom: control.sheet && parent ? parent.SafeArea.margins.bottom : 0
        parent: control.sheet ? Overlay.overlay : control
        x: 0
        y: control.sheet ? (parent ? parent.height - height : 0) : control.height + 6
        width: control.sheet ? (parent ? parent.width : 400) : Math.max(control.width, 180)
        implicitHeight: control.sheet
            ? Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (parent ? parent.height : 800) * 0.75)
            : Math.min(contentItem.implicitHeight + 12, 360)
        padding: control.sheet ? 10 : 6
        topPadding: control.sheet ? (control.title.length ? 64 : 28) : 6
        bottomPadding: control.sheet ? 14 + safeBottom : 6
        modal: control.sheet
        dim: control.sheet
        transformOrigin: Item.Top
        Overlay.modal: Rectangle { color: Theme.overlay }
        enter: control.sheet ? riseIn : dropIn
        exit: control.sheet ? riseOut : dropOut
        property Transition dropIn: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.fast }
                NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            }
        }
        property Transition dropOut: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Motion.instant } }
        property Transition riseIn: Transition {
            NumberAnimation { property: "y"; from: menu.parent ? menu.parent.height : 800; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
        }
        property Transition riseOut: Transition {
            NumberAnimation { property: "y"; to: menu.parent ? menu.parent.height : 800; duration: Motion.normal; easing.type: Easing.InCubic }
        }
        contentItem: ListView {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            spacing: control.sheet ? 2 : 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: SurfacePanel {
            radius: Theme.radiusMd
            // Glass: a list pours down out of its field, a sheet up from the edge.
            origin: control.sheet ? Qt.point(width / 2, height) : Qt.point(width / 2, 0)
            topLeftRadius: control.sheet ? Theme.radiusXl : Theme.radiusMd
            topRightRadius: control.sheet ? Theme.radiusXl : Theme.radiusMd
            bottomLeftRadius: control.sheet ? 0 : Theme.radiusMd
            bottomRightRadius: control.sheet ? 0 : Theme.radiusMd
            fill: Theme.surface
            border.width: control.sheet ? 0 : 1
            border.color: Theme.borderStrong
            Rectangle {
                visible: control.sheet
                anchors.horizontalCenter: parent.horizontalCenter
                y: 10
                width: 36; height: 4; radius: 2
                color: Theme.alpha(Theme.text, 0.2)
            }
            Text {
                visible: control.sheet && control.title.length > 0
                x: 26
                y: 28
                text: control.title
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
            }
        }
    }
}
