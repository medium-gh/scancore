import QtQuick
import QtQuick.Controls.Basic
import Scancore

// A television's remote (App.tv): the D-pad moves the focus to the nearest
// control in that direction, Select presses it, and a ring shows where the
// focus is. Pages need nothing of their own: every button, cover and tile
// that takes focus with Tab is a stop. The view a stop is in scrolls to
// show it.
Item {
    id: spatial
    // Where to look for stops (the shell; readers keep their own keys).
    property Item scope
    readonly property bool active: App.tv
    readonly property Item current: active && Window.window ? Window.window.activeFocusItem : null

    function isStop(item) {
        if (!item || !item.visible || !item.enabled || item.width <= 0 || item.height <= 0 || item.opacity <= 0.01) return false
        if (item instanceof AbstractButton) return (item.focusPolicy & Qt.TabFocus) !== 0
        return item.activeFocusOnTab === true
    }
    function collect(item, out) {
        if (!item || !item.visible || item.opacity <= 0.01) return
        if (isStop(item)) out.push(item)
        // A stop's own insides are not stops.
        else for (let i = 0; i < item.children.length; ++i) collect(item.children[i], out)
    }
    function rectOf(item) {
        const p = item.mapToItem(null, 0, 0)
        return Qt.rect(p.x, p.y, item.width, item.height)
    }
    // Inside the window (and inside the views clipping it).
    function onScreen(item) {
        const r = rectOf(item)
        const w = Window.window
        return r.x + r.width > 0 && r.y + r.height > 0 && r.x < w.width && r.y < w.height
    }

    // An open menu, sheet, dialog or panel (a popup in the window overlay,
    // the topmost that has stops): the remote moves inside it, never among
    // the controls behind it.
    function popupScope() {
        const ov = Window.window ? Window.window.Overlay.overlay : null
        if (!ov) return null
        const open = []
        for (let i = 0; i < ov.children.length; ++i) {
            const c = ov.children[i]
            if (c.visible && c.opacity > 0.01 && c.width > 0 && c.height > 0) open.push({ item: c, z: c.z, at: i })
        }
        open.sort((a, b) => b.z - a.z || b.at - a.at)
        for (const o of open) {
            const stops = []
            collect(o.item, stops)
            if (stops.length > 0) return o.item
        }
        return null
    }
    function inside(item, root) {
        for (let p = item; p; p = p.parent) if (p === root) return true
        return false
    }
    // A key pressed while a popup is open (the window passes it here before
    // the popup sees it): the D-pad moves inside the popup, Select presses.
    // False leaves the key to the popup (a list or a field of its own).
    function popupKey(key) {
        if (!active) return false
        const root = popupScope()
        if (!root) return false
        // A dropdown steps through its own choices.
        if (current instanceof ComboBox) return false
        const dirs = { [Qt.Key_Left]: "left", [Qt.Key_Right]: "right", [Qt.Key_Up]: "up", [Qt.Key_Down]: "down" }
        if (dirs[key] !== undefined) { move(dirs[key]); return true }
        if (current && inside(current, root)) return activate()
        return false
    }

    // "left", "right", "up" or "down". Returns whether it moved.
    // `within`: another place to move in instead of the shell (a reader's
    // controls).
    function move(direction, within) {
        const stops = []
        const root = popupScope() || within || scope
        for (let i = 0; i < root.children.length; ++i) collect(root.children[i], stops)
        if (stops.length === 0) return false
        const from = current && isStop(current) && inside(current, root) ? rectOf(current) : null
        if (!from) {
            // Nothing focused yet: the first stop on screen, top left.
            const shown = stops.filter(onScreen).sort((a, b) => rectOf(a).y - rectOf(b).y || rectOf(a).x - rectOf(b).x)
            if (shown.length) focusOn(shown[0])
            return shown.length > 0
        }
        const cx = from.x + from.width / 2, cy = from.y + from.height / 2
        let best = null, bestScore = Infinity
        // Up and down with nothing in that cone: the nearest ahead anywhere
        // across (a page's header buttons over its first row). Left and
        // right stay put instead of jumping to another row.
        let loose = null, looseScore = Infinity
        // ...and only within the view it is in (the page, not the sidebar).
        const fromViews = []
        for (let p = current ? current.parent : null; p; p = p.parent) if (p instanceof Flickable) fromViews.push(p)
        const sameView = (item) => {
            for (let p = item.parent; p; p = p.parent) if (p instanceof Flickable && fromViews.indexOf(p) >= 0) return true
            return fromViews.length === 0
        }
        for (const item of stops) {
            if (item === current) continue
            const r = rectOf(item)
            const x = r.x + r.width / 2, y = r.y + r.height / 2
            // along: how far ahead; gap: how far off the line (0 when the two
            // overlap across the way); off: centre to centre across.
            let along, gap, off, ahead
            const horizontal = direction === "left" || direction === "right"
            switch (direction) {
            case "left": along = from.x - (r.x + r.width); ahead = x < cx - 1; break
            case "right": along = r.x - (from.x + from.width); ahead = x > cx + 1; break
            case "up": along = from.y - (r.y + r.height); ahead = y < cy - 1; break
            default: along = r.y - (from.y + from.height); ahead = y > cy + 1
            }
            if (horizontal) { gap = Math.max(0, Math.max(from.y, r.y) - Math.min(from.y + from.height, r.y + r.height)); off = Math.abs(y - cy) }
            else { gap = Math.max(0, Math.max(from.x, r.x) - Math.min(from.x + from.width, r.x + r.width)); off = Math.abs(x - cx) }
            // Ahead only (a little overlap is fine), and in that direction:
            // in line, or within 45 degrees of it.
            // One inside the other (a card's own button): ahead by centres.
            const inside = (r.x >= from.x && r.y >= from.y && r.x + r.width <= from.x + from.width && r.y + r.height <= from.y + from.height)
                || (from.x >= r.x && from.y >= r.y && from.x + from.width <= r.x + r.width && from.y + from.height <= r.y + r.height)
            if (inside && ahead) { along = horizontal ? Math.abs(x - cx) : Math.abs(y - cy); gap = 0 }
            if (!ahead || (!inside && along < -Math.min(from.width, from.height) * 0.3)) continue
            if (gap > 0 && gap > Math.max(0, along)) {
                const wide = Math.max(0, along) + off * 0.5
                if (!horizontal && along >= 0 && wide < looseScore && sameView(item)) { looseScore = wide; loose = item }
                continue
            }
            // In line first, then the nearest; the centres break ties.
            const score = Math.max(0, along) + gap * 3 + off * 0.1 + (gap > 0 ? 400 : 0)
            if (score < bestScore) {
                bestScore = score
                best = item
            }
        }
        if (!best) best = loose
        if (best) focusOn(best)
        return best !== null
    }

    function focusOn(item) {
        item.forceActiveFocus(Qt.TabFocusReason)
        reveal(item)
    }
    // Scrolls the views around `item` so it shows, with a little room.
    function reveal(item) {
        for (let p = item.parent; p; p = p.parent) {
            if (!(p instanceof Flickable)) continue
            const r = item.mapToItem(p.contentItem, 0, 0)
            const margin = 40
            if (p.contentHeight > p.height) {
                if (r.y - margin < p.contentY) p.contentY = Math.max(p.originY, r.y - margin)
                else if (r.y + item.height + margin > p.contentY + p.height)
                    p.contentY = Math.min(p.originY + p.contentHeight - p.height, r.y + item.height + margin - p.height)
            }
            if (p.contentWidth > p.width) {
                if (r.x - margin < p.contentX) p.contentX = Math.max(p.originX, r.x - margin)
                else if (r.x + item.width + margin > p.contentX + p.width)
                    p.contentX = Math.min(p.originX + p.contentWidth - p.width, r.x + item.width + margin - p.width)
            }
        }
    }
    // A reader on a TV: whether the focus is on one of its controls.
    function onControl(within) {
        return active && current !== null && isStop(current) && inside(current, within)
    }
    // The control nearest an edge of `within`: its top left, or (bottom)
    // its bottom left. False when it has none on screen.
    function focusEdge(within, bottom) {
        if (!within) return false
        const stops = []
        for (let i = 0; i < within.children.length; ++i) collect(within.children[i], stops)
        const shown = stops.filter(onScreen)
        shown.sort((a, b) => (bottom ? rectOf(b).y - rectOf(a).y : rectOf(a).y - rectOf(b).y) || rectOf(a).x - rectOf(b).x)
        if (!shown.length) return false
        focusOn(shown[0])
        return true
    }
    // Its first control, top left (Select brings the controls up).
    function focusFirst(within) {
        const first = firstStop(within)
        if (first) focusOn(first)
        return first !== null
    }
    function activate() {
        if (current instanceof AbstractButton) {
            current.clicked()
            return true
        }
        // Stops that are not buttons (the options of a segmented control).
        if (current && typeof current.tvActivate === "function") {
            current.tvActivate()
            return true
        }
        return false
    }

    // A popup that opens takes the focus to its first control (a reopened
    // one would otherwise keep the last, often its Close button). Checked
    // a few times a second, on a TV only.
    property Item openPopup: null
    function firstStop(root) {
        const stops = []
        for (let i = 0; i < root.children.length; ++i) collect(root.children[i], stops)
        const shown = stops.filter(onScreen)
        shown.sort((a, b) => rectOf(a).y - rectOf(b).y || rectOf(a).x - rectOf(b).x)
        return shown.length ? shown[0] : null
    }
    Timer {
        running: spatial.active
        interval: 200
        repeat: true
        onTriggered: {
            const p = spatial.popupScope()
            if (p === spatial.openPopup) return
            spatial.openPopup = p
            // A dropdown's list: the dropdown keeps the focus and its keys.
            if (spatial.current instanceof ComboBox) return
            const first = p ? spatial.firstStop(p) : null
            if (first) spatial.focusOn(first)
        }
    }

    // The ring around what has the focus.
    Rectangle {
        id: ring
        // Over the popups too.
        parent: Window.window ? Window.window.Overlay.overlay : spatial
        z: 100000
        visible: spatial.active && spatial.current !== null && spatial.isStop(spatial.current)
        color: "transparent"
        radius: Theme.radiusMd + 3
        border.width: 3
        border.color: Theme.retro ? Theme.tile : Theme.accentIsMono ? "#ffffff" : Theme.accent
        // Follows the item as it moves or its view scrolls.
        FrameAnimation {
            running: ring.visible
            onTriggered: {
                const r = spatial.rectOf(spatial.current)
                ring.x = r.x - 4
                ring.y = r.y - 4
                ring.width = r.width + 8
                ring.height = r.height + 8
            }
        }
    }
}
