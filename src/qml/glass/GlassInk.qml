import QtQuick
import Scancore

// What sits on glass (text and icons, drawn white): inked by what is behind
// the glass, per pixel, dark over light content and light over dark, with
// a soft halo of the other tone so it stands out from busy content.
// Coloured things (an accent, a cover) keep their colours; filled controls
// that must keep theirs go beside it, not inside.
Item {
    id: ink
    // What this glass shows: the window's stage for the bars; glass inside
    // a page sets the layer right behind it (the hero art, the wallpaper).
    property Item backdrop: GlassScene.backdrop
    readonly property bool shaded: !!backdrop && GraphicsInfo.api !== GraphicsInfo.Software
    // The halo's strength and reach.
    property real halo: 0.42
    property real haloRadius: 3.5
    // Coloured content (an accent, a cover) keeps its colour; otherwise only
    // what is drawn counts and it all takes the ink.
    property bool keepColour: false

    readonly property rect area: {
        const b = ink.backdrop
        if (!b || b.width <= 0 || b.height <= 0) return Qt.rect(0, 0, 1, 1)
        let moved = b.x + b.y + b.width + b.height + ink.width + ink.height
        for (let p = ink; p; p = p.parent) moved += p.x + p.y + p.scale
        const at = ink.mapToItem(b, 0, 0)
        return Qt.rect(at.x / b.width, at.y / b.height, ink.width / b.width, ink.height / b.height)
    }

    layer.enabled: shaded && width > 0 && height > 0
    // At the screen's own resolution (the default is one texel per point,
    // which stretches text on scaled displays).
    layer.textureSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
    layer.smooth: true
    layer.effect: ShaderEffect {
        property Item backdrop: ink.backdrop
        readonly property rect area: ink.area
        readonly property vector4d dark: Qt.vector4d(0.09, 0.09, 0.11, 1)
        readonly property vector4d light: Qt.vector4d(1, 1, 1, 1)
        readonly property size pixel: Qt.size(1 / Math.max(1, ink.width), 1 / Math.max(1, ink.height))  // per point: the halo's reach is in points
        readonly property real halo: ink.halo
        readonly property real haloRadius: ink.haloRadius
        readonly property real keepColour: ink.keepColour ? 1 : 0
        fragmentShader: "qrc:/shaders/ink.frag.qsb"
    }
}
