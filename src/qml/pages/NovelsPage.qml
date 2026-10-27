import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Scancore

// Novel discovery (the chosen catalogue's and Royal Road's shelves) and
// search, through the catalogue or across novel sources.
Item {
    id: page
    property string query: ""
    property string source: App.settings.novelSource || "all"
    property var searchTask: null
    // The results so far: sources answer one by one, so the grid fills
    // while the slower ones are still searching.
    readonly property var found: !searchTask ? null : searchTask.ok ? searchTask.result : searchTask.loading ? (searchTask.partial || null) : null
    readonly property int stillSearching: found && searchTask.loading ? (found.pending || 0) : 0
    property var discoverTask: Novels.discover()
    // "All sources" searches the chosen novel catalogue, then finds the pick
    // on a source.
    readonly property bool useCatalog: source === "all" && (App.settings.catalogNovel || "anilist") !== "none"
        && (App.settings.catalogNovel !== "mal" || Trackers.malAvailable)
    property var feedsTask: useCatalog ? Novels.catalogFeeds(App.settings.showNsfwSources === true) : null
    readonly property var feeds: feedsTask && feedsTask.ok ? feedsTask.result : ({ shelves: [] })
    readonly property string catalogLabel: feedsTask && feedsTask.ok && feedsTask.result.label ? feedsTask.result.label : "AniList"
    property bool catalogFailed: false
    readonly property bool catalogResults: !!searchTask && searchTask.ok && !catalogFailed && useCatalog && query.length > 0
    property var historyTask: Library.continueItems("novel", 16)
    property var libraryTask: Library.entries("novel")
    // Books on this device are listed without a search.
    readonly property bool browsingLocal: source === "local" && query.length === 0
    readonly property bool searching: !editing && (query.length > 0 || source === "local")
    // Edit page: arrange the sections (PageLayout).
    property bool editing: false
    PageArranger { id: topLayout; page: "novels-top"; column: column; editing: page.editing; defaults: ["search", "sources"] }
    PageArranger {
        id: idleLayout
        page: "novels"
        column: idle
        editing: page.editing
        defaults: ["recent", "continue", "saved", "trending", "popular", "top", "newest", "completed", "lightnovels"]
    }
    Component.onCompleted: if (source === "local") run("")
    readonly property var allSources: { App.dependOn(Extensions.novelPackInstalled, Extensions.revision); return Novels.sources() }
    // Each source's entry in the running search, by key, for the chips.
    readonly property var providerMap: {
        const map = {}
        for (const p of (page.found && page.found.providers) || []) map[p.source] = p
        return map
    }
    readonly property var sources: allSources.filter(s => s.enabled && s.searchable && !s.extended)
    readonly property var moreSources: allSources.filter(s => s.enabled && s.searchable && s.extended)
        .sort((a, b) => a.label.localeCompare(b.label))

    // Library changes come every couple of seconds while reading (progress):
    // a page out of sight (another section, under a reader or the player)
    // only notes them and reloads once it shows again.
    property bool libraryStale: false
    onVisibleChanged: if (visible && libraryStale) { libraryStale = false; refresh.restart() }
    Connections {
        target: Library
        function onChanged() { if (page.visible) refresh.restart(); else page.libraryStale = true }
    }
    Timer {
        id: refresh
        interval: 500
        onTriggered: {
            page.historyTask = Library.continueItems("novel", 16)
            page.libraryTask = Library.entries("novel")
        }
    }
    property var history: []
    property var saved: []
    Connections {
        target: page.historyTask
        function onFinished() { page.history = Router.keepTask(page.history, page.historyTask) }
    }
    Connections {
        target: page.libraryTask
        function onFinished() { page.saved = Router.keepTask(page.saved, page.libraryTask) }
    }

    function search(text) {
        field.text = text
        run(text)
    }

    function run(text) {
        if (Router.takeRepositoryLink(text)) return
        query = text.trim()
        if (query.length > 0) recent.remember(query)
        catalogFailed = false
        // A pasted book link goes to the sources, which open it.
        const link = /^https?:\/\//.test(query)
        searchTask = query.length > 0 && useCatalog && !link ? Novels.catalog(query)
            : query.length > 0 || source === "local" ? Novels.search(query, source) : null
        flick.scrollTo(0)
    }

    Connections {
        target: page.searchTask
        function onFailed() {
            if (!page.useCatalog || page.catalogFailed || page.query.length === 0) return
            page.catalogFailed = true
            page.searchTask = Novels.search(page.query, "all")
        }
    }

    ReadFromDialog { id: readFrom }

    SmoothFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 60

        Column {
            id: column
            width: parent.width
            spacing: Math.round(Theme.sectionGap * 0.72)

            PageHeader {
                title: qsTr("Novels")
                subtitle: qsTr("Web novels and light novels from ") + page.sources.length + qsTr(" sources")
                IconButton {
                    iconName: "layout-dashboard"
                    tip: qsTr("Edit page")
                    active: page.editing
                    onClicked: page.editing = !page.editing
                }
                // A book of your own (issue #16): an EPUB, FB2 or MOBI
                // opens in the novel reader, a PDF as pages.
                IconButton {
                    iconName: "folder-open"
                    tip: qsTr("Open a book file (EPUB, FB2, MOBI or PDF)")
                    onClicked: bookFile.open()
                }
                // Plugins, beside comics' extensions.
                ScButton {
                    text: qsTr("Extensions")
                    iconName: "puzzle"
                    onClicked: Router.openExtensions()
                }
            }

            PageSection {
                arranger: topLayout
                key: "search"
                label: qsTr("Search bar")
                SearchField {
                    id: field
                    x: Theme.gutter
                    width: Math.min(parent.width - 2 * Theme.gutter, 760)
                    large: true
                    placeholderText: qsTr("Search by title, or paste a book link")
                    onSubmitted: (text) => page.run(text)
                    onTextChanged: if (text.length === 0 && page.query.length > 0) page.run("")
                }
            }
            PageSection {
                arranger: topLayout
                key: "sources"
                label: qsTr("Source chips")
                Column {
                    width: parent.width
                    spacing: 10
                    Select {
                        visible: page.moreSources.length > 0
                        x: Theme.gutter
                        implicitWidth: 220
                        model: [{ value: "", label: qsTr("More sources (") + page.moreSources.length + ")" }]
                            .concat(page.moreSources.map(x => ({ value: x.key, label: x.label + (x.language && x.language !== "en" ? " · " + x.language : "") })))
                        value: page.moreSources.some(x => x.key === page.source) ? page.source : ""
                        onPicked: (v) => {
                            if (!v) return
                            page.source = v
                            if (page.searching) page.run(page.query)
                        }
                    }
                    SourceChips {
                        sources: page.sources
                        current: page.source
                        providers: page.providerMap
                        showLanguage: false
                        onPicked: (key) => {
                            page.source = key
                            App.setSetting("novelSource", key)
                            if (key === "all" || page.searching || key === "local") page.run(page.query)
                        }
                    }
                }
            }

            Column {
                visible: page.searching
                width: parent.width
                spacing: 18
                Text {
                    x: Theme.gutter
                    visible: !!page.found
                    text: !page.found ? ""
                        : page.browsingLocal ? page.found.books.length + qsTr(" on this device")
                        : page.catalogResults ? qsTr("%1 titles for “%2” from %3's catalogue").arg(page.found.books.length).arg(page.query).arg(page.catalogLabel)
                        : page.found.books.length + qsTr(" results for “") + page.query + "”"
                            + (page.catalogFailed ? qsTr(" · %1 is unavailable, so your sources were searched").arg(page.catalogLabel) : "")
                                + (page.stillSearching ? (page.stillSearching === 1 ? qsTr(" · 1 source still searching") : qsTr(" · %1 sources still searching").arg(page.stillSearching)) : "")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
                Flow {
                    x: Theme.gutter
                    width: parent.width - 2 * Theme.gutter
                    spacing: 8
                    Repeater {
                        model: page.found ? page.found.providers.filter(p => p.verifyUrl) : []
                        ScButton {
                            required property var modelData
                            compact: true
                            iconName: "globe"
                            text: qsTr("Pass %1's browser check").arg(modelData.label)
                            onClicked: Router.openSiteCheck(modelData.verifyUrl)
                        }
                    }
                }

                CoverGrid {
                    items: page.found ? page.found.books : []
                    subtitleFor: (it) => it.catalog
                        ? [it.type, it.status, it.year > 0 ? String(it.year) : ""].filter(s => s).join(" · ")
                        : it.sourceLabel + (it.author ? " · " + it.author : "")
                    onOpened: (it, cover) => it.catalog ? readFrom.start(it) : Router.openSeries(it, cover)
                }
                StateView {
                    width: parent.width
                    task: page.searchTask
                    empty: page.searchTask && page.searchTask.ok && page.found.books.length === 0
                    emptyTitle: page.browsingLocal ? qsTr("No books on this device yet") : qsTr("No novels found")
                    emptyText: page.browsingLocal
                        ? qsTr("Choose the folders that hold your EPUB, FB2 and MOBI books in Settings → Sources.")
                        : qsTr("Try fewer words, another source, or paste the book's link.")
                    emptyIcon: page.browsingLocal ? "folder-open" : "search"
                    onRetry: page.run(page.query)
                }
                ScButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: page.browsingLocal && page.searchTask && page.searchTask.ok && page.found.books.length === 0
                    text: qsTr("Choose folders")
                    iconName: "folder-open"
                    onClicked: Router.openSettings("sources")
                }
            }

            Column {
                id: idle
                visible: !page.searching
                width: parent.width
                spacing: Theme.sectionGap

                PageSection {
                    arranger: idleLayout
                    key: "recent"
                    label: qsTr("Recent searches")
                    RecentSearches {
                        id: recent
                        key: "recentNovelSearches"
                        onPicked: (text) => page.search(text)
                    }
                }

                PageSection {
                    arranger: idleLayout
                    key: "continue"
                    label: qsTr("Continue reading")
                    Shelf {
                        title: qsTr("Continue reading")
                        items: page.history
                        subtitleFor: (it) => it.unitTitle || ""
                        progressFor: (it) => it.completed ? 1 : (it.percent || 0)
                        onOpened: (it, cover) => Router.resume(it)
                    }
                }
                PageSection {
                    arranger: idleLayout
                    key: "saved"
                    label: qsTr("Saved novels")
                    Shelf {
                        title: qsTr("Saved novels")
                        items: page.saved
                        badgeFor: (it) => it.newUnits > 0 ? "+" + it.newUnits : ""
                        subtitleFor: (it) => it.author || ""
                        onOpened: (it, cover) => Router.openSeries(it, cover)
                    }
                }
                Repeater {
                    model: page.useCatalog ? page.feeds.shelves : []
                    PageSection {
                        id: feed
                        required property var modelData
                        arranger: idleLayout
                        key: modelData.key
                        label: Router.shelfTitle(modelData.key)
                        Shelf {
                            width: parent.width
                            title: Router.shelfTitle(feed.modelData.key)
                            subtitle: page.catalogLabel
                            items: feed.modelData.items
                            subtitleFor: (it) => [it.type, it.status].filter(s => s).join(" · ")
                            badgeFor: (it) => it.rating ? "★ " + it.rating : ""
                            onOpened: (it, cover) => readFrom.start(it)
                        }
                    }
                }
                Shelf {
                    visible: page.useCatalog && page.feedsTask !== null && !page.feedsTask.ok
                    title: qsTr("Trending this week")
                    loading: page.feedsTask ? page.feedsTask.loading : false
                    error: page.feedsTask && !page.feedsTask.loading ? page.feedsTask.error : ""
                    onRetry: page.feedsTask = Novels.catalogFeeds(App.settings.showNsfwSources === true)
                }
                Repeater {
                    model: page.discoverTask && page.discoverTask.ok ? page.discoverTask.result.shelves : []
                    delegate: PageSection {
                        id: discover
                        required property var modelData
                        arranger: idleLayout
                        key: "discover:" + modelData.source + ":" + modelData.title
                        label: modelData.title + " · " + modelData.source
                        Shelf {
                            title: discover.modelData.title
                            subtitle: discover.modelData.source
                            items: discover.modelData.books
                            subtitleFor: (it) => it.genres && it.genres.length ? it.genres.slice(0, 2).join(", ") : (it.status || "")
                            badgeFor: (it) => it.rating ? "★ " + Number(it.rating).toFixed(1) : ""
                            onOpened: (it, cover) => Router.openSeries(it, cover)
                        }
                    }
                }
                Shelf {
                    visible: page.discoverTask && !page.discoverTask.ok
                    title: qsTr("Discover")
                    loading: page.discoverTask ? page.discoverTask.loading : false
                    error: page.discoverTask && !page.discoverTask.loading ? page.discoverTask.error : ""
                    onRetry: page.discoverTask = Novels.discover()
                }
            }
        }
    }

    BackToTop { flick: flick }
    EditPageBar {
        editing: page.editing
        arrangers: [topLayout, idleLayout]
        onDone: page.editing = false
    }

    FileDialog {
        id: bookFile
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Books (*.epub *.fb2 *.fb2.zip *.mobi *.azw3 *.azw *.prc *.pdf)"), qsTr("All files (*)")]
        onAccepted: Router.openFile(selectedFile)
    }
}
