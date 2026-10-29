import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Window
import Scancore

// Full-window comic reader. Vertical strip (webtoons) or single pages
// (manga), with eased scrolling, auto-hiding controls, progress saved as you
// read and the next chapter fetched ahead of time.
Rectangle {
    id: reader
    property var series: ({})
    property var chapters: []
    property int index: 0

    readonly property var unit: chapters[index] || ({})
    readonly property bool hasPrev: index > 0
    readonly property bool hasNext: index < chapters.length - 1

    property var task: null
    property var nextTask: null
    readonly property var panels: task && task.ok ? task.result.panels : []
    readonly property string referer: task && task.ok ? task.result.referer : ""

    // Settings chosen for this series alone (the reader's settings, "Only
    // this series") win over the general ones. Kept with the series' own
    // settings ("reader"), which sync to the reader's other devices; older
    // ones from the readerSeries setting are read until they change.
    property int seriesRevision: 0
    readonly property var seriesPrefs: {
        App.dependOn(seriesRevision)
        if (!series.id) return null
        const own = Library.seriesSettings(series.id).reader
        if (own && typeof own === "object") return own
        const all = App.settings.readerSeries || {}
        return all[series.id] ? all[series.id] : null
    }
    function storeSeriesPrefs(prefs) {
        Library.setSeriesSetting(series.id, "reader", prefs)
        // Gone from the old place, so it is not read back from there.
        const all = Object.assign({}, App.settings.readerSeries || {})
        if (all[series.id] !== undefined) {
            delete all[series.id]
            App.setSetting("readerSeries", all)
        }
        seriesRevision++
    }
    function opt(name, fallback) {
        if (seriesPrefs && seriesPrefs[name] !== undefined) return seriesPrefs[name]
        const value = App.settings[name]
        return value === undefined || value === null ? fallback : value
    }
    function setOpt(name, value) {
        if (!seriesPrefs) {
            App.setSetting(name, value)
            return
        }
        const mine = Object.assign({}, seriesPrefs)
        mine[name] = value
        storeSeriesPrefs(mine)
    }
    function setSeriesOnly(on) {
        if (on)
            storeSeriesPrefs({ readerMode: mode, readerDirection: direction, readerWidth: opt("readerWidth", 820),
                               readerGap: gap, readerSpread: opt("readerSpread", "single"), readerCrop: crop })
        else
            storeSeriesPrefs(null)
    }

    readonly property string mode: opt("readerMode", "strip") === "paged" ? "paged" : "strip"
    readonly property string direction: opt("readerDirection", "ltr") === "rtl" ? "rtl" : "ltr"
    readonly property int gap: opt("readerGap", 0)
    readonly property bool crop: opt("readerCrop", false) === true
    // Pages translated over (Translator), from the source's language.
    // SCANCORE_START {"translate": true} opens readers translating (tests).
    property bool translating: App.startAction.translate === true
    readonly property string translateFrom: {
        const key = reader.series.source || ""
        const found = key ? Comics.sources().find(s => s.key === key) : null
        return found && found.language ? String(found.language) : ""
    }
    // What a language model is told about a page besides its bubbles: the
    // series (its memory of names and the story is kept under it), the
    // chapter, and up to three pages before and one after, in `list`.
    function translateContext(list, i, chapter) {
        const url = (k) => list[k] ? list[k].url : ""
        return {
            work: series.title || "",
            scope: String(series.id || series.sourceUrl || series.title || ""),
            chapter: (chapters[chapter] || {}).title || "",
            before: [url(i - 3), url(i - 2), url(i - 1)].filter(u => u),
            after: [url(i + 1)].filter(u => u)
        }
    }
    // Small scans enlarged: sharpened as they are (0 off, to 1).
    readonly property real sharpen: Number(opt("readerSharpen", 0)) || 0
    // The strip runs on into the next chapter: it joins the strip once
    // fetched (at 70% of this one), after a line naming it, and the chapter
    // read changes as it scrolls past (Mihon's continuous webtoon). Off, a
    // chapter ends with the next-chapter card.
    readonly property bool continuous: mode === "strip" && opt("readerContinuous", true) !== false
    // The strip's rows: {chapter, panel, ratio}; panel -1 is the line before
    // a chapter; ratio is the page's height over its width once known (from
    // the source, or measured when it loads), so a row built again (scrolled
    // back to) has its height at once. stripChapters: chapter index →
    // {panels, referer, start row}.
    ListModel { id: stripModel }
    property var stripChapters: ({})
    property var chapterTasks: ({})
    function stripFill() {
        stripModel.clear()
        stripChapters = {}
        if (mode !== "strip" || !task || !task.ok) return
        stripAppend(index, task.result)
    }
    function stripAppend(ci, result) {
        if (stripChapters[ci] || !result || !result.panels) return
        const chapters = Object.assign({}, stripChapters)
        if (stripModel.count > 0) stripModel.append({ chapter: ci, panel: -1, ratio: 0 })
        chapters[ci] = { panels: result.panels, referer: result.referer || "", start: stripModel.count }
        stripChapters = chapters
        const rows = []
        for (let k = 0; k < result.panels.length; ++k) {
            const p = result.panels[k]
            rows.push({ chapter: ci, panel: k, ratio: p.width > 0 && p.height > 0 ? p.height / p.width : 0 })
        }
        stripModel.append(rows)
    }
    // A page above the one being read changed height (it loaded, and its
    // estimate was off): the strip moves with it, so what is on screen stays
    // put instead of jumping (and showing the page that grew, still black
    // while it fades in).
    function keepStripPlace(item, delta, oldHeight) {
        if (delta === 0 || !strip.visible) return
        if (item.y + oldHeight <= strip.contentY + strip.height * 0.5 && item.y < strip.contentY)
            strip.contentY += delta
    }
    function stripRow(p) {
        const c = stripChapters[index]
        return c ? c.start + p : p
    }
    // Scrolled into another chapter of the strip: it becomes the one read
    // (its progress, page count and the next one's prefetch), with no jump.
    function stripSwitch(ci, panel) {
        if (ci === index || !chapterTasks[ci] || restoring) return
        if (ci > index) markFinished()
        saveProgress(true)
        index = ci
        finished = false
        prefetchFailures = 0
        task = chapterTasks[ci]
        nextTask = null
        currentPanel = panel
        Presence.show("comic", series.title || "", chapters[ci].title || "")
    }
    readonly property int stripWidth: Math.min(width, opt("readerWidth", 820))
    // Colour filter: brightness and warmth are shades over the page (they
    // change live); grey and invert are applied while pages decode.
    readonly property real brightness: Math.max(0.2, Math.min(1, Number(opt("readerBrightness", 1))))
    readonly property real warmth: Math.max(0, Math.min(1, Number(opt("readerWarmth", 0))))
    readonly property string imageFilter: [opt("readerGrey", false) === true ? "grey" : "",
                                           opt("readerInvert", false) === true ? "invert" : ""].filter(f => f).join(" ")
    // Pages: a wide spread shown as its two halves, one after the other.
    readonly property bool splitWide: opt("readerSplitWide", false) === true
    readonly property string tapZones: opt("readerTapZones", "sides")
    readonly property string tapInvert: opt("readerTapInvert", "none")
    readonly property bool stripTaps: opt("readerStripTaps", false) === true
    // Holding a finger (or the mouse) on the page magnifies it twice, under
    // the finger or at the middle, until it lets go: small dialogue and
    // margin notes. Double tap (double click) zooms in on paged pages.
    readonly property bool longPressZoom: opt("readerLongPressZoom", true) !== false
    readonly property bool longPressAtCentre: opt("readerLongPressZoomAt", "press") === "centre"
    readonly property bool doubleTapZoom: opt("readerDoubleTapZoom", true) !== false
    property bool magnifying: false
    property point magnifyAt: Qt.point(0, 0)
    function startMagnifier(item, x, y) {
        if (!longPressZoom) return
        magnifyAt = longPressAtCentre ? Qt.point(item.width / 2, item.height / 2) : Qt.point(x, y)
        magnifying = true
        controlsShown = false
    }
    function moveMagnifier(x, y) { if (magnifying && !longPressAtCentre) magnifyAt = Qt.point(x, y) }

    // ---------------------------------------------------------- this page
    // Copy, save or share a page: a right click on it, a long press when the
    // long press does not zoom, or More → This page.
    // {url, referer, chapter, number} of a chapter's page, or null.
    function pageRef(chapter, panel) {
        const c = stripChapters[chapter]
        const list = c ? c.panels : chapter === index ? panels : []
        const p = panel >= 0 ? list[panel] : null
        return p && p.url ? { url: p.url, referer: c ? (c.referer || referer) : referer, chapter: chapter, number: panel + 1 } : null
    }
    // The page under (x, y) of the strip or the paged view.
    function pageUnder(item, x, y) {
        if (item === strip) {
            const i = strip.indexAt(x + strip.contentX, y + strip.contentY)
            const row = i >= 0 ? stripModel.get(i) : null
            return row && row.panel >= 0 ? pageRef(row.chapter, row.panel) : null
        }
        const shown = pageView.shown
        if (shown.length > 1) {
            const q = secondImage.mapFromItem(item, x, y)
            if (q.x >= 0 && q.x <= secondImage.width) return pageRef(index, shown[1])
        }
        return pageRef(index, shown[0])
    }
    function pageInView() { return mode === "paged" ? pageRef(index, pageView.shown[0]) : pageRef(index, currentPanel) }
    function pageMenu(anchor, x, y, page) {
        if (!page) return
        magnifying = false
        const list = [{ text: qsTr("Copy image"), icon: "copy", run: () => pageDo("copy", page) },
                      { text: qsTr("Save image…"), icon: "download", run: () => pageDo("save", page) }]
        if (App.platform === "android") list.push({ text: qsTr("Share…"), icon: "share-2", run: () => pageDo("share", page) })
        pageActions.openAt(anchor, x, y, list)
    }
    property var pageTask: null
    property string pageAction: ""
    property var pageTarget: null
    function pageDo(action, page) {
        pageAction = action
        pageTarget = page
        pageTask = App.pageFile(page.url, page.referer)
    }
    function pageName(page, suffix) {
        const chapter = (chapters[page.chapter] || {})
        const parts = [series.title || "", chapter.number ? qsTr("Chapter %1").arg(chapter.number) : (chapter.title || ""), qsTr("page %1").arg(page.number)]
        return parts.filter(t => t).join(" - ").replace(/[\\/:*?"<>|]+/g, " ").replace(/\s+/g, " ").trim().slice(0, 150) + "." + suffix
    }
    Connections {
        target: reader.pageTask
        function onFinished(file) {
            if (reader.pageAction === "copy") {
                const ok = App.copyImageFile(file.path, file.mime)
                Router.toast(ok ? qsTr("Page copied") : qsTr("The page could not be copied"), ok ? "success" : "error")
            } else if (reader.pageAction === "share") {
                if (!App.shareFile(file.path, file.mime, reader.pageName(reader.pageTarget, file.suffix)))
                    Router.toast(qsTr("The page could not be shared"), "error")
            } else {
                pageSave.file = file
                pageSave.nameFilters = [qsTr("Picture (*.%1)").arg(file.suffix)]
                pageSave.defaultSuffix = file.suffix
                const name = reader.pageName(reader.pageTarget, file.suffix)
                const folder = String(pageSave.currentFolder || "")
                pageSave.selectedFile = folder ? folder + "/" + encodeURIComponent(name) : name
                pageSave.open()
            }
        }
        function onFailed(error) { Router.toast(error, "error") }
    }
    ActionMenu { id: pageActions }
    FileDialog {
        id: pageSave
        property var file: null
        fileMode: FileDialog.SaveFile
        // Computers: the Pictures folder to start with.
        currentFolder: App.platform === "android" ? "" : StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: {
            const ok = App.saveFileTo(file.path, selectedFile)
            Router.toast(ok ? qsTr("Page saved") : qsTr("The page could not be saved"), ok ? "success" : "error")
        }
    }
    readonly property bool volumeKeys: App.platform === "android" && opt("readerVolumeKeys", false) === true
    readonly property var tapZoneOptions: [{ value: "sides", label: qsTr("Tap zones: left and right") }, { value: "lshape", label: qsTr("Tap zones: L shape") },
        { value: "kindle", label: qsTr("Tap zones: Kindle") }, { value: "edge", label: qsTr("Tap zones: edges") }, { value: "off", label: qsTr("Tap zones: off") }]
    readonly property var tapInvertOptions: [{ value: "none", label: qsTr("Not flipped") }, { value: "horizontal", label: qsTr("Flipped left and right") },
        { value: "vertical", label: qsTr("Flipped top and bottom") }, { value: "both", label: qsTr("Flipped both ways") }]
    onVolumeKeysChanged: App.setVolumeKeys(volumeKeys)
    property int currentPanel: 0
    property bool controlsShown: true

    // Auto-scroll (as in TachiyomiSY): the strip moves by itself, or the
    // pages turn every few seconds; speed 1–20. Holding the strip pauses it;
    // the end of the last chapter stops it.
    property bool autoScrolling: false
    readonly property int autoSpeed: Math.max(1, Math.min(20, Number(opt("readerAutoSpeed", 5)) || 5))
    function setAutoSpeed(v) { setOpt("readerAutoSpeed", Math.max(1, Math.min(20, v))) }
    FrameAnimation {
        running: reader.autoScrolling && reader.mode === "strip" && Qt.application.state === Qt.ApplicationActive
                 && !strip.dragging && !strip.flicking && !settingsPopup.opened && !reader.magnifying
        onTriggered: {
            const end = strip.originY + strip.contentHeight - strip.height
            if (strip.contentY >= end - 1) {
                if (!reader.hasNext || !reader.continuous) reader.autoScrolling = false
                return
            }
            strip.contentY = Math.min(end, strip.contentY + reader.autoSpeed * 40 * Math.min(frameTime, 0.05))
        }
    }
    Timer {
        running: reader.autoScrolling && reader.mode === "paged" && Qt.application.state === Qt.ApplicationActive
                 && !settingsPopup.opened && !pageView.zoomed
        interval: (22 - reader.autoSpeed) * 1000
        repeat: true
        onTriggered: {
            if (pageView.view >= pageView.views.length - 1 && !reader.hasNext) reader.autoScrolling = false
            else pageView.turn(1)
        }
    }
    property bool finished: false
    property var resumePosition: null
    // How far down the current image the reading line is (strip), 0-1.
    property real stripPartial: 0
    // The page is being put back where it was read to: not a page turn.
    property bool restoring: false
    Timer { id: restored; interval: 400; onTriggered: reader.restoring = false }

    // Drawn under the phone's status and navigation bars; the controls are
    // padded by these insets instead (see the immersive stack in Main.qml).
    readonly property bool edgeToEdge: true
    // Hidden bars wait above the top edge: never let them show outside.
    clip: true
    readonly property real insetTop: SafeArea.margins.top
    readonly property real insetBottom: SafeArea.margins.bottom
    // Phones: on full screen while the controls are hidden (Settings →
    // Reading → Hide the status bar), pages to the very top, beside a punch
    // hole too.
    readonly property bool barsHidden: App.platform === "android" && App.settings.readerHideBars !== false && !controlsShown
    onBarsHiddenChanged: App.setSystemBars(!barsHidden)

    color: "#000000"
    focus: true

    function load(i, fromEnd) {
        saveProgress(true)
        prefetchFailures = 0
        index = i
        finished = false
        currentPanel = 0
        const last = Library.progressMap(series.id)[chapters[i].key]
        resumePosition = last && !last.completed && last.position ? last.position : null
        if (nextTask && nextTask.ok && nextTask.done && nextFor === i) task = nextTask
        else task = Comics.panels(chapters[i].url, series.id, chapters[i].key)
        nextTask = null
        chapterTasks = ({ [i]: task })
        stripFill()
        strip.contentY = strip.originY
        pageView.resetZoom()
        pageView.page = 0
        showControls()
        Presence.show("comic", series.title || "", chapters[i].title || "")
    }
    property int nextFor: -1

    // The screen stays on while reading (a setting, on by default).
    readonly property bool keepScreenOn: App.settings.readerKeepAwake !== false && Qt.application.state === Qt.ApplicationActive
    onKeepScreenOnChanged: App.keepAwake("reader", keepScreenOn)

    Component.onCompleted: {
        load(index)
        App.keepAwake("reader", keepScreenOn)
        Activity.setMedia("comic")
        App.setVolumeKeys(volumeKeys)
    }
    Component.onDestruction: {
        App.setSystemBars(true)
        App.keepAwake("reader", false)
        App.setVolumeKeys(false)
        Activity.setMedia("")
        Presence.clear()
        saveProgress(true)
    }

    onModeChanged: stripFill()
    Connections {
        target: reader.task
        function onFinished() {
            // A chapter switched to in the strip is there already.
            if (reader.stripChapters[reader.index]) return
            reader.stripFill()
            Qt.callLater(() => {
                const at = reader.resumePosition
                if (at && (at.panel > 0 || at.offset > 0)) {
                    reader.restoring = true
                    const p = Math.min(at.panel, reader.panels.length - 1)
                    if (reader.mode === "strip") {
                        const row = reader.stripRow(p)
                        strip.positionViewAtIndex(row, ListView.Beginning)
                        // Where in that image: tall webtoon images hold a
                        // whole scene each.
                        const item = strip.itemAtIndex(row)
                        if (item && at.offset > 0)
                            strip.contentY = Math.max(strip.originY, item.y + item.height * at.offset - strip.height * 0.35)
                    } else pageView.page = pageView.views[pageView.viewOf(p)][0]
                    reader.currentPanel = p
                    restored.restart()
                }
            })
        }
        function onFailed(error) { reader.showControls() }
    }

    // ------------------------------------------------------------ progress

    property real lastSaved: 0
    function saveProgress(force) {
        if (!series.id || !unit.key || panels.length === 0) return
        const now = Date.now()
        if (!force && now - lastSaved < 1500) return
        lastSaved = now
        // Read only once its end was reached (markFinished): a chapter of a
        // few tall images is otherwise "read" on its last image's first
        // screen, and Continue went on to the next chapter.
        const strip = reader.mode === "strip"
        const percent = finished ? 1 : Math.min(0.97, (currentPanel + (strip ? stripPartial : 1)) / panels.length)
        Library.recordProgress(series, unit, strip ? { type: reader.mode, panel: currentPanel, offset: stripPartial } : { type: reader.mode, panel: currentPanel },
                               percent, finished)
    }

    onCurrentPanelChanged: {
        // E-ink: a black frame between pages clears the last one's ghost.
        if (Theme.eink && mode === "paged" && App.settings.einkFlash !== false) einkFlash.start()
        saveProgress(false)
        if (panels.length > 0 && currentPanel >= panels.length * 0.7) prefetchNext()
    }

    function prefetchNext() {
        if (!hasNext || nextFor === index + 1) return
        nextFor = index + 1
        nextTask = Comics.panels(chapters[index + 1].url, series.id, chapters[index + 1].key)
        chapterTasks = Object.assign({}, chapterTasks, { [index + 1]: nextTask })
    }
    Connections {
        target: reader.nextTask
        function onFinished(result) {
            reader.prefetchFailures = 0
            Comics.prefetch(result.panels, result.referer, 6)
            // Only after the chapter it follows (not after one jumped away from).
            if (reader.continuous && reader.stripChapters[reader.nextFor - 1]) reader.stripAppend(reader.nextFor, result)
        }
        // A moment offline or a site's hiccup: asked again a few times, so
        // the strip still runs on (the card at its end offers it by hand).
        function onFailed(error) {
            if (reader.nextFor !== reader.index + 1 || reader.prefetchFailures >= 5) return
            reader.prefetchFailures += 1
            reader.nextFor = -1
            prefetchRetry.interval = 2000 * reader.prefetchFailures
            prefetchRetry.restart()
        }
    }
    property int prefetchFailures: 0
    Timer {
        id: prefetchRetry
        onTriggered: if (reader.panels.length > 0 && reader.currentPanel >= reader.panels.length * 0.7) reader.prefetchNext()
    }

    function markFinished() {
        if (finished) return
        finished = true
        saveProgress(true)
    }

    // ------------------------------------------------------------ controls

    // This chapter's page reactions (PageReactions below).
    readonly property var pageReactions: reactions
    function showControls() {
        controlsShown = true
        hideTimer.restart()
    }
    function toggleControls() {
        if (controlsShown) controlsShown = false
        else showControls()
    }
    Timer {
        id: hideTimer
        interval: 2600
        onTriggered: {
            // Stay up while the pointer rests on the controls, a popup is
            // open, or there are no pages yet (loading, or an error to act on).
            if (settingsPopup.opened || chapterPopup.opened || comments.opened || reactPopup.opened || translatePopup.opened || topHover.hovered
                || reader.panels.length === 0)
                restart()
            else
                reader.controlsShown = false
        }
    }

    // Where a tap lands: "prev", "next" or "menu". Zones are laid out for
    // left-to-right reading and mirrored for right to left; the invert
    // setting flips them on top of that.
    readonly property var tapLayouts: ({
        sides:  ["prev", "menu", "next", "prev", "menu", "next", "prev", "menu", "next"],
        lshape: ["prev", "prev", "prev", "prev", "menu", "next", "next", "next", "next"],
        kindle: ["menu", "menu", "menu", "prev", "next", "next", "prev", "next", "next"],
        edge:   ["next", "menu", "next", "next", "menu", "next", "next", "prev", "next"],
        off:    ["menu", "menu", "menu", "menu", "menu", "menu", "menu", "menu", "menu"]
    })
    function tapZone(x, y, w, h) {
        const flipX = tapInvert === "horizontal" || tapInvert === "both"
        if (flipX !== (mode === "paged" && direction === "rtl")) x = w - x
        if (tapInvert === "vertical" || tapInvert === "both") y = h - y
        const c = Math.max(0, Math.min(2, Math.floor(x * 3 / w)))
        const r = Math.max(0, Math.min(2, Math.floor(y * 3 / h)))
        return (tapLayouts[tapZones] || tapLayouts.sides)[r * 3 + c]
    }
    function act(action) {
        if (action === "menu") {
            toggleControls()
            return
        }
        const step = action === "next" ? 1 : -1
        if (mode === "paged") pageView.turn(step)
        else scrollBy(step * height * 0.85)
    }
    Connections {
        target: App
        function onVolumeKey(down) { reader.act(down ? "next" : "prev") }
    }

    function scrollBy(delta) {
        if (mode === "strip") {
            stripWheel.scrollTo(stripWheel.ease.running ? stripWheel.target + delta : strip.contentY + delta)
        } else {
            pageView.turn(delta > 0 ? 1 : -1)
        }
    }

    function exit() {
        saveProgress(true)
        Router.leave(reader)
    }

    // Keys: this series' key profile (Settings → Keyboard), and, side by
    // side, only in the reader last clicked.
    property string keyProfile: series.id ? (Library.seriesSettings(series.id).keys || "") : ""
    function setKeyProfile(name) {
        Library.setSeriesSetting(series.id, "keys", name)
        keyProfile = name
    }
    readonly property bool keysHere: {
        const shell = Router.shell
        if (!shell || !shell.splitItem) return true
        return shell.keyReader === reader || (shell.keyReader !== shell.splitItem && reader !== shell.splitItem)
    }
    onActiveFocusChanged: if (activeFocus && Router.shell) Router.shell.keyReader = reader
    // Over everything in the reader, it notes the press and passes it on
    // (a handler on the reader itself never hears presses its pages take).
    MouseArea {
        anchors.fill: parent
        z: 100000
        enabled: !!Router.shell && !!Router.shell.splitItem
        acceptedButtons: Qt.AllButtons
        onPressed: (mouse) => {
            mouse.accepted = false
            Router.shell.keyReader = reader
            for (let item = reader.Window.activeFocusItem; item; item = item.parent)
                if (item === reader) return
            reader.forceActiveFocus()
        }
    }

    function keyHint(action) {
        const keys = Keymap.labels(action, keyProfile)
        return keys.length ? " (" + keys[0] + ")" : ""
    }

    // Scrolling keys come from Settings → Keyboard too, but only count
    // while the reader has focus (not in its chapter list, say).
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) exit()
        else if (Keymap.matches("reader.down", event, keyProfile)) scrollBy(height * 0.18)
        else if (Keymap.matches("reader.up", event, keyProfile)) scrollBy(-height * 0.18)
        else if (Keymap.matches("reader.pageDown", event, keyProfile)) scrollBy(height * 0.85)
        else if (Keymap.matches("reader.pageUp", event, keyProfile)) scrollBy(-height * 0.85)
        else if (event.key === Qt.Key_Right) mode === "paged" ? pageView.turn(direction === "rtl" ? -1 : 1) : (hasNext && load(index + 1))
        else if (event.key === Qt.Key_Left) mode === "paged" ? pageView.turn(direction === "rtl" ? 1 : -1) : (hasPrev && load(index - 1))
        else return
        event.accepted = true
    }

    // The rest come from Settings → Keyboard (see Keymap).
    Shortcut { sequences: Keymap.sequences("reader.next", reader.keyProfile); enabled: reader.keysHere; onActivated: if (reader.hasNext) reader.load(reader.index + 1) }
    Shortcut { sequences: Keymap.sequences("reader.prev", reader.keyProfile); enabled: reader.keysHere; onActivated: if (reader.hasPrev) reader.load(reader.index - 1) }
    Shortcut {
        sequences: Keymap.sequences("reader.first", reader.keyProfile)
        enabled: reader.keysHere
        onActivated: reader.mode === "strip" ? strip.positionViewAtBeginning() : (pageView.page = 0)
    }
    Shortcut {
        sequences: Keymap.sequences("reader.last", reader.keyProfile)
        enabled: reader.keysHere
        onActivated: reader.mode === "strip" ? strip.positionViewAtEnd() : (pageView.page = pageView.views[pageView.views.length - 1][0])
    }
    Shortcut {
        sequences: Keymap.sequences("reader.fullscreen", reader.keyProfile)
        enabled: reader.keysHere
        onActivated: fullScreen.toggle()
    }
    ReaderWindow { id: fullScreen }
    Shortcut { sequences: Keymap.sequences("reader.mode", reader.keyProfile); enabled: reader.keysHere; onActivated: reader.setOpt("readerMode", reader.mode === "strip" ? "paged" : "strip") }
    Shortcut { sequences: Keymap.sequences("reader.wider", reader.keyProfile); enabled: reader.keysHere; onActivated: reader.setOpt("readerWidth", Math.min(2400, reader.stripWidth + 80)) }
    Shortcut { sequences: Keymap.sequences("reader.narrower", reader.keyProfile); enabled: reader.keysHere; onActivated: reader.setOpt("readerWidth", Math.max(360, reader.stripWidth - 80)) }

    Shortcut { sequences: Keymap.sequences("reader.auto", reader.keyProfile); enabled: reader.keysHere; onActivated: reader.autoScrolling = !reader.autoScrolling }
    Shortcut { sequences: Keymap.sequences("reader.lens", reader.keyProfile); enabled: reader.keysHere && App.platform !== "android"; onActivated: reader.lensOn = !reader.lensOn }

    // Computers: a lens under the mouse (L, or the reader's settings),
    // twice as large, for small lettering and margin notes.
    property bool lensOn: App.settings.readerLens === true
    onLensOnChanged: App.setSetting("readerLens", lensOn)
    HoverHandler {
        id: lensHover
        enabled: reader.lensOn && App.platform !== "android"
    }
    Rectangle {
        id: lens
        z: 2
        readonly property real size: 260
        readonly property Item view: reader.mode === "strip" ? strip : pageView
        // SCANCORE_START {"lensAt": [x, y]} holds it there (tests).
        readonly property point at: App.startAction.lensAt ? Qt.point(App.startAction.lensAt[0], App.startAction.lensAt[1])
                                                           : lensHover.point.position
        visible: reader.lensOn && (lensHover.hovered || !!App.startAction.lensAt) && !reader.controlsShown
        width: size
        height: size
        radius: size / 2
        x: at.x - size / 2
        y: at.y - size / 2
        color: "black"
        border.width: 2
        border.color: Qt.rgba(1, 1, 1, 0.7)
        // The page under the mouse, half the lens wide, drawn at twice that.
        ShaderEffectSource {
            id: lensImage
            anchors.fill: parent
            anchors.margins: 2
            visible: false
            sourceItem: lens.visible ? lens.view : null
            live: true
            hideSource: false
            // Drawn at the lens's own size, so the page's shader enlarges it
            // (sharper than stretching a small copy).
            textureSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
            sourceRect: {
                const p = lens.view.mapFromItem(reader, lens.at.x, lens.at.y)
                return Qt.rect(p.x - lens.size / 4, p.y - lens.size / 4, lens.size / 2, lens.size / 2)
            }
        }
        // Round, with soft edges: the covers' shader cuts the circle.
        ShaderEffect {
            anchors.fill: lensImage
            property variant source: lensImage
            property size size: Qt.size(width, height)
            property size crop: Qt.size(1, 1)
            property real radius: width / 2
            property real zoom: 1
            fragmentShader: "qrc:/shaders/cover.frag.qsb"
        }
    }

    // ------------------------------------------------------------ strip

    ListView {
        id: strip
        anchors.fill: parent
        visible: reader.mode === "strip"
        interactive: !reader.magnifying
        transform: Scale {
            origin.x: reader.magnifyAt.x
            origin.y: reader.magnifyAt.y
            xScale: reader.magnifying ? 2 : 1
            yScale: xScale
            Behavior on xScale { NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic } }
        }
        model: stripModel
        cacheBuffer: Math.max(2400, height * 3)
        boundsBehavior: Flickable.StopAtBounds
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        spacing: reader.gap
        reuseItems: false

        onContentYChanged: {
            const i = indexAt(width / 2, contentY + height * 0.35)
            const row = i >= 0 ? stripModel.get(i) : null
            if (row && row.panel >= 0) {
                const item = itemAtIndex(i)
                reader.stripPartial = item && item.height > 0 ? Math.max(0, Math.min(1, (contentY + height * 0.35 - item.y) / item.height)) : 0
                if (row.chapter !== reader.index) reader.stripSwitch(row.chapter, row.panel)
                else reader.currentPanel = row.panel
            }
            // The end reached by scrolling (or auto-scroll), not the strip
            // being short while its pages load or put back where it was.
            if (atYEnd && reader.panels.length > 0 && reader.currentPanel >= reader.panels.length - 1
                && (moving || stripWheel.ease.running || reader.autoScrolling) && !reader.restoring)
                reader.markFinished()
            if (moving || stripWheel.ease.running) reader.controlsShown = false
        }

        // Wheel, touchpad and keys (scrollBy) share one eased scroll.
        WheelScroll {
            id: stripWheel
            wheelStep: 180
        }

        // Phone: a tap toggles the controls; on desktop single clicks are left
        // alone and a double click does. On the list itself, which takes
        // touches before anything behind it.
        TapHandler {
            onSingleTapped: (point) => {
                if (reader.stripTaps) {
                    const p = reader.mapFromItem(null, point.scenePosition.x, point.scenePosition.y)
                    reader.act(reader.tapZone(p.x, p.y, reader.width, reader.height))
                } else if (App.platform === "android") {
                    reader.toggleControls()
                }
            }
            onDoubleTapped: if (App.platform !== "android") reader.toggleControls()
            onLongPressed: {
                if (reader.longPressZoom) reader.startMagnifier(strip, point.position.x, point.position.y)
                else reader.pageMenu(strip, point.position.x, point.position.y, reader.pageUnder(strip, point.position.x, point.position.y))
            }
        }
        // Right click: this page's menu (mice only; touch taps also fire a
        // right-button handler).
        TapHandler {
            acceptedButtons: Qt.RightButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onTapped: (point) => reader.pageMenu(strip, point.position.x, point.position.y, reader.pageUnder(strip, point.position.x, point.position.y))
        }
        // Follows the finger while magnifying, and lets go with it.
        PointHandler {
            onPointChanged: reader.moveMagnifier(point.position.x, point.position.y)
            onActiveChanged: if (!active) reader.magnifying = false
        }

        // Ctrl+wheel: the strip's width.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ControlModifier
            onWheel: (event) => reader.setOpt("readerWidth", Math.max(360, Math.min(2400, reader.stripWidth + (event.angleDelta.y > 0 ? 60 : -60))))
        }

        delegate: Item {
            id: panelItem
            required property int index
            required property int chapter
            required property int panel
            required property real ratio
            readonly property var source: reader.stripChapters[chapter] || null
            readonly property var modelData: source && panel >= 0 ? source.panels[panel] || ({}) : ({})
            readonly property bool line: panel < 0
            width: strip.width
            height: line ? 140
                : image.status === PanelImage.Ready && image.sourceWidth > 0
                ? reader.stripWidth * image.sourceHeight / image.sourceWidth
                : reader.stripWidth * (ratio > 0 ? ratio : 1.4)
            property real lastHeight: 0
            onHeightChanged: {
                if (lastHeight > 0) reader.keepStripPlace(panelItem, height - lastHeight, lastHeight)
                lastHeight = height
            }
            function onScreen() { return y < strip.contentY + strip.height && y + height > strip.contentY }

            // Between two chapters of the strip.
            Column {
                visible: panelItem.line
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 420)
                spacing: 6
                Rectangle { width: parent.width; height: 1; color: "#3f3f46" }
                Item { width: 1; height: 10 }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Finished: ") + ((reader.chapters[panelItem.chapter - 1] || {}).title || "")
                    elide: Text.ElideRight
                    color: "#9a9aa2"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Next: ") + ((reader.chapters[panelItem.chapter] || {}).title || "")
                    elide: Text.ElideRight
                    color: "#e4e4e7"
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                }
            }

            PanelImage {
                id: image
                anchors.horizontalCenter: parent.horizontalCenter
                width: reader.stripWidth
                height: parent.height
                source: panelItem.line ? "" : panelItem.modelData.url || ""
                referer: panelItem.source ? panelItem.source.referer : reader.referer
                visible: !panelItem.line
                filter: reader.imageFilter
                sharpen: reader.sharpen
                maxDecodeWidth: 2400
                // Ready off screen: there at once. Loading in view: a short
                // fade from the spinner.
                opacity: 0
                onStatusChanged: {
                    if (status !== PanelImage.Ready) {
                        fadeIn.stop()
                        opacity = 0
                        return
                    }
                    if (sourceWidth > 0 && panelItem.ratio <= 0)
                        stripModel.setProperty(panelItem.index, "ratio", sourceHeight / sourceWidth)
                    if (panelItem.onScreen()) fadeIn.restart()
                    else opacity = 1
                }
                NumberAnimation { id: fadeIn; target: image; property: "opacity"; to: 1; duration: Motion.fast }
            }
            TranslationLayer {
                anchors.fill: image
                translating: reader.translating
                url: panelItem.line ? "" : panelItem.modelData.url || ""
                referer: panelItem.source ? panelItem.source.referer : reader.referer
                from: reader.translateFrom
                context: panelItem.source && !panelItem.line ? reader.translateContext(panelItem.source.panels, panelItem.panel, panelItem.chapter) : ({})
            }
            Column {
                anchors.centerIn: parent
                spacing: 10
                visible: !panelItem.line && image.status !== PanelImage.Ready
                Spinner {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: image.status === PanelImage.Loading
                    color: "#7a7a80"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: image.status === PanelImage.Error
                    text: qsTr("Page ") + (panelItem.panel + 1) + qsTr(" did not load")
                    color: "#a0a0a8"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
                ScButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: image.status === PanelImage.Error
                    text: qsTr("Retry")
                    compact: true
                    onClicked: image.reload()
                }
            }
        }

        footer: Item {
            width: strip.width
            height: Math.max(320, strip.height * 0.6)
            visible: reader.panels.length > 0
            NextChapterCard {
                anchors.centerIn: parent
                host: reader
            }
        }
    }

    // The strip's scroll bar, placed by page (the row on screen and how far
    // through it) rather than by the list's height, which ListView guesses
    // from the pages made so far and which jumps as pages of other heights
    // load. Drag it to go to a page.
    Item {
        id: stripBar
        readonly property int rows: stripModel.count
        readonly property real progress: {
            if (rows < 2) return 0
            const y = strip.contentY + 1
            const row = strip.indexAt(strip.width / 2, y)
            const item = row >= 0 ? strip.itemAtIndex(row) : null
            const within = item ? Math.min(1, Math.max(0, (y - item.y) / Math.max(1, item.height))) : 0
            return row < 0 ? (strip.atYEnd ? 1 : 0) : Math.min(1, (row + within) / rows)
        }
        readonly property bool shown: drag.pressed || drag.containsMouse || reader.controlsShown || strip.moving || stripWheel.ease.running
        visible: reader.mode === "strip" && rows > 1
        anchors.right: strip.right
        anchors.top: strip.top
        anchors.bottom: strip.bottom
        width: 16
        Rectangle {
            readonly property real length: Math.max(32, stripBar.height * Math.min(0.5, 2 / Math.max(1, stripBar.rows)))
            anchors.right: parent.right
            anchors.rightMargin: 2
            width: drag.pressed || drag.containsMouse ? 10 : 6
            height: length
            y: (stripBar.height - length) * stripBar.progress
            radius: width / 2
            color: drag.pressed ? Theme.textMuted : Theme.alpha(Theme.text, drag.containsMouse ? 0.35 : 0.2)
            opacity: stripBar.shown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Motion.slow } }
            Behavior on width { NumberAnimation { duration: Motion.fast } }
        }
        MouseArea {
            id: drag
            anchors.fill: parent
            enabled: stripBar.shown
            hoverEnabled: true
            preventStealing: true
            function go(y) {
                const row = Math.max(0, Math.min(stripBar.rows - 1, Math.floor(y / Math.max(1, height) * stripBar.rows)))
                strip.positionViewAtIndex(row, ListView.Beginning)
            }
            onPressed: (mouse) => go(mouse.y)
            onPositionChanged: (mouse) => { if (pressed) go(mouse.y) }
        }
    }

    // ------------------------------------------------------------ paged

    Item {
        id: pageView
        transform: Scale {
            origin.x: reader.magnifyAt.x
            origin.y: reader.magnifyAt.y
            xScale: reader.magnifying ? 2 : 1
            yScale: xScale
            Behavior on xScale { NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic } }
        }
        anchors.fill: parent
        visible: reader.mode === "paged"
        // The first page of the pages shown.
        property int page: 0
        property int direction: 1
        readonly property bool rtl: reader.direction === "rtl"

        // Two pages side by side in a wide window, when chosen: the cover
        // alone, then pairs, and a landscape page (already a spread) alone.
        readonly property bool twoUp: reader.opt("readerSpread", "single") === "double" && width > height * 1.15
        // Pages found to be landscape once loaded (many sources give no
        // sizes up front); the pages ahead load early, so this is usually
        // known before they are shown.
        property var widePages: ({})
        function noteShape(i, w, h) {
            if (i < 0 || w <= 0 || h <= 0) return
            const wide = w > h * 1.1
            if (!!widePages[i] === wide) return
            const next = Object.assign({}, widePages)
            next[i] = wide
            widePages = next
        }
        Connections {
            target: reader
            function onPanelsChanged() { pageView.widePages = {} }
        }
        function isWide(i) {
            if (widePages[i] !== undefined) return widePages[i]
            const p = reader.panels[i]
            return !!p && p.width > 0 && p.height > 0 && p.width > p.height * 1.1
        }
        readonly property var views: {
            const out = []
            const n = reader.panels.length
            const wide = (i) => isWide(i)
            let i = 0
            while (i < n) {
                if (twoUp && i > 0 && i + 1 < n && !wide(i) && !wide(i + 1)) {
                    out.push([i, i + 1])
                    i += 2
                } else {
                    out.push([i])
                    i += 1
                }
            }
            return out.length ? out : [[0]]
        }
        function viewOf(p) {
            for (let v = 0; v < views.length; ++v)
                if (views[v].indexOf(p) >= 0) return v
            return 0
        }
        readonly property int view: viewOf(page)
        readonly property var shown: views[view] || [page]
        // A wide page split in two: which half is showing (0 the first in
        // reading order), and which side of the image that is.
        property int part: 0
        readonly property bool split: reader.splitWide && !twoUp && shown.length === 1 && isWide(shown[0])
        readonly property string half: split ? ((part === 0) !== rtl ? "left" : "right") : ""
        // Turning to one or two pages keeps the same first page in view.
        onTwoUpChanged: page = views[viewOf(page)][0]

        function turn(step) {
            if (split && (step > 0 ? part === 0 : part === 1)) {
                direction = step
                part = step > 0 ? 1 : 0
                pageSwap.restart()
                return
            }
            const next = view + step
            if (next < 0) {
                if (reader.hasPrev) reader.load(reader.index - 1)
                return
            }
            if (next >= views.length) {
                reader.markFinished()
                if (reader.hasNext) reader.load(reader.index + 1)
                return
            }
            direction = step
            page = views[next][0]
            // Back onto a split page: its second half first.
            if (step < 0 && split) part = 1
        }
        onPageChanged: {
            part = 0
            resetZoom()
            // From `page` itself: `view` and `shown` may not have caught up
            // when this runs.
            const v = viewOf(page)
            const pagesShown = views[v] || [page]
            reader.currentPanel = pagesShown[pagesShown.length - 1]
            if (v === views.length - 1 && !reader.restoring) reader.markFinished()
            pageSwap.restart()
        }

        // ---- zoom: pinch, double-click the middle, or Ctrl + wheel
        readonly property bool zoomed: zoomLayer.scale > 1.01
        function clampPan() {
            zoomLayer.x = Math.min(0, Math.max(pageHolder.width * (1 - zoomLayer.scale), zoomLayer.x))
            zoomLayer.y = Math.min(0, Math.max(pageHolder.height * (1 - zoomLayer.scale), zoomLayer.y))
        }
        function zoomAt(px, py, target) {
            const z = Math.max(1, Math.min(4, target))
            const k = z / zoomLayer.scale
            zoomLayer.x = px - (px - zoomLayer.x) * k
            zoomLayer.y = py - (py - zoomLayer.y) * k
            zoomLayer.scale = z
            clampPan()
        }
        function resetZoom() {
            zoomAnim.stop()
            zoomLayer.scale = 1
            zoomLayer.x = 0
            zoomLayer.y = 0
        }
        function toggleZoom(px, py) {
            const z = zoomed ? 1 : 2.2
            const k = z / zoomLayer.scale
            zoomAnim.toScale = z
            zoomAnim.toX = z === 1 ? 0 : Math.min(0, Math.max(pageHolder.width * (1 - z), px - (px - zoomLayer.x) * k))
            zoomAnim.toY = z === 1 ? 0 : Math.min(0, Math.max(pageHolder.height * (1 - z), py - (py - zoomLayer.y) * k))
            zoomAnim.restart()
        }
        ParallelAnimation {
            id: zoomAnim
            property real toScale: 1
            property real toX: 0
            property real toY: 0
            NumberAnimation { target: zoomLayer; property: "scale"; to: zoomAnim.toScale; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { target: zoomLayer; property: "x"; to: zoomAnim.toX; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { target: zoomLayer; property: "y"; to: zoomAnim.toY; duration: Motion.normal; easing.type: Easing.OutCubic }
        }

        Item {
            id: pageHolder
            width: parent.width
            height: parent.height
            Item {
                id: zoomLayer
                width: pageHolder.width
                height: pageHolder.height
                transformOrigin: Item.TopLeft
                // One page centred, or two meeting in the middle (in reading
                // order: right to left puts the first on the right).
                Item {
                    id: pages
                    anchors.fill: parent
                    readonly property bool two: pageView.shown.length > 1
                    readonly property real slot: two ? width / 2 : width
                    readonly property real total: pageImage.width + (two ? secondImage.width : 0)
                    PanelImage {
                        id: pageImage
                        // Decoded for the width it is fitted to, again
                        // sharper for a zoom.
                        fitIn: Qt.size(pages.slot, pages.height)
                        zoom: zoomLayer.scale
                        readonly property real ratio: sourceWidth > 0 ? sourceHeight / sourceWidth : 1.45
                        width: Math.min(pages.slot, pages.height / ratio)
                        height: width * ratio
                        x: !pages.two ? (pages.width - width) / 2
                            : pageView.rtl ? pages.width / 2 : pages.width / 2 - width
                        y: (pages.height - height) / 2
                        source: reader.mode === "paged" && reader.panels.length > 0 ? reader.panels[Math.min(pageView.shown[0], reader.panels.length - 1)].url : ""
                        referer: reader.referer
                        cropBorders: reader.crop
                        filter: reader.imageFilter
                        sharpen: reader.sharpen
                        half: pageView.half
                        maxDecodeWidth: 2400
                        // A half is half the page's width.
                        onLoaded: pageView.noteShape(pageView.shown[0], half ? sourceWidth * 2 : sourceWidth, sourceHeight)
                    }
                    // Translated over (not with borders cropped, whose
                    // edges the translation does not know).
                    TranslationLayer {
                        x: pageImage.x
                        y: pageImage.y
                        width: pageImage.width
                        height: pageImage.height
                        translating: reader.translating && !reader.crop
                        url: pageImage.source
                        referer: reader.referer
                        from: reader.translateFrom
                        half: pageImage.half
                        context: reader.translateContext(reader.panels, pageView.shown[0], reader.index)
                    }
                    PanelImage {
                        id: secondImage
                        // Decoded for the width it is fitted to, again
                        // sharper for a zoom.
                        fitIn: Qt.size(pages.slot, pages.height)
                        zoom: zoomLayer.scale
                        readonly property real ratio: sourceWidth > 0 ? sourceHeight / sourceWidth : 1.45
                        visible: pages.two
                        width: Math.min(pages.slot, pages.height / ratio)
                        height: width * ratio
                        x: pageView.rtl ? pages.width / 2 - width : pages.width / 2
                        y: (pages.height - height) / 2
                        source: pages.two && reader.mode === "paged" ? reader.panels[pageView.shown[1]].url : ""
                        referer: reader.referer
                        cropBorders: reader.crop
                        filter: reader.imageFilter
                        sharpen: reader.sharpen
                        maxDecodeWidth: 2400
                        onLoaded: pageView.noteShape(pageView.shown[1], sourceWidth, sourceHeight)
                    }
                    TranslationLayer {
                        visible: pages.two && translating
                        x: secondImage.x
                        y: secondImage.y
                        width: secondImage.width
                        height: secondImage.height
                        translating: reader.translating && !reader.crop && pages.two
                        url: secondImage.source
                        referer: reader.referer
                        from: reader.translateFrom
                        context: reader.translateContext(reader.panels, pageView.shown[1], reader.index)
                    }
                }
            }
            Spinner {
                anchors.centerIn: parent
                visible: pageImage.status === PanelImage.Loading || secondImage.status === PanelImage.Loading
                color: "#7a7a80"
            }
            Column {
                anchors.centerIn: parent
                spacing: 10
                visible: pageImage.status === PanelImage.Error || secondImage.status === PanelImage.Error
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Page ") + (pageView.shown[pageImage.status === PanelImage.Error ? 0 : pageView.shown.length - 1] + 1) + qsTr(" did not load")
                    color: "#a0a0a8"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
                ScButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Retry")
                    compact: true
                    onClicked: {
                        if (pageImage.status === PanelImage.Error) pageImage.reload()
                        if (secondImage.status === PanelImage.Error) secondImage.reload()
                    }
                }
            }
        }
        // Preload the next pages and the one before.
        Repeater {
            model: {
                if (reader.mode !== "paged") return []
                const last = pageView.shown[pageView.shown.length - 1]
                return [last + 1, last + 2, last + 3, pageView.shown[0] - 1]
            }
            PanelImage {
                required property var modelData
                visible: false
                width: 1
                height: 1
                // Fetched and measured only: the page shown decodes its own.
                preload: true
                source: modelData >= 0 && modelData < reader.panels.length ? reader.panels[modelData].url : ""
                referer: reader.referer
                cropBorders: reader.crop
                filter: reader.imageFilter
                sharpen: reader.sharpen
                maxDecodeWidth: 2400
                onLoaded: pageView.noteShape(modelData, sourceWidth, sourceHeight)
            }
        }
        ParallelAnimation {
            id: pageSwap
            NumberAnimation { target: pageHolder; property: "opacity"; from: 0.35; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { target: pageHolder; property: "x"; from: 26 * pageView.direction * (pageView.rtl ? -1 : 1); to: 0; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            // The tap zones turn the page (unless zoomed in) or show the
            // controls; a double click on the controls' zone zooms.
            onClicked: (mouse) => { if (!mouse.wasHeld) reader.act(pageView.zoomed ? "menu" : reader.tapZone(mouse.x, mouse.y, width, height)) }
            onDoubleClicked: (mouse) => {
                if (reader.doubleTapZoom && (pageView.zoomed || reader.tapZone(mouse.x, mouse.y, width, height) === "menu"))
                    pageView.toggleZoom(mouse.x, mouse.y)
            }
            onPressAndHold: (mouse) => {
                if (swipe.active || pinch.active) return
                if (!reader.longPressZoom) reader.pageMenu(pageView, mouse.x, mouse.y, reader.pageUnder(pageView, mouse.x, mouse.y))
                else if (!pageView.zoomed) reader.startMagnifier(pageView, mouse.x, mouse.y)
            }
            // Right click: this page's menu (mice only).
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onTapped: (point) => reader.pageMenu(pageView, point.position.x, point.position.y, reader.pageUnder(pageView, point.position.x, point.position.y))
            }
            onPositionChanged: (mouse) => reader.moveMagnifier(mouse.x, mouse.y)
            onReleased: reader.magnifying = false
            onCanceled: reader.magnifying = false
            onWheel: (wheel) => {
                if (wheel.modifiers & Qt.ControlModifier) {
                    pageView.zoomAt(wheel.x, wheel.y, zoomLayer.scale * (wheel.angleDelta.y > 0 ? 1.2 : 1 / 1.2))
                } else if (pageView.zoomed) {
                    zoomLayer.x += wheel.angleDelta.x / 120 * 90
                    zoomLayer.y += wheel.angleDelta.y / 120 * 90
                    pageView.clampPan()
                } else {
                    pageView.turn(wheel.angleDelta.y < 0 ? 1 : -1)
                }
            }

            PinchHandler {
                id: pinch
                target: null
                property real startScale: 1
                onActiveChanged: if (active) startScale = zoomLayer.scale
                onActiveScaleChanged: if (active) pageView.zoomAt(centroid.position.x, centroid.position.y, startScale * activeScale)
            }

            // Zoomed in: dragging moves around the page.
            DragHandler {
                id: pan
                target: null
                enabled: pageView.zoomed
                property point start
                onActiveChanged: if (active) start = Qt.point(zoomLayer.x, zoomLayer.y)
                onTranslationChanged: if (active) {
                    zoomLayer.x = start.x + translation.x
                    zoomLayer.y = start.y + translation.y
                    pageView.clampPan()
                }
            }

            // Swipe (or drag) sideways to turn the page; the page follows
            // and springs back from a short swipe. Inside the MouseArea, so it
            // sees the press before the MouseArea takes it.
            DragHandler {
                id: swipe
                target: null
                enabled: !pageView.zoomed && !pinch.active && !reader.magnifying
                yAxis.enabled: false
                // A quick flick turns the page however short it is (as
                // Android's pagers do); a slow drag needs to go further.
                property real flingX: 0
                onTranslationChanged: if (active) {
                    pageHolder.x = translation.x * 0.7
                    flingX = centroid.velocity.x
                }
                onActiveChanged: {
                    if (active) {
                        pageSwap.stop()
                        flingX = 0
                        return
                    }
                    const dx = translation.x
                    const flung = Math.abs(flingX) > 500 && Math.abs(dx) > 16 && (flingX < 0) === (dx < 0)
                    if (flung || Math.abs(dx) > Math.min(90, pageView.width * 0.12)) {
                        const forward = dx < 0 ? !pageView.rtl : pageView.rtl
                        pageView.turn(forward ? 1 : -1)
                    }
                    if (!pageSwap.running) springBack.start()
                }
            }
            NumberAnimation { id: springBack; target: pageHolder; property: "x"; to: 0; duration: Motion.normal; easing.type: Easing.OutCubic }
        }
    }

    // Colour filter shades: warmth, then brightness. They take no input.
    Rectangle {
        anchors.fill: parent
        color: "#ff9a3c"
        opacity: reader.warmth * 0.28
        visible: opacity > 0
    }
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 1 - reader.brightness
        visible: opacity > 0
    }

    // The tap zones, shown for a moment when they change.
    Grid {
        id: zonePreview
        anchors.fill: parent
        z: 40
        columns: 3
        opacity: 0
        visible: opacity > 0
        Repeater {
            model: 9
            Rectangle {
                required property int index
                width: reader.width / 3
                height: reader.height / 3
                readonly property string action: reader.tapZone((index % 3 + 0.5) * width, (Math.floor(index / 3) + 0.5) * height, reader.width, reader.height)
                color: action === "next" ? Qt.rgba(0.2, 0.75, 0.45, 0.45) : action === "prev" ? Qt.rgba(0.95, 0.45, 0.3, 0.45) : Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.15)
                Text {
                    anchors.centerIn: parent
                    text: parent.action === "next" ? qsTr("Next") : parent.action === "prev" ? qsTr("Previous") : qsTr("Menu")
                    color: "white"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                }
            }
        }
        SequentialAnimation {
            id: zoneFlash
            NumberAnimation { target: zonePreview; property: "opacity"; to: 1; duration: Motion.fast }
            PauseAnimation { duration: 1400 }
            NumberAnimation { target: zonePreview; property: "opacity"; to: 0; duration: Motion.slow }
        }
    }
    onTapZonesChanged: zoneFlash.restart()
    onTapInvertChanged: zoneFlash.restart()

    // ------------------------------------------------------------ states

    Column {
        anchors.centerIn: parent
        spacing: 14
        visible: reader.task !== null && (reader.task.loading || !reader.task.ok)
        Spinner {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: reader.task !== null && reader.task.loading
            size: 30
            color: "#9a9aa2"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: reader.task !== null && reader.task.loading
            text: reader.unit.title || ""
            color: "#9a9aa2"
            font.family: Theme.fontSans
            font.pixelSize: Theme.textMd
        }
        Text {
            width: Math.min(420, reader.width - 48)
            visible: reader.task !== null && !reader.task.loading && !reader.task.ok
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: reader.task ? reader.task.error : ""
            color: "#d4d4d8"
            font.family: Theme.fontSans
            font.pixelSize: Theme.textMd
        }
        ScButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: reader.task !== null && !reader.task.loading && !reader.task.ok
            text: qsTr("Try again")
            iconName: "refresh-cw"
            onClicked: reader.load(reader.index)
        }
    }

    // Desktop: moving the mouse reveals the controls.
    PointerMotion {
        onMoved: reader.showControls()
    }

    // The page progress bar (ReaderProgress): its look, edge, size, colours
    // and strength are set in Settings → Reading. Tap or drag along it to go
    // to a page.
    function jumpTo(i) {
        if (i < 0 || i >= reader.panels.length) return
        if (reader.mode === "strip") strip.positionViewAtIndex(reader.stripRow(i), ListView.Beginning)
        else pageView.page = pageView.views[pageView.viewOf(i)][0]
    }
    Item {
        id: rail
        // Above the controls' shading, so it reads the same with them shown.
        z: 50
        anchors.fill: parent
        // Full screen (or the "with controls" setting) keeps it out of the way
        // until the controls come up.
        readonly property var mode: App.settings.readerProgressBar
        readonly property bool shown: mode !== false && reader.panels.length > 0
                                      && (mode !== "controls" && !fullScreen.fullScreen || reader.controlsShown)
        readonly property int thickness: Number(App.settings.readerProgressWidth) || 3
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        ReaderProgress {
            anchors.fill: parent
            inset: reader.insetTop
            insetBottom: reader.insetBottom
            count: reader.panels.length
            current: reader.currentPanel
            finished: reader.finished
            look: App.settings.readerProgressLook || "segments"
            marks: reactions.marks
            markColor: Theme.accent
            edge: App.settings.readerProgressEdge || "left"
            reversed: reader.direction === "rtl"
            thickness: rail.thickness
            unread: App.settings.readerRailUnread || "#e4e4e7"
            read: App.settings.readerRailRead || "#8b8b94"
            currentColor: App.settings.readerRailCurrent || "#000000"
            strength: App.settings.readerProgressOpacity !== undefined ? Number(App.settings.readerProgressOpacity) : 0.85
            onPicked: (i) => {
                if (i !== reader.currentPanel) reader.jumpTo(i)
                // Shown controls (and with them the bar) stay while it is used.
                if (reader.controlsShown) reader.showControls()
            }
        }
    }

    // ------------------------------------------------------------ top bar

    // The series, and the chapter with the page you are on.
    component TitleLines: Column {
        property real maxWidth: 200
        spacing: 1
        Text {
            text: reader.series.title || ""
            color: "white"
            font.family: Theme.fontSans
            font.pixelSize: Theme.textMd
            font.weight: Font.DemiBold
            width: Math.max(0, Math.min(implicitWidth, parent.maxWidth))
            elide: Text.ElideRight
        }
        Text {
            text: (reader.unit.title || "")
                  + (reader.panels.length ? "  ·  " + (reader.currentPanel + 1) + " / " + reader.panels.length : "")
                  + (reader.task && reader.task.ok && reader.task.result.offline ? qsTr("  ·  Downloaded") : "")
                  + (App.settings.incognito === true ? qsTr("  ·  Incognito") : "")
            color: "#b4b4bb"
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            width: Math.max(0, Math.min(implicitWidth, parent.maxWidth))
            elide: Text.ElideRight
        }
    }

    // Keeps the status bar legible over light pages while the controls are
    // hidden. Drawn over the top bar, which slides away under it instead of
    // across the phone's clock and icons.
    Rectangle {
        z: 3
        width: parent.width
        height: reader.insetTop
        visible: height > 0 && !reader.barsHidden
        color: Qt.rgba(0, 0, 0, 0.6)
    }

    Item {
        id: topBar
        // Phones: right under the status bar, as in the novel reader.
        readonly property int pad: Theme.compact ? 8 : 16
        // Too narrow for the title beside the buttons.
        readonly property bool twoRows: reader.width - 100 - topActions.width < 160
        width: parent.width
        height: (Theme.compact ? 64 : 76) + (twoRows ? 56 : 0) + reader.insetTop
        y: reader.controlsShown ? 0 : -height
        opacity: reader.controlsShown ? 1 : 0
        Behavior on y { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        HoverHandler { id: topHover; enabled: App.platform !== "android" }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.85) }
                // Dark through the buttons, so the bar reads as one piece
                // with the status bar, then fading into the page.
                GradientStop { position: (reader.insetTop + topBar.pad + 36 + (topBar.twoRows ? 40 : 0)) / topBar.height; color: Qt.rgba(0, 0, 0, 0.72) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0) }
            }
        }
        Row {
            x: 18
            y: topBar.pad + reader.insetTop
            spacing: 12
            IconButton {
                iconName: "arrow-left"
                iconColor: "white"
                tip: qsTr("Close reader (Esc)")
                onClicked: reader.exit()
            }
            TitleLines {
                visible: !topBar.twoRows
                anchors.verticalCenter: parent.verticalCenter
                maxWidth: reader.width - 100 - topActions.width
            }
        }
        // Phones: the buttons take the first row, the title the second.
        TitleLines {
            visible: topBar.twoRows
            x: 18
            y: topBar.pad + reader.insetTop + 40
            maxWidth: reader.width - 36
        }
        Row {
            id: topActions
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: topBar.pad + reader.insetTop
            // Phones: smaller buttons, no gaps, so all fit beside the close button.
            readonly property int button: reader.width < 420 ? 32 : 34
            spacing: reader.width < 420 ? 0 : 4
            // What the row would hold; when that is wider than the room
            // beside the close button, the occasional ones (reactions,
            // comments, another device, full screen) go into a More menu.
            readonly property bool online: !String(reader.series.sourceUrl || "").startsWith("file:")
            readonly property bool canReact: reactions.available && online
            readonly property bool canHandOff: Live.devices.length > 0 && !!reader.series.id && !App.settings.incognito
            readonly property int spares: (canReact ? 1 : 0) + (online ? 1 : 0) + (canHandOff ? 1 : 0) + 1
            readonly property int core: 6 + (translateTo.visible ? 1 : 0) + (newWindow.visible ? 1 : 0) + (splitButton.visible ? 1 : 0)
            readonly property real room: reader.width - 36 - 34 - 12
            readonly property bool crowded: (core + spares + (Theme.touch ? 1 : 0)) * (button + spacing) > room
            // Pages are on the progress bar at the left edge; chapters here.
            // Computers: on in a window of its own (where it left off).
            IconButton {
                id: newWindow
                diameter: topActions.button
                visible: Router.windows && !(Window.window && Window.window.isPageWindow === true)
                iconName: "columns-2"
                iconColor: "white"
                tip: qsTr("Open in a new window")
                onClicked: {
                    reader.saveProgress(true)
                    Router.openWindow("pages/ComicReader.qml", { series: reader.series, chapters: reader.chapters, index: reader.index })
                    Router.leave(reader)
                }
            }
            IconButton {
                diameter: topActions.button
                iconName: "skip-back"
                iconColor: "white"
                enabled: reader.hasPrev
                opacity: enabled ? 1 : 0.4
                tip: qsTr("Previous chapter") + reader.keyHint("reader.prev")
                onClicked: reader.load(reader.index - 1)
            }
            IconButton {
                diameter: topActions.button
                iconName: "skip-forward"
                iconColor: "white"
                enabled: reader.hasNext
                opacity: enabled ? 1 : 0.4
                tip: qsTr("Next chapter") + reader.keyHint("reader.next")
                onClicked: reader.load(reader.index + 1)
            }
            IconButton {
                diameter: topActions.button
                iconName: "list"
                iconColor: "white"
                tip: qsTr("Chapters")
                onClicked: chapterPopup.open()
            }
            IconButton {
                diameter: topActions.button
                iconName: reader.mode === "strip" ? "move-vertical" : "layers"
                iconColor: "white"
                tip: (reader.mode === "strip" ? qsTr("Switch to pages") : qsTr("Switch to vertical strip")) + reader.keyHint("reader.mode")
                onClicked: reader.setOpt("readerMode", reader.mode === "strip" ? "paged" : "strip")
            }
            // Wide windows: another comic or novel on the right half.
            IconButton {
                id: splitButton
                diameter: topActions.button
                visible: !!Router.shell && Router.shell.canSplit && Router.shell.splitItem !== reader
                iconName: "panel-left"
                iconColor: "white"
                tip: qsTr("Read another beside this")
                onClicked: splitPicker.start()
            }
            IconButton {
                diameter: topActions.button
                visible: topActions.canReact && !topActions.crowded
                iconName: "heart"
                iconColor: reactions.mineFor(reader.currentPanel) ? Theme.accent : "white"
                tip: qsTr("React to this page")
                onClicked: reactPopup.open()
            }
            IconButton {
                diameter: topActions.button
                visible: topActions.online && !topActions.crowded
                iconName: "message-circle"
                iconColor: "white"
                tip: qsTr("Comments")
                onClicked: comments.open()
            }
            IconButton {
                diameter: topActions.button
                iconName: "languages"
                iconColor: reader.translating ? Theme.accent : "white"
                tip: reader.translating ? (reader.narrowBar ? qsTr("Translate into %1").arg(reader.languageName(Translator.target)) : qsTr("Show the original"))
                                        : qsTr("Translate the pages")
                onClicked: {
                    if (!reader.translating && !Translator.pagesReady) {
                        Router.toast(Translator.hint, "info")
                        return
                    }
                    // Phones have no room for the language button: this one
                    // opens the languages, with "Show the original" first.
                    if (reader.translating && reader.narrowBar) {
                        reader.showControls()
                        translatePopup.open()
                        return
                    }
                    reader.translating = !reader.translating
                }
            }
            IconButton {
                id: translateTo
                diameter: topActions.button
                visible: reader.translating && !reader.narrowBar
                iconName: "chevron-down"
                iconColor: "white"
                tip: qsTr("Translate into %1").arg(reader.languageName(Translator.target))
                onClicked: {
                    reader.showControls()
                    translatePopup.open()
                }
            }
            ContinueOn {
                id: continueOn
                diameter: topActions.button
                visible: topActions.canHandOff && !topActions.crowded
                seriesId: reader.series.id || ""
                iconColor: "white"
                save: () => reader.saveProgress(true)
            }
            IconButton {
                diameter: topActions.button
                iconName: "sliders-horizontal"
                iconColor: "white"
                tip: qsTr("Reader settings")
                onClicked: settingsPopup.open()
            }
            IconButton {
                diameter: topActions.button
                visible: !topActions.crowded
                iconName: fullScreen.fullScreen ? "minimize" : "maximize"
                iconColor: "white"
                tip: qsTr("Full screen") + reader.keyHint("reader.fullscreen")
                onClicked: fullScreen.toggle()
            }
            // Narrow screens: the occasional buttons, as a menu.
            IconButton {
                id: moreActions
                diameter: topActions.button
                // Touch screens always have it: This page… is in it.
                visible: topActions.crowded || Theme.touch
                iconName: "ellipsis"
                iconColor: "white"
                tip: qsTr("More")
                onClicked: {
                    reader.showControls()
                    const list = []
                    const page = reader.pageInView()
                    if (page) list.push({ text: qsTr("This page…"), icon: "image", run: () => reader.pageMenu(moreActions, moreActions.width, moreActions.height, page) })
                    if (topActions.crowded) {
                    if (topActions.canReact)
                        list.push({ text: qsTr("React to this page"), icon: "heart", run: () => reactPopup.open() })
                    if (topActions.online)
                        list.push({ text: qsTr("Comments"), icon: "message-circle", run: () => comments.open() })
                    if (topActions.canHandOff)
                        for (const device of Live.devices)
                            list.push({ text: qsTr("Continue on %1").arg(device.name), icon: device.platform === "android" ? "smartphone" : "monitor",
                                        run: () => continueOn.send(device) })
                    list.push({ text: fullScreen.fullScreen ? qsTr("Leave full screen") : qsTr("Full screen"),
                                icon: fullScreen.fullScreen ? "minimize" : "maximize", run: () => fullScreen.toggle() })
                    }
                    moreMenu.openAt(moreActions, moreActions.width, moreActions.height, list)
                }
                ActionMenu { id: moreMenu }
            }
        }
    }

    // ------------------------------------------------------------ popups

    // Too narrow for a language button beside the others.
    readonly property bool narrowBar: width < 420
    function languageName(code) {
        const found = Customize.languages.find(l => l.value === code)
        return found ? found.label : code
    }

    // The language pages are translated into. The controls stay up while it
    // is open and for a while after a choice, since the pages redraw under it.
    Popup {
        id: translatePopup
        x: parent.width - width - 18
        y: topBar.height - 6
        width: Math.min(260, parent.width - 36)
        height: Math.min(parent.height - 160 - reader.insetTop - reader.insetBottom, 420)
        padding: 6
        onOpened: {
            reader.showControls()
            languageList.positionViewAtIndex(Math.max(0, Customize.languages.findIndex(l => l.value === Translator.target)), ListView.Center)
        }
        onClosed: reader.showControls()
        background: Rectangle { radius: Theme.radiusLg; color: "#151517"; border.width: 1; border.color: "#2a2a30" }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.fast } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast } }
        contentItem: ListView {
            id: languageList
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            clip: true
            model: Customize.languages
            ScrollBar.vertical: ScScrollBar {}
            header: ItemDelegate {
                width: languageList.width
                height: 42
                onClicked: {
                    translatePopup.close()
                    reader.translating = false
                }
                background: Rectangle { radius: Theme.radiusSm; color: parent.hovered ? "#1f1f23" : "transparent" }
                contentItem: Row {
                    spacing: 10
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "eye"; size: 16; color: "#c4c4cb" }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Show the original")
                        color: "white"
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        font.weight: Font.DemiBold
                    }
                }
            }
            delegate: ItemDelegate {
                required property var modelData
                readonly property bool chosen: modelData.value === Translator.target
                width: languageList.width
                height: 38
                onClicked: {
                    App.setSetting("translateTarget", modelData.value)
                    translatePopup.close()
                }
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: chosen ? "#2a2a30" : parent.hovered ? "#1f1f23" : "transparent"
                }
                contentItem: Text {
                    text: modelData.label
                    color: chosen ? "white" : "#c4c4cb"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    font.weight: chosen ? Font.DemiBold : Font.Normal
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    Popup {
        id: chapterPopup
        x: parent.width - width - 18
        y: topBar.height - 6
        width: Math.min(340, parent.width - 36)
        height: Math.min(parent.height - 160 - reader.insetTop - reader.insetBottom, 560)
        padding: 6
        onOpened: chapterList.positionViewAtIndex(reader.index, ListView.Center)
        background: Rectangle { radius: Theme.radiusLg; color: "#151517"; border.width: 1; border.color: "#2a2a30" }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.fast } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast } }
        contentItem: ListView {
            id: chapterList
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            clip: true
            model: reader.chapters
            ScrollBar.vertical: ScScrollBar {}
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: chapterList.width
                height: 38
                onClicked: {
                    chapterPopup.close()
                    reader.load(index)
                }
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: index === reader.index ? "#2a2a30" : parent.hovered ? "#1f1f23" : "transparent"
                }
                contentItem: Text {
                    text: modelData.title
                    color: index === reader.index ? "white" : "#c4c4cb"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    font.weight: index === reader.index ? Font.DemiBold : Font.Normal
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    SplitPicker { id: splitPicker }

    // Reactions on this chapter's pages (dots on the page bar).
    PageReactions {
        id: reactions
        media: "comic"
        title: comments.title
        comickHid: comments.comickHid
        url: comments.url
        unit: comments.unit
    }
    Popup {
        id: reactPopup
        x: parent.width - width - 18
        y: topBar.height - 6
        padding: 10
        background: Rectangle { radius: Theme.radiusLg; color: "#151517"; border.width: 1; border.color: "#2a2a30" }
        contentItem: Row {
            spacing: 4
            Repeater {
                model: reactions.kinds
                delegate: AbstractButton {
                    id: kind
                    required property var modelData
                    readonly property int count: reactions.countsFor(reader.currentPanel)[modelData.type] || 0
                    readonly property bool chosen: reactions.mineFor(reader.currentPanel) === modelData.type
                    width: 46
                    height: 52
                    hoverEnabled: true
                    onClicked: { reactions.react(reader.currentPanel, modelData.type); reactPopup.close() }
                    background: Rectangle {
                        radius: Theme.radiusMd
                        color: kind.chosen ? Theme.alpha(Theme.accent, 0.3) : kind.hovered ? "#26262b" : "transparent"
                    }
                    contentItem: Column {
                        spacing: 0
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: kind.modelData.glyph; font.pixelSize: 22 }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: kind.count > 0 ? kind.count : ""
                            color: "#b4b4bb"
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textXs
                        }
                    }
                }
            }
        }
    }

    CommentsPanel {
        id: comments
        media: "comic"
        title: (reader.series.extra && reader.series.extra.catalogTitle) || reader.series.title || ""
        comickHid: (reader.series.extra && reader.series.extra.hid) || reader.series.hid || ""
        url: reader.series.sourceUrl || ""
        unit: String(reader.unit.number || "")
        label: qsTr("Chapter ") + (reader.unit.number || "")
    }

    Popup {
        id: settingsPopup
        // Phones: a sheet from the bottom edge, as wide as the screen, that
        // scrolls; computers: a panel under the top bar.
        readonly property bool sheet: Theme.compact
        readonly property real restY: sheet ? parent.height - height : topBar.height - 6
        x: sheet ? 0 : parent.width - width - 18
        y: restY
        width: sheet ? parent.width : Math.min(320, parent.width - 36)
        padding: 18
        bottomPadding: sheet ? 18 + reader.insetBottom : 18
        background: GlassPanel {
            radius: Theme.radiusLg
            fill: "#151517"
            border.width: 1
            border.color: "#2a2a30"
            // Square along the bottom edge.
            squareBottom: settingsPopup.sheet
        }
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.fast }
                NumberAnimation { property: "y"; from: settingsPopup.restY + (settingsPopup.sheet ? 60 : -10); to: settingsPopup.restY; duration: Motion.normal; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast } }
        // Scrolls when it is taller than the window (phones).
        contentItem: Flickable {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            implicitHeight: Math.min(settingsColumn.implicitHeight, settingsPopup.sheet ? reader.height * 0.62 : reader.height - 110 - reader.insetTop - reader.insetBottom)
            contentHeight: settingsColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            Column {
                id: settingsColumn
                width: parent.width
                spacing: 16
                component Heading: Text { color: "#9a9aa2"; font.family: Theme.fontSans; font.pixelSize: Theme.textXs; font.capitalization: Font.AllUppercase; font.letterSpacing: 1 }
                component Label: Text { anchors.verticalCenter: parent.verticalCenter; color: "#d4d4d8"; font.family: Theme.fontSans; font.pixelSize: Theme.textSm }
                Heading { text: qsTr("Layout") }
                Segmented {
                    width: parent.width
                    fill: true
                    options: [{ value: "strip", label: qsTr("Vertical"), icon: "move-vertical" }, { value: "paged", label: qsTr("Pages"), icon: "layers" }]
                    value: reader.mode
                    onPicked: (v) => reader.setOpt("readerMode", v)
                }
                Segmented {
                    width: parent.width
                    fill: true
                    visible: reader.mode === "paged"
                    options: [{ value: "ltr", label: qsTr("Left to right") }, { value: "rtl", label: qsTr("Right to left") }]
                    value: reader.direction
                    onPicked: (v) => reader.setOpt("readerDirection", v)
                }
                Segmented {
                    width: parent.width
                    fill: true
                    visible: reader.mode === "paged" && App.platform !== "android"
                    options: [{ value: "single", label: qsTr("One page") }, { value: "double", label: qsTr("Two pages") }]
                    value: reader.opt("readerSpread", "single")
                    onPicked: (v) => reader.setOpt("readerSpread", v)
                }
                Row {
                    visible: reader.mode === "paged"
                    spacing: 10
                    Toggle {
                        checked: reader.crop
                        onToggled: reader.setOpt("readerCrop", checked)
                    }
                    Label { text: qsTr("Trim white or black borders") }
                }
                Row {
                    visible: reader.mode === "paged"
                    spacing: 10
                    Toggle {
                        checked: reader.splitWide
                        onToggled: reader.setOpt("readerSplitWide", checked)
                    }
                    Label { text: qsTr("Split wide pages in two") }
                }
                Row {
                    visible: App.platform !== "android"
                    spacing: 10
                    Toggle {
                        checked: reader.lensOn
                        onToggled: reader.lensOn = checked
                    }
                    Label { text: qsTr("Magnifying lens under the mouse (L)") }
                }
                Row {
                    visible: reader.mode === "strip"
                    spacing: 10
                    Toggle {
                        checked: reader.continuous
                        onToggled: reader.setOpt("readerContinuous", checked)
                    }
                    Label { text: qsTr("Run on into the next chapter") }
                }
                Row {
                    spacing: 10
                    Toggle {
                        checked: reader.autoScrolling
                        onToggled: reader.autoScrolling = checked
                    }
                    Label { text: reader.mode === "strip" ? qsTr("Auto-scroll") : qsTr("Turn the pages by themselves") }
                }
                Heading { text: reader.mode === "strip" ? qsTr("Auto-scroll speed · %1").arg(reader.autoSpeed) : qsTr("A page every %1 s").arg(22 - reader.autoSpeed) }
                ScSlider {
                    width: parent.width
                    from: 1
                    to: 20
                    stepSize: 1
                    value: reader.autoSpeed
                    onMoved: reader.setAutoSpeed(value)
                }
                Heading { visible: reader.mode === "strip"; text: qsTr("Width · %1 px").arg(reader.stripWidth) }
                ScSlider {
                    visible: reader.mode === "strip"
                    width: parent.width
                    from: 360
                    to: 2000
                    stepSize: 20
                    value: reader.stripWidth
                    onMoved: reader.setOpt("readerWidth", value)
                }
                Heading { visible: reader.mode === "strip"; text: qsTr("Gap between panels · %1 px").arg(reader.gap) }
                ScSlider {
                    visible: reader.mode === "strip"
                    width: parent.width
                    from: 0
                    to: 40
                    stepSize: 2
                    value: reader.gap
                    onMoved: reader.setOpt("readerGap", value)
                }

                Rectangle { width: parent.width; height: 1; color: "#2a2a30" }
                Heading { text: qsTr("Colour · brightness %1%").arg(Math.round(reader.brightness * 100)) }
                ScSlider {
                    width: parent.width
                    from: 0.2
                    to: 1
                    stepSize: 0.05
                    value: reader.brightness
                    onMoved: reader.setOpt("readerBrightness", value)
                }
                Heading { text: qsTr("Warmth · %1%").arg(Math.round(reader.warmth * 100)) }
                ScSlider {
                    width: parent.width
                    from: 0
                    to: 1
                    stepSize: 0.05
                    value: reader.warmth
                    onMoved: reader.setOpt("readerWarmth", value)
                }
                Row {
                    spacing: 10
                    Toggle {
                        checked: reader.opt("readerGrey", false) === true
                        onToggled: reader.setOpt("readerGrey", checked)
                    }
                    Label { text: qsTr("Greyscale") }
                }
                Row {
                    spacing: 10
                    Toggle {
                        checked: reader.opt("readerInvert", false) === true
                        onToggled: reader.setOpt("readerInvert", checked)
                    }
                    Label { text: qsTr("Invert colours") }
                }
                // Small scans, enlarged: sharper lines (resources/shaders/panel.frag).
                Heading { text: qsTr("Sharpen small scans") }
                Segmented {
                    width: parent.width
                    fill: true
                    options: [{ value: 0, label: qsTr("Off") }, { value: 0.35, label: qsTr("Light") }, { value: 0.75, label: qsTr("Strong") }]
                    value: reader.sharpen >= 0.6 ? 0.75 : reader.sharpen > 0 ? 0.35 : 0
                    onPicked: (v) => reader.setOpt("readerSharpen", v)
                }
                // Computers: downloads enlarged twice with waifu2x (OnDevice).
                Row {
                    id: upscaleRow
                    visible: OnDevice.available
                    spacing: 10
                    Toggle {
                        id: upscaleToggle
                        checked: reader.opt("readerUpscale", false) === true
                        onToggled: {
                            reader.setOpt("readerUpscale", checked)
                            if (checked && !OnDevice.upscalerInstalled()) {
                                upscaleInstall.task = OnDevice.installUpscaler()
                                Router.toast(qsTr("Downloading the upscaler (about 15 MB the first time)…"), "info")
                            }
                        }
                    }
                    Label {
                        width: upscaleRow.parent.width - upscaleToggle.width - upscaleRow.spacing
                        wrapMode: Text.Wrap
                        text: qsTr("Enlarge downloaded pages (sharper; slower downloads)")
                    }
                    Connections {
                        id: upscaleInstall
                        property var task: null
                        target: task
                        function onFinished() { Router.toast(qsTr("Chapters downloaded from now on are enlarged."), "success") }
                        function onFailed(error) { Router.toast(error, "error") }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: "#2a2a30" }
                Heading { text: qsTr("Taps") }
                Select {
                    width: parent.width
                    Accessible.name: qsTr("Tap zones")
                    model: reader.tapZoneOptions
                    value: reader.tapZones
                    onPicked: (v) => reader.setOpt("readerTapZones", v)
                }
                Select {
                    width: parent.width
                    visible: reader.tapZones !== "off"
                    Accessible.name: qsTr("Flip the tap zones")
                    model: reader.tapInvertOptions
                    value: reader.tapInvert
                    onPicked: (v) => reader.setOpt("readerTapInvert", v)
                }
                Row {
                    visible: reader.mode === "strip" && reader.tapZones !== "off"
                    spacing: 10
                    Toggle {
                        checked: reader.stripTaps
                        onToggled: reader.setOpt("readerStripTaps", checked)
                    }
                    Label { text: qsTr("Tap zones in the vertical strip") }
                }
                Row {
                    visible: App.platform === "android"
                    spacing: 10
                    Toggle {
                        checked: reader.volumeKeys
                        onToggled: reader.setOpt("readerVolumeKeys", checked)
                    }
                    Label { text: qsTr("Volume keys turn pages") }
                }

                Rectangle { width: parent.width; height: 1; color: "#2a2a30" }
                Heading { text: qsTr("Page progress bar") }
                Segmented {
                    width: parent.width
                    fill: true
                    options: [{ value: true, label: qsTr("Always") }, { value: "controls", label: qsTr("With controls") }, { value: false, label: qsTr("Off") }]
                    value: rail.mode
                    onPicked: (v) => App.setSetting("readerProgressBar", v)
                }
                Segmented {
                    width: parent.width
                    fill: true
                    visible: rail.mode !== false
                    options: [{ value: 3, label: qsTr("Thin") }, { value: 6, label: qsTr("Medium") }, { value: 10, label: qsTr("Thick") }]
                    value: rail.thickness
                    onPicked: (v) => App.setSetting("readerProgressWidth", v)
                }
                Segmented {
                    width: parent.width
                    fill: true
                    visible: rail.mode !== false
                    options: [{ value: "segments", label: qsTr("Pages") }, { value: "line", label: qsTr("Line") }, { value: "floating", label: qsTr("Floating") }]
                    value: App.settings.readerProgressLook || "segments"
                    onPicked: (v) => App.setSetting("readerProgressLook", v)
                }
                Segmented {
                    width: parent.width
                    fill: true
                    visible: rail.mode !== false
                    options: [{ value: "left", label: qsTr("Left") }, { value: "right", label: qsTr("Right") }, { value: "bottom", label: qsTr("Bottom") }]
                    value: App.settings.readerProgressEdge || "left"
                    onPicked: (v) => App.setSetting("readerProgressEdge", v)
                }
                Row {
                    spacing: 10
                    Toggle {
                        checked: fullScreen.fullScreen
                        onToggled: if (checked !== fullScreen.fullScreen) fullScreen.toggle()
                    }
                    Label { text: qsTr("Full screen") }
                }
                // Key profiles (Settings → Keyboard), once there are any.
                Rectangle { visible: keysSelect.visible; width: parent.width; height: 1; color: "#2a2a30" }
                Heading { visible: keysSelect.visible; text: qsTr("Keys") }
                Select {
                    id: keysSelect
                    visible: !!reader.series.id && Keymap.profileNames.length > 0
                    width: parent.width
                    Accessible.name: qsTr("Keys for this series")
                    model: [{ value: "", label: qsTr("Everyday keys") }].concat(Keymap.profileNames.map(n => ({ value: n, label: n })))
                    value: reader.keyProfile
                    onPicked: (v) => reader.setKeyProfile(v)
                }
                Rectangle { width: parent.width; height: 1; color: "#2a2a30" }
                Row {
                    visible: !!reader.series.id
                    spacing: 10
                    Toggle {
                        checked: reader.seriesPrefs !== null
                        onToggled: reader.setSeriesOnly(checked)
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: qsTr("Only for this series"); color: "#d4d4d8"; font.family: Theme.fontSans; font.pixelSize: Theme.textSm }
                        Text { text: qsTr("Keeps these settings for %1 alone").arg(reader.series.title || qsTr("it")); width: 230; elide: Text.ElideRight; color: "#8a8a92"; font.family: Theme.fontSans; font.pixelSize: Theme.textXs + 1 }
                    }
                }
            }
        }
    }

    Rectangle {
        id: einkFlashCover
        anchors.fill: parent
        z: 1000
        color: "black"
        visible: einkFlash.running
    }
    Timer { id: einkFlash; interval: 120 }
    // Auto-scroll running: pause, slower and faster, by the thumb.
    Rectangle {
        id: autoPill
        z: 60
        visible: opacity > 0
        opacity: reader.autoScrolling || autoPause.running ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Motion.fast } }
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - 28 - reader.insetBottom - (reader.controlsShown ? 56 : 0)
        Behavior on y { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } }
        width: autoRow.width + 12
        height: 44
        radius: 22
        color: "#e6151517"
        border.width: 1
        border.color: "#2a2a30"
        // Paused from here: the pill stays a moment to start it again.
        Timer { id: autoPause; interval: 4000 }
        Row {
            id: autoRow
            anchors.centerIn: parent
            spacing: 2
            IconButton { iconName: "minus"; iconColor: "white"; tip: qsTr("Slower"); onClicked: reader.setAutoSpeed(reader.autoSpeed - 1) }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                horizontalAlignment: Text.AlignHCenter
                text: reader.autoSpeed
                color: "#e4e4e7"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
            }
            IconButton { iconName: "plus"; iconColor: "white"; tip: qsTr("Faster"); onClicked: reader.setAutoSpeed(reader.autoSpeed + 1) }
            IconButton {
                iconName: reader.autoScrolling ? "pause" : "play"
                iconColor: "white"
                tip: reader.autoScrolling ? qsTr("Pause auto-scroll") : qsTr("Auto-scroll")
                onClicked: {
                    reader.autoScrolling = !reader.autoScrolling
                    if (!reader.autoScrolling) autoPause.restart()
                }
            }
        }
    }
}
