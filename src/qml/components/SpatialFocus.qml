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
    // Where the focus goes first, and back to when it is lost (a closed
    // dialog took it along): the first control on screen in it.
    property Item start: scope
    // ...or this one when it can take the focus (the current section).
    property Item startItem: null
    // Another SpatialFocus that has the remote meanwhile (the welcome over
    // the shell): keys and moves asked of this one go to it.
    property var handOff: null
    readonly property bool active: App.tv && !handOff
    // The one that has the remote now.
    readonly property var owner: handOff || spatial
    readonly property Item current: active && Window.window ? Window.window.activeFocusItem : null

    function isStop(item) {
        if (!item || !item.visible || !item.enabled || item.width <= 0 || item.height <= 0 || item.opacity <= 0.01) return false
        // A web page's surface (Qt WebEngine's) takes focus like a control:
        // the embedded player's whole picture became the "first control".
        if (String(item).indexOf("QtWebEngineCore::") === 0) return false
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
    // The part of it that shows: cut by the views that clip it (a page's
    // last row half under the footer). One scrolled out of sight keeps
    // its whole rect (moving to it scrolls it in).
    function shownRectOf(item) {
        const r = rectOf(item)
        let x1 = r.x, y1 = r.y, x2 = r.x + r.width, y2 = r.y + r.height
        for (let p = item.parent; p; p = p.parent) {
            if (!p.clip) continue
            const c = rectOf(p)
            x1 = Math.max(x1, c.x); y1 = Math.max(y1, c.y)
            x2 = Math.min(x2, c.x + c.width); y2 = Math.min(y2, c.y + c.height)
        }
        return x2 - x1 >= 4 && y2 - y1 >= 4 ? Qt.rect(x1, y1, x2 - x1, y2 - y1) : r
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
    // The remoteRegion around an item (the sidebar), or null (the page).
    function regionOf(item) {
        for (let p = item; p; p = p.parent) if (p.remoteRegion === true) return p
        return null
    }
    // The control focused last inside a region.
    property Item regionLeft: null
    function inside(item, root) {
        for (let p = item; p; p = p.parent) if (p === root) return true
        return false
    }
    // Whether the focus is nowhere a key does anything: on no item, on the
    // window itself, or on one that has gone (hidden, disabled, a popup
    // that closed).
    function lost() {
        const w = Window.window
        if (!current || !w || current === w.contentItem || !current.visible || !current.enabled) return true
        const ov = w.Overlay.overlay
        return current === ov || (ov && inside(current, ov) && !popupScope())
    }
    // Every D-pad and Select press (the window passes them here before
    // any item sees them). While a popup is open the D-pad moves inside
    // it and Select presses; with the focus lost the D-pad brings it back.
    // False leaves the key to the focused item (the shell's own keys, a
    // reader, a list or a field).
    function windowKey(key) {
        if (handOff) return handOff.windowKey(key)
        if (!active) return false
        const dirs = { [Qt.Key_Left]: "left", [Qt.Key_Right]: "right", [Qt.Key_Up]: "up", [Qt.Key_Down]: "down" }
        const root = popupScope()
        if (!root) {
            if (!lost()) return false
            // From where the focus was (a section's Remove took its row
            // along), else from the start.
            if (dirs[key] !== undefined) return move(dirs[key], null, true) || focusStart()
            return false
        }
        // A dropdown steps through its own choices.
        if (current instanceof ComboBox) return false
        if (dirs[key] !== undefined) { move(dirs[key]); return true }
        if (current && inside(current, root)) return activate()
        return false
    }

    // "left", "right", "up" or "down". Returns whether it moved.
    // `within`: another place to move in instead of the shell (a reader's
    // controls).
    // `fromLast`: with the focus lost, from where it was last.
    function move(direction, within, fromLast) {
        const stops = []
        const root = popupScope() || within || scope
        for (let i = 0; i < root.children.length; ++i) collect(root.children[i], stops)
        if (stops.length === 0) return false
        const from = current && isStop(current) && inside(current, root) ? shownRectOf(current)
            : fromLast && lastRect ? lastRect : null
        if (fromLast && !from) return false
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
        let cross = null, crossScore = Infinity
        let loose = null, looseScore = Infinity, looseAlong = 0, looseFar = 0, looseOff = 0, bestAlong = 0, bestOff = 0
        // ...and only within the view it is in (the page, not the sidebar).
        const fromViews = []
        for (let p = current ? current.parent : null; p; p = p.parent) if (p instanceof Flickable) fromViews.push(p)
        // (or one that never scrolls: a footer's Back and Continue under
        // the page.)
        // ...never into another region (the sidebar beside a page: Down
        // from a page's last row went to Collapse).
        const fromRegion = current ? regionOf(current) : null
        const sameView = (item) => {
            if (regionOf(item) !== fromRegion) return false
            let views = 0
            for (let p = item.parent; p; p = p.parent) if (p instanceof Flickable) {
                if (fromViews.indexOf(p) >= 0) return true
                ++views
            }
            return fromViews.length === 0 || views === 0
        }
        for (const item of stops) {
            if (item === current) continue
            const r = shownRectOf(item)
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
            // Left and right: level only, side by side (from the last of a
            // row Right went up to the header, or down to the next row; the
            // sidebar and the page cross below).
            const cone = horizontal ? 6 : Math.max(0, along)
            if (gap > 0 && gap > cone) {
                const wide = Math.max(0, along) + off * 0.5
                if (!horizontal && along >= 0 && wide < looseScore && sameView(item)) { looseScore = wide; loose = item; looseAlong = along; looseFar = along + r.height; looseOff = off }
                // Left and right into another region (the sidebar to the
                // page, its hero card lower than Home): the nearest across.
                const side = off + Math.max(0, along) * 0.2
                if (horizontal && along >= 0 && side < crossScore && regionOf(item) !== fromRegion) { crossScore = side; cross = item }
                continue
            }
            // In line first, then the nearest; the centres break ties.
            const score = Math.max(0, along) + gap * 3 + off * 0.1 + (gap > 0 ? 400 : 0)
            if (score < bestScore) {
                bestScore = score
                best = item
                bestAlong = Math.max(0, along)
                bestOff = off
            }
        }
        // ...or, up and down, when the one in line is beyond it (Continue
        // under a picture: Up to the choices beside it, not to the header
        // at the top). As Android's FocusFinder: the one in line wins when
        // it is nearer than the other's far edge, else the nearer by
        // 13 * ahead² + across².
        const weigh = (a, o) => 13 * a * a + o * o
        if (!best || (loose && bestAlong > looseFar && weigh(looseAlong, looseOff) < weigh(bestAlong, bestOff))) best = loose
        if (!best) best = cross
        // Back into a region from outside it: where it was left (the
        // sidebar's Home, not the section level with the page's card).
        if (best && (direction === "left" || direction === "right") && regionLeft && regionLeft !== current && regionOf(best) && regionOf(best) !== fromRegion
                && regionOf(best) === regionOf(regionLeft) && isStop(regionLeft) && onScreen(regionLeft)) best = regionLeft
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
            // The view's own margins are part of its range (a row of
            // covers inset 32: the first cover went under the sidebar).
            const top = p.originY - p.topMargin, left = p.originX - p.leftMargin
            const bottom = p.originY + p.contentHeight + p.bottomMargin - p.height
            const right = p.originX + p.contentWidth + p.rightMargin - p.width
            if (bottom > top) {
                if (r.y - margin < p.contentY) p.contentY = Math.max(top, r.y - margin)
                else if (r.y + item.height + margin > p.contentY + p.height)
                    p.contentY = Math.min(bottom, r.y + item.height + margin - p.height)
            }
            if (right > left) {
                if (r.x - margin < p.contentX) p.contentX = Math.max(left, r.x - margin)
                else if (r.x + item.width + margin > p.contentX + p.width)
                    p.contentX = Math.min(right, r.x + item.width + margin - p.width)
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
    // Where the focus starts: startItem, else the first control in start.
    function focusStart() {
        if (startItem && isStop(startItem) && onScreen(startItem)) {
            focusOn(startItem)
            return true
        }
        return focusFirst(start)
    }
    // Its first control, top left (Select brings the controls up).
    function focusFirst(within) {
        const first = within ? firstStop(within) : null
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
    property Item opener: null
    onCurrentChanged: {
        if (current && isStop(current) && regionOf(current)) regionLeft = current
        if (active && !openPopup && current && isStop(current) && !popupScope()) opener = current
    }
    function firstStop(root) {
        const stops = []
        for (let i = 0; i < root.children.length; ++i) collect(root.children[i], stops)
        const shown = stops.filter(onScreen)
        shown.sort((a, b) => rectOf(a).y - rectOf(b).y || rectOf(a).x - rectOf(b).x)
        // A control can ask to be the first (a step's main action), also
        // below the fold (focusing it scrolls it in).
        return stops.find(item => item.remoteFirst === true) || shown[0] || null
    }
    Timer {
        running: spatial.active
        interval: 200
        repeat: true
        onTriggered: {
            const p = spatial.popupScope()
            // The last control focused with no popup open: the one that
            // opens the next.
            if (!p && !spatial.openPopup && spatial.current && spatial.isStop(spatial.current)) spatial.opener = spatial.current
            if (p === spatial.openPopup) return
            // Closed: back to the control that opened it (Qt leaves the
            // focus nowhere, and the remote did nothing until a restart).
            if (!p && spatial.lost()) {
                if (spatial.opener && spatial.isStop(spatial.opener) && spatial.onScreen(spatial.opener)) spatial.focusOn(spatial.opener)
                else spatial.focusStart()
            }
            spatial.openPopup = p
            // A dropdown's list: the dropdown keeps the focus and its keys.
            if (spatial.current instanceof ComboBox) return
            const first = p ? spatial.firstStop(p) : null
            if (first) spatial.focusOn(first)
        }
    }

    // Where the focus was last (the ring's place).
    property var lastRect: null

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
                spatial.lastRect = r
                ring.x = r.x - 4
                ring.y = r.y - 4
                ring.width = r.width + 8
                ring.height = r.height + 8
            }
        }
    }
}
