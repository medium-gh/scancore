import QtQuick
import Scancore

// Run inside the app (tests/app/run.cmake starts it with SCANCORE_START "probe"):
// every section, every page that opens over one, every Settings category and
// the full-window pages, opened and closed in the Standard and the Retro
// look. Once nothing moves, the page on top must be shown: visible, opaque,
// flat and in place, and so must everything it sits in. Exits 0 when all
// were, 1 otherwise. 2.12.1 and 2.12.3 both shipped pages left hidden.
Item {
    id: run
    readonly property var sections: ["home", "comics", "novels", "anime", "music", "library", "history", "activity", "latest", "downloads", "settings"]
    readonly property var pages: [
        ["pages/ExtensionsPage.qml", {}],
        ["pages/ExtensionsDiagnostics.qml", {}],
        ["pages/Upcoming.qml", {}],
        ["pages/LibraryCare.qml", {}],
        ["pages/AnimeSchedule.qml", {}],
        ["pages/AnimeBrowse.qml", { initialFilters: {} }],
        ["pages/FriendsPage.qml", {}],
        ["pages/YearReview.qml", {}],
        ["pages/SourcePage.qml", { sourceKey: "local" }],
        ["pages/ComicDetails.qml", { url: "", summary: { title: "Probe", source: "local" } }],
        ["pages/NovelDetails.qml", { url: "", summary: { title: "Probe", source: "local" } }],
        ["pages/AnimeDetails.qml", { anilistId: 0, summary: { title: "Probe" } }],
    ]
    readonly property var fullWindow: [
        ["pages/Wrapped.qml", { year: new Date().getFullYear() }],
        ["pages/NowPlayingPage.qml", {}],
    ]
    property var steps: []
    property int checked: 0
    property var failures: []
    property string look: ""

    function then(what, action, check) { steps.push({ what: what, action: action, check: check }) }
    function later(ms, fn) {
        const t = Qt.createQmlObject("import QtQuick; Timer {}", run)
        t.interval = ms
        t.triggered.connect(() => { t.destroy(); fn() })
        t.start()
    }

    function immersive() {
        for (const child of run.Window.window.contentItem.parent.children)
            if (child.edgeToEdge !== undefined && child.depth !== undefined) return child
        return null
    }
    function flat(item) { return !item.transform || item.transform.length === 0 }
    // What keeps `item` from being seen, or "" when nothing does.
    function hidden(item) {
        if (!item) return "no page"
        if (!flat(item)) return "the page is turned"
        if (Math.abs(item.x) > 0.5) return "the page is at x " + item.x
        if (Math.abs(item.scale - 1) > 0.001) return "the page is at scale " + item.scale
        for (let it = item; it && it !== run.Window.window.contentItem.parent; it = it.parent) {
            if (!it.visible) return (it === item ? "the page" : "a parent (" + it + ")") + " is not visible"
            if (it.opacity < 0.999) return (it === item ? "the page" : "a parent (" + it + ")") + " is at opacity " + it.opacity
        }
        return ""
    }
    function shownNow(full) {
        const over = immersive()
        if (full) {
            if (!over || over.busy) return "still moving"
            return hidden(over.currentItem)
        }
        if (over && (over.depth > 0 || over.busy)) return "a full-window page is still open"
        const stack = Router.shell.stackFor(Router.shell.section)
        if (!stack) return "the section's stack is not there"
        if (stack.busy) return "still moving"
        return hidden(stack.currentItem)
    }
    // Waits for the page change to end, then for anything still
    // fading (a section's own Behavior); hidden after 6 s fails.
    function check(what, full, done) {
        const started = Date.now()
        const poll = () => {
            const why = shownNow(full)
            if (!why) { checked += 1; done(); return }
            if (Date.now() - started > 6000) {
                failures.push(look + ": " + what + ": " + why)
                console.warn("HIDDEN", look + ":", what + ":", why)
                done()
                return
            }
            later(50, poll)
        }
        later(50, poll)
    }

    function plan(lookName) {
        then("look " + lookName, () => { App.setSetting("uiMode", lookName); look = lookName }, false)
        for (const s of sections)
            then("section " + s, () => Router.go(s), true)
        then("home again", () => Router.go("home"), true)
        for (const p of pages) {
            then("open " + p[0], () => Router.push(p[0], p[1]), true)
            then("over " + p[0], () => Router.push("pages/Upcoming.qml", {}), true)
            then("back to " + p[0], () => Router.back(), true)
            then("back from " + p[0], () => Router.back(), true)
        }
        // Changes cut short by the next one.
        then("quick push, push, back", () => {
            Router.push("pages/Upcoming.qml", {})
            later(30, () => Router.push("pages/LibraryCare.qml", {}))
            later(60, () => Router.back())
        }, true)
        then("quick back, back", () => { Router.back(); later(30, () => Router.back()) }, true)
        then("quick push, back", () => { Router.push("pages/Upcoming.qml", {}); later(30, () => Router.back()) }, true)
        // Every Settings category.
        then("settings", () => Router.go("settings"), true)
        then("settings categories", null, null)
        for (const p of fullWindow) {
            then("full window " + p[0], () => Router.shell.openImmersive(p[0], p[1]), "full")
            then("closed " + p[0], () => Router.back(), true)
        }
        then("section from a full-window page", () => {
            Router.shell.openImmersive(fullWindow[0][0], fullWindow[0][1])
            later(30, () => Router.go("library"))
        }, true)
        then("home at the end", () => Router.go("home"), true)
    }

    function settingsCategories(done) {
        const page = Router.shell.stackFor("settings").currentItem
        const list = page && page.sections ? Array.from(page.sections) : []
        if (list.length === 0) { failures.push(look + ": no Settings categories"); done(); return }
        let i = 0
        const next = () => {
            if (i >= list.length) { done(); return }
            const s = list[i++]
            page.open(s)
            check("Settings → " + (s.key || s.title), false, () => {
                Router.back()
                check("back from Settings → " + (s.key || s.title), false, next)
            })
        }
        next()
    }

    function runStep(i) {
        if (i >= steps.length) {
            console.warn("PAGES", checked, "shown,", failures.length, "hidden")
            for (const f of failures) console.warn("  hidden:", f)
            Qt.exit(failures.length ? 1 : 0)
            return
        }
        const s = steps[i]
        if (s.action === null) { settingsCategories(() => runStep(i + 1)); return }
        s.action()
        if (s.check === false) { later(1500, () => runStep(i + 1)); return }
        check(s.what, s.check === "full", () => runStep(i + 1))
    }

    Component.onCompleted: {
        // A fresh profile: no welcome over the pages, motion on.
        App.setSetting("onboarded", true)
        App.setSetting("reducedMotion", false)
        const looks = (App.startAction.looks || ["standard", "retro"])
        for (const l of looks) plan(l)
        later(1500, () => runStep(0))
    }
}
