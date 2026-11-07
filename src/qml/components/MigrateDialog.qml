import QtQuick
import QtQuick.Controls.Basic
import Scancore

// "Move to another source": finds each series on the other sources (under
// every name its catalogue knows), lets the reader pick where it goes, and
// moves it there with its progress, library list and tracking. One series
// shows every match and a preview; several get the best match each, which
// the reader can change, and move one after another.
Dialog {
    id: dialog
    // [{id, media, title, cover, source, sourceUrl, extra}]
    property var entries: []
    // Per entry: {state: "finding"|"found"|"none"|"moving"|"moved"|"failed",
    // matches, pick, error, read}
    property var rows: []
    property int finding: 0
    property bool moving: false
    property bool progress: true
    property bool keepOld: false
    // One series: the match picked, its chapters once loaded.
    property var preview: null
    property var previewTask: null

    readonly property bool batch: entries.length > 1
    readonly property int movable: rows.filter(r => r.state === "found").length
    readonly property int moved: rows.filter(r => r.state === "moved").length
    readonly property bool finished: !moving && moved > 0 && rows.every(r => r.state !== "found" && r.state !== "moving")

    signal migrated(var result)

    // Tasks belong to JavaScript: one only held by a local would be
    // collected before it answers.
    property var pending: []
    function hold(task) {
        pending = pending.concat([task])
        const release = () => { pending = pending.filter(t => t !== task) }
        task.finished.connect(release)
        task.failed.connect(release)
        return task
    }

    function start(list) {
        entries = list
        rows = list.map(() => ({ state: "finding", matches: [], pick: 0 }))
        preview = null
        previewTask = null
        moving = false
        progress = true
        keepOld = false
        open()
        finding = 0
        // A few at a time: each search asks every source.
        for (let i = 0; i < Math.min(3, list.length); ++i) findNext()
    }

    function setRow(i, changes) {
        const copy = rows.slice()
        copy[i] = Object.assign({}, copy[i], changes)
        rows = copy
    }

    function itemFor(entry) {
        const extra = entry.extra || {}
        const titles = (extra.titles || []).slice()
        if (extra.catalogTitle) titles.unshift(extra.catalogTitle)
        titles.unshift(entry.title)
        const ids = {}
        if (extra.anilist) ids.anilist = extra.anilist
        if (extra.mal) ids.mal = extra.mal
        return { media: entry.media, title: entry.title, titles: titles, cover: entry.cover || "",
                 catalog: extra.catalog || "", catalogId: extra.catalogId || "", hid: extra.hid || "", ids: ids }
    }

    // Built-in sources answer first and their matches show straight away;
    // installed extensions (slower, through the extension host) are asked
    // at the same time and their matches added when they come.
    function findNext() {
        const i = finding
        if (i >= entries.length) return
        finding = i + 1
        const entry = entries[i]
        const novel = entry.media === "novel"
        const item = itemFor(entry)
        const searches = [hold(novel ? Novels.resolve(item) : Comics.resolve(item))]
        if (novel ? Novels.hasExtensions() : Comics.hasExtensions())
            searches.push(hold(novel ? Novels.resolve(item, "extensions") : Comics.resolve(item, "", "extensions")))
        let left = searches.length
        let error = ""
        let answered = false
        const settle = () => {
            if (--left > 0) return
            if (rows[i].state === "finding") setRow(i, { state: "none", error: answered ? "" : error })
            findNext()
        }
        for (const task of searches) {
            task.finished.connect((result) => {
                if (!dialog.visible) return
                answered = true
                const found = (result.matches || []).filter(m => m.source !== entry.source)
                if (found.length) {
                    const row = rows[i]
                    // A move already under way keeps the list it chose from.
                    if (row.state === "finding" || row.state === "found")
                        setRow(i, { state: "found", matches: (row.matches || []).concat(found) })
                }
                settle()
            })
            task.failed.connect((message) => {
                error = message
                settle()
            })
        }
    }

    // How many chapters will count as read: everything up to the furthest
    // one read on the old source, by number (as the move does).
    function furthestRead(entry) {
        const progress = Library.progressMap(entry.id)
        let furthest = -1
        for (const unit of Library.storedUnits(entry.id)) {
            const p = progress[unit.key]
            const n = parseFloat(unit.number)
            if (p && p.completed && !isNaN(n)) furthest = Math.max(furthest, n)
        }
        return furthest
    }

    function choose(match) {
        const entry = entries[0]
        preview = match
        previewTask = entry.media === "novel" ? Novels.book(match.url, { title: match.title, cover: match.cover })
                                              : Comics.series(match.url, { title: match.title, cover: match.cover })
    }

    function load(entry, match) {
        return entry.media === "novel" ? Novels.book(match.url, { title: match.title, cover: match.cover || entry.cover })
                                       : Comics.series(match.url, { title: match.title, cover: match.cover || entry.cover })
    }

    function migrateOne(i, done) {
        const entry = entries[i]
        const match = rows[i].matches[rows[i].pick]
        setRow(i, { state: "moving" })
        const fail = (error) => { setRow(i, { state: "failed", error: error }); done() }
        const loaded = hold(load(entry, match))
        loaded.failed.connect(fail)
        loaded.finished.connect((result) => {
            if (!result.chapters || result.chapters.length === 0) { fail(qsTr("The new source lists no chapters for this series.")); return }
            const task = hold(Library.migrate(entry.id, result.series.id, { progress: dialog.progress, keepOld: dialog.keepOld }))
            task.failed.connect(fail)
            task.finished.connect((moved) => {
                setRow(i, { state: "moved", read: moved.read })
                if (!dialog.batch) dialog.migrated(Object.assign({ series: result.series }, moved))
                done()
            })
        })
    }

    function moveAll() {
        moving = true
        let i = 0
        const next = () => {
            while (i < rows.length && rows[i].state !== "found") ++i
            if (i >= rows.length) {
                moving = false
                Router.toast(moved === 1 ? qsTr("Moved 1 series") : qsTr("Moved %1 series").arg(moved), "success")
                if (!batch) close()
                return
            }
            migrateOne(i++, next)
        }
        next()
    }

    function moveSingle() {
        const copy = rows.slice()
        copy[0] = Object.assign({}, copy[0], { state: "found", matches: [preview], pick: 0 })
        rows = copy
        moveAll()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(batch ? 560 : 480, (parent ? parent.width : 480) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 48)
    modal: true
    focus: true
    padding: 24
    closePolicy: moving ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: Theme.overlay }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast }
            NumberAnimation { property: "scale"; to: 0.97; duration: Motion.fast }
        }
    }

    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    contentItem: Flickable {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        implicitHeight: body.implicitHeight
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScScrollBar {}

        Column {
            id: body
            width: parent.width
            spacing: 16

            Column {
                width: parent.width
                spacing: 4
                Text {
                    width: parent.width
                    text: dialog.batch ? qsTr("Move %1 series to other sources").arg(dialog.entries.length)
                                       : qsTr("Move to another source")
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    text: dialog.batch
                        ? qsTr("Each one goes to the best match on your other sources. Change any of them before moving.")
                        : qsTr("Your progress, library list and tracking go with it.")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                }
            }

            // One series: its matches, then the one picked.
            Row {
                visible: !dialog.batch && dialog.entries.length === 1
                width: parent.width
                spacing: 14
                Cover {
                    width: 54
                    height: 78
                    radius: 8
                    source: dialog.preview ? (dialog.preview.cover || (dialog.entries[0] || {}).cover || "") : ((dialog.entries[0] || {}).cover || "")
                }
                Column {
                    width: parent.width - 68
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        width: parent.width
                        text: dialog.preview ? dialog.preview.title : ((dialog.entries[0] || {}).title || "")
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        readonly property var row: dialog.rows[0] || { state: "finding" }
                        readonly property var chapters: dialog.previewTask && dialog.previewTask.ok ? dialog.previewTask.result.chapters : []
                        readonly property int willRead: {
                            if (!dialog.preview || dialog.entries.length === 0) return 0
                            const furthest = dialog.furthestRead(dialog.entries[0])
                            return chapters.filter(c => !isNaN(parseFloat(c.number)) && parseFloat(c.number) <= furthest).length
                        }
                        text: dialog.preview
                            ? (!dialog.previewTask || dialog.previewTask.loading ? qsTr("Loading its chapters…")
                               : !dialog.previewTask.ok ? dialog.previewTask.error
                               : dialog.preview.sourceLabel + " · " + (chapters.length === 1 ? qsTr("1 chapter") : qsTr("%1 chapters").arg(chapters.length))
                                 + (chapters.length ? qsTr(", newest %1").arg(chapters[chapters.length - 1].title) : "")
                                 + (dialog.progress && willRead > 0 ? "\n" + qsTr("%1 will count as read.").arg(willRead) : ""))
                            : row.state === "finding" ? qsTr("Looking for it on your other sources…")
                            : row.state === "none" ? (row.error || qsTr("None of your other sources has it."))
                            : qsTr("Move it to")
                        color: dialog.previewTask && dialog.previewTask.done && !dialog.previewTask.ok ? Theme.danger : Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                }
            }

            Spinner {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !dialog.batch && ((dialog.rows[0] || {}).state === "finding" || (!!dialog.previewTask && dialog.previewTask.loading))
            }

            Column {
                visible: !dialog.batch && !dialog.preview
                width: parent.width
                spacing: 6
                Repeater {
                    model: !dialog.batch && dialog.rows.length ? dialog.rows[0].matches : []
                    AbstractButton {
                        id: option
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: 54
                        hoverEnabled: true
                        onClicked: dialog.choose(modelData)
                        background: Rectangle {
                            radius: Theme.radiusMd
                            color: option.hovered ? Theme.surface3 : Theme.surface2
                            border.width: 1
                            border.color: option.index === 0 ? Theme.alpha(Theme.accent, 0.4) : Theme.border
                        }
                        contentItem: Item {
                            Column {
                                x: 14
                                width: parent.width - 52
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    text: option.modelData.sourceLabel + (option.modelData.preferred ? "" : " · " + String(option.modelData.language || "").toUpperCase())
                                    color: Theme.text
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.textMd
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: parent.width
                                    text: option.modelData.title
                                    color: Theme.textMuted
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.textSm
                                    elide: Text.ElideRight
                                }
                            }
                            Icon {
                                anchors.right: parent.right
                                anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron-right"
                                size: 16
                                color: Theme.textFaint
                            }
                        }
                    }
                }
            }

            // Several series: one row each, in a list of its own so only the
            // rows on screen exist (and re-read `rows` when one changes).
            ListView {
                maximumFlickVelocity: Motion.flickVelocity
                flickDeceleration: Motion.flickDeceleration
                visible: dialog.batch
                width: parent.width
                height: Math.min(contentHeight, 70 * 6.5)
                spacing: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                model: dialog.batch ? dialog.rows.length : 0
                WheelScroll {}
                ScrollBar.vertical: ScScrollBar {}
                delegate: Rectangle {
                        id: batchRow
                        required property int index
                        readonly property var entry: dialog.entries[index] || {}
                        readonly property var row: dialog.rows[index] || { state: "finding", matches: [] }
                        width: ListView.view ? ListView.view.width : 300
                        height: 64
                        radius: Theme.radiusMd
                        color: Theme.surface2
                        border.width: 1
                        border.color: Theme.border
                        Cover {
                            id: rowCover
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 48
                            radius: 5
                            source: batchRow.entry.cover || ""
                        }
                        Column {
                            anchors.left: rowCover.right
                            anchors.leftMargin: 12
                            anchors.right: rowEnd.left
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: parent.width
                                text: batchRow.entry.title || ""
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: batchRow.row.state === "finding" ? qsTr("Looking…")
                                    : batchRow.row.state === "none" ? qsTr("Not found on your other sources")
                                    : batchRow.row.state === "moving" ? qsTr("Moving…")
                                    : batchRow.row.state === "moved" ? qsTr("Moved")
                                    : batchRow.row.state === "failed" ? batchRow.row.error
                                    : (batchRow.entry.sourceLabel || batchRow.entry.source || "")
                                color: batchRow.row.state === "failed" ? Theme.danger
                                     : batchRow.row.state === "moved" ? Theme.success : Theme.textMuted
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textXs
                                elide: Text.ElideRight
                            }
                        }
                        Item {
                            id: rowEnd
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(220, batchRow.width * 0.45)
                            height: 36
                            Select {
                                anchors.fill: parent
                                visible: batchRow.row.state === "found"
                                model: batchRow.row.matches.map((m, i) => ({ value: i, label: m.sourceLabel + " · " + m.title }))
                                value: batchRow.row.pick
                                onPicked: (v) => dialog.setRow(batchRow.index, { pick: v })
                            }
                            Spinner {
                                anchors.centerIn: parent
                                visible: batchRow.row.state === "finding" || batchRow.row.state === "moving"
                            }
                            Icon {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                visible: batchRow.row.state === "moved"
                                name: "check"
                                size: 18
                                color: Theme.success
                            }
                        }
                    }
            }

            Column {
                width: parent.width
                spacing: 10
                visible: dialog.batch ? dialog.movable > 0 && !dialog.moving : !!dialog.preview
                SettingRow {
                    width: parent.width
                    title: qsTr("Bring my progress")
                    description: qsTr("Chapters up to the furthest one you read count as read.")
                    Toggle { checked: dialog.progress; onToggled: dialog.progress = checked }
                }
                SettingRow {
                    width: parent.width
                    title: qsTr("Keep the old one too")
                    description: qsTr("Copy instead of move: both stay in your library.")
                    Toggle { checked: dialog.keepOld; onToggled: dialog.keepOld = checked }
                }
            }

            Row {
                anchors.right: parent.right
                spacing: 8
                ScButton {
                    visible: !dialog.batch && !!dialog.preview && !dialog.moving
                    text: qsTr("Back")
                    variant: "ghost"
                    onClicked: { dialog.preview = null; dialog.previewTask = null }
                }
                ScButton {
                    text: dialog.finished || (!dialog.batch && !dialog.preview) ? qsTr("Close") : qsTr("Cancel")
                    enabled: !dialog.moving
                    onClicked: dialog.close()
                }
                ScButton {
                    visible: dialog.batch ? !dialog.finished : !!dialog.preview
                    variant: "primary"
                    iconName: "arrow-right-left"
                    text: dialog.batch ? qsTr("Move %1").arg(dialog.movable) : qsTr("Move")
                    enabled: !dialog.moving && (dialog.batch ? dialog.movable > 0
                                                             : !!dialog.previewTask && dialog.previewTask.ok && dialog.previewTask.result.chapters.length > 0)
                    onClicked: dialog.batch ? dialog.moveAll() : dialog.moveSingle()
                }
            }
        }
    }
}
