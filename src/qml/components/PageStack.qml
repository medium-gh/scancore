import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Per-section navigation stack. Pages slide and fade with a slight depth
// shift; the page underneath recedes rather than vanishing. In the Retro
// mode pages turn like a door about the screen's left edge instead
// (Windows Phone's turnstile): the old one swings away, the new one in.
StackView {
    id: stack
    property string initial

    Component.onCompleted: if (initial) push(Qt.resolvedUrl("../" + initial), {}, StackView.Immediate)

    // A TV's remote: a page that opens takes the focus (its main action,
    // else its first control), and going back gives it to the control
    // that opened the page. Qt left it on the page that went, and the
    // remote did nothing.
    property int tvDepth: 0
    property var tvOpeners: []
    function tvShown() {
        if (!App.tv) return
        // (The page has taken the focus by now: the control it came from.)
        if (depth > tvDepth && Router.shell) tvOpeners[depth - 2] = Router.shell.lastControl()
        tvDepth = depth
        tvFocus.restart()
    }
    Timer {
        id: tvFocus
        interval: Motion.normal + 80
        onTriggered: {
            if (stack.busy) { restart(); return }
            if (Router.shell && stack.currentItem) Router.shell.focusPage(stack, stack.currentItem, stack.tvOpeners[stack.depth - 1] || null)
        }
    }

    // The turnstile's doors. A transition only animates its item's own
    // properties, so each page gets one as a transform while it turns. The
    // page comes from the transition itself (ViewTransition.item): StackView
    // starts the transitions before it changes currentItem, so a door hung
    // from currentItem went on the page before (2.12.x: the old page jumped
    // to the end angle and the new one appeared flat, a single frame).
    Rotation { id: inTurn; origin.x: 0; origin.y: stack.height / 2; axis { x: 0; y: 1; z: 0 } }
    Rotation { id: outTurn; origin.x: 0; origin.y: stack.height / 2; axis { x: 0; y: 1; z: 0 } }
    function hinge(item, door, angle) {
        door.angle = angle
        if (item) item.transform = [door]
    }
    // Once nothing moves, every page is flat and the current one shown,
    // however the turns ended (finished, or cut short by the next push).
    function settle() {
        for (let i = 0; i < depth; ++i) {
            const item = get(i, StackView.DontLoad)
            if (item) item.transform = []
        }
        if (currentItem) currentItem.opacity = 1
        inTurn.angle = 0
        outTurn.angle = 0
    }
    onBusyChanged: if (!busy) settle()
    // Immediate changes run no transition (and busy never changes).
    onCurrentItemChanged: {
        if (!busy) settle()
        tvShown()
    }

    // Forward: the old page swings away into the screen, the new one comes
    // in from the other side. Back: the other way round.
    Transition {
        id: turnForwardIn
        SequentialAnimation {
            ScriptAction { script: stack.hinge(turnForwardIn.ViewTransition.item, inTurn, -80) }
            PropertyAction { property: "opacity"; value: 0 }
            PauseAnimation { duration: Motion.fast }
            PropertyAction { property: "opacity"; value: 1 }
            NumberAnimation { target: inTurn; property: "angle"; from: -80; to: 0; duration: Motion.normal; easing.type: Easing.OutCubic }
        }
    }
    Transition {
        id: turnForwardOut
        SequentialAnimation {
            ScriptAction { script: stack.hinge(turnForwardOut.ViewTransition.item, outTurn, 0) }
            NumberAnimation { target: outTurn; property: "angle"; from: 0; to: 60; duration: Motion.fast; easing.type: Easing.InCubic }
            PropertyAction { property: "opacity"; value: 0 }
        }
    }
    Transition {
        id: turnBackIn
        SequentialAnimation {
            ScriptAction { script: stack.hinge(turnBackIn.ViewTransition.item, inTurn, 60) }
            PropertyAction { property: "opacity"; value: 0 }
            PauseAnimation { duration: Motion.fast }
            PropertyAction { property: "opacity"; value: 1 }
            NumberAnimation { target: inTurn; property: "angle"; from: 60; to: 0; duration: Motion.normal; easing.type: Easing.OutCubic }
        }
    }
    Transition {
        id: turnBackOut
        SequentialAnimation {
            ScriptAction { script: stack.hinge(turnBackOut.ViewTransition.item, outTurn, 0) }
            NumberAnimation { target: outTurn; property: "angle"; from: 0; to: -80; duration: Motion.fast; easing.type: Easing.InCubic }
            PropertyAction { property: "opacity"; value: 0 }
        }
    }

    Transition {
        id: slideIn
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "x"; from: 48; to: 0; duration: Motion.page; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
        }
    }
    Transition {
        id: slideOut
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "x"; from: 0; to: -28; duration: Motion.page; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
        }
    }
    Transition {
        id: slideBackIn
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "x"; from: -28; to: 0; duration: Motion.page; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
        }
    }
    Transition {
        id: slideBackOut
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Motion.fast; easing.type: Easing.OutCubic }
            NumberAnimation { property: "x"; from: 0; to: 48; duration: Motion.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.accelerate }
        }
    }

    readonly property bool turnstile: Theme.retro && !Motion.reduced
    pushEnter: turnstile ? turnForwardIn : slideIn
    pushExit: turnstile ? turnForwardOut : slideOut
    popEnter: turnstile ? turnBackIn : slideBackIn
    popExit: turnstile ? turnBackOut : slideBackOut
    replaceEnter: pushEnter
    replaceExit: pushExit
}
