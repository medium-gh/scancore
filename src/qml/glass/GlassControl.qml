import QtQuick
import Scancore

// A control's glass in the Glass mode (buttons, chips, search fields): a
// capsule of glass over the wallpaper that swells a little under the
// pointer and squashes when pressed (wider, lower, a deeper lens), both on
// springs, so it moves like a drop of liquid. Goes inside the control's
// background, which turns see-through in the Glass mode.
Item {
    id: control
    property real radius: height / 2
    property bool pressed: false
    property bool hovered: false
    anchors.fill: parent
    visible: Theme.glass

    LiquidSpring { id: press; stiffness: 560; damping: 14; target: control.pressed ? 1 : 0 }
    LiquidSpring { id: hover; stiffness: 300; damping: 16; target: control.hovered ? 1 : 0 }

    GlassSurface {
        // Room to swell into.
        x: -6; y: -6
        width: control.width + 12
        height: control.height + 12
        backdrop: GlassScene.backdropFor(control)
        shadow: false
        bend: Math.min(10, control.height * 0.22)
        rimWidth: Math.min(12, control.height * 0.3)
        readonly property real sx: 1 + 0.025 * hover.value + 0.04 * press.value
        readonly property real sy: 1 + 0.025 * hover.value - 0.09 * press.value
        shapes: [{
            x: 6 + control.width * (1 - sx) / 2, y: 6 + control.height * (1 - sy) / 2,
            w: control.width * sx, h: control.height * sy,
            r: Math.min(control.radius * sy, control.height * sy / 2),
            lift: 0.8 * press.value + 0.3 * hover.value
        }]
    }
}
