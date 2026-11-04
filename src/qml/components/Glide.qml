import QtQuick
import Scancore

// Touch flings as Android's own lists fling (OverScroller, which Mihon and
// other native apps use): the finger's speed over its last 100 ms (as
// Android's VelocityTracker measures it), up to 8000 dp/s, carried a spline
// distance that grows faster than the speed, so a quick swipe goes far and
// a gentle one settles soon. Flings in a row the same way add up (as on iOS
// and in Chrome), so long lists go fast. Qt's fling still runs it (a touch
// stops it); only its speed and braking come from here.
//
// Inside a Flickable or ListView (vertical or horizontal), or with `flick`.
Connections {
    property Flickable flick
    target: flick
    // A plain Flickable has no orientation (or its signal).
    ignoreUnknownSignals: true
    enabled: Theme.touch && !Motion.reduced
    // Read when the list or its orientation changes, not bound: a ListView
    // rewrites its flickableDirection while it sets its orientation, and a
    // binding on both looped for every shelf and chip row (#35).
    property bool horizontal: false
    function readDirection() {
        horizontal = !!flick && (flick.orientation !== undefined ? flick.orientation === Qt.Horizontal
                                                                 : flick.flickableDirection === Flickable.HorizontalFlick)
    }
    onFlickChanged: readDirection()
    Component.onCompleted: readDirection()
    function onOrientationChanged() { readDirection() }
    function onFlickableDirectionChanged() { readDirection() }
    // The finger's last positions, and the fling that last ran (to add to).
    property var state: ({ samples: [], v: 0, decel: 0, at: 0, pressedAt: 0 })

    function position() { return horizontal ? flick.contentX : flick.contentY }
    function note() {
        if (!flick.dragging) return
        const now = Date.now()
        const s = state.samples
        s.push([now, position()])
        while (s.length > 2 && now - s[0][0] > 100) s.shift()
    }
    function onContentYChanged() { if (!horizontal) note() }
    function onContentXChanged() { if (horizontal) note() }
    // The finger lifting starts the glide: Qt's Flickable often starts no
    // fling of its own on Android (its speed estimate from the last touch
    // events comes out too low), which is why lists stopped dead.
    function onDraggingChanged() {
        if (!flick.dragging) {
            start()
            return
        }
        state.samples = []
        state.pressedAt = Date.now()
    }

    // Content pixels a second (positive: contentY or contentX growing), a
    // least-squares line through the last 100 ms; 0 when the finger had
    // stopped before it lifted.
    function releaseVelocity() {
        const s = state.samples
        if (s.length < 2 || Date.now() - s[s.length - 1][0] > 40) return 0
        const t0 = s[0][0]
        let n = 0, st = 0, sp = 0, stt = 0, stp = 0
        for (const [t, p] of s) {
            const x = (t - t0) / 1000
            n += 1; st += x; sp += p; stt += x * x; stp += x * p
        }
        const d = n * stt - st * st
        return d > 1e-9 ? (n * stp - st * sp) / d : 0
    }

    function start() {
        let v = releaseVelocity()
        if (state.samples.length < 2) return // nothing measured: Qt's own
        const sign = v < 0 ? -1 : 1
        v = Math.min(Math.abs(v), Motion.flingMax)
        // What was left of the last fling when this swipe began, if it went
        // the same way.
        if (state.v !== 0 && Math.sign(state.v) === sign && v > 800) {
            const left = Math.abs(state.v) - state.decel * (state.pressedAt - state.at) / 1000
            if (left > 0) v = Math.min(v + left, Motion.flingMax * 3)
        }
        if (v < 50) {
            flick.cancelFlick()
            return
        }
        const distance = Motion.flingDistance(v)
        const decel = v * v / (2 * distance)
        state.v = sign * v
        state.decel = decel
        state.at = Date.now()
        Qt.callLater(() => {
            if (!flick || flick.dragging) return
            flick.flickDeceleration = decel
            if (flick.maximumFlickVelocity < v) flick.maximumFlickVelocity = v
            // flick() takes the content's movement the other way round.
            if (horizontal) flick.flick(-sign * v, 0)
            else flick.flick(0, -sign * v)
        })
    }
}
