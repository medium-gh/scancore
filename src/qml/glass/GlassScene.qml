pragma Singleton
import QtQuick
import Scancore

// The Glass mode's shared state: what the glass shows (the backdrop, a
// layer of the window's pages and wallpaper, set by Main), the room the
// floating bar takes along the bottom of a phone (lists scroll clear of it),
// and whether that bar is drawn in while the page scrolls down.
QtObject {
    id: scene

    readonly property bool on: Theme.glass
    // An Item with layer.enabled: every piece of glass samples its texture.
    property Item backdrop: null
    // The wallpaper alone (a layer): glass inside a page shows it, since
    // that glass is part of the backdrop above and cannot show itself.
    property Item wallpaper: null
    // The backdrop well blurred (a layer), for menus, sheets and dialogs:
    // their glass is frosted so lines of text on it stay easy to read. Made
    // only while one is open (frostUsers counts them).
    property Item frosted: null
    property int frostUsers: 0

    // What glass inside a page shows: the page's own backdrop when it has
    // one (Now Playing's blurred cover, a reader's pages: those sit above
    // the wallpaper), else the wallpaper. A page offers one as a
    // `glassBackdrop` property. Read in a binding, it follows that property.
    function backdropFor(item) {
        for (let p = item ? item.parent : null; p; p = p.parent)
            if (p.glassBackdrop !== undefined) return p.glassBackdrop || wallpaper
        return wallpaper
    }

    // Phones: the floating bar's height plus its margins (and the accessory
    // when one shows). Lists and pages add it below their content.
    property real clearance: 0

    // Scrolling down draws the bar in (the current tab only); scrolling up,
    // or reaching the top, brings it back. A little travel either way first,
    // so a jitter does not toggle it.
    property bool barMinimized: false
    property real travel: 0
    function scrolled(delta, atTop) {
        if (!on) return
        if (atTop) { travel = 0; barMinimized = false; return }
        travel = (travel > 0) === (delta > 0) ? travel + delta : delta
        if (travel > 28) barMinimized = true
        else if (travel < -16) barMinimized = false
    }
}
