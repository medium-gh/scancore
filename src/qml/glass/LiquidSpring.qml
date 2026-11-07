import QtQuick
import Scancore

// A spring that keeps its velocity: it overshoots a little and settles, as
// a liquid does, and a new target mid-flight bends the motion instead of
// restarting it. Glass pieces read `velocity` to stretch along their way.
// It steps itself on the window's frames while moving (FrameAnimation) and
// stops when settled, so a resting piece of glass costs nothing.
// Reduce motion: jumps to the target.
QtObject {
    id: spring
    property real target: 0
    // Not a binding to target: it follows it by physics. It starts there.
    property real value: 0
    property real velocity: 0
    property bool started: false
    Component.onCompleted: { value = target; started = true }
    // Looser than a UI spring on purpose: visible overshoot and settle.
    property real stiffness: 240
    property real damping: 17
    readonly property bool moving: driver.running

    function snap(v) { target = v; value = v; velocity = 0 }

    onTargetChanged: {
        if (!started || Motion.reduced) { value = target; velocity = 0; return }
        driver.running = true
    }

    property FrameAnimation driver: FrameAnimation {
        running: false
        onTriggered: {
            // At most 1/30 s per frame (a stall must not fling it), in 4 ms slices.
            let left = Math.min(frameTime, 1 / 30) * (Motion.scale > 0 ? 1 / Motion.scale : 1)
            let v = spring.velocity, x = spring.value
            while (left > 0) {
                const h = Math.min(left, 0.004)
                v += (spring.stiffness * (spring.target - x) - spring.damping * v) * h
                x += v * h
                left -= h
            }
            if (Math.abs(spring.target - x) < 0.05 && Math.abs(v) < 0.5) {
                spring.value = spring.target
                spring.velocity = 0
                running = false
                return
            }
            spring.value = x
            spring.velocity = v
        }
    }
}
