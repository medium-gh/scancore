import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Everything at a glance: continue where you left off, library updates and
// what is new across comics, novels and anime.
Item {
    id: page

    property var historyTask: Library.continueItems("", 24)
    property var libraryTask: Library.entries("")
    // A section removed in Edit page loads nothing.
    readonly property bool comicsOn: { App.dependOn(PageLayout.revision); return !PageLayout.isRemoved("home", "comics") }
    readonly property bool animeOn: { App.dependOn(PageLayout.revision); return !PageLayout.isRemoved("home", "anime") }
    readonly property bool novelsOn: { App.dependOn(PageLayout.revision); return !PageLayout.isRemoved("home", "novels") }
    // Only a change to the added shelves rebuilds them (not moving one).
    readonly property string extrasJson: { App.dependOn(PageLayout.revision); return JSON.stringify(PageLayout.extras("home")) }
    property var latestTask: comicsOn ? Comics.latest(30) : null
    property var animeTask: animeOn ? Anime.home() : null
    property var novelsTask: novelsOn ? Novels.discover() : null

    // The last results, kept while a refresh runs.
    property var history: []
    property var library: []
    readonly property var updates: library.filter(e => e.newUnits > 0)
    Connections {
        target: page.historyTask
        function onFinished() { page.history = Router.keepTask(page.history, page.historyTask) }
    }
    Connections {
        target: page.libraryTask
        function onFinished() { page.library = Router.keepTask(page.library, page.libraryTask) }
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
    Timer {
        id: refresh
        interval: 400
        onTriggered: {
            page.historyTask = Library.continueItems("", 24)
            page.libraryTask = Library.entries("")
            Story.refreshCards()
        }
    }

    ReadFromDialog { id: storyReadFrom }
    Component.onCompleted: Story.refreshCards()

    // Edit page: arrange the sections (PageLayout).
    property bool editing: false
    PageArranger {
        id: layout
        page: "home"
        column: column
        editing: page.editing
        removable: true
        defaults: ["greeting", "resume", "continue", "story", "updates", "comics", "anime", "novels"]
    }

    // The time of day's, or the reader's own words (Settings → Interface).
    function greeting() {
        const h = new Date().getHours()
        const time = h < 5 ? qsTr("Up late") : h < 12 ? qsTr("Good morning") : h < 18 ? qsTr("Good afternoon") : qsTr("Good evening")
        const name = typeof App.settings.homeName === "string" ? App.settings.homeName.trim() : ""
        const own = typeof App.settings.homeGreeting === "string" ? App.settings.homeGreeting.trim() : ""
        if (own) return own.replace(/\{time\}/g, time).replace(/\{name\}/g, name).replace(/\s+([,!.?])/g, "$1").replace(/,\s*$/, "").trim()
        return name ? qsTr("%1, %2").arg(time).arg(name) : time
    }

    SmoothFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 60

        Column {
            id: column
            width: parent.width
            spacing: Theme.sectionGap

            // Edit page, at the top right whatever comes first.
            Item {
                width: parent.width
                // Glass: the hero's orb has it.
                visible: !Theme.glass
                height: visible ? editButton.height : 0
                IconButton {
                    id: editButton
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.gutter - 6
                    y: 12
                    iconName: "layout-dashboard"
                    tip: qsTr("Edit page")
                    active: page.editing
                    onClicked: page.editing = !page.editing
                }
            }

            PageSection {
                arranger: layout
                key: "greeting"
                label: qsTr("Greeting")
                // Glass: what was read last is the hero, where to pick up on it.
                GlassHero {
                    visible: Theme.glass && page.history.length > 0
                    height: visible ? implicitHeight : 0
                    entry: page.history.length ? page.history[0] : ({})
                    onEditPage: page.editing = !page.editing
                }
                Column {
                    visible: !(Theme.glass && page.history.length > 0)
                    width: parent.width
                    spacing: 18
                    Item {
                        width: parent.width
                        height: Theme.compact ? 70 : 110
                        Column {
                            id: heading
                            x: Theme.gutter
                            anchors.bottom: parent.bottom
                            spacing: 6
                            Text {
                                text: Qt.formatDate(new Date(), "dddd, d MMMM")
                                color: Theme.textFaint
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                font.weight: Font.Medium
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 1.2
                            }
                            Text {
                                id: greetingText
                                text: page.greeting()
                                // A long greeting of your own shrinks to fit.
                                width: Math.min(implicitWidth, page.width - 2 * Theme.gutter)
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 18
                                elide: Text.ElideRight
                                color: Theme.text
                                font.family: Theme.fontDisplay
                                font.pixelSize: Theme.compact ? 32 : 42
                                font.weight: Font.Bold
                                font.letterSpacing: -1
                            }
                        }
                        // Only when it fits beside the greeting (not on landscape phones).
                        Row {
                            id: quickLinks
                            visible: !Theme.compact && heading.width + width + 3 * Theme.gutter <= parent.width
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.gutter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 6
                            spacing: 10
                            ScButton {
                                text: qsTr("Comics")
                                iconName: "book-open"
                                onClicked: Router.go("comics")
                            }
                            ScButton {
                                text: qsTr("Novels")
                                iconName: "book-text"
                                onClicked: Router.go("novels")
                            }
                            ScButton {
                                text: qsTr("Anime")
                                iconName: "tv"
                                onClicked: Router.go("anime")
                            }
                        }
                    }
                    // December to 7 January: the year's Wrapped.
                    Rectangle {
                        id: wrappedBanner
                        readonly property int year: Router.wrappedYear()
                        visible: year > 0 && Number(App.settings.wrappedDismissed) !== year
                        x: Theme.gutter
                        width: parent.width - 2 * Theme.gutter
                        height: visible ? Math.max(76, bannerText.height + 32) : 0
                        radius: Theme.radiusXl
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            // Wrapped's own opening colours.
                            GradientStop { position: 0; color: "#4b1fd1" }
                            GradientStop { position: 0.6; color: "#c0267a" }
                            GradientStop { position: 1; color: "#e2471f" }
                        }
                        TapHandler { onTapped: Router.openWrapped(wrappedBanner.year) }
                        Image {
                            id: bannerMark
                            x: 18
                            anchors.verticalCenter: parent.verticalCenter
                            // The mark, twice as wide as it is high.
                            source: App.icon("scancore-mark", "#ffffff")
                            sourceSize: Qt.size(80 * Screen.devicePixelRatio, 40 * Screen.devicePixelRatio)
                            width: 40
                            height: 20
                            fillMode: Image.PreserveAspectFit
                        }
                        Column {
                            id: bannerText
                            anchors.left: bannerMark.right
                            anchors.leftMargin: 14
                            anchors.right: bannerClose.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: qsTr("Your %1 Wrapped is here").arg(wrappedBanner.year)
                                color: "white"
                                font.family: Theme.fontDisplay
                                font.pixelSize: Theme.textXl
                                font.weight: Font.Bold
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: qsTr("Your year in comics, novels, anime and music")
                                color: Qt.rgba(1, 1, 1, 0.8)
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                            }
                        }
                        IconButton {
                            id: bannerClose
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "x"
                            iconColor: "white"
                            tip: qsTr("Not now")
                            onClicked: App.setSetting("wrappedDismissed", wrappedBanner.year)
                        }
                    }
                }
            }

            // The latest of them, large.
            PageSection {
                arranger: layout
                key: "resume"
                label: qsTr("Last read or watched")
                shown: page.history.length > 0 && !Theme.glass
                Loader {
                    width: parent.width
                    active: page.history.length > 0
                    visible: active
                    sourceComponent: ResumeCard { entry: page.history[0] }
                }
            }

            PageSection {
                arranger: layout
                key: "continue"
                label: qsTr("Continue")
                Shelf {
                    title: qsTr("Continue")
                    items: page.history.slice(1)
                    loading: page.historyTask ? page.historyTask.loading && page.history.length === 0 : false
                    subtitleFor: (it) => it.media === "anime" ? qsTr("Episode ") + (it.unitNumber || it.unitKey) : (it.unitTitle || "")
                    progressFor: (it) => it.completed ? 1 : (it.percent || 0)
                    onOpened: (it, cover) => Router.resume(it)
                }
            }

            // An anime season finished: where its comic or novel goes on.
            PageSection {
                arranger: layout
                key: "story"
                label: qsTr("Continue the story")
                shown: Story.cards.length > 0
                Column {
                    width: parent.width
                    spacing: 14
                    visible: Story.cards.length > 0
                    Text {
                        x: Theme.gutter
                        text: qsTr("Continue the story")
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.textXl
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        maximumFlickVelocity: Motion.flickVelocity
                        flickDeceleration: Motion.flickDeceleration
                        WheelScroll {}
                        width: parent.width
                        height: Theme.compact ? 132 : 144
                        orientation: ListView.Horizontal
                        leftMargin: Theme.gutter
                        rightMargin: Theme.gutter
                        spacing: 12
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: Story.cards
                        delegate: StoryCard {
                            required property var modelData
                            entry: modelData
                            onOpen: (e) => {
                                if (e.seriesId) Router.openSeries(Library.seriesRow(e.seriesId), null)
                                else if (e.entry) storyReadFrom.start(e.entry)
                            }
                        }
                    }
                }
            }

            PageSection {
                arranger: layout
                key: "updates"
                label: qsTr("New in your library")
                Shelf {
                    title: qsTr("New in your library")
                    items: page.updates
                    // Seen without reading: the shelf and the badges go.
                    actionText: qsTr("Clear")
                    onAction: Library.markAllSeen()
                    badgeFor: (it) => "+" + it.newUnits
                    subtitleFor: (it) => it.latestTitle || ""
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
            }

            PageSection {
                arranger: layout
                key: "comics"
                label: qsTr("Latest comic chapters")
                Shelf {
                    title: qsTr("Latest comic chapters")
                    actionText: qsTr("More")
                    onAction: Router.go("comics")
                    items: page.latestTask && page.latestTask.ok ? page.latestTask.result.updates : []
                    loading: page.latestTask ? page.latestTask.loading : false
                    error: page.latestTask && !page.latestTask.loading ? page.latestTask.error : ""
                    onRetry: page.latestTask = Comics.latest(30)
                    subtitleFor: (it) => (it.chapterTitle || "") + (it.publishedAt ? " · " + App.relativeTime(it.publishedAt) : "")
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
            }

            PageSection {
                arranger: layout
                key: "anime"
                label: qsTr("Trending anime")
                Shelf {
                    title: qsTr("Trending anime")
                    actionText: qsTr("Browse")
                    onAction: Router.go("anime")
                    items: page.animeTask && page.animeTask.ok ? page.animeTask.result.trending : []
                    loading: page.animeTask ? page.animeTask.loading : false
                    error: page.animeTask && !page.animeTask.loading ? page.animeTask.error : ""
                    onRetry: page.animeTask = Anime.home()
                    subtitleFor: (it) => [it.format, it.year].filter(Boolean).join(" · ")
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
            }

            PageSection {
                arranger: layout
                key: "novels"
                label: qsTr("Novels")
                Column {
                    readonly property bool hasContent: page.novelsTask !== null
                    width: parent.width
                    spacing: Theme.sectionGap
                    Repeater {
                        model: page.novelsTask && page.novelsTask.ok ? page.novelsTask.result.shelves.slice(0, 2) : []
                        delegate: Shelf {
                            required property var modelData
                            title: modelData.title + qsTr(" novels")
                            subtitle: modelData.source
                            items: modelData.books
                            subtitleFor: (it) => it.author || it.genres.slice(0, 2).join(", ")
                            onOpened: (it, cover) => Router.openSeries(it, cover)
                        }
                    }
                    Shelf {
                        visible: page.novelsTask && (page.novelsTask.loading || page.novelsTask.error.length > 0)
                        title: qsTr("Novels")
                        loading: page.novelsTask ? page.novelsTask.loading : false
                        error: page.novelsTask && !page.novelsTask.loading ? page.novelsTask.error : ""
                        onRetry: page.novelsTask = Novels.discover()
                    }
                }
            }

            // Shelves of an extension's own listings, added in Edit page.
            Repeater {
                model: JSON.parse(page.extrasJson)
                PageSection {
                    id: extraSection
                    required property var modelData
                    arranger: layout
                    key: modelData.key
                    label: modelData.label
                    readonly property var service: modelData.media === "anime" ? Anime : modelData.media === "novel" ? Novels : Comics
                    property var task: service.browse(modelData.source, modelData.listing, 1)
                    Shelf {
                        title: extraSection.modelData.label
                        items: extraSection.task && extraSection.task.ok ? extraSection.task.result.items : []
                        loading: extraSection.task ? extraSection.task.loading : false
                        error: extraSection.task && !extraSection.task.loading ? extraSection.task.error : ""
                        actionText: qsTr("More")
                        onAction: Router.openSource(extraSection.modelData.source)
                        onRetry: extraSection.task = extraSection.service.browse(extraSection.modelData.source, extraSection.modelData.listing, 1)
                        onOpened: (it, cover) => Router.openSeries(it, cover)
                    }
                }
            }
        }
    }

    BackToTop { flick: flick }
    EditPageBar {
        editing: page.editing
        arrangers: [layout]
        canAdd: true
        onDone: page.editing = false
        onAdd: addSection.open()
    }
    AddSectionSheet { id: addSection; arranger: layout }
}
