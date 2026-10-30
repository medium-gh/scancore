import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Scancore

// Full-window novel reader with adjustable typography, reading themes,
// remembered position and the next chapter loaded ahead. Reads aloud with the
// system's voices (Speech), following the spoken paragraph, and can scroll
// by itself.
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
    property int nextFor: -1
    readonly property var blocks: task && task.ok ? task.result.blocks : []
    // The chapter translated (Translator), paragraph by paragraph, from the
    // source's language (WTR-LAB's raw Chinese novels, and any other).
    // Remembered per series (Library: translate); with Settings → Reading →
    // Translate novels in other languages, it starts by itself whenever the
    // source's language is not the one translated into (WTR-LAB, in English,
    // for someone reading in Spanish).
    property bool translating: App.startAction.translate === true || startsTranslated()
    function startsTranslated() {
        const own = series.id ? Library.seriesSettings(series.id).translate : undefined
        if (own !== undefined && own !== null) return own === true
        const from = String(translateFrom).split(/[-_]/)[0]
        return App.settings.translateNovelsAuto === true && Translator.textReady && from !== ""
            && from !== String(Translator.target).split(/[-_]/)[0]
    }
    readonly property string translateFrom: {
        const key = series.source || ""
        const found = key ? Novels.sources().find(s => s.key === key) : null
        return found && found.language ? String(found.language) : ""
    }
    property var translateTask: null
    readonly property var translated: translating && translateTask && translateTask.ok ? translateTask.result : []
    function translateChapter() {
        translateTask = translating && blocks.length
            ? Translator.texts(blocks.map(b => b.kind === "break" ? "" : (b.text || "")), translateFrom,
                { work: series.title || "", scope: String(series.id || series.sourceUrl || series.title || ""), chapter: unit.title || "" }) : null
    }
    onBlocksChanged: if (translating) translateChapter()
    onTranslatingChanged: translateChapter()
    readonly property string translateTarget: Translator.target
    onTranslateTargetChanged: if (translating) translateChapter()
    Connections {
        target: reader.translateTask
        function onFailed(error) {
            Router.toast(error, "error")
            reader.translating = false
        }
    }
    function plainHtml(text) { return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    property bool barsShown: true
    // Phones: on full screen while the bars are hidden (Settings → Reading).
    readonly property bool systemBarsHidden: App.platform === "android" && App.settings.readerHideBars !== false && !barsShown
    onSystemBarsHiddenChanged: App.setSystemBars(!systemBarsHidden)
    property real lastY: 0
    property var resumePosition: null
    property bool finished: false

    readonly property var themes: Theme.readingThemes
    readonly property var look: Theme.readingLook(App.settings.novelTheme)
    readonly property int fontSize: App.settings.novelSize || 19
    readonly property real lineHeight: App.settings.novelLineHeight || 1.5
    readonly property int margin: App.settings.novelMargin === undefined ? 32 : App.settings.novelMargin
    readonly property int columnWidth: Math.min(width - 2 * margin, App.settings.novelWidth || 720)
    readonly property string fontFamily: App.settings.novelFont || Theme.fontSerif
    readonly property int fontWeight: App.settings.novelWeight || Font.Normal
    readonly property real paragraphSpacing: App.settings.novelParagraphSpacing === undefined ? 0.9 : App.settings.novelParagraphSpacing
    readonly property bool indent: App.settings.novelIndent === true

    // Reading aloud (ReadAloud, which goes on when the reader closes):
    // whether it is this chapter, and the paragraph being spoken (-1 for
    // the title).
    readonly property bool readingHere: ReadAloud.active && ReadAloud.series.id === series.id && ReadAloud.chapterIndex === index
    readonly property bool speaking: readingHere && ReadAloud.playing
    readonly property int spokenBlock: readingHere ? ReadAloud.block : -2
    // Scrolling by itself, at novelAutoScrollSpeed pixels a second.
    property bool autoScrolling: false

    // Drawn under the phone's status bar in the reading theme's colour; the
    // bars and text are padded by the inset instead.
    readonly property bool edgeToEdge: true
    // Hidden bars wait above the top edge: never let them show outside.
    clip: true
    readonly property bool systemBarsDark: Theme.isDark(look.bg)
    readonly property real insetTop: SafeArea.margins.top
    readonly property real insetBottom: SafeArea.margins.bottom

    color: look.bg
    focus: true
    Behavior on color { ColorAnimation { duration: Motion.slow } }

    // The chapter's text fades in (the list's header plays it).
    signal reveal()

    function load(i, followingVoice) {
        savePosition(true)
        // A chapter picked by hand stops the voice; one the voice moved on
        // to is only shown.
        if (!followingVoice && ReadAloud.active && ReadAloud.series.id === series.id && ReadAloud.chapterIndex !== i) stopSpeaking()
        index = i
        finished = false
        const p = Library.progressMap(series.id)[chapters[i].key]
        resumePosition = p && !p.completed && p.position ? p.position : null
        // The next chapter fetched ahead, finished or still on its way.
        const ahead = nextTask && nextFor === i && (nextTask.loading || nextTask.ok)
        task = ahead ? nextTask : Novels.chapter(chapters[i].url, series.id)
        nextTask = null
        nextFor = -1
        flick.positionViewAtBeginning()
        barsShown = true
        if (task.done) Qt.callLater(arrived)
        reveal()
        Presence.show("novel", series.title || "", chapters[i].title || "")
    }

    // The screen stays on while reading (a setting, on by default).
    readonly property bool keepScreenOn: App.settings.readerKeepAwake !== false && Qt.application.state === Qt.ApplicationActive
    onKeepScreenOnChanged: App.keepAwake("reader", keepScreenOn)

    Component.onCompleted: {
        load(index)
        App.keepAwake("reader", keepScreenOn)
        Activity.setMedia("novel")
    }
    Component.onDestruction: {
        App.setSystemBars(true)
        App.keepAwake("reader", false)
        // Reading aloud goes on without the reader (the mini player has it).
        Activity.setMedia("")
        Presence.clear()
        savePosition(true)
    }

    Connections {
        target: reader.task
        function onFinished() { Qt.callLater(reader.arrived) }
    }

    // A chapter's text is in: go back to where reading stopped, and count a
    // chapter that fits on one screen as read.
    function arrived() {
        if (blocks.length === 0) return
        // The list places its paragraphs later; their positions are needed now.
        flick.forceLayout()
        if (resumePosition && resumePosition.block !== undefined)
            goToBlock(resumePosition.block)
        resumePosition = null
        if (flick.contentHeight - flick.height <= 4) {
            finished = true
            savePosition(true)
        }
    }

    // Reads the chapter aloud from block `from` (-1: from its title).
    function startSpeaking(from) {
        const title = (task && task.ok && task.result.title) ? task.result.title : (unit.title || "")
        ReadAloud.start(series, chapters, index, blocks, from, title)
        autoScrolling = false
    }
    function stopSpeaking() { ReadAloud.stop() }
    // The spoken sentence in a paragraph's markup, in the highlight colour.
    // start and length count the paragraph's plain text: tags are skipped
    // and an entity (&amp;) is one character.
    readonly property color sentenceColor: Theme.accentIsMono ? (Theme.isDark(look.bg) ? "#fbbf24" : "#b45309") : Theme.accent
    function markSentence(html, start, length) {
        if (length <= 0) return html
        const open = '<font color="' + String(sentenceColor).slice(0, 7) + '">'
        let out = "", seen = 0, opened = false, closed = false
        for (let i = 0; i < html.length; ++i) {
            const c = html[i]
            if (c === "<") {
                const end = html.indexOf(">", i)
                if (end < 0) { out += html.slice(i); break }
                out += html.slice(i, end + 1)
                i = end
                continue
            }
            if (!opened && seen === start) { out += open; opened = true }
            if (opened && !closed && seen === start + length) { out += "</font>"; closed = true }
            if (c === "&") {
                const end = html.indexOf(";", i)
                if (end > i && end - i < 10) { out += html.slice(i, end + 1); i = end; ++seen; continue }
            }
            out += c
            ++seen
        }
        if (opened && !closed) out += "</font>"
        return out
    }

    // A voice setting changed while reading: the next sentence uses it.
    function restartVoice() {
        if (!speaking) return
        ReadAloud.pause()
        ReadAloud.play()
    }

    Connections {
        target: ReadAloud
        // Keep the spoken sentence on screen, without jumping for every line.
        function onPositionChanged() {
            if (!reader.readingHere || ReadAloud.block < 0 || App.settings.ttsFollow === false) return
            const item = flick.itemAtIndex(ReadAloud.block)
            if (!item || item.y < flick.contentY + 80 || item.y + item.height > flick.contentY + flick.height - 80)
                flick.showBlock(ReadAloud.block, flick.height * 0.25, !!item)
        }
        // The voice went on to another chapter of this book: show it.
        function onChapterChanged() {
            if (ReadAloud.active && ReadAloud.series.id === reader.series.id && ReadAloud.chapterIndex >= 0
                    && ReadAloud.chapterIndex !== reader.index)
                reader.load(ReadAloud.chapterIndex, true)
        }
        function onFailed(message) { Router.toast(message, "error") }
        function onSlept() { Router.toast(qsTr("Sleep timer: reading aloud stopped"), "info") }
    }

    FrameAnimation {
        running: reader.autoScrolling && !flick.moving && !flick.dragging
        onTriggered: {
            if (flick.atYEnd) {
                reader.autoScrolling = false
                return
            }
            flick.contentY += (Number(App.settings.novelAutoScrollSpeed) || 40) * frameTime
        }
    }

    // Paragraph `i` just under the bar at the top.
    function goToBlock(i) { flick.showBlock(Math.max(0, Math.min(i, blocks.length - 1)), 80, false) }

    // The paragraph just under the bar at the top (the title counts as 0).
    function firstVisibleBlock() {
        // indexAt misses the gaps between paragraphs; look a little lower.
        for (let dy = 80; dy < 240; dy += 20) {
            const i = flick.indexAt(flick.width / 2, flick.contentY + dy)
            if (i >= 0) return i
        }
        return 0
    }

    property real lastSaved: 0
    function savePosition(force) {
        if (!series.id || !unit.key || blocks.length === 0) return
        if (!force && Date.now() - lastSaved < 1500) return
        lastSaved = Date.now()
        const percent = finished ? 1 : flick.progress
        Library.recordProgress(series, unit, { type: "text", block: firstVisibleBlock(), percent: percent }, percent, finished || percent > 0.97)
    }

    function exit() {
        savePosition(true)
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

    function toggleFullScreen() { fullScreen.toggle() }
    ReaderWindow { id: fullScreen }

    // The keys come from Settings → Keyboard (see Keymap), as in the comic
    // reader; scrolling keys only while the reader has focus.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) exit()
        else if (Keymap.matches("reader.down", event, keyProfile)) flick.scrollTo(flick.target + 90)
        else if (Keymap.matches("reader.up", event, keyProfile)) flick.scrollTo(flick.target - 90)
        else if (Keymap.matches("reader.pageDown", event, keyProfile)) flick.scrollTo(flick.contentY + flick.height * 0.85)
        else if (Keymap.matches("reader.pageUp", event, keyProfile)) flick.scrollTo(flick.contentY - flick.height * 0.85)
        else if (event.key === Qt.Key_Right) { if (hasNext) load(index + 1) }
        else if (event.key === Qt.Key_Left) { if (hasPrev) load(index - 1) }
        else return
        event.accepted = true
    }
    Shortcut { sequences: Keymap.sequences("reader.next", reader.keyProfile); enabled: reader.keysHere; onActivated: if (reader.hasNext) reader.load(reader.index + 1) }
    Shortcut { sequences: Keymap.sequences("reader.prev", reader.keyProfile); enabled: reader.keysHere; onActivated: if (reader.hasPrev) reader.load(reader.index - 1) }
    Shortcut { sequences: Keymap.sequences("reader.first", reader.keyProfile); enabled: reader.keysHere; onActivated: flick.positionViewAtBeginning() }
    Shortcut { sequences: Keymap.sequences("reader.last", reader.keyProfile); enabled: reader.keysHere; onActivated: flick.positionViewAtEnd() }
    Shortcut { sequences: Keymap.sequences("reader.fullscreen", reader.keyProfile); enabled: reader.keysHere; onActivated: reader.toggleFullScreen() }
    Shortcut { sequences: Keymap.sequences("reader.wider", reader.keyProfile); enabled: reader.keysHere; onActivated: App.setSetting("novelSize", Math.min(34, reader.fontSize + 1)) }
    Shortcut { sequences: Keymap.sequences("reader.narrower", reader.keyProfile); enabled: reader.keysHere; onActivated: App.setSetting("novelSize", Math.max(13, reader.fontSize - 1)) }

    // The chapter as a list of its paragraphs, so only those near the
    // screen exist: a chapter of thousands of paragraphs opens and scrolls
    // as fast as a short one. Positions are by paragraph (block).
    ListView {
        id: flick
        anchors.fill: parent
        model: reader.blocks
        clip: true
        spacing: reader.fontSize * reader.paragraphSpacing
        cacheBuffer: Math.max(1200, height * 2)
        boundsBehavior: Flickable.StopAtBounds
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        reuseItems: true
        ScrollBar.vertical: ScScrollBar {}
        // Where an eased scroll is heading (contentY when none is running).
        readonly property real target: wheel.ease.running ? wheel.target : contentY
        // How far through the chapter, from 0 to 1 (contentY starts at
        // originY, which moves as paragraph heights become known).
        readonly property real progress: {
            const max = contentHeight - height
            return max > 0 ? Math.min(1, Math.max(0, (contentY - originY) / max)) : 1
        }
        function scrollTo(y, animated) { wheel.scrollTo(y, animated) }
        // Paragraph `i` at `offset` pixels below the top of the screen.
        function showBlock(i, offset, animated) {
            const item = itemAtIndex(i)
            if (!item) {
                positionViewAtIndex(i, ListView.Beginning)
                contentY = Math.max(originY, contentY - offset)
                return
            }
            scrollTo(item.y - offset, animated)
        }
        WheelScroll { id: wheel; wheelStep: 110 }

        // Phone: a tap toggles the bar. On the list, which takes touches
        // before anything behind it. E-ink: the sides turn a page at once
        // (no scrolling to redraw), the middle toggles the bar.
        TapHandler {
            enabled: App.platform === "android" || Theme.eink
            onSingleTapped: (point) => {
                const x = point.position.x
                if (Theme.eink && x < flick.width / 3) flick.scrollTo(flick.contentY - flick.height * 0.92, false)
                else if (Theme.eink && x > flick.width * 2 / 3) flick.scrollTo(flick.contentY + flick.height * 0.92, false)
                else reader.barsShown = !reader.barsShown
            }
        }

        onContentYChanged: {
            const delta = contentY - reader.lastY
            if (Math.abs(delta) > 6) {
                reader.barsShown = delta < 0 || contentY - originY < 60
                reader.lastY = contentY
            }
            reader.savePosition(false)
            if (progress > 0.6 && reader.hasNext && reader.nextFor !== reader.index + 1) {
                reader.nextFor = reader.index + 1
                reader.nextTask = Novels.chapter(reader.chapters[reader.index + 1].url, reader.series.id)
            }
            if (atYEnd && reader.blocks.length > 0) {
                reader.finished = true
                reader.savePosition(true)
            }
        }

        header: Column {
            id: body
            x: (flick.width - width) / 2
            width: reader.columnWidth
            topPadding: 110 + (topBar.twoRows ? 46 : 0) + reader.insetTop
            bottomPadding: flick.spacing
            spacing: reader.fontSize * reader.paragraphSpacing

            opacity: 1
            transform: Translate { id: bodyShift; y: 0 }
            Connections {
                target: reader
                function onReveal() { contentReveal.restart() }
            }
            ParallelAnimation {
                id: contentReveal
                NumberAnimation { target: body; property: "opacity"; from: 0; to: 1; duration: Motion.slow; easing.type: Easing.OutCubic }
                NumberAnimation { target: bodyShift; property: "y"; from: 18; to: 0; duration: Motion.page; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
            }

            Text {
                width: parent.width
                text: reader.series.title || ""
                color: reader.look.muted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.3
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: (reader.task && reader.task.ok && reader.task.result.title) ? reader.task.result.title : (reader.unit.title || "")
                color: reader.look.text
                font.family: Theme.fontDisplay
                font.pixelSize: Math.round(reader.fontSize * 1.55)
                font.weight: Font.Bold
                wrapMode: Text.Wrap
                bottomPadding: reader.fontSize * 0.6
            }
        }

        delegate: Item {
            required property var modelData
            required property int index
            // ListView puts every delegate at x 0; the column is centred inside.
            width: flick.width
            height: block.height
            Item {
                id: block
                x: (parent.width - width) / 2
                width: reader.columnWidth
                height: modelData.kind === "break" ? reader.fontSize * 1.6 : text.implicitHeight
                // The paragraph being read aloud, and its sentence.
                Rectangle {
                    visible: reader.spokenBlock === index
                    anchors.fill: parent
                    anchors.margins: -Math.round(reader.fontSize * 0.35)
                    radius: Theme.radiusSm
                    color: Theme.alpha(String(reader.look.text).slice(0, 7), 0.08)
                }

                Text {
                    id: text
                    visible: modelData.kind !== "break"
                    width: parent.width
                    leftPadding: modelData.kind === "quote" ? 22 : 0
                    text: (reader.indent && modelData.kind === "paragraph" ? "\u2003" : "")
                        + (reader.translated.length > index && reader.translated[index] ? reader.plainHtml(reader.translated[index])
                           : reader.spokenBlock === index && App.settings.ttsHighlight !== false
                           ? reader.markSentence(modelData.html, ReadAloud.sentenceStart, ReadAloud.sentenceLength) : modelData.html)
                    textFormat: Text.StyledText
                    color: reader.look.text
                    font.family: modelData.kind === "heading" ? Theme.fontDisplay : reader.fontFamily
                    font.pixelSize: modelData.kind === "heading" ? Math.round(reader.fontSize * 1.2) : reader.fontSize
                    font.weight: modelData.kind === "heading" ? Font.DemiBold : reader.fontWeight
                    font.italic: modelData.kind === "quote"
                    lineHeight: reader.lineHeight
                    wrapMode: Text.Wrap
                    horizontalAlignment: App.settings.novelJustify === true ? Text.AlignJustify : Text.AlignLeft
                    Rectangle {
                        visible: modelData.kind === "quote"
                        width: 3
                        height: parent.height
                        radius: 2
                        color: reader.look.muted
                        opacity: 0.5
                    }
                }
                Text {
                    visible: modelData.kind === "break"
                    anchors.centerIn: parent
                    text: "⁂"
                    color: reader.look.muted
                    font.pixelSize: reader.fontSize
                }
            }
        }

        footer: Item {
            width: flick.width
            height: buttons.height + 40 + 120 + reader.insetBottom
            Row {
                id: buttons
                visible: reader.blocks.length > 0
                y: 40
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 12
                ScButton {
                    text: qsTr("Previous")
                    iconName: "chevron-left"
                    enabled: reader.hasPrev
                    onClicked: reader.load(reader.index - 1)
                }
                ScButton {
                    variant: "primary"
                    text: reader.hasNext ? qsTr("Next chapter") : qsTr("Finished")
                    iconName: reader.hasNext ? "chevron-right" : "check"
                    onClicked: reader.hasNext ? reader.load(reader.index + 1) : reader.exit()
                }
            }
        }
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 260
        spacing: 14
        visible: reader.task !== null && (reader.task.loading || !reader.task.ok)
        Spinner {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: reader.task !== null && reader.task.loading
            color: reader.look.muted
            size: 28
        }
        Text {
            width: Math.min(420, reader.width - 48)
            visible: reader.task !== null && !reader.task.loading && !reader.task.ok
            text: reader.task ? reader.task.error : ""
            color: reader.look.text
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
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

    // Desktop: the bar returns when the mouse moves near the top.
    PointerMotion {
        onMoved: (position) => { if (position.y < 90 + (topBar.twoRows ? 46 : 0) + reader.insetTop) reader.barsShown = true }
    }

    // Dimmer than the screen allows, for reading in the dark.
    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: Math.min(0.8, Number(App.settings.novelDim) || 0)
        visible: opacity > 0
    }

    // While reading aloud: back a sentence, play or pause, on a sentence,
    // the sleep timer and stop.
    Rectangle {
        z: 2
        visible: reader.readingHere
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24 + reader.insetBottom
        width: speechRow.width + 16
        height: 52
        radius: Theme.pill(height)
        color: Theme.alpha(String(reader.look.chrome).slice(0, 7), 0.96)
        border.width: 1
        border.color: Theme.alpha(String(reader.look.text).slice(0, 7), 0.12)
        Row {
            id: speechRow
            anchors.centerIn: parent
            spacing: 4
            IconButton {
                iconName: "skip-back"
                iconColor: reader.look.text
                tip: qsTr("Previous sentence")
                onClicked: ReadAloud.skip(-1)
            }
            IconButton {
                iconName: ReadAloud.playing ? "pause" : "play"
                iconColor: reader.look.text
                tip: ReadAloud.playing ? qsTr("Pause") : qsTr("Go on reading aloud")
                onClicked: ReadAloud.toggle()
            }
            IconButton {
                iconName: "skip-forward"
                iconColor: reader.look.text
                tip: qsTr("Next sentence")
                onClicked: ReadAloud.skip(1)
            }
            IconButton {
                id: sleepButton
                iconName: "moon"
                iconColor: ReadAloud.sleepMode !== "off" ? Theme.accent : reader.look.text
                tip: ReadAloud.sleepMode === "minutes" ? qsTr("Sleep timer: stops at %1").arg(Qt.formatTime(new Date(ReadAloud.sleepEndsAt), "hh:mm"))
                    : ReadAloud.sleepMode === "chapter" ? qsTr("Sleep timer: stops at the end of the chapter") : qsTr("Sleep timer")
                onClicked: sleepMenu.openAt(sleepButton, 0, -sleepMenu.implicitHeight - 8, [
                    { text: qsTr("Off"), checked: ReadAloud.sleepMode === "off", run: () => ReadAloud.setSleep(0) },
                    { text: qsTr("In 15 minutes"), run: () => ReadAloud.setSleep(15) },
                    { text: qsTr("In 30 minutes"), run: () => ReadAloud.setSleep(30) },
                    { text: qsTr("In 45 minutes"), run: () => ReadAloud.setSleep(45) },
                    { text: qsTr("In an hour"), run: () => ReadAloud.setSleep(60) },
                    { text: qsTr("At the end of the chapter"), checked: ReadAloud.sleepMode === "chapter", run: () => ReadAloud.setSleep(-1) }
                ])
            }
            IconButton {
                iconName: "x"
                iconColor: reader.look.text
                tip: qsTr("Stop reading aloud")
                onClicked: reader.stopSpeaking()
            }
        }
    }
    ActionMenu { id: sleepMenu }

    // The status bar area keeps the reading theme's colour (the bar's while
    // it is shown). It is drawn over the top bar, which slides away under
    // it instead of across the phone's clock and icons.
    Rectangle {
        z: 3
        width: parent.width
        height: reader.insetTop
        visible: height > 0 && !reader.systemBarsHidden
        color: reader.barsShown ? Theme.alpha(reader.look.chrome, 0.94) : reader.look.bg
        Behavior on color { ColorAnimation { duration: Motion.slow } }
    }

    // Reading progress along the top edge.
    Rectangle {
        z: 2
        y: reader.insetTop
        height: 2
        width: parent.width * flick.progress
        color: Theme.accent
        opacity: 0.8
    }

    Rectangle {
        id: topBar
        z: 1
        width: parent.width
        // Phones: the buttons leave the title no room, so it gets a row of
        // its own under them (as in the comic reader).
        readonly property bool twoRows: reader.width - novelActions.width - 110 < 140
        readonly property real rowY: reader.insetTop + 32
        height: 64 + (twoRows ? 46 : 0) + reader.insetTop
        y: reader.barsShown ? 0 : -height
        color: Theme.alpha(reader.look.chrome, 0.94)
        Behavior on y { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.alpha(reader.look.text, 0.08) }

        IconButton {
            id: closeButton
            x: 16
            y: topBar.rowY - height / 2
            iconName: "arrow-left"
            iconColor: reader.look.text
            tip: qsTr("Close (Esc)")
            onClicked: reader.exit()
        }
        Column {
            x: topBar.twoRows ? 24 : closeButton.x + closeButton.width + 12
            y: topBar.twoRows ? reader.insetTop + 60 : topBar.rowY - height / 2
            width: topBar.twoRows ? topBar.width - 48 : Math.max(0, reader.width - 90 - novelActions.width)
            Text {
                text: reader.series.title || ""
                color: reader.look.text
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
                width: Math.min(implicitWidth, parent.width)
                elide: Text.ElideRight
            }
            Text {
                text: (reader.index + 1) + qsTr(" of ") + reader.chapters.length + " · " + (reader.unit.title || "")
                    + (reader.task && reader.task.ok && reader.task.result.offline ? qsTr(" · Downloaded") : "")
                    + (App.settings.incognito === true ? qsTr(" · Incognito") : "")
                color: reader.look.muted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                width: Math.min(implicitWidth, parent.width)
                elide: Text.ElideRight
            }
        }
        Row {
            id: novelActions
            anchors.right: parent.right
            anchors.rightMargin: 16
            y: topBar.rowY - height / 2
            // Phones: smaller buttons, no gaps, so all fit beside the close button.
            readonly property int button: reader.width < 420 ? 32 : 34
            spacing: reader.width < 420 ? 0 : 4
            IconButton { diameter: novelActions.button; iconName: "chevron-left"; iconColor: reader.look.text; enabled: reader.hasPrev; tip: qsTr("Previous chapter") + reader.keyHint("reader.prev"); onClicked: reader.load(reader.index - 1) }
            IconButton { diameter: novelActions.button; iconName: "chevron-right"; iconColor: reader.look.text; enabled: reader.hasNext; tip: qsTr("Next chapter") + reader.keyHint("reader.next"); onClicked: reader.load(reader.index + 1) }
            IconButton { diameter: novelActions.button; iconName: "list"; iconColor: reader.look.text; tip: qsTr("Chapters"); onClicked: chapterPopup.open() }
            // Computers: on in a window of its own (where it left off).
            IconButton {
                diameter: novelActions.button
                visible: Router.windows && !(Window.window && Window.window.isPageWindow === true)
                iconName: "columns-2"
                iconColor: reader.look.text
                tip: qsTr("Open in a new window")
                onClicked: {
                    reader.savePosition(true)
                    Router.openWindow("pages/NovelReader.qml", { series: reader.series, chapters: reader.chapters, index: reader.index })
                    Router.leave(reader)
                }
            }
            // Wide windows: another comic or novel on the right half.
            IconButton {
                diameter: novelActions.button
                visible: !!Router.shell && Router.shell.canSplit && Router.shell.splitItem !== reader
                iconName: "panel-left"
                iconColor: reader.look.text
                tip: qsTr("Read another beside this")
                onClicked: splitPicker.start()
            }
            IconButton {
                diameter: novelActions.button
                visible: !String(reader.series.sourceUrl || "").startsWith("file:")
                iconName: "message-circle"
                iconColor: reader.look.text
                tip: qsTr("Comments")
                onClicked: comments.open()
            }
            ContinueOn {
                diameter: novelActions.button
                seriesId: reader.series.id || ""
                iconColor: reader.look.text
                save: () => reader.savePosition(true)
            }
            IconButton {
                diameter: novelActions.button
                iconName: reader.speaking ? "pause" : "volume-2"
                iconColor: reader.readingHere ? Theme.accent : reader.look.text
                enabled: Speech.available && reader.blocks.length > 0
                tip: !Speech.available ? qsTr("No voices are installed on this device")
                    : reader.speaking ? qsTr("Pause reading aloud") : reader.readingHere ? qsTr("Go on reading aloud") : qsTr("Read aloud from here")
                onClicked: reader.readingHere ? ReadAloud.toggle()
                    : reader.startSpeaking(flick.contentY - flick.originY < 40 ? -1 : reader.firstVisibleBlock())
            }
            IconButton {
                diameter: novelActions.button
                visible: Translator.textReady
                iconName: "languages"
                iconColor: reader.translating ? Theme.accent : reader.look.text
                enabled: reader.blocks.length > 0 && !(reader.translateTask && reader.translateTask.loading)
                tip: reader.translateTask && reader.translateTask.loading ? qsTr("Translating…")
                    : reader.translating ? qsTr("Show the original") : qsTr("Translate the chapter")
                onClicked: {
                    reader.translating = !reader.translating
                    // This series is read translated (or not) from now on.
                    if (reader.series.id) Library.setSeriesSetting(reader.series.id, "translate", reader.translating)
                }
            }
            IconButton {
                diameter: novelActions.button
                iconName: "move-vertical"
                iconColor: reader.autoScrolling ? Theme.accent : reader.look.text
                enabled: reader.blocks.length > 0 && !reader.speaking
                tip: reader.autoScrolling ? qsTr("Stop scrolling") : qsTr("Scroll by itself")
                onClicked: reader.autoScrolling = !reader.autoScrolling
            }
            IconButton { diameter: novelActions.button; iconName: "type"; iconColor: reader.look.text; tip: qsTr("Text settings"); onClicked: typePopup.open() }
            IconButton {
                diameter: novelActions.button
                iconName: fullScreen.fullScreen ? "minimize" : "maximize"
                iconColor: reader.look.text
                tip: qsTr("Full screen") + reader.keyHint("reader.fullscreen")
                onClicked: reader.toggleFullScreen()
            }
        }
    }

    CommentsPanel {
        id: comments
        media: "novel"
        title: reader.series.title || ""
        url: reader.series.sourceUrl || ""
        // The same chapter on any source is one discussion.
        unit: App.chapterNumber(String(reader.unit.number || ""), reader.unit.title || "")
        label: qsTr("Chapter ") + (unit || reader.unit.number || "")
    }

    Popup {
        id: chapterPopup
        x: parent.width - width - 16
        y: 70 + (topBar.twoRows ? 46 : 0) + reader.insetTop
        width: Math.min(360, parent.width - 32)
        height: Math.min(parent.height - 120, 600)
        padding: 6
        onOpened: chapterList.positionViewAtIndex(reader.index, ListView.Center)
        background: Rectangle { radius: Theme.radiusLg; color: Theme.surface; border.width: 1; border.color: Theme.borderStrong }
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
                background: Rectangle { radius: Theme.radiusSm; color: index === reader.index ? Theme.surface3 : parent.hovered ? Theme.surface2 : "transparent" }
                contentItem: Text {
                    text: modelData.title
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    font.weight: index === reader.index ? Font.DemiBold : Font.Normal
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    Popup {
        id: typePopup
        // Phones: a sheet from the bottom edge that scrolls; computers: a
        // panel under the top bar.
        readonly property bool sheet: Theme.compact
        readonly property real restY: sheet ? parent.height - height : 70 + (topBar.twoRows ? 46 : 0) + reader.insetTop
        x: sheet ? 0 : parent.width - width - 16
        y: restY
        width: sheet ? parent.width : Math.min(340, parent.width - 32)
        padding: 20
        bottomPadding: sheet ? 20 + reader.insetBottom : 20
        background: GlassPanel {
            radius: Theme.radiusLg
            fill: Theme.surface
            border.width: 1
            border.color: Theme.borderStrong
            squareBottom: typePopup.sheet
        }
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.fast }
                NumberAnimation { property: "y"; from: typePopup.restY + (typePopup.sheet ? 60 : -12); to: typePopup.restY; duration: Motion.normal; easing.type: Easing.OutCubic }
            }
        }
        contentItem: Flickable {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            implicitHeight: Math.min(options.implicitHeight, typePopup.sheet ? reader.height * 0.62 : reader.height - 140 - (topBar.twoRows ? 46 : 0) - reader.insetTop)
            contentHeight: options.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            WheelScroll {}
            ScrollBar.vertical: ScScrollBar {}
            ReaderOptions {
                id: options
                width: parent.width
                reader: reader
            }
        }
    }

    ColorPicker {
        id: bgPicker
        title: qsTr("Page colour")
        onChanged: (c) => App.setSetting("novelCustomBg", String(c).slice(0, 7))
    }
    ColorPicker {
        id: textPicker
        title: qsTr("Text colour")
        onChanged: (c) => App.setSetting("novelCustomText", String(c).slice(0, 7))
    }

    // Everything in the text panel, as a column of labelled controls.
    component OptionLabel: Text {
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textXs
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1
    }
    component OptionSwitch: Row {
        property alias checked: toggle.checked
        property alias text: label.text
        signal toggled(bool checked)
        spacing: 10
        Toggle { id: toggle; onToggled: parent.toggled(checked) }
        Text { id: label; anchors.verticalCenter: parent.verticalCenter; color: Theme.text; font.family: Theme.fontSans; font.pixelSize: Theme.textSm }
    }

    component ReaderOptions: Column {
        id: opts
        property Item reader
        spacing: 12

        OptionLabel { text: qsTr("Theme") }
        Flow {
            width: parent.width
            spacing: 10
            Repeater {
                model: ["app", "paper", "sepia", "dark", "black", "custom"]
                Rectangle {
                    required property string modelData
                    readonly property var colors: Theme.readingLook(modelData)
                    width: 44
                    height: 44
                    radius: 22
                    color: colors.bg
                    border.width: (App.settings.novelTheme || "app") === modelData ? 2 : 1
                    border.color: (App.settings.novelTheme || "app") === modelData ? Theme.accent : Theme.borderStrong
                    Text { anchors.centerIn: parent; text: "Aa"; color: parent.colors.text; font.family: Theme.fontSerif; font.pixelSize: 15 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: App.setSetting("novelTheme", parent.modelData) }
                }
            }
        }
        Row {
            visible: App.settings.novelTheme === "custom"
            spacing: 8
            ScButton { compact: true; text: qsTr("Page colour"); onClicked: { bgPicker.edit(Theme.readingLook("custom").bg); bgPicker.open() } }
            ScButton { compact: true; text: qsTr("Text colour"); onClicked: { textPicker.edit(Theme.readingLook("custom").text); textPicker.open() } }
        }

        OptionLabel { text: qsTr("Font") }
        Select {
            width: parent.width
            // The app's own three first, then every font on this device.
            model: [{ value: "Literata", label: qsTr("Literata (serif)") }, { value: "DM Sans", label: qsTr("DM Sans (sans)") },
                    { value: "Space Grotesk", label: "Space Grotesk" }]
                .concat(Qt.fontFamilies().filter(f => ["Literata", "DM Sans", "Space Grotesk"].indexOf(f) < 0 && !f.startsWith("."))
                .map(f => ({ value: f, label: f })))
            value: opts.reader.fontFamily
            onPicked: (v) => App.setSetting("novelFont", v)
        }
        Segmented {
            options: [{ value: 300, label: qsTr("Light") }, { value: 400, label: qsTr("Regular") }, { value: 500, label: qsTr("Medium") }, { value: 600, label: qsTr("Bold") }]
            value: opts.reader.fontWeight
            onPicked: (v) => App.setSetting("novelWeight", v)
        }

        OptionLabel { text: qsTr("Size · %1").arg(opts.reader.fontSize) }
        ScSlider { width: parent.width; from: 13; to: 40; stepSize: 1; value: opts.reader.fontSize; onMoved: App.setSetting("novelSize", value) }
        OptionLabel { text: qsTr("Line spacing · %1").arg(opts.reader.lineHeight.toFixed(2)) }
        ScSlider { width: parent.width; from: 1.1; to: 2.6; stepSize: 0.05; value: opts.reader.lineHeight; onMoved: App.setSetting("novelLineHeight", Math.round(value * 100) / 100) }
        OptionLabel { text: qsTr("Paragraph spacing · %1").arg(opts.reader.paragraphSpacing.toFixed(1)) }
        ScSlider { width: parent.width; from: 0; to: 2.5; stepSize: 0.1; value: opts.reader.paragraphSpacing; onMoved: App.setSetting("novelParagraphSpacing", Math.round(value * 10) / 10) }
        OptionLabel { text: qsTr("Width · %1").arg(opts.reader.columnWidth) }
        ScSlider { width: parent.width; from: 400; to: 1400; stepSize: 10; value: App.settings.novelWidth || 720; onMoved: App.setSetting("novelWidth", value) }
        OptionLabel { text: qsTr("Side margins · %1").arg(opts.reader.margin) }
        ScSlider { width: parent.width; from: 8; to: 120; stepSize: 2; value: opts.reader.margin; onMoved: App.setSetting("novelMargin", value) }
        OptionSwitch { text: qsTr("Justify text"); checked: App.settings.novelJustify === true; onToggled: (c) => App.setSetting("novelJustify", c) }
        OptionSwitch { text: qsTr("Indent paragraphs"); checked: opts.reader.indent; onToggled: (c) => App.setSetting("novelIndent", c) }
        OptionLabel { text: qsTr("Dim the page · %1%").arg(Math.round((Number(App.settings.novelDim) || 0) * 100)) }
        ScSlider { width: parent.width; from: 0; to: 0.8; stepSize: 0.05; value: Number(App.settings.novelDim) || 0; onMoved: App.setSetting("novelDim", Math.round(value * 100) / 100) }
        OptionLabel { text: qsTr("Auto-scroll speed · %1").arg(Number(App.settings.novelAutoScrollSpeed) || 40) }
        ScSlider { width: parent.width; from: 10; to: 200; stepSize: 5; value: Number(App.settings.novelAutoScrollSpeed) || 40; onMoved: App.setSetting("novelAutoScrollSpeed", value) }

        // Key profiles (Settings → Keyboard), once there are any.
        OptionLabel { visible: keysSelect.visible; text: qsTr("Keys for this series") }
        Select {
            id: keysSelect
            visible: !!opts.reader.series.id && Keymap.profileNames.length > 0
            width: parent.width
            Accessible.name: qsTr("Keys for this series")
            model: [{ value: "", label: qsTr("Everyday keys") }].concat(Keymap.profileNames.map(n => ({ value: n, label: n })))
            value: opts.reader.keyProfile
            onPicked: (v) => opts.reader.setKeyProfile(v)
        }

        OptionLabel { text: qsTr("Read aloud") }
        Text {
            visible: !Speech.available
            width: parent.width
            text: qsTr("No voices are installed on this device. Add one in the system's text-to-speech settings.")
            color: Theme.textMuted
            wrapMode: Text.Wrap
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
        }
        Select {
            visible: Speech.available && Speech.voices.length > 0
            width: parent.width
            model: [{ value: "", label: qsTr("System voice") }].concat(Speech.voices.map(v => ({ value: v.id, label: v.name + " · " + v.language })))
            value: App.settings.ttsVoice || ""
            onPicked: (v) => { App.setSetting("ttsVoice", v); opts.reader.restartVoice() }
        }
        OptionLabel { visible: Speech.available; text: qsTr("Voice speed · %1×").arg(Number(App.settings.ttsRate || 1).toFixed(2)) }
        ScSlider {
            visible: Speech.available
            width: parent.width; from: 0.5; to: 3; stepSize: 0.05; value: Number(App.settings.ttsRate) || 1
            onMoved: App.setSetting("ttsRate", Math.round(value * 100) / 100)
            onPressedChanged: if (!pressed) opts.reader.restartVoice()
        }
        OptionLabel { visible: Speech.available; text: qsTr("Voice pitch · %1×").arg(Number(App.settings.ttsPitch || 1).toFixed(2)) }
        ScSlider {
            visible: Speech.available
            width: parent.width; from: 0.5; to: 2; stepSize: 0.05; value: Number(App.settings.ttsPitch) || 1
            onMoved: App.setSetting("ttsPitch", Math.round(value * 100) / 100)
            onPressedChanged: if (!pressed) opts.reader.restartVoice()
        }
        OptionSwitch { visible: Speech.available; text: qsTr("Go on to the next chapter"); checked: App.settings.ttsContinue !== false; onToggled: (c) => App.setSetting("ttsContinue", c) }
        OptionSwitch { visible: Speech.available; text: qsTr("Keep the spoken paragraph on screen"); checked: App.settings.ttsFollow !== false; onToggled: (c) => App.setSetting("ttsFollow", c) }
        OptionSwitch { visible: Speech.available; text: qsTr("Highlight the spoken sentence"); checked: App.settings.ttsHighlight !== false; onToggled: (c) => App.setSetting("ttsHighlight", c) }
        OptionLabel { visible: Speech.available; text: qsTr("Pause between paragraphs · %1 s").arg(((Number(App.settings.ttsParagraphPause) || (App.settings.ttsParagraphPause === 0 ? 0 : 350)) / 1000).toFixed(1)) }
        ScSlider {
            visible: Speech.available
            width: parent.width; from: 0; to: 2000; stepSize: 50
            value: App.settings.ttsParagraphPause === undefined ? 350 : Number(App.settings.ttsParagraphPause)
            onMoved: App.setSetting("ttsParagraphPause", Math.round(value))
            onPressedChanged: if (!pressed) opts.reader.restartVoice()
        }
        OptionLabel { visible: Speech.available; text: qsTr("Pause between sentences · %1 s").arg(((Number(App.settings.ttsSentencePause) || 0) / 1000).toFixed(1)) }
        ScSlider {
            visible: Speech.available
            width: parent.width; from: 0; to: 1500; stepSize: 50
            value: Number(App.settings.ttsSentencePause) || 0
            onMoved: App.setSetting("ttsSentencePause", Math.round(value))
            onPressedChanged: if (!pressed) opts.reader.restartVoice()
        }
    }

    SplitPicker { id: splitPicker }
}
