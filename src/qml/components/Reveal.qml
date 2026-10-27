import QtQuick
import Scancore

// Shows its content with a height ease and a fade, and hides it the same
// way. Closed, it takes no room at all: `gap` (the space above the content)
// goes with it, so a Column of Reveals with spacing 0 never jumps.
Item {
    id: reveal
    property bool open: true
    property int gap: 0
    default property alias content: holder.data

    width: parent ? parent.width : 0
    height: open ? holder.implicitHeight + gap : 0
    opacity: open ? 1 : 0
    visible: open || heightEase.running || opacity > 0
    clip: heightEase.running || !open

    Behavior on height {
        NumberAnimation { id: heightEase; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized }
    }
    Behavior on opacity { NumberAnimation { duration: reveal.open ? Motion.normal : Motion.fast } }

    Column {
        id: holder
        y: reveal.gap
        width: parent.width
    }
}
