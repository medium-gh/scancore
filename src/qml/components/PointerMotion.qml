import QtQuick
import Scancore

// Reports real mouse movement over its parent. Qt Quick re-sends a hover at
// the pointer's last position on every repainted frame, and on phones a
// finished touch leaves such a position behind too, so a plain HoverHandler
// fires constantly while anything under it animates or scrolls. Disabled on
// phones, which have no pointer to follow.
HoverHandler {
    id: handler
    signal moved(point position)
    property point last: Qt.point(-1e6, -1e6)

    enabled: !App.mobile
    onPointChanged: {
        const p = point.scenePosition
        if (Math.abs(p.x - last.x) + Math.abs(p.y - last.y) < 3) return
        last = p
        moved(point.position)
    }
}
