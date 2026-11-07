import QtQuick
import QtQuick.Effects
import Scancore

// One liquid glass surface: up to four rounded shapes (in this item's
// coordinates) joined by a smooth union, over GlassScene.backdrop.
//   shapes: [{ x, y, w, h, r, lift (0..1), inset (a lens inside) }]
// Things on the glass go in a GlassInk on top (they are inked by what is
// behind). Where shaders cannot run, a plain see-through fill instead.
Item {
    id: surface
    property var shapes: []
    property real merge: 18
    property real blurPx: 3
    property real wave: 8
    property real waveScale: 70
    property real seed: 3
    property real tint: 0.12
    property color tintColor: "white"
    property real bend: 12
    property real rimWidth: 16
    // A soft, close shadow under the outer shapes.
    property bool shadow: true

    // What this glass shows: the window's stage for the bars; glass inside
    // a page sets the layer right behind it (the hero art, the wallpaper).
    property Item backdrop: GlassScene.backdrop
    readonly property bool shaded: !!backdrop && GraphicsInfo.api !== GraphicsInfo.Software

    // Where this item is on the backdrop, 0..1. mapToItem is not a binding:
    // every ancestor's position is read here, so a move anywhere redoes it.
    readonly property rect area: {
        const b = surface.backdrop
        if (!b || b.width <= 0 || b.height <= 0) return Qt.rect(0, 0, 1, 1)
        let moved = b.x + b.y + b.width + b.height + surface.width + surface.height
        for (let p = surface; p; p = p.parent) moved += p.x + p.y + p.scale
        const at = surface.mapToItem(b, 0, 0)
        return Qt.rect(at.x / b.width, at.y / b.height, surface.width / b.width, surface.height / b.height)
    }

    // A shape with no size (a droplet melted away) is moved far off the
    // surface: at zero width it would still leave a one-pixel line.
    function shape(i) {
        const s = shapes[i]
        return s && s.w >= 1 && s.h >= 1 ? Qt.vector4d(s.x, s.y, s.w, s.h) : Qt.vector4d(-10000, -10000, 1, 1)
    }
    function field(i, key, fallback) { const s = shapes[i]; return s && s[key] !== undefined ? s[key] : fallback }

    // Shadows: one layer of the outer shapes, inset a little so only the
    // shadow shows past the glass.
    Item {
        id: shadowShapes
        anchors.fill: parent
        visible: surface.shadow
        layer.enabled: visible
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: "#3a000000"; shadowBlur: 0.55; shadowVerticalOffset: 6 }
        Repeater {
            model: surface.shapes.length
            Rectangle {
                required property int index
                readonly property var s: surface.shapes[index] || ({})
                visible: !s.inset && s.w > 6 && s.h > 6
                x: (s.x || 0) + 2; y: (s.y || 0) + 2
                width: Math.max(0, (s.w || 0) - 4); height: Math.max(0, (s.h || 0) - 4)
                radius: Math.max(0, Math.min(s.r || 0, Math.min(width, height) / 2))
                color: "black"
            }
        }
    }

    ShaderEffect {
        anchors.fill: parent
        visible: surface.shaded
        property Item backdrop: surface.backdrop
        readonly property size size: Qt.size(width, height)
        readonly property rect area: surface.area
        readonly property size texel: surface.backdrop ? Qt.size(1 / surface.backdrop.width, 1 / surface.backdrop.height) : Qt.size(0, 0)
        readonly property vector4d s0: surface.shape(0)
        readonly property vector4d s1: surface.shape(1)
        readonly property vector4d s2: surface.shape(2)
        readonly property vector4d s3: surface.shape(3)
        readonly property vector4d radii: Qt.vector4d(surface.field(0, "r", 0), surface.field(1, "r", 0), surface.field(2, "r", 0), surface.field(3, "r", 0))
        readonly property vector4d lift: Qt.vector4d(surface.field(0, "lift", 0), surface.field(1, "lift", 0), surface.field(2, "lift", 0), surface.field(3, "lift", 0))
        readonly property vector4d inset: Qt.vector4d(surface.field(0, "inset", false) ? 1 : 0, surface.field(1, "inset", false) ? 1 : 0,
                                                     surface.field(2, "inset", false) ? 1 : 0, surface.field(3, "inset", false) ? 1 : 0)
        readonly property real count: Math.min(4, surface.shapes.length)
        readonly property real merge: surface.merge
        readonly property real blurPx: surface.blurPx
        readonly property real wave: surface.wave
        readonly property real waveScale: surface.waveScale
        readonly property real seed: surface.seed
        readonly property real tint: surface.tint
        readonly property color tintColor: surface.tintColor
        readonly property real bend: surface.bend
        readonly property real rimWidth: surface.rimWidth
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
    }

    // No shaders: the shapes as a plain see-through fill.
    Repeater {
        model: surface.shaded ? 0 : surface.shapes.length
        Rectangle {
            required property int index
            readonly property var s: surface.shapes[index] || ({})
            x: s.x || 0; y: s.y || 0; width: s.w || 0; height: s.h || 0
            radius: Math.min(s.r || 0, Math.min(width, height) / 2)
            color: Theme.alpha(Theme.surface, s.inset ? 0.5 : 0.86)
            border.width: 1
            border.color: Theme.alpha(Theme.text, 0.12)
        }
    }
}
