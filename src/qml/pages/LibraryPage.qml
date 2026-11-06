import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Saved comics, novels and anime, each on its own: categories (reading,
// planned, on hold, completed, dropped), favourites, three views (grid,
// compact grid, list) and update checks.
Item {
    id: page
    property string filter: ""
    property var task: Library.entries("", true)
    property var checkTask: null
    // Select: pick several series to move, list or remove together.
    property bool selecting: false
    property var selection: ({})
    readonly property int selectedCount: Object.keys(selection).length
    onMediaChanged: { selecting = false; selection = ({}) }

    readonly property string media: {
        const m = App.settings.libraryMedia || "comic"
        return m === "music" && !Router.sectionShown("music") ? "comic" : m
    }
    // A list hidden or removed (on another device too) shows All.
    readonly property string category: {
        const c = App.settings.libraryCategory || "all"
        return c === "all" || c === "favourites" || categories.some(x => x.value === c) ? c : "all"
    }
    readonly property string view: App.settings.libraryView || "grid"
    readonly property string sort: App.settings.librarySort || "updates"
    readonly property var categories: Router.libraryCategories(media)

    // The last result, kept while a refresh runs.
    property var all: []
    Connections {
        target: page.task
        function onFinished() { page.all = Router.keepTask(page.all, page.task) }
    }
    readonly property var inMedia: all.filter(e => e.media === media)
    // How many series each chip holds.
    readonly property var counts: {
        const c = { all: inMedia.length, favourites: 0 }
        for (const e of inMedia) {
            const k = e.category || "reading"
            c[k] = (c[k] || 0) + 1
            if (e.favourite) c.favourites += 1
        }
        return c
    }
    readonly property var entries: {
        const f = filter.trim().toLowerCase()
        const out = inMedia.filter(e => (category === "all" || (category === "favourites" ? !!e.favourite : (e.category || "reading") === category))
                                        && (!f || (e.title || "").toLowerCase().includes(f)))
        // "updates" keeps the stored order: new chapters first, then recent.
        if (sort === "recent") out.sort((a, b) => (b.progressAt || 0) - (a.progressAt || 0))
        else if (sort === "added") out.sort((a, b) => (b.addedAt || 0) - (a.addedAt || 0))
        else if (sort === "title") out.sort((a, b) => String(a.title || "").localeCompare(String(b.title || "")))
        else if (sort === "unread") out.sort((a, b) => (b.unread || 0) - (a.unread || 0) || (b.progressAt || 0) - (a.progressAt || 0))
        return out
    }
    function stepCategory(step) {
        const order = ["all", "favourites"].concat(categories.map(c => c.value))
        const at = order.indexOf(category)
        const next = order[at + step]
        if (next !== undefined) App.setSetting("libraryCategory", next)
    }
    readonly property string categoryLabel: category === "favourites" ? qsTr("Favourites")
        : (categories.find(c => c.value === category) || { label: "" }).label

    readonly property bool checking: checkTask ? checkTask.loading : false
    readonly property var mediaOptions: [{ value: "comic", label: qsTr("Comics"), icon: "book-open" },
        { value: "novel", label: qsTr("Novels"), icon: "book-text" }, { value: "anime", label: qsTr("Anime"), icon: "tv" }]
        .concat(Router.sectionShown("music") ? [{ value: "music", label: qsTr("Music"), icon: "volume-2" }] : [])
    // Songs saved from Music: openings, endings, files and plugins' songs.
    readonly property bool songs: media === "music"
    readonly property var savedSongs: {
        App.dependOn(Music.libraryRevision)
        if (!songs) return []
        const q = filter.toLowerCase()
        const all = Music.library()
        return q ? all.filter(s => (s.title + " " + (s.artists || []).join(" ") + " " + (s.anime || s.album || "")).toLowerCase().includes(q)) : all
    }
    // Rows on screen; more come 50 at a time (a Repeater of hundreds is
    // slow to build and to tear down on phones).
    property int songsShown: 50
    readonly property var sortOptions: [{ value: "updates", label: qsTr("New chapters first") },
        { value: "recent", label: media === "anime" ? qsTr("Recently watched") : qsTr("Recently read") },
        { value: "added", label: qsTr("Recently added") }, { value: "title", label: qsTr("Title A–Z") },
        { value: "unread", label: media === "anime" ? qsTr("Most unwatched") : qsTr("Most unread") }]
    readonly property var viewOptions: [{ value: "grid", icon: "layout-grid", label: qsTr("Grid") },
        { value: "compact", icon: "grid-2x2", label: qsTr("Compact grid") }, { value: "list", icon: "list", label: qsTr("List") }]

    function search(text) { filter = text }
    function toggleSelecting() { if (selecting) endSelection(); else selecting = true }
    function toggle(it) {
        const s = Object.assign({}, selection)
        if (s[it.id]) delete s[it.id]
        else s[it.id] = true
        selection = s
    }
    function selectAll() {
        const s = {}
        for (const e of entries) s[e.id] = true
        selection = s
    }
    function endSelection() { selecting = false; selection = ({}) }
    function moveSelected() {
        const list = []
        for (const id of Object.keys(selection)) {
            const row = Library.seriesRow(id)
            if (!row.id || String(row.sourceUrl || "").startsWith("file:")) continue
            list.push({ id: row.id, media: row.media, title: row.title, cover: row.cover || "", source: row.source,
                        sourceLabel: row.media === "novel" ? ((Novels.sources().find(x => x.key === row.source) || {}).label || row.source)
                                                         : Comics.sourceLabel(row.source),
                        sourceUrl: row.sourceUrl, extra: row.extra || {} })
        }
        if (list.length === 0) { Router.toast(qsTr("Files on this device stay where they are."), "info"); return }
        migrate.start(list)
    }
    function subtitle(it) {
        return it.unitTitle
            ? (it.media === "anime" ? qsTr("Episode ") + (it.unitNumber || it.unitKey) : it.unitTitle)
            : (it.media === "anime" ? qsTr("Anime") : it.media === "novel" ? qsTr("Novel") : qsTr("Comic"))
    }

    // Library changes come every couple of seconds while reading (progress):
    // a page out of sight (another section, under a reader or the player)
    // only notes them and reloads once it shows again.
    property bool libraryStale: false
    onVisibleChanged: if (visible && libraryStale) { libraryStale = false; refresh.restart() }
    Connections {
        target: Library
        function onChanged() { if (page.visible) refresh.restart(); else page.libraryStale = true }
    }
    Timer { id: refresh; interval: 300; onTriggered: page.task = Library.entries("", true) }
    Connections {
        target: page.checkTask
        function onFinished(result) {
            Router.toast(result.newUnits > 0 ? result.newUnits + qsTr(" new chapters found") : qsTr("Everything is up to date"),
                         result.newUnits > 0 ? "success" : "info")
        }
        function onFailed(error) { Router.toast(error, "error") }
    }

    ActionMenu { id: menu }
    // The user's own lists (issue #9): made, renamed and removed here and
    // in Settings → Library.
    ConfirmDialog {
        id: listName
        property string listId: ""
        asksText: true
        heading: listId ? qsTr("Rename list") : qsTr("New list")
        placeholder: qsTr("Name, like Isekai or Weekly")
        confirmText: listId ? qsTr("Rename") : qsTr("Add")
        onConfirmed: {
            if (listId) Library.renameCategory(listId, value)
            else App.setSetting("libraryCategory", Library.addCategory(value))
        }
        function ask(id, name) { listId = id; value = name; open() }
    }
    ConfirmDialog {
        id: listRemove
        property var list: null
        heading: list ? qsTr("Remove the list %1?").arg(list.label) : ""
        message: qsTr("Its series move to %1; nothing is removed from your library.")
            .arg((page.categories.find(c => c.value === "reading") || { label: qsTr("Reading") }).label)
        confirmText: qsTr("Remove")
        destructive: true
        onConfirmed: Library.removeCategory(list.value)
    }
    // Every list: rename, move or hide it; the user's own can be removed.
    // Built-in lists keep their meaning (trackers, sync and backups use
    // them), so they are hidden rather than removed.
    function listMenu(chip, x, y, list) {
        const at = page.categories.findIndex(c => c.value === list.value)
        menu.openAt(chip, x, y, [
            { text: qsTr("Rename"), icon: "pencil", run: () => listName.ask(list.value, list.label) },
            list.renamed ? { text: qsTr("Use the name %1").arg(list.defaultLabel), icon: "rotate-ccw",
                             run: () => Library.renameCategory(list.value, "") } : undefined,
            { text: qsTr("Move left"), icon: "chevron-left", enabled: at > 0, run: () => Library.moveCategory(list.value, -1, true) },
            { text: qsTr("Move right"), icon: "chevron-right", enabled: at < page.categories.length - 1,
              run: () => Library.moveCategory(list.value, 1, true) },
            null,
            { text: qsTr("Hide list"), icon: "eye-off", run: () => page.hideList(list) },
            list.custom ? { text: qsTr("Remove list"), icon: "trash", danger: true, run: () => { listRemove.list = list; listRemove.open() } }
                        : undefined].filter(i => i !== undefined))
    }
    function hideList(list) {
        Library.setCategoryHidden(list.value, true)
        Router.toast(qsTr("%1 is hidden. Its series stay on it and under All; Settings → Library shows it again.").arg(list.label), "info",
                     { label: qsTr("Undo"), run: () => Library.setCategoryHidden(list.value, false) })
    }
    MigrateDialog {
        id: migrate
        onClosed: page.endSelection()
    }
    ConfirmDialog {
        id: removeSelected
        heading: page.selectedCount === 1 ? qsTr("Remove 1 series from your library?") : qsTr("Remove %1 series from your library?").arg(page.selectedCount)
        message: qsTr("Your reading history and downloads are kept.")
        confirmText: qsTr("Remove")
        destructive: true
        onConfirmed: {
            for (const id of Object.keys(page.selection)) Library.remove(id)
            page.endSelection()
        }
    }

    SmoothFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 60

        // Touch screens: a sideways swipe moves to the next list (All,
        // Favourites, then the reader's lists), as in Mihon; in Retro, to the
        // next pivot.
        DragHandler {
            enabled: Theme.touch
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            property real lastX: 0
            onTranslationChanged: if (active) lastX = translation.x
            onActiveChanged: {
                if (active) { lastX = 0; return }
                const step = lastX < -80 ? 1 : lastX > 80 ? -1 : 0
                if (!step) return
                if (Theme.retro) return step > 0 ? mediaPivot.next() : mediaPivot.previous()
                page.stepCategory(step)
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.compact ? 14 : 20

            PageHeader {
                keepBeside: Theme.compact
                title: qsTr("Library")
                subtitle: page.all.length + qsTr(" saved")
                    + (Library.unreadUpdates > 0 ? " · " + Library.unreadUpdates + qsTr(" new chapters") : "")
                ScButton {
                    visible: !Theme.compact && !page.songs
                    text: page.selecting ? qsTr("Done") : qsTr("Select")
                    iconName: page.selecting ? "x" : "list-checks"
                    variant: "ghost"
                    onClicked: page.toggleSelecting()
                }
                ScButton {
                    visible: !Theme.compact
                    text: qsTr("Upcoming", "release radar")
                    iconName: "calendar"
                    variant: "ghost"
                    onClicked: Router.openUpcoming()
                }
                // New chapters and episodes seen without reading them: the
                // badges go, nothing is marked read.
                ScButton {
                    visible: !Theme.compact && Library.unreadUpdates > 0
                    text: qsTr("Clear updates")
                    iconName: "check-check"
                    variant: "ghost"
                    onClicked: Library.markAllSeen()
                }
                ScButton {
                    visible: !Theme.compact
                    text: page.checking ? qsTr("Checking…") : qsTr("Check for updates")
                    iconName: "refresh-cw"
                    loading: page.checking
                    onClicked: page.checkTask = Library.checkUpdates()
                }
                // Phones: the same two as icons, so the title keeps its row.
                IconButton {
                    visible: Theme.compact && !page.songs
                    diameter: 40
                    filled: !page.selecting
                    iconName: page.selecting ? "x" : "list-checks"
                    active: page.selecting
                    tip: page.selecting ? qsTr("Done") : qsTr("Select")
                    Accessible.name: tip
                    onClicked: page.toggleSelecting()
                }
                IconButton {
                    visible: Theme.compact
                    diameter: 40
                    filled: true
                    iconName: "calendar"
                    tip: qsTr("Upcoming", "release radar")
                    Accessible.name: tip
                    onClicked: Router.openUpcoming()
                }
                IconButton {
                    visible: Theme.compact && Library.unreadUpdates > 0
                    diameter: 40
                    filled: true
                    iconName: "check-check"
                    tip: qsTr("Clear updates")
                    Accessible.name: tip
                    onClicked: Library.markAllSeen()
                }
                IconButton {
                    id: checkButton
                    visible: Theme.compact
                    diameter: 40
                    filled: true
                    iconName: "refresh-cw"
                    enabled: !page.checking
                    tip: page.checking ? qsTr("Checking…") : qsTr("Check for updates")
                    Accessible.name: tip
                    onClicked: page.checkTask = Library.checkUpdates()
                    RotationAnimator on rotation {
                        running: page.checking && !Motion.reduced
                        loops: Animation.Infinite
                        from: 0; to: 360; duration: 900
                        onRunningChanged: if (!running) checkButton.rotation = 0
                    }
                }
            }

            // Sources that stopped answering, or series saved twice. Hidden
            // with its ×, it stays away until there are more of them.
            Rectangle {
                id: careBanner
                visible: Upkeep.attention > Number(App.settings.libraryCareHiddenAt || 0)
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                height: visible ? careRow.height + 20 : 0
                radius: Theme.radiusLg
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                Item {
                    id: careRow
                    x: 14
                    y: 10
                    width: parent.width - 28
                    height: Math.max(careText.implicitHeight, reviewButton.height)
                    Icon { id: careIcon; anchors.verticalCenter: parent.verticalCenter; name: "activity"; size: 18; color: Theme.warning }
                    Text {
                        id: careText
                        anchors.left: careIcon.right
                        anchors.leftMargin: 12
                        anchors.right: reviewButton.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: Upkeep.attention === 1 ? qsTr("Something in your library needs a look: a source that stopped answering or a series saved twice.")
                            : qsTr("%1 things in your library need a look: sources that stopped answering or series saved twice.").arg(Upkeep.attention)
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    ScButton {
                        id: reviewButton
                        anchors.right: hideCare.left
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        text: qsTr("Review")
                        onClicked: Router.openLibraryCare()
                    }
                    IconButton {
                        id: hideCare
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "x"
                        tip: qsTr("Hide until there is something new")
                        onClicked: App.setSetting("libraryCareHiddenAt", Upkeep.attention)
                    }
                }
            }

            // Retro: the media as pivot headers (swiping the page sideways
            // turns them too).
            PivotHeaders {
                id: mediaPivot
                visible: Theme.retro
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                height: visible ? implicitHeight : 0
                options: page.mediaOptions
                value: page.media
                onPicked: (v) => App.setSetting("libraryMedia", v)
            }

            Segmented {
                visible: Theme.compact && !Theme.retro
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                fill: true
                // Four (with Music) only fit a phone as words.
                options: page.mediaOptions.length > 3 ? page.mediaOptions.map(o => ({ value: o.value, label: o.label })) : page.mediaOptions
                value: page.media
                onPicked: (v) => App.setSetting("libraryMedia", v)
            }

            // Phones: the filter with sort and view as icon buttons beside it.
            Row {
                visible: Theme.compact
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                spacing: 8
                SearchField {
                    width: parent.width - (page.songs ? 0 : 2 * (40 + parent.spacing))
                    placeholderText: page.songs ? qsTr("Filter songs") : qsTr("Filter library")
                    text: page.filter
                    onTextChanged: page.filter = text
                }
                IconButton {
                    id: sortButton
                    visible: !page.songs
                    anchors.verticalCenter: parent.verticalCenter
                    diameter: 40
                    filled: true
                    iconName: "arrow-up-down"
                    tip: qsTr("Sort")
                    Accessible.name: tip
                    onClicked: menu.openAt(sortButton, 0, height + 6, page.sortOptions.map(o => ({
                        text: o.label, checked: o.value === page.sort, run: () => App.setSetting("librarySort", o.value) })))
                }
                IconButton {
                    id: viewButton
                    visible: !page.songs
                    anchors.verticalCenter: parent.verticalCenter
                    diameter: 40
                    filled: true
                    iconName: (page.viewOptions.find(o => o.value === page.view) || page.viewOptions[0]).icon
                    tip: qsTr("View")
                    Accessible.name: tip
                    onClicked: menu.openAt(viewButton, 0, height + 6, page.viewOptions.map(o => ({
                        text: o.label, icon: o.icon, checked: o.value === page.view, run: () => App.setSetting("libraryView", o.value) })))
                }
            }

            Flow {
                visible: !Theme.compact
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                spacing: 12
                Segmented {
                    visible: !Theme.retro
                    options: page.mediaOptions
                    value: page.media
                    onPicked: (v) => App.setSetting("libraryMedia", v)
                }
                SearchField {
                    width: 240
                    placeholderText: qsTr("Filter library")
                    text: page.filter
                    onTextChanged: page.filter = text
                }
                Select {
                    visible: !page.songs
                    implicitWidth: 196
                    model: page.sortOptions
                    value: page.sort
                    onPicked: (v) => App.setSetting("librarySort", v)
                }
                Segmented {
                    visible: !page.songs
                    options: page.viewOptions.map(o => ({ value: o.value, icon: o.icon }))
                    value: page.view
                    onPicked: (v) => App.setSetting("libraryView", v)
                }
            }

            ChipRow {
                visible: !page.songs
                Chip {
                    text: qsTr("All")
                    detail: String(page.counts.all || 0)
                    selected: page.category === "all"
                    onClicked: App.setSetting("libraryCategory", "all")
                }
                Chip {
                    text: qsTr("Favourites")
                    iconName: "heart"
                    detail: String(page.counts.favourites || 0)
                    selected: page.category === "favourites"
                    onClicked: App.setSetting("libraryCategory", "favourites")
                }
                Repeater {
                    model: page.categories
                    Chip {
                        id: listChip
                        required property var modelData
                        text: modelData.label
                        detail: String(page.counts[modelData.value] || 0)
                        selected: page.category === modelData.value
                        onClicked: App.setSetting("libraryCategory", modelData.value)
                        // Rename, move, hide (or remove) the list.
                        onPressAndHold: page.listMenu(listChip, width / 2, height, modelData)
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            // Mice only: touch taps ignore acceptedButtons (a tap would open it).
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onTapped: (point) => page.listMenu(listChip, point.position.x, point.position.y, listChip.modelData)
                        }
                    }
                }
                Chip {
                    text: qsTr("New list")
                    iconName: "plus"
                    onClicked: listName.ask("", "")
                }
            }

            // What to do with the series picked.
            Flow {
                visible: page.selecting
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                spacing: 8
                Text {
                    height: 36
                    verticalAlignment: Text.AlignVCenter
                    text: page.selectedCount === 0 ? qsTr("Tap series to pick them") : qsTr("%1 picked").arg(page.selectedCount)
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                    rightPadding: 8
                }
                ScButton {
                    compact: true
                    text: qsTr("All")
                    onClicked: page.selectAll()
                }
                ScButton {
                    visible: page.media !== "anime"
                    compact: true
                    iconName: "arrow-right-left"
                    text: qsTr("Move to another source")
                    enabled: page.selectedCount > 0
                    onClicked: page.moveSelected()
                }
                Select {
                    implicitWidth: 170
                    enabled: page.selectedCount > 0
                    model: [{ value: "", label: qsTr("Move to list…") }].concat(page.categories)
                    value: ""
                    onPicked: (v) => {
                        currentIndex = 0
                        if (!v) return
                        for (const id of Object.keys(page.selection)) Library.setCategory(id, v)
                        Router.toast(qsTr("Moved to %1").arg(page.categories.find(c => c.value === v).label), "success")
                        page.endSelection()
                    }
                }
                ScButton {
                    compact: true
                    iconName: "trash"
                    text: qsTr("Remove")
                    enabled: page.selectedCount > 0
                    onClicked: removeSelected.open()
                }
            }

            // Music: the songs saved, played as one list.
            Column {
                visible: page.songs
                width: parent.width
                Row {
                    visible: page.savedSongs.length > 0
                    x: Theme.gutter
                    spacing: 10
                    bottomPadding: 6
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.savedSongs.length === 1 ? qsTr("1 song") : qsTr("%1 songs").arg(page.savedSongs.length)
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                    }
                    ScButton {
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        variant: "ghost"
                        iconName: "play"
                        text: qsTr("Play all")
                        onClicked: MusicQueue.playList(page.savedSongs, 0)
                    }
                }
                Repeater {
                    model: page.savedSongs.slice(0, page.songsShown)
                    SongRow {
                        required property var modelData
                        required property int index
                        x: Theme.gutter - 8
                        width: Math.min(parent.width - 2 * Theme.gutter + 16, 776)
                        song: modelData
                        onPlayed: MusicQueue.playList(page.savedSongs, index)
                    }
                }
                ScButton {
                    visible: page.savedSongs.length > page.songsShown
                    x: Theme.gutter
                    variant: "ghost"
                    compact: true
                    text: qsTr("Show %1 more").arg(Math.min(50, page.savedSongs.length - page.songsShown))
                    onClicked: page.songsShown += 50
                }
                StateView {
                    width: parent.width
                    visible: page.savedSongs.length === 0
                    empty: true
                    emptyTitle: page.filter ? qsTr("Nothing matches") : qsTr("No songs saved yet")
                    emptyText: qsTr("In Music, an anime's page or Now Playing, choose “Save to library” on a song to keep it here.")
                    emptyIcon: "volume-2"
                }
            }

            CoverGrid {
                visible: page.view !== "list" && !page.songs
                items: page.view !== "list" && !page.songs ? page.entries : []
                titlesInside: page.view === "compact"
                minCardWidth: page.view === "compact" ? Math.round((Theme.compact ? 84 : 120) * Theme.coverScale)
                                                      : Math.round((Theme.compact ? 104 : 150) * Theme.coverScale)
                badgeFor: (it) => it.newUnits > 0 ? "+" + it.newUnits : ""
                progressFor: (it) => it.unitKey ? (it.completed ? 1 : (it.percent || 0)) : -1
                subtitleFor: (it) => page.subtitle(it)
                markedFor: { const s = page.selection; return (it) => !!s[it.id] }
                onOpened: (it, cover) => page.selecting ? page.toggle(it) : Router.openSeries(it, cover)
            }
            CoverList {
                visible: page.view === "list" && !page.songs
                items: page.view === "list" && !page.songs ? page.entries : []
                badgeFor: (it) => it.newUnits > 0 ? "+" + it.newUnits : ""
                progressFor: (it) => it.unitKey ? (it.completed ? 1 : (it.percent || 0)) : -1
                subtitleFor: (it) => [page.subtitle(it), it.sourceLabel || ""].filter(s => s).join(" · ")
                markedFor: { const s = page.selection; return (it) => !!s[it.id] }
                onOpened: (it, cover) => page.selecting ? page.toggle(it) : Router.openSeries(it, cover)
            }

            StateView {
                visible: !page.songs
                width: parent.width
                task: page.all.length === 0 ? page.task : null
                empty: page.task && page.task.ok && page.entries.length === 0
                emptyTitle: page.filter ? qsTr("Nothing matches")
                    : page.all.length === 0 ? qsTr("Your library is empty")
                    : page.category === "all" ? qsTr("Nothing saved here yet")
                    : qsTr("Nothing in %1").arg(page.categoryLabel)
                emptyText: page.all.length === 0 || page.category === "all"
                    ? qsTr("Open any comic, novel or anime and choose “Add to library” to keep it here. SCANCORE checks saved series for new chapters in the background.")
                    : page.category === "favourites" ? qsTr("Tap the heart on a saved series to keep it here.")
                    : qsTr("Choose a category on a saved series' page to move it here.")
                emptyIcon: page.category === "favourites" ? "heart" : "library"
                onRetry: page.task = Library.entries("", true)
            }
        }
    }

    BackToTop { flick: flick }
}
