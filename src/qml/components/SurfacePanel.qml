import QtQuick
import QtQuick.Effects
import Scancore

// A menu's, sheet's or dialog's background. With glass on (Settings →
// Theme), what is behind it shows through, blurred, under a see-through
// `fill`; otherwise it is `fill` as it is. The window behind is copied once
// when the panel has opened (and again when it moves or changes size), not
// every frame, so an open menu costs no more than a plain one.
Rectangle {
    id: panel
    property color fill: Theme.surface
    // How much of `fill` covers the blur.
    property real tint: Theme.dark ? 0.72 : 0.78
    // A sheet along the bottom edge: square corners there.
    property bool squareBottom: false

    // The window's own content (an ApplicationWindow's contentItem, which
    // the popups' overlay is not part of); null where that is not so.
    readonly property Item behind: {
        const win = panel.Window.window
        const item = win ? win.contentItem : null
        for (let p = panel.parent; p; p = p.parent)
            if (p === item) return null
        return item
    }
    readonly property bool active: Theme.glass && visible && width > 0 && height > 0 && !!behind
                                   && GraphicsInfo.api !== GraphicsInfo.Software

    // Plain until the blur is there, then fading to see-through.
    color: active && settled ? Theme.alpha(fill, tint) : fill
    Behavior on color { ColorAnimation { duration: Motion.fast } }
    bottomLeftRadius: squareBottom ? 0 : radius
    bottomRightRadius: squareBottom ? 0 : radius

    // The blur is made once the panel has settled (after a popup's opening
    // animation: one made while it was still see-through stays empty), and
    // made again when the panel moves or changes size.
    property bool settled: false
    function unsettle() {
        settled = false
        if (active) settle.restart()
    }
    onActiveChanged: unsettle()
    onWidthChanged: unsettle()
    onHeightChanged: unsettle()
    onXChanged: unsettle()
    onYChanged: unsettle()
    // The popup it is the background of, sliding or growing into place.
    Connections {
        target: panel.parent
        function onXChanged() { panel.unsettle() }
        function onYChanged() { panel.unsettle() }
        function onScaleChanged() { panel.unsettle() }
    }
    Timer { id: settle; interval: 250; onTriggered: panel.settled = panel.active }

    Loader {
        anchors.fill: parent
        z: -1
        active: panel.active && panel.settled
        sourceComponent: Item {
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: shape
            }
            // What is behind, on the window's own background (which its
            // content item does not hold): the blur gets an opaque copy, as
            // one with see-through gaps smears and lets the sharp copy show.
            Item {
                id: backdrop
                anchors.fill: parent
                Rectangle {
                    anchors.fill: parent
                    color: Theme.base
                }
                ShaderEffectSource {
                    anchors.fill: parent
                    sourceItem: panel.behind
                    sourceRect: {
                        const at = panel.mapToItem(panel.behind, 0, 0)
                        return Qt.rect(at.x, at.y, panel.width, panel.height)
                    }
                    // Copied once; drawn under the blur (one that is not
                    // drawn and not live is never filled).
                    live: false
                }
            }
            MultiEffect {
                anchors.fill: parent
                source: backdrop
                autoPaddingEnabled: false
                blurEnabled: true
                blur: 1
                blurMax: 64
                blurMultiplier: 1.5
            }
        }
    }
    Rectangle {
        id: shape
        anchors.fill: parent
        visible: false
        layer.enabled: panel.active
        radius: panel.radius
        bottomLeftRadius: panel.bottomLeftRadius
        bottomRightRadius: panel.bottomRightRadius
    }
}
