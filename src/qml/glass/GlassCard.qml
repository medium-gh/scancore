import QtQuick
import Scancore

// Glass for a card inside a page in the Glass mode (a settings group, a
// text field, a segmented switch): glass over the wallpaper, tinted toward
// the theme's surface so the lines on it stay easy to read. Goes inside the
// card's background, which turns see-through in the Glass mode.
Item {
    id: card
    property real radius: 12
    // How much of the surface colour (more for long text, less for small
    // controls).
    property real tint: 0.38
    property bool hovered: false
    anchors.fill: parent
    visible: Theme.glass

    LiquidSpring { id: hover; stiffness: 300; damping: 18; target: card.hovered ? 1 : 0 }

    GlassSurface {
        x: -2; y: -2
        width: card.width + 4
        height: card.height + 4
        backdrop: GlassScene.backdropFor(card)
        shadow: false
        tint: card.tint - 0.08 * hover.value
        tintColor: Theme.alpha(Theme.surface, 1)
        blurPx: 6
        wave: 3
        bend: Math.min(10, card.height * 0.2)
        rimWidth: Math.min(14, card.height * 0.3)
        shapes: [{ x: 2, y: 2, w: card.width, h: card.height, r: Math.min(card.radius, card.height / 2), lift: 0.3 * hover.value }]
    }
}
