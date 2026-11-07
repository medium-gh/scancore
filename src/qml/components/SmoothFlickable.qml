import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Vertical Flickable with eased mouse-wheel scrolling and touchpad scrolling
// at the user's speed (WheelScroll).
Flickable {
    id: flick
    property alias wheelStep: wheel.wheelStep
    // Where an eased scroll is heading (contentY when none is running).
    readonly property real target: wheel.ease.running ? wheel.target : contentY
    readonly property real maxY: wheel.maxY

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    maximumFlickVelocity: Motion.flickVelocity
    flickDeceleration: Motion.flickDeceleration
    ScrollBar.vertical: ScScrollBar {}

    // Glass mode on phones: the page scrolls clear of the floating bar, and
    // scrolling down draws the bar in (GlassScene).
    bottomMargin: GlassScene.clearance
    property real lastY: 0
    onContentYChanged: {
        if (GlassScene.on && visible && moving)
            GlassScene.scrolled(contentY - lastY, contentY <= originY + 4)
        lastY = contentY
    }

    function scrollTo(y, animated) { wheel.scrollTo(y, animated) }

    WheelScroll { id: wheel }
}
