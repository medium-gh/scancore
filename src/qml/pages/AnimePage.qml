import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Basic
import Scancore

// Anime home: rotating spotlight, continue watching, today's schedule and
// the anime catalogue's lists: AniList's trending, seasonal, popular and
// top, or MyAnimeList's top airing, seasonal, popular and top rated.
Item {
    id: page
    // Assigned, not bound: the catalogue's change handler can run while a
    // binding here is still being evaluated, and the task the binding then
    // kept was destroyed, leaving the spotlight and the lists empty.
    property var homeTask: null
    // Settings → Connections: whose lists these are.
    readonly property string catalog: App.settings.catalogAnime || "anilist"
    onCatalogChanged: homeTask = Anime.home()
    Component.onCompleted: if (!homeTask) homeTask = Anime.home()
    readonly property bool mal: !!home && home.catalog === "mal"
    property var historyTask: Library.continueItems("anime", 16)
    property var todayTask: Anime.schedule(Math.floor(new Date().setHours(0, 0, 0, 0) / 1000), Math.floor(new Date().setHours(23, 59, 59, 999) / 1000))
    readonly property var home: homeTask && homeTask.ok ? homeTask.result : null
    readonly property var spotlight: home ? (home.spotlight || home.trending).filter(m => m.banner).slice(0, 6) : []
    // Installed anime sources (extensions), each browsable by itself.
    readonly property var sources: { App.dependOn(Extensions.revision); return Anime.sources().filter(s => s.enabled) }
    property int spot: 0

    // Edit page: arrange the sections (PageLayout).
    property bool editing: false
    PageArranger {
        id: layout
        page: "anime"
        column: column
        editing: page.editing
        defaults: ["spotlight", "continue", "sources", "airing", "trending", "season", "popular", "top"]
    }
    // Over the spotlight when it is the first section shown; else a row.
    readonly property bool toolsOnSpotlight: !editing && (PageLayout.revision, PageLayout.order("anime", layout.declared)
        .filter(k => !PageLayout.isHidden("anime", k))[0] === "spotlight")

    // Browse, Schedule, Extensions, Edit page and search (phones get a
    // search button instead).
    component AnimeTools: Item {
        width: parent ? parent.width : 800
        height: Math.max(80, buttons.y + buttons.height + 8)
        SearchField {
            visible: !Theme.compact
            anchors.right: parent.right
            anchors.rightMargin: Theme.gutter
            y: 24
            width: 300
            placeholderText: qsTr("Search anime")
            onSubmitted: (text) => Router.openAnimeBrowse({ q: text })
        }
        // Wraps on phones rather than running off the screen.
        Flow {
            id: buttons
            x: Theme.gutter
            y: 24
            width: parent.width - 2 * Theme.gutter - (Theme.compact ? 0 : 330)
            spacing: 8
            ScButton { text: qsTr("Browse"); iconName: "compass"; compact: true; onClicked: Router.openAnimeBrowse({}) }
            ScButton { text: qsTr("Schedule"); iconName: "calendar"; compact: true; onClicked: Router.openAnimeSchedule() }
            ScButton { visible: Theme.compact; text: qsTr("Search"); iconName: "search"; compact: true; onClicked: Router.openAnimeBrowse({}) }
            ScButton { text: qsTr("Extensions"); iconName: "puzzle"; compact: true; onClicked: Router.openExtensions() }
            ScButton {
                visible: Account.signedIn
                text: Party.active ? qsTr("Party %1").arg(Party.code) : qsTr("Watch party")
                iconName: "users"
                compact: true
                onClicked: partyPanel.open()
            }
            IconButton {
                iconName: "layout-dashboard"
                tip: qsTr("Edit page")
                active: page.editing
                onClicked: page.editing = !page.editing
            }
        }
    }

    function search(text) { Router.openAnimeBrowse({ q: text }) }

    // Library changes come every couple of seconds while reading (progress):
    // a page out of sight (another section, under a reader or the player)
    // only notes them and reloads once it shows again.
    property bool libraryStale: false
    onVisibleChanged: if (visible && libraryStale) { libraryStale = false; refresh.restart() }
    Connections {
        target: Library
        function onChanged() { if (page.visible) refresh.restart(); else page.libraryStale = true }
    }
    Timer { id: refresh; interval: 500; onTriggered: page.historyTask = Library.continueItems("anime", 16) }
    property var history: []
    Connections {
        target: page.historyTask
        function onFinished() { page.history = Router.keepTask(page.history, page.historyTask) }
    }

    // Slide timing: the active dot fills as it runs; hovering the spotlight
    // pauses it.
    property real slideProgress: 0
    function showSlide(i) {
        const n = spotlight.length
        if (n === 0) return
        spot = (i + n) % n
        slideTimer.restart()
    }
    NumberAnimation {
        id: slideTimer
        target: page
        property: "slideProgress"
        from: 0
        to: 1
        duration: 7000
        running: page.spotlight.length > 1 && page.visible && !Motion.reduced
        paused: running && (heroHover.hovered || swipe.active)
        onFinished: page.showSlide(page.spot + 1)
    }

    SmoothFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 60

        Column {
            id: column
            width: parent.width
            spacing: Theme.sectionGap

            AnimeTools { visible: !page.toolsOnSpotlight }

            // ------------------------------------------------ spotlight
            PageSection {
                arranger: layout
                key: "spotlight"
                label: qsTr("Spotlight")
            Item {
                id: hero
                width: parent.width
                height: Math.min(520, Math.max(380, page.height * 0.56))
                clip: true

                HoverHandler { id: heroHover; enabled: !Theme.touch }
                // Swipe (or drag) sideways for the next or previous title, as
                // in most apps: the slide follows the finger, a long drag or a
                // quick flick changes it, and a sideways swipe wins over the
                // page's scrolling even when the finger drifts a little.
                DragHandler {
                    id: swipe
                    target: null
                    yAxis.enabled: false
                    dragThreshold: 12
                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                    // The last pull while the finger was down, and its speed.
                    property real lastX: 0
                    property double lastTime: 0
                    property double startTime: 0
                    property real velocity: 0
                    onTranslationChanged: {
                        if (active) {
                            settle.stop()
                            hero.dragX = translation.x
                        }
                        const now = Date.now()
                        if (lastTime > 0) velocity = (translation.x - lastX) * 1000 / Math.max(1, now - lastTime)
                        lastX = translation.x
                        lastTime = now
                    }
                    onActiveChanged: {
                        if (active) {
                            lastX = 0
                            lastTime = 0
                            velocity = 0
                            startTime = Date.now()
                            return
                        }
                        // Not translation: Qt has reset it by now (0 on
                        // release, which is why swiping never changed slides).
                        const dx = lastX
                        // A quick flick may bring only one or two moves, so
                        // a short swipe done fast counts as well.
                        const quick = Math.abs(dx) >= 30 && Date.now() - startTime < 250
                        const flick = quick || (Math.abs(dx) > 20 && Math.abs(velocity) > 500 && (velocity < 0) === (dx < 0))
                        if (Math.abs(dx) >= 60 || flick) {
                            // The next title comes in from the side swiped from.
                            hero.dragX = (dx < 0 ? 1 : -1) * Math.min(120, hero.width * 0.25)
                            page.showSlide(page.spot + (dx < 0 ? 1 : -1))
                        }
                        settle.restart()
                    }
                }
                // How far the finger has pulled the slide; settles back to 0.
                property real dragX: 0
                NumberAnimation {
                    id: settle
                    target: hero
                    property: "dragX"
                    to: 0
                    duration: Motion.slow
                    easing.type: Easing.OutCubic
                }

                // Over a background picture or a see-through window there is no
                // page colour to fade into; the slides fade out themselves.
                Rectangle {
                    id: heroFade
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true
                    gradient: Gradient {
                        GradientStop { position: 0.5; color: "white" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }
                Item {
                    anchors.fill: parent
                    // Glass: over the wallpaper, so the same.
                    layer.enabled: Theme.layered || Theme.glass
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: heroFade
                        // A soft mask (by default MultiEffect cuts a mask to
                        // a hard edge, and nothing faded).
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1
                    }
                    // The pictures follow the finger under shades that stay
                    // put (moving the shades with them uncovered the slowly
                    // zooming banner's edge), a little wider than the
                    // spotlight and never moved further than that, so no
                    // edge of them shows either.
                    Item {
                        anchors.fill: parent
                        anchors.leftMargin: -hero.width * 0.06
                        anchors.rightMargin: -hero.width * 0.06
                        transform: Translate { x: Math.max(-hero.width * 0.06, Math.min(hero.width * 0.06, hero.dragX * 0.35)) }
                        opacity: 1 - Math.min(0.35, Math.abs(hero.dragX) / Math.max(1, hero.width))
                        Repeater {
                            model: page.spotlight
                            delegate: Item {
                                id: slide
                                required property var modelData
                                required property int index
                                readonly property bool current: index === page.spot
                                anchors.fill: parent
                                opacity: current ? 1 : 0
                                visible: opacity > 0
                                Behavior on opacity { NumberAnimation { duration: 900; easing.type: Easing.InOutQuad } }
                                Image {
                                    id: banner
                                    anchors.fill: parent
                                    source: App.image(slide.modelData.banner)
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: 1920
                                    scale: slide.current ? 1.08 : 1.0
                                    Behavior on scale { NumberAnimation { duration: 8000; easing.type: Easing.Linear } }
                                }
                            }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Theme.alpha(Theme.base, 0.95) }
                            GradientStop { position: 0.55; color: Theme.alpha(Theme.base, 0.45) }
                            GradientStop { position: 1.0; color: Theme.alpha(Theme.base, 0.1) }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: !Theme.glass
                        gradient: Gradient {
                            GradientStop { position: 0.55; color: "transparent" }
                            GradientStop { position: 1.0; color: Theme.bg }
                        }
                    }
                }

                Column {
                    id: heroText
                    readonly property var media: page.spotlight[page.spot] || null
                    x: Theme.gutter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 48
                    width: Math.min(620, parent.width - 2 * Theme.gutter)
                    spacing: 14
                    visible: media !== null

                    // Re-run the entrance on each slide change.
                    onMediaChanged: textReveal.restart()
                    ParallelAnimation {
                        id: textReveal
                        NumberAnimation { target: heroText; property: "opacity"; from: 0; to: 1; duration: Motion.page; easing.type: Easing.OutCubic }
                        NumberAnimation { target: heroShift; property: "x"; from: -20; to: 0; duration: Motion.page; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
                    }
                    transform: [Translate { id: heroShift }, Translate { x: hero.dragX }]

                    Text {
                        text: "#" + (page.spot + 1) + qsTr(" trending")
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs
                        font.weight: Font.Bold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1.6
                    }
                    Text {
                        width: parent.width
                        text: heroText.media ? heroText.media.title : ""
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: 44
                        font.weight: Font.Bold
                        font.letterSpacing: -1
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        lineHeight: 1.0
                    }
                    Text {
                        text: heroText.media ? [heroText.media.format, heroText.media.year, heroText.media.episodes ? heroText.media.episodes + qsTr(" episodes") : "", heroText.media.score ? heroText.media.score + "%" : "", (heroText.media.genres || []).slice(0, 3).join(", ")].filter(Boolean).join("  ·  ") : ""
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                    }
                    Row {
                        spacing: 10
                        ScButton {
                            variant: "primary"
                            text: qsTr("Watch now")
                            iconName: "play"
                            onClicked: Router.openSeries(heroText.media, null)
                        }
                        ScButton {
                            text: heroText.media && Library.revision >= 0 && Library.isSaved(heroText.media.id) ? qsTr("In library") : qsTr("Add to library")
                            iconName: "bookmark"
                            onClicked: {
                                if (Library.isSaved(heroText.media.id)) Library.remove(heroText.media.id)
                                else Library.save(Anime.seriesFor(heroText.media))
                            }
                        }
                    }
                }

                // Phones: below the buttons, where nothing else sits.
                Row {
                    x: Theme.compact ? Theme.gutter : parent.width - width - Theme.gutter
                    y: parent.height - height - (Theme.compact ? 18 : 56)
                    spacing: 8
                    Repeater {
                        model: page.spotlight.length
                        Rectangle {
                            id: dot
                            required property int index
                            readonly property bool active: index === page.spot
                            width: active ? 28 : 8
                            height: 8
                            radius: 4
                            color: Theme.alpha(Theme.text, active ? 0.35 : 0.3)
                            clip: true
                            Behavior on width { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
                            // Fills while the slide shows; solid when it does not advance.
                            Rectangle {
                                visible: dot.active
                                height: parent.height
                                radius: 4
                                width: slideTimer.running ? Math.max(8, parent.width * page.slideProgress) : parent.width
                                color: Theme.text
                            }
                            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: page.showSlide(index) }
                        }
                    }
                }

                // The page's buttons sit over the spotlight when it is at the top.
                AnimeTools { visible: page.toolsOnSpotlight }

                StateView {
                    anchors.fill: parent
                    task: page.homeTask
                    onRetry: page.homeTask = Anime.home()
                }
            }
            }

            PageSection {
                arranger: layout
                key: "continue"
                label: qsTr("Continue watching")
                Shelf {
                    title: qsTr("Continue watching")
                    items: page.history
                    subtitleFor: (it) => qsTr("Episode ") + (it.unitNumber || it.unitKey || "")
                    progressFor: (it) => it.completed ? 1 : (it.percent || 0)
                    onOpened: (it, cover) => Router.resume(it)
                }
            }
            PageSection {
                arranger: layout
                key: "sources"
                label: qsTr("Your anime sources")
                Column {
                    readonly property bool hasContent: page.sources.length > 0
                    visible: hasContent
                    x: Theme.gutter
                    width: parent.width - 2 * Theme.gutter
                    spacing: 12
                    Text {
                        text: qsTr("Your anime sources")
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.textXl
                        font.weight: Font.DemiBold
                    }
                    // A couple of dozen until asked: extensions can add hundreds.
                    Flow {
                        id: animeSourceChips
                        property bool all: false
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: animeSourceChips.all ? page.sources : page.sources.slice(0, 24)
                            Chip {
                                required property var modelData
                                text: modelData.label + (modelData.language && modelData.language !== "en" ? " · " + modelData.language.toUpperCase() : "")
                                onClicked: Router.openSource(modelData.key)
                            }
                        }
                        Chip {
                            visible: page.sources.length > 24
                            text: animeSourceChips.all ? qsTr("Show fewer") : qsTr("All %1").arg(page.sources.length)
                            onClicked: animeSourceChips.all = !animeSourceChips.all
                        }
                    }
                }
            }
            PageSection {
                arranger: layout
                key: "airing"
                label: qsTr("Airing today")
                Shelf {
                    title: qsTr("Airing today")
                    actionText: qsTr("Full schedule")
                    onAction: Router.openAnimeSchedule()
                    items: page.todayTask && page.todayTask.ok ? page.todayTask.result : []
                    loading: page.todayTask ? page.todayTask.loading : false
                    subtitleFor: (it) => (it.airingEpisode ? qsTr("Ep ") + it.airingEpisode + (it.airingEpisodeLast ? "–" + it.airingEpisodeLast : "") + " · " : "")
                                         + Qt.formatTime(new Date(it.airingAt * 1000), "hh:mm")
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
            }
            PageSection {
                arranger: layout
                key: "trending"
                label: page.mal ? qsTr("Top airing") : qsTr("Trending now")
                Shelf {
                    title: page.mal ? qsTr("Top airing") : qsTr("Trending now")
                    items: page.home ? page.home.trending : []
                    loading: page.homeTask ? page.homeTask.loading : false
                    subtitleFor: (it) => [it.format, it.year].filter(Boolean).join(" · ")
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
            }
            PageSection {
                arranger: layout
                key: "season"
                label: qsTr("This season")
                Shelf {
                    title: page.home ? page.home.seasonName : qsTr("This season")
                    items: page.home ? page.home.season : []
                    loading: page.homeTask ? page.homeTask.loading : false
                    subtitleFor: (it) => it.nextEpisode ? qsTr("Ep ") + it.nextEpisode + " " + App.relativeTime(it.nextAiringAt) : [it.format, it.status].filter(Boolean).join(" · ")
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
            }
            PageSection {
                arranger: layout
                key: "popular"
                label: page.mal ? qsTr("Most popular") : qsTr("All-time popular")
                Shelf {
                    title: page.mal ? qsTr("Most popular") : qsTr("All-time popular")
                    items: page.home ? page.home.popular : []
                    subtitleFor: (it) => [it.format, it.year].filter(Boolean).join(" · ")
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
            }
            PageSection {
                arranger: layout
                key: "top"
                label: qsTr("Top rated")
                Shelf {
                    title: qsTr("Top rated")
                    items: page.home ? page.home.top : []
                    badgeFor: (it) => it.score ? it.score + "%" : ""
                    subtitleFor: (it) => [it.format, it.year].filter(Boolean).join(" · ")
                    onOpened: (it, cover) => Router.openSeries(it, cover)
                }
            }
        }
    }

    BackToTop { flick: flick }
    EditPageBar {
        editing: page.editing
        arrangers: [layout]
        onDone: page.editing = false
    }

    PartyPanel { id: partyPanel }
}
