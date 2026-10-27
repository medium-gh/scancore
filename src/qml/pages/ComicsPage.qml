import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Comic search across every source, plus the latest chapters when idle.
Item {
    id: page
    property string query: ""
    property string source: App.settings.comicSource || "all"
    property var searchTask: null
    property var browseTask: null
    property var browseItems: []
    property int browsePage: 0
    property bool browseHasNext: true
    property int browseGeneration: 0
    readonly property bool browsingSource: source !== "all" && source !== "local" && query.length === 0
        && sources.some(s => s.key === source && s.canBrowse)
    // The results so far: sources answer one by one, so the grid fills
    // while the slower ones are still searching.
    readonly property var found: browsingSource ? ({ comics: browseItems, providers: [] })
        : !searchTask ? null : searchTask.ok ? searchTask.result : searchTask.loading ? (searchTask.partial || null) : null
    // Beside a catalogue search: the installed extensions, searched too (the
    // catalogue does not know their titles), streamed in as they answer.
    property var extensionTask: null
    readonly property var extensionFound: !extensionTask ? null : extensionTask.ok ? extensionTask.result
        : extensionTask.loading ? (extensionTask.partial || null) : null
    readonly property bool hasExtensions: sources.some(s => String(s.key).startsWith("mihon:"))
    readonly property int stillSearching: found && searchTask && searchTask.loading ? (found.pending || 0) : 0
    property int latestLimit: 36
    property var latestTask: Comics.latest(latestLimit)
    property var historyTask: Library.continueItems("comic", 16)
    // The shelves of the chosen comic catalogue, when search goes through it.
    property var feedsTask: useCatalog ? Comics.catalogFeeds(App.settings.showNsfwSources === true) : null
    readonly property var feeds: feedsTask && feedsTask.ok ? feedsTask.result : ({ shelves: [] })
    // The catalogue's name, for messages.
    readonly property string catalogLabel: feedsTask && feedsTask.ok && feedsTask.result.label ? feedsTask.result.label
        : App.settings.catalogComic === "anilist" ? "AniList" : App.settings.catalogComic === "mal" ? "MyAnimeList" : qsTr("Comick")
    property var history: []
    Connections {
        target: page.historyTask
        function onFinished() { page.history = Router.keepTask(page.history, page.historyTask) }
    }
    // Library changes come every couple of seconds while reading (progress):
    // a page out of sight (another section, under a reader or the player)
    // only notes them and reloads once it shows again.
    property bool libraryStale: false
    onVisibleChanged: if (visible && libraryStale) { libraryStale = false; refreshHistory.restart() }
    Connections {
        target: Library
        function onChanged() { if (page.visible) refreshHistory.restart(); else page.libraryStale = true }
    }
    Timer { id: refreshHistory; interval: 500; onTriggered: page.historyTask = Library.continueItems("comic", 16) }
    // Files on this device are listed without a search.
    // "All sources" searches the chosen comic catalogue, then finds the pick
    // on a source.
    readonly property bool useCatalog: source === "all" && (App.settings.catalogComic || "comick") !== "none"
        && (App.settings.catalogComic !== "mal" || Trackers.malAvailable)
    // The catalogue could not be reached, so the sources were searched instead.
    property bool catalogFailed: false
    // Comick's own search filters (CatalogueFilters), kept between visits;
    // with some set, an empty search box browses by them.
    readonly property bool comick: useCatalog && (App.settings.catalogComic || "comick") === "comick"
    readonly property var catalogFilters: comick ? filterSheet.normal(App.settings.comickFilters) : ({})
    readonly property int filterCount: comick ? filterSheet.count(catalogFilters) : 0
    readonly property bool filtering: filterCount > 0
    readonly property bool catalogResults: !!searchTask && searchTask.ok && !catalogFailed && useCatalog && (query.length > 0 || filtering)
    readonly property bool browsingLocal: source === "local" && query.length === 0
    readonly property bool searching: !editing && (query.length > 0 || filtering || source === "local" || browsingSource)
    // Edit page: arrange the sections (PageLayout).
    property bool editing: false
    PageArranger { id: topLayout; page: "comics-top"; column: column; editing: page.editing; defaults: ["search", "sources"] }
    PageArranger {
        id: idleLayout
        page: "comics"
        column: idle
        editing: page.editing
        defaults: ["recent", "continue", "trending", "popular", "newest", "completed", "top", "manhwa", "manhua", "latest"]
    }
    Component.onCompleted: if (source === "local" || browsingSource || filtering) run("")

    function moreSource() {
        if (!browsingSource || (browseTask && browseTask.loading) || !browseHasNext) return
        const generation = browseGeneration
        const next = browsePage + 1
        browseTask = Comics.browse(source, "popular", next)
        browseTask.finished.connect((result) => {
            if (generation !== page.browseGeneration) return
            const seen = new Set(page.browseItems.map(item => item.id))
            const fresh = result.items.filter(item => {
                if (seen.has(item.id)) return false
                seen.add(item.id)
                return true
            })
            page.browseItems = page.browseItems.concat(fresh)
            page.browsePage = next
            page.browseHasNext = result.hasNextPage
        })
    }

    function search(text) {
        field.text = text
        run(text)
    }

    function run(text) {
        const trimmed = text.trim()
        if (Router.takeRepositoryLink(trimmed)) return
        // A pasted series link opens that series directly.
        if (/^https?:\/\//.test(trimmed) && Comics.sourceForUrl(trimmed)) {
            Router.openSeries({ media: "comic", sourceUrl: trimmed, title: "", source: Comics.sourceForUrl(trimmed) }, null)
            return
        }
        query = trimmed
        browseGeneration += 1
        if (browseTask && browseTask.loading) browseTask.cancel()
        browseTask = null
        browseItems = []
        browsePage = 0
        browseHasNext = true
        if (query.length > 0) recent.remember(query)
        catalogFailed = false
        searchTask = (query.length > 0 || filtering) && useCatalog ? Comics.catalog(query, catalogFilters)
            : query.length > 0 || source === "local" ? Comics.search(query, source) : null
        extensionTask = query.length > 0 && useCatalog && hasExtensions ? Comics.search(query, "extensions") : null
        if (browsingSource) moreSource()
        flick.scrollTo(0)
    }

    Connections {
        target: page.searchTask
        function onFailed() {
            if (!page.useCatalog || page.catalogFailed || page.query.length === 0) return
            page.catalogFailed = true
            // Every source now, extensions included.
            page.extensionTask = null
            page.searchTask = Comics.search(page.query, "all")
        }
    }

    ReadFromDialog { id: readFrom }
    CatalogueFilters {
        id: filterSheet
        onChosen: (filters) => {
            App.setSetting("comickFilters", filters)
            page.run(field.text)
        }
    }

    // Each source's entry in the running search, by key, for the chips.
    readonly property var providerMap: {
        const map = {}
        for (const p of (page.found && page.found.providers) || []) map[p.source] = p
        return map
    }
    readonly property var sources: { App.dependOn(Extensions.revision); return Comics.sources().filter(s => s.enabled && (!s.nsfw || App.settings.showNsfwSources)) }

    SmoothFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 60

        Column {
            id: column
            width: parent.width
            spacing: Math.round(Theme.sectionGap * 0.72)

            PageHeader {
                title: qsTr("Comics")
                subtitle: page.sources.length + qsTr(" sources · manhwa, manga and webtoons")
                IconButton {
                    iconName: "layout-dashboard"
                    tip: qsTr("Edit page")
                    active: page.editing
                    onClicked: page.editing = !page.editing
                }
                ScButton {
                    text: qsTr("Extensions")
                    iconName: "puzzle"
                    onClicked: Router.openExtensions()
                    // A dot when updates wait.
                    Rectangle {
                        visible: Extensions.updateCount > 0
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 4
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.accent
                    }
                }
            }

            PageSection {
                arranger: topLayout
                key: "search"
                label: qsTr("Search bar")
                Column {
                    x: Theme.gutter
                    width: Math.min(parent.width - 2 * Theme.gutter, 760)
                    spacing: 8
                    Row {
                        width: parent.width
                        spacing: 8
                        SearchField {
                            id: field
                            width: parent.width - (filterButton.visible ? filterButton.width + parent.spacing : 0)
                            large: true
                            placeholderText: page.useCatalog ? qsTr("Search every manhwa, manga and comic") : qsTr("Search titles across every source")
                            onSubmitted: (text) => page.run(text)
                            onTextChanged: if (text.length === 0 && page.query.length > 0) page.run("")
                        }
                        // Comick's filters: genres, tags and much more.
                        IconButton {
                            id: filterButton
                            visible: page.comick
                            anchors.verticalCenter: field.verticalCenter
                            diameter: Math.max(40, field.height)
                            iconName: "funnel"
                            tip: qsTr("Search filters")
                            active: page.filtering
                            onClicked: filterSheet.start(page.catalogFilters)
                            Rectangle {
                                visible: page.filtering
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 2
                                width: Math.max(18, countText.implicitWidth + 8)
                                height: 18
                                radius: 9
                                color: Theme.accent
                                Text {
                                    id: countText
                                    anchors.centerIn: parent
                                    text: page.filterCount
                                    color: Theme.accentInk
                                    font.family: Theme.fontSans
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }
                    // What the filters do now, and a way out.
                    Row {
                        visible: page.filtering
                        spacing: 6
                        Chip {
                            text: page.filterCount === 1 ? qsTr("1 filter on") : qsTr("%1 filters on").arg(page.filterCount)
                            iconName: "funnel"
                            onClicked: filterSheet.start(page.catalogFilters)
                        }
                        Chip {
                            text: qsTr("Clear")
                            iconName: "funnel-x"
                            onClicked: {
                                App.setSetting("comickFilters", ({}))
                                page.run(field.text)
                            }
                        }
                    }
                }
            }

            PageSection {
                arranger: topLayout
                key: "sources"
                label: qsTr("Source chips")
                SourceChips {
                    sources: page.sources
                    current: page.source
                    providers: page.providerMap
                    showLanguage: true
                    onPicked: (key) => {
                        page.source = key
                        App.setSetting("comicSource", key)
                        page.run(page.query)
                    }
                }
            }

            // ---------------------------------------------------- results
            Column {
                visible: page.searching
                width: parent.width
                spacing: 18

                Item {
                    width: parent.width
                    height: 24
                    visible: !!page.found
                    Text {
                        x: Theme.gutter
                        text: !page.found ? ""
                            : page.browsingLocal ? page.found.comics.length + qsTr(" on this device")
                            : page.browsingSource ? page.found.comics.length + " · " + ((page.sources.find(s => s.key === page.source) || {}).label || "")
                            : page.catalogResults && page.query.length === 0 ? qsTr("%1 titles from %2's catalogue, filtered").arg(page.found.comics.length).arg(page.catalogLabel)
                            : page.catalogResults ? qsTr("%1 titles for “%2” from %3's catalogue").arg(page.found.comics.length).arg(page.query).arg(page.catalogLabel)
                            : page.found.comics.length + qsTr(" results for “") + page.query + "”"
                                + (page.catalogFailed ? qsTr(" · %1 is unavailable, so your sources were searched").arg(page.catalogLabel) : "")
                                + (page.stillSearching ? (page.stillSearching === 1 ? qsTr(" · 1 source still searching") : qsTr(" · %1 sources still searching").arg(page.stillSearching)) : "")
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.gutter
                        readonly property var failed: page.found ? page.found.providers.filter(p => !p.ok && !p.pending) : []
                        visible: failed.length > 0
                        text: (failed.length === 1 ? qsTr("%1 is unavailable") : qsTr("%1 are unavailable")).arg(failed.map(p => p.label).join(", "))
                        color: Theme.warning
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        MouseArea {
                            id: failedHover
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                        ToolTip.visible: failedHover.containsMouse
                        ToolTip.text: failed.map(p => p.label + ": " + p.error).join("\n")
                    }
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
                    keyboardNavigation: page.browsingSource
                    items: page.found ? page.found.comics : []
                    subtitleFor: (it) => it.catalog
                        ? [it.type, it.status, it.lastChapter ? qsTr("Ch. ") + it.lastChapter : ""].filter(s => s).join(" · ")
                        : [it.sourceLabel, it.type, it.units ? (it.units === 1 ? qsTr("1 chapter") : qsTr("%1 chapters").arg(it.units)) : ""].filter(s => s).join(" · ")
                    onOpened: (it, cover) => it.catalog ? readFrom.start(it) : Router.openSeries(it, cover)
                }

                StateView {
                    width: parent.width
                    task: page.browsingSource ? page.browseTask : page.searchTask
                    empty: (page.browsingSource ? page.browseTask && page.browseTask.ok : page.searchTask && page.searchTask.ok) && !!page.found && page.found.comics.length === 0
                           && !(page.extensionFound && (page.extensionFound.comics.length > 0 || page.extensionTask.loading))
                    emptyTitle: page.browsingLocal ? qsTr("No comics on this device yet") : qsTr("No comics found")
                    emptyText: page.browsingLocal
                        ? qsTr("Choose the folders that hold your comics in Settings → Sources. Folders of images, CBZ, CBR and PDF files all work.")
                        : qsTr("Try a shorter title or another source. You can also paste a series link.")
                    emptyIcon: page.browsingLocal ? "folder-open" : "search"
                    onRetry: page.browsingSource ? page.moreSource() : page.run(page.query)
                }
                ScButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: page.browsingSource && page.browseTask && page.browseTask.ok && page.browseHasNext
                    text: qsTr("Show more")
                    onClicked: page.moreSource()
                }
                // Your extensions' results, under the catalogue's.
                Text {
                    visible: page.catalogResults && !!page.extensionFound
                    x: Theme.gutter
                    width: parent.width - 2 * Theme.gutter
                    topPadding: 8
                    text: !page.extensionFound ? ""
                        : qsTr("%1 from your extensions").arg(page.extensionFound.comics.length)
                          + (page.extensionTask.loading && page.extensionFound.pending
                             ? " · " + qsTr("%1 still searching").arg(page.extensionFound.pending) : "")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                }
                // Extensions that met a browser check while searching: passed
                // on a tap, not over the page.
                Flow {
                    x: Theme.gutter
                    width: parent.width - 2 * Theme.gutter
                    spacing: 8
                    visible: page.catalogResults && !!page.extensionFound
                    Repeater {
                        model: page.catalogResults && page.extensionFound ? page.extensionFound.providers.filter(p => p.verifyUrl).slice(0, 6) : []
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
                    visible: page.catalogResults && !!page.extensionFound && page.extensionFound.comics.length > 0
                    items: visible ? page.extensionFound.comics : []
                    subtitleFor: (it) => [it.sourceLabel, it.type, it.units ? (it.units === 1 ? qsTr("1 chapter") : qsTr("%1 chapters").arg(it.units)) : ""].filter(s => s).join(" · ")
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
                ScButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: page.browsingLocal && page.searchTask && page.searchTask.ok && page.found.comics.length === 0
                    text: qsTr("Choose folders")
                    iconName: "folder-open"
                    onClicked: Router.openSettings("sources")
                }
            }

            // ---------------------------------------------------- idle
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
                        key: "recentComicSearches"
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

                // While the catalogue loads (or fails), one shelf shows that.
                Shelf {
                    width: parent.width
                    visible: page.useCatalog && !(page.feedsTask && page.feedsTask.ok)
                    title: qsTr("Trending this week")
                    items: []
                    loading: page.feedsTask ? page.feedsTask.loading : false
                    error: page.feedsTask && !page.feedsTask.loading ? page.feedsTask.error : ""
                    onRetry: page.feedsTask = Comics.catalogFeeds(App.settings.showNsfwSources === true)
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
                            items: feed.modelData.items
                            subtitleFor: (it) => [it.type, it.lastChapter ? qsTr("Ch. ") + it.lastChapter : ""].filter(s => s).join(" · ")
                            onOpened: (it, cover) => readFrom.start(it)
                        }
                    }
                }

                PageSection {
                    arranger: idleLayout
                    key: "latest"
                    label: qsTr("Latest chapters")
                  Column {
                    width: parent.width
                    spacing: 16
                    Text {
                        x: Theme.gutter
                        text: qsTr("Latest chapters")
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.textXl
                        font.weight: Font.DemiBold
                    }
                    CoverGrid {
                        items: page.latestTask && page.latestTask.ok ? page.latestTask.result.updates : []
                        subtitleFor: (it) => (it.chapterTitle || "") + (it.publishedAt ? " · " + App.relativeTime(it.publishedAt) : "")
                        onOpened: (it, cover) => Router.openSeries(it, cover)
                    }
                    StateView {
                        width: parent.width
                        task: page.latestTask
                        onRetry: page.latestTask = Comics.latest(page.latestLimit)
                    }
                    ScButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: page.latestTask && page.latestTask.ok && page.latestLimit < 120
                        text: qsTr("Show more")
                        onClicked: {
                            page.latestLimit += 36
                            page.latestTask = Comics.latest(page.latestLimit)
                        }
                    }
                  }
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
}
