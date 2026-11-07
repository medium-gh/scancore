import QtQuick
import Scancore

// A menu's, sheet's or dialog's background: `fill`, rounded as asked, with
// square corners along the bottom for a sheet that sits on the bottom edge.
// In the Glass mode it is glass over the page (a heavier kind than the
// bars': more blur, tinted toward the theme's surface, so lines of text
// stay easy to read), and it pours out when it opens: from a small capsule
// at `origin` to its full shape, on a spring that overshoots a little.
Rectangle {
    id: panel
    property color fill: Theme.surface
    // A sheet along the bottom edge: square corners there.
    property bool squareBottom: false
    // Glass: where it pours out from, in its own coordinates: a sheet from
    // the bottom edge, anything else from its centre unless it says (a
    // menu from where it was opened, a list from its field).
    readonly property bool sheetLike: squareBottom || bottomLeftRadius === 0
    property point origin: sheetLike ? Qt.point(width / 2, height) : Qt.point(width / 2, height / 2)

    color: Theme.glass ? "transparent" : fill
    bottomLeftRadius: squareBottom ? 0 : radius
    bottomRightRadius: squareBottom ? 0 : radius
    // Glass draws its own edge: no border (whatever the panel asked for).
    Binding { target: panel.border; property: "width"; value: 0; when: Theme.glass }

    LiquidSpring { id: pour; stiffness: 260; damping: 19 }
    function startPour() { if (Theme.glass && visible) { pour.snap(0); pour.target = 1 } }
    onVisibleChanged: startPour()
    // The frosted backdrop is made while a panel shows.
    readonly property bool frosting: Theme.glass && visible
    onFrostingChanged: GlassScene.frostUsers += frosting ? 1 : -1
    Component.onCompleted: { startPour(); if (frosting) GlassScene.frostUsers += 1 }
    Component.onDestruction: if (frosting) GlassScene.frostUsers -= 1

    GlassSurface {
        id: glassFill
        visible: Theme.glass
        // Room for the overshoot.
        readonly property real m: 10
        // A sheet's square bottom: its rounding runs on past the bottom edge.
        readonly property real cornerR: Math.max(panel.radius, panel.topLeftRadius)
        readonly property real extra: panel.sheetLike ? cornerR : 0
        x: -m; y: -m
        width: panel.width + 2 * m
        height: panel.height + 2 * m + extra
        backdrop: GlassScene.frosted || GlassScene.backdrop
        tint: 0.5
        tintColor: Theme.alpha(panel.fill, 1)
        blurPx: 3
        wave: 3
        readonly property real p: pour.value
        // The capsule it starts as, about the origin.
        readonly property real sw: Math.min(panel.width, 140)
        readonly property real sh: Math.min(panel.height, 44)
        readonly property real sx: Math.max(0, Math.min(panel.width - sw, panel.origin.x - sw / 2))
        readonly property real sy: Math.max(0, Math.min(panel.height - sh, panel.origin.y - sh / 2))
        readonly property real w: Math.max(1, sw + (panel.width - sw) * p)
        readonly property real h: Math.max(1, sh + (panel.height - sh) * p)
        shapes: [{
            x: m + sx * (1 - p) - (p > 1 ? (w - panel.width) / 2 : 0),
            y: m + sy * (1 - p) - (p > 1 ? (h - panel.height) / 2 : 0),
            w: w, h: h + extra,
            r: Math.min(w, h) / 2 * Math.max(0, 1 - p) + Math.min(cornerR, Math.min(w, h) / 2) * Math.min(1, p),
            lift: Math.min(1, Math.abs(pour.velocity) / 6)
        }]
    }
}
