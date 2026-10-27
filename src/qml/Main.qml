import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Scancore

ApplicationWindow {
    id: window
    width: 1360
    height: 860
    minimumWidth: 360
    minimumHeight: 560
    visible: true
    title: "SCANCORE"
    // Clear only when the window has an alpha channel to show through.
    color: Customize.translucentWindow ? "transparent" : Theme.base
    font.family: Theme.fontSans
    // The default background and palette come from the platform theme (GTK,
    // KDE), which would tint the app with the system's colours. The custom
    // theme may add a picture here.
    background: WindowBackdrop {
        playing: !window.immersiveOpen && window.visibility !== Window.Minimized && window.visibility !== Window.Hidden
    }
    palette {
        window: Theme.base
        windowText: Theme.text
        base: Theme.surface
        alternateBase: Theme.surface2
        text: Theme.text
        button: Theme.surface2
        buttonText: Theme.text
        brightText: Theme.text
        highlight: Theme.accent
        highlightedText: Theme.accentInk
        placeholderText: Theme.textFaint
        toolTipBase: Theme.surface3
        toolTipText: Theme.text
        link: Theme.accent
        mid: Theme.surface3
        dark: Theme.base
        light: Theme.surface3
    }

    readonly property bool immersiveOpen: immersive.depth > 0

    // Settings → Interface: how navigation sits on wide windows. Phones
    // always use the bottom tabs; the Retro mode has its app bar everywhere.
    readonly property string navStyle: Theme.retro ? "retro" : Theme.compact ? "tabs"
        : ["sidebar", "floating", "rail", "dock"].indexOf(App.settings.navStyle) >= 0 ? App.settings.navStyle : "sidebar"
    readonly property string navSide: navStyle === "dock" ? (App.settings.dockSide === "top" ? "top" : "bottom")
        : App.settings.navSide === "right" ? "right" : "left"
    readonly property bool sidebarShown: navStyle === "sidebar" || navStyle === "floating" || navStyle === "rail"
    readonly property int navInset: navStyle === "floating" ? 10 : 0

    // A .sct theme dropped on the window, or opened from Settings.
    property var themeTask: null
    property url pendingTheme
    function importTheme(url) {
        themeTask = Customize.importTheme(url)
    }
    Connections {
        target: window.themeTask
        function onFinished(theme) { Router.applyTheme(theme) }
        function onFailed(error) { shell.toast(error, "error") }
    }


    // Status bar icons for the screen under them: a reader or the player
    // covers the window and may say (systemBarsDark), else they are dark
    // screens; otherwise the app's theme decides, not the phone's mode.
    readonly property bool darkBars: {
        const page = immersive.currentItem
        if (immersive.depth > 0 && page) return page.systemBarsDark !== undefined ? page.systemBarsDark === true : true
        return Theme.dark
    }
    onDarkBarsChanged: App.setBarIcons(darkBars)

    Component.onCompleted: {
        App.setBarIcons(darkBars)
        Theme.compact = Qt.binding(() => window.width < 720)
        Router.shell = shell
        const start = App.startAction
        if (start.size) {
            window.width = start.size[0]
            window.height = start.size[1]
            // "fixed": true keeps that size where a compositor would fill
            // its screen (test runs under cage).
            if (start.fixed) {
                window.minimumWidth = window.maximumWidth = start.size[0]
                window.minimumHeight = window.maximumHeight = start.size[1]
            }
        }
        if (start.screenshot) screenshotTimer.start()
        // The page chosen in Settings → Interface → Start.
        const first = start.section || App.settings.startSection
        if (first && first !== "home") shell.goSection(first)
        if (start.siteCheck) Qt.callLater(() => Router.openSiteCheck(start.siteCheck))
        if (start.toasts) for (const t of start.toasts) shell.toast(t.message, t.kind)
        // {"notify": {title, body, delay}}: one in the notification area and
        // the system's (after `delay` ms, e.g. once the app is in the
        // background).
        if (start.notify) notifyHook.start()
        if (start.palette) Qt.callLater(() => commandPalette.open())
        // {"retroBar": "open" | "more"}: the Retro app bar opened, or its
        // button held.
        if (start.retroBar === "open") retroBar.expanded = true
        if (start.retroBar === "more") retroBar.moreOpen = true
        if (start.shortcuts) Qt.callLater(() => shortcutsSheet.open())
        if (start.push) Qt.callLater(() => shell.push(start.push.path, start.push.props || {}))
        // {"probe": url}: a test object over the window, made here so it
        // outlives any page (tests/app).
        if (start.probe) Qt.callLater(() => {
            const probe = Qt.createComponent(start.probe)
            if (probe.status === Component.Ready) probe.createObject(window.contentItem)
            else console.warn("probe:", probe.errorString())
        })
        if (start.installNovelPack) {
            const task = Extensions.installNovelPack()
            task.finished.connect((r) => console.log("novel pack installed:", JSON.stringify(r)))
            task.failed.connect((e) => console.log("novel pack failed:", e))
        }
        if (start.open) Qt.callLater(() => {
            if (start.autoResume) shell.resume(start.open)
            else Router.openSeries(start.open, null)
        })
    }

    Connections {
        target: Library
        function onUpdatesFound(count, titles) {
            shell.toast(count + qsTr(" new chapter") + (count === 1 ? "" : "s") + qsTr(" in your library"), "success")
        }
    }
    Connections {
        target: Downloads
        function onFinished(title) { shell.toast(qsTr("Downloaded ") + title, "success") }
    }
    // A crash last time: the report, to send.
    CrashReportsDialog { id: crashPrompt }
    Timer { running: true; interval: 2500; onTriggered: crashPrompt.showLatest() }

    // A watch party joined from elsewhere: its anime opens once the host's
    // state arrives; when the host moves to another anime, a toast offers it.
    Connections {
        target: Party
        property string opened: ""
        function onStateReceived() {
            const s = Party.state
            if (!Party.active || Party.hosting || !s.anime || Party.watching === s.anime.anilistId) return
            const key = Party.code + ":" + s.anime.anilistId
            if (key === opened) return
            const first = !opened.startsWith(Party.code + ":")
            opened = key
            if (first) Router.openPartyAnime(s)
            else shell.toast(qsTr("The host moved on to %1").arg(s.anime.title), "info", { label: qsTr("Open"), run: () => Router.openPartyAnime(Party.state) })
        }
    }

    // Another of the account's apps sent where it is (Continue on…).
    Connections {
        target: Live
        function onHandoffReceived(entry, from) {
            shell.toast(qsTr("From %1: %2, %3").arg(from.name).arg(entry.title).arg(entry.unitTitle), "info",
                        { label: qsTr("Continue"), run: () => shell.resume(entry) })
            // Sent on purpose from the other device: bring this one forward.
            if (!window.visible) window.show()
            window.raise()
            window.requestActivate()
        }
    }
    // A file or link given on the command line or by a second launch.
    Connections {
        target: App
        function onOpenRequested(target) {
            const lower = String(target).toLowerCase()
            // The home-screen widget's rows: that series, where it was left.
            if (lower.startsWith("scancore://continue/")) {
                shell.resumeSeries(decodeURIComponent(String(target).slice("scancore://continue/".length)))
                return
            }
            if (lower.endsWith(".sct")) {
                window.pendingTheme = target
                themeConfirm.open()
            } else if (/\.(cbz|zip|cbr|rar|cb7|7z|cbt|pdf|epub|fb2|mobi|azw|azw3|prc)$/.test(lower)) {
                Router.openFile(target)
            } else if (/^[a-z]+:\/\//.test(lower) && !/^https?:/.test(lower) && Extensions.repositoryLink(target).url) {
                // A one-tap "add repository" link from a repository's page.
                Router.addRepository(target)
            }
        }
        // An extension's request is waiting for a check only the reader can pass.
        function onBrowserCheckWanted(url) { Router.openSiteCheck(url) }
        // A browser check has been left; pages waiting on it load again.
        function onSiteChecked(url) {
            shell.toast(qsTr("Browser check done for %1").arg(url.replace(/^https?:\/\//, "").replace(/\/.*$/, "")), "success")
        }
    }

    // Extensions that were installing in the background (adding a repository).
    Connections {
        target: Extensions
        function onInstallsFinished(installed, failed, firstError) {
            if (installed > 0)
                shell.toast(installed === 1 ? qsTr("1 extension installed") : qsTr("%1 extensions installed").arg(installed), "success")
            if (failed > 0)
                shell.toast(failed === 1 ? firstError : qsTr("%1 extensions could not be installed: %2").arg(failed).arg(firstError), "error")
        }
    }

    // On Android a Back that nothing else took can arrive as a close request.
    onClosing: (close) => {
        if (App.platform !== "android") {
            if (App.settings.closeToTray === true) {
                close.accepted = false
                window.hide()
            }
            return
        }
        close.accepted = false
        shell.systemBack()
    }

    // ------------------------------------------------------------------ shell

    QtObject {
        id: shell
        property string section: "home"
        property var visited: ({ home: true })
        // Pages over the current section's first one (Retro's back button).
        readonly property int depth: {
            App.dependOn(section, visited)
            const stack = stackFor(section)
            return stack ? stack.depth : 1
        }
        // After the first page is up, the others are built over several
        // frames instead of in one go, so a first visit (and the press that
        // led there) never freezes the window; slow phones gain most.
        property bool settled: false

        function stackFor(name) {
            for (let i = 0; i < sectionRepeater.count; ++i) {
                const loader = sectionRepeater.itemAt(i)
                if (loader && loader.key === name) return loader.item
            }
            return null
        }

        function goSection(name) {
            // Not a page: the notification area opens over the current one.
            if (name === "notifications") {
                notificationsPanel.open()
                return
            }
            if (immersive.depth > 0) immersive.clear()
            if (name === section) {
                const stack = stackFor(name)
                if (stack && stack.depth > 1) stack.pop(null)
                return
            }
            // A new object so bindings on `visited` see the change.
            const v = Object.assign({}, visited)
            v[name] = true
            visited = v
            section = name
            if (App.profiling) App.profilePlace(name)
            Router.section = name
        }

        function push(path, props, fromCover, coverSource) {
            if (immersive.depth > 0) immersive.clear()
            const stack = stackFor(section)
            if (!stack) return
            if (fromCover && coverSource) hero.launch(fromCover, coverSource)
            stack.push(Qt.resolvedUrl(path), props)
            if (App.profiling) App.profilePlace(path)
        }

        // A TV's remote, for controls that take the D-pad's keys themselves
        // (Select): the focus moves on as from anywhere else.
        function moveFocus(direction) { return spatialFocus.move(direction) }

        // Side by side: `path` with `props` beside the reader that is open.
        readonly property bool canSplit: window.width >= 1100 && immersive.depth > 0
        readonly property var splitItem: splitPane.active ? splitLoader.item : null
        function openSplit(path, props) { splitPane.open(path, props) }
        function closeSplit() { if (splitPane.page != "") splitPane.close() }
        // Which of the two readers the keys go to: the one last clicked or
        // focused (both answering the same shortcut, Qt would run neither).
        property var keyReader: null

        function openImmersive(path, props) {
            immersive.push(Qt.resolvedUrl(path), props)
            if (App.profiling) App.profilePlace(path)
        }

        // Closes the reader or player, or pops the current page. Returns
        // false when there is nothing to go back to.
        function back() {
            if (immersive.depth > 0) {
                // pop() never removes a stack's last item, and a reader is
                // always alone in this one.
                immersive.popCurrentItem()
                return true
            }
            const stack = stackFor(section)
            if (stack && stack.depth > 1) {
                stack.pop()
                return true
            }
            // Retro: every section leads back to the hub.
            if (Theme.retro && section !== "home") {
                goSection("home")
                return true
            }
            return false
        }

        // Back from the system (Android Back, Alt+Left): go back, and on
        // Android then return to Home and finally send the app to the
        // background, as Back does on an app's first screen. Android does not
        // close an app's last window.
        function systemBack() {
            // The welcome: its previous step. On its first step, Back closes
            // it when Settings showed it again, or, on the first run, sends
            // SCANCORE to the background as on any first screen (nothing
            // behind it moves).
            if (onboarding.item) {
                if (!onboarding.item.back()) {
                    if (App.settings.onboarded === true) onboarding.item.finish()
                    else if (App.platform === "android") App.moveToBackground()
                }
                return
            }
            // A reader or the player may have its own steps first (the
            // player: its panel, then full screen).
            const page = immersive.currentItem
            if (immersive.depth > 0 && page && typeof page.handleBack === "function" && page.handleBack()) return
            if (back() || App.platform !== "android") return
            if (section !== "home") goSection("home")
            else App.moveToBackground()
        }

        function addRepository(link) { repositorySheet.start(link) }
        function toast(message, kind, action) { toasts.show(message, kind, action) }

        function land(target) { hero.land(target) }

        // A series by its id where it was left (the widget): the entry
        // from Continue, else its page.
        property var resumeTask: null
        function resumeSeries(id) {
            const task = Library.continueItems("", 40)
            resumeTask = task
            task.finished.connect((list) => {
                const entry = Array.from(list).find(e => e.id === id)
                if (entry) { resume(entry); return }
                const row = Library.seriesRow(id)
                if (row && row.sourceUrl) Router.openSeries(row, null)
            })
        }
        function resume(entry) {
            const props = { url: entry.sourceUrl, summary: entry, autoResume: true }
            if (entry.media === "anime" && Router.extensionAnime(entry.source)) {
                push("pages/AnimeDetails.qml", { sourceUrl: entry.sourceUrl, summary: entry, autoResume: true })
            } else if (entry.media === "anime") {
                push("pages/AnimeDetails.qml", { anilistId: Router.anilistIdOf(entry), summary: entry, autoResume: true })
            } else if (entry.media === "novel") {
                push("pages/NovelDetails.qml", props)
            } else {
                push("pages/ComicDetails.qml", props)
            }
        }

        function openPalette() { commandPalette.open() }

        function openShortcuts() { shortcutsSheet.open() }

        function showWelcome() { onboarding.active = true }
    }

    // A focus scope that holds focus whenever no reader is open, so Escape
    // from any page (or from nowhere in particular) reaches it.
    FocusScope {
        id: shellView
        anchors.fill: parent
        focus: true
        // The parent's safe area, so these margins do not feed back into it.
        anchors.topMargin: parent.SafeArea.margins.top
        anchors.bottomMargin: parent.SafeArea.margins.bottom
        // Phones on their side: clear of a punch hole or notch at the side
        // (the window draws into the cutout, Screen.java).
        anchors.leftMargin: parent.SafeArea.margins.left
        anchors.rightMargin: parent.SafeArea.margins.right
        enabled: !window.immersiveOpen
        opacity: window.immersiveOpen ? 0 : 1
        scale: window.immersiveOpen ? 0.985 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        Behavior on scale { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
        // Disabling drops focus; take it back when the reader closes.
        onEnabledChanged: if (enabled) forceActiveFocus()
        Keys.onEscapePressed: (event) => event.accepted = shell.back()
        // A TV's remote: the D-pad between controls, Select presses.
        Keys.onPressed: (event) => {
            if (!App.tv) return
            const dirs = { [Qt.Key_Left]: "left", [Qt.Key_Right]: "right", [Qt.Key_Up]: "up", [Qt.Key_Down]: "down" }
            if (dirs[event.key] !== undefined) event.accepted = spatialFocus.move(dirs[event.key])
            else if (event.key === Qt.Key_Select || event.key === Qt.Key_Enter || event.key === Qt.Key_Return)
                event.accepted = spatialFocus.activate()
        }

        Shadow {
            target: sidebar
            visible: sidebar.visible && sidebar.floating
            lift: 0.5
        }
        Sidebar {
            id: sidebar
            visible: window.sidebarShown
            style: window.navStyle
            side: window.navSide
            width: visible ? implicitWidth : 0
            x: side === "right" ? parent.width - width - window.navInset : window.navInset
            y: window.navInset
            height: parent.height - 2 * window.navInset
            current: shell.section
            onNavigate: (name) => shell.goSection(name)
            onOpenPalette: commandPalette.open()
        }

        Item {
            id: content
            anchors.left: window.sidebarShown && window.navSide === "left" ? sidebar.right : parent.left
            anchors.right: window.sidebarShown && window.navSide === "right" ? sidebar.left : parent.right
            anchors.top: dock.visible && window.navSide === "top" ? dock.bottom : parent.top
            anchors.bottom: retroBar.visible ? retroBar.top : tabBar.visible ? tabBar.top : dock.visible && window.navSide === "bottom" ? dock.top : parent.bottom
            anchors.topMargin: dock.visible && window.navSide === "top" ? 6 : 0
            anchors.bottomMargin: (dock.visible && window.navSide === "bottom" ? 6 : 0) + readAloudBar.height
            clip: true

            Repeater {
                id: sectionRepeater
                model: ["home", "comics", "novels", "anime", "music", "library", "history", "activity", "downloads", "settings"]
                delegate: Loader {
                    id: sectionLoader
                    required property string modelData
                    readonly property string key: modelData
                    readonly property bool current: shell.section === key
                    anchors.fill: parent
                    active: shell.visited[key] === true
                    asynchronous: shell.settled
                    visible: opacity > 0.001
                    opacity: current ? 1 : 0
                    z: current ? 1 : 0
                    transform: Translate { y: sectionLoader.current ? 0 : 14; Behavior on y { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate } } }
                    Behavior on opacity { NumberAnimation { duration: sectionLoader.current ? Motion.slow : Motion.fast; easing.type: Easing.OutCubic } }

                    // Retro: Home is the hub.
                    sourceComponent: sectionLoader.key === "home" && Theme.retro ? retroHome : sectionStack
                    Component {
                        id: retroHome
                        PageStack { initial: "retro/RetroHub.qml" }
                    }
                    Component {
                        id: sectionStack
                        PageStack {
                            initial: {
                                switch (sectionLoader.key) {
                                case "home": return "pages/HomePage.qml"
                                case "comics": return "pages/ComicsPage.qml"
                                case "novels": return "pages/NovelsPage.qml"
                                case "anime": return "pages/AnimePage.qml"
                                case "music": return "pages/MusicPage.qml"
                                case "library": return "pages/LibraryPage.qml"
                                case "history": return "pages/HistoryPage.qml"
                                case "activity": return "pages/ActivityPage.qml"
                                case "downloads": return "pages/DownloadsPage.qml"
                                default: return "pages/SettingsPage.qml"
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Wide windows with the dock style: a floating bar along the top or bottom.
    Shadow {
        parent: shellView
        target: dock
        visible: dock.visible
        lift: 0.8
        z: 2
    }
    Dock {
        id: dock
        parent: shellView
        visible: window.navStyle === "dock"
        side: window.navSide
        z: 3
        anchors.horizontalCenter: parent.horizontalCenter
        y: side === "top" ? 12 : parent.height - height - 12
        width: Math.min(implicitWidth, parent.width - 24)
        height: implicitHeight
        current: shell.section
        onNavigate: (name) => shell.goSection(name)
        onOpenPalette: commandPalette.open()
    }

    // Reading aloud with the reader closed: the mini player, above the
    // navigation along the bottom.
    ReadAloudBar {
        id: readAloudBar
        parent: shellView
        z: 3
        // Plain positions, not anchors: the bars it sits on are reparented
        // into shellView too, and anchors to them could be resolved first.
        x: window.sidebarShown && window.navSide === "left" ? sidebar.x + sidebar.width : 0
        width: (parent ? parent.width : 0) - (window.sidebarShown ? sidebar.width + window.navInset : 0)
        y: (parent ? parent.height : 0) - height - (retroBar.visible ? retroBar.height : tabBar.visible ? tabBar.height
            : dock.visible && window.navSide === "bottom" ? dock.height + 20 : 0)
    }

    // Retro: the app bar along the bottom, on every device.
    RetroBar {
        id: retroBar
        parent: shellView
        visible: Theme.retro
        z: 3
        height: visible ? implicitHeight : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        section: shell.section
        canGoBack: shell.section !== "home" || shell.depth > 1
        onBack: shell.back()
    }

    // Phone layout: bottom tabs instead of the sidebar.
    TabBar {
        id: tabBar
        parent: shellView
        visible: Theme.compact && !Theme.retro
        height: visible ? 62 : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        current: shell.section
        onNavigate: (name) => shell.goSection(name)
        onOpenPalette: commandPalette.open()
    }

    // Readers and the player cover the whole window.
    ImmersiveStack {
        id: immersive
        // Readers that set edgeToEdge draw under the system bars and pad
        // their own controls; everything else stays inside the safe area.
        readonly property bool edgeToEdge: currentItem !== null && currentItem.edgeToEdge === true
        // The window's own root, not ApplicationWindow's content, which Qt
        // (6.9 and later) pads by the safe area: inside it a reader could not
        // reach under the status bar and was told its inset was 0, so its
        // hidden top bar stuck out above it, over the phone's clock.
        parent: window.contentItem.parent
        anchors.fill: parent
        // The parent's safe area, so these margins do not feed back into it.
        anchors.topMargin: edgeToEdge ? 0 : parent.SafeArea.margins.top
        anchors.bottomMargin: edgeToEdge ? 0 : parent.SafeArea.margins.bottom
        // A side cutout (a phone on its side) is kept clear by every page;
        // the player's picture may reach into it (StackView.view.cutoutLeft).
        anchors.leftMargin: cutoutLeft
        // Side by side: the second reader has the right half.
        anchors.rightMargin: cutoutRight + (splitPane.active ? splitPane.width : 0)
        readonly property real cutoutLeft: parent.SafeArea.margins.left
        readonly property real cutoutRight: parent.SafeArea.margins.right
        visible: depth > 0 || busy
        z: 10
        // Readers handle their own keys, so they need focus as soon as they open.
        onCurrentItemChanged: if (currentItem) currentItem.forceActiveFocus()
        // The reader beside goes with the last one here.
        onDepthChanged: if (depth === 0) shell.closeSplit()
    }

    // Side by side (wide windows): a second reader on the right half, opened
    // from a reader's Side by side button (Router.toSplit); its own close
    // closes only it (Router.leave).
    Item {
        id: splitPane
        parent: window.contentItem.parent
        property url page: ""
        property var props: ({})
        readonly property bool active: page != "" && immersive.depth > 0 && window.width >= 1100
        visible: active
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.rightMargin: immersive.cutoutRight
        width: Math.round((parent.width - immersive.cutoutLeft - immersive.cutoutRight) / 2)
        z: 10
        Rectangle { anchors.fill: parent; color: "#000000" }
        Loader {
            id: splitLoader
            anchors.fill: parent
            anchors.leftMargin: 1
            onLoaded: item.forceActiveFocus()
        }
        // Between the two: a line to tell them apart.
        Rectangle { width: 1; height: parent.height; color: "#2a2a30" }
        function open(path, props) {
            page = Qt.resolvedUrl(path)
            splitLoader.setSource(page, props || {})
        }
        function close() {
            page = ""
            splitLoader.source = ""
            if (immersive.currentItem) immersive.currentItem.forceActiveFocus()
        }
    }

    HeroLayer {
        id: hero
        anchors.fill: parent
        z: 20
    }

    // First-run welcome; Settings can open it again.
    Loader {
        id: onboarding
        anchors.fill: parent
        z: 50
        active: App.settings.onboarded !== true
        sourceComponent: Onboarding {
            onFinished: onboarding.active = false
        }
    }

    // The song playing, over the pages (not over a reader or the player),
    // above the phone's tabs or a bottom dock; the incognito badge sits
    // above it.
    MiniPlayer {
        id: miniPlayer
        parent: shellView
        z: 4
        visible: MusicQueue.active && immersive.depth === 0
        width: Math.min(parent.width - 24, 560)
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - 10 - (retroBar.visible ? retroBar.barHeight : tabBar.visible ? tabBar.height
            : dock.visible && dock.side !== "top" ? parent.height - dock.y : 0)
    }

    // Incognito badge over the pages, at the bottom (readers say it in
    // their own header).
    AbstractButton {
        id: incognitoBadge
        parent: shellView
        z: 4
        visible: App.settings.incognito === true
        anchors.horizontalCenter: parent.horizontalCenter
        // Above the phone's tabs or a bottom dock, clear of page headers.
        y: (miniPlayer.visible ? miniPlayer.y - 8 : parent.height - 12 - (retroBar.visible ? retroBar.barHeight : tabBar.visible ? tabBar.height
            : dock.visible && dock.side !== "top" ? parent.height - dock.y : 0)) - height
        height: 28
        width: badgeRow.implicitWidth + 22
        hoverEnabled: true
        Accessible.name: qsTr("Incognito is on. Turn it off")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Nothing you read or watch is recorded. Click to turn off.")
        onClicked: Router.setIncognito(false)
        background: Rectangle {
            radius: Theme.pill(height)
            color: incognitoBadge.hovered ? Theme.surface3 : Theme.surface2
            border.width: 1
            border.color: Theme.borderStrong
        }
        contentItem: Item {
            Row {
                id: badgeRow
                anchors.centerIn: parent
                spacing: 6
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "venetian-mask"; size: 15; color: Theme.text }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Incognito")
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    SpatialFocus {
        id: spatialFocus
        scope: shellView
    }
    // A TV opens with the focus on the first control.
    Timer {
        running: App.tv
        interval: 1500
        onTriggered: if (!spatialFocus.current || !spatialFocus.isStop(spatialFocus.current)) spatialFocus.move("down")
    }

    CommandPalette {
        id: commandPalette
        z: 30
    }

    LockScreen {}

    ShortcutsSheet {
        id: shortcutsSheet
        z: 30
    }

    UpdateBanner {
        z: 35
    }

    // Drop a .sct theme on the window to use it.
    DropArea {
        anchors.fill: parent
        z: 60
        onEntered: (drag) => drag.accepted = drag.hasUrls && drag.urls.some(u => String(u).toLowerCase().endsWith(".sct"))
        onDropped: (drop) => {
            const url = drop.urls.find(u => String(u).toLowerCase().endsWith(".sct"))
            if (!url) return
            window.pendingTheme = url
            themeConfirm.open()
        }
    }
    ConfirmDialog {
        id: themeConfirm
        heading: qsTr("Use this theme?")
        message: decodeURIComponent(String(window.pendingTheme).replace(/^.*\//, ""))
            + qsTr(" replaces your custom theme and switches SCANCORE to it.")
        confirmText: qsTr("Use theme")
        onConfirmed: window.importTheme(window.pendingTheme)
    }
    AddRepositoryDialog { id: repositorySheet }
    NotificationsPanel { id: notificationsPanel }
    Timer { interval: 1500; running: true; onTriggered: shell.settled = true }
    Timer {
        id: notifyHook
        interval: App.startAction.notify ? (App.startAction.notify.delay || 0) : 0
        onTriggered: Notifications.add("app", App.startAction.notify.title || "Test", App.startAction.notify.body || "", true)
    }

    // Android 13 and later: ask once, after the welcome, to allow
    // notifications (new chapters, announcements, updates).
    Timer {
        interval: 1200
        running: App.platform === "android" && App.settings.onboarded === true && App.settings.notificationsAsked !== true && !onboarding.active
        onTriggered: {
            App.setSetting("notificationsAsked", true)
            Notifications.askPermission()
        }
    }

    // Android: once, after the welcome, ask to be let off battery
    // optimisation, which stops update checks, downloads and reading aloud
    // soon after the app leaves the screen. Settings → Library has it later.
    ConfirmDialog {
        id: batteryAsk
        heading: qsTr("Let SCANCORE run in the background?")
        message: qsTr("Android's battery optimisation closes SCANCORE soon after you leave it, so new chapters are not checked, downloads pause and reading aloud stops. Android will ask you to allow it; you can change it later in Settings → Library.")
        confirmText: qsTr("Allow")
        onConfirmed: App.askBatteryUnrestricted()
    }
    Timer {
        interval: 2500
        running: App.platform === "android" && App.batteryRestricted && App.settings.onboarded === true
            && App.settings.batteryAsked !== true && !onboarding.active
            && App.settings.notificationsAsked === true && Qt.application.state === Qt.ApplicationActive
        onTriggered: {
            App.setSetting("batteryAsked", true)
            batteryAsk.open()
        }
    }
    Toasts {
        id: toasts
        z: 40
        // Above the phone's tab bar, not over it.
        bottomInset: (tabBar.visible ? tabBar.height : retroBar.visible ? retroBar.barHeight : 0) + readAloudBar.height + (incognitoBadge.visible ? incognitoBadge.height + 12 : 0)
    }

    // Development hook: SCANCORE_START {"screenshot": "/path.png", "delay": ms}
    Timer {
        id: screenshotTimer
        interval: App.startAction.delay || 8000
        onTriggered: {
            App.saveWindowImage(window, App.startAction.screenshot)
            Qt.quit()
        }
    }

    // ------------------------------------------------------------------ keys

    // The keys come from Settings → Keyboard (see Keymap).
    Shortcut { sequences: Keymap.sequences("palette"); onActivated: commandPalette.open() }
    Shortcut { sequences: Keymap.sequences("shortcuts"); enabled: App.platform !== "android"; onActivated: shortcutsSheet.open() }
    // StandardKey.Back is also Android's Back key.
    Shortcut { sequences: Keymap.sequences("back").concat([StandardKey.Back]); onActivated: shell.systemBack() }
    // The back button on mice.
    TapHandler {
        acceptedButtons: Qt.BackButton
        // Mice only: touch taps ignore acceptedButtons.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onTapped: shell.back()
    }
    Shortcut {
        sequences: Keymap.sequences("sidebar")
        enabled: window.navStyle === "sidebar" || window.navStyle === "floating"
        onActivated: App.setSetting("sidebarCollapsed", !(App.settings.sidebarCollapsed === true))
    }
    Shortcut { sequences: Keymap.sequences("home"); onActivated: shell.goSection("home") }
    Shortcut { sequences: Keymap.sequences("comics"); onActivated: shell.goSection("comics") }
    Shortcut { sequences: Keymap.sequences("novels"); onActivated: shell.goSection("novels") }
    Shortcut { sequences: Keymap.sequences("anime"); onActivated: shell.goSection("anime") }
    Shortcut { sequences: Keymap.sequences("library"); onActivated: shell.goSection("library") }
    Shortcut { sequences: Keymap.sequences("history"); onActivated: shell.goSection("history") }
    Shortcut { sequences: Keymap.sequences("activity"); onActivated: shell.goSection("activity") }
    Shortcut { sequences: Keymap.sequences("downloads"); onActivated: shell.goSection("downloads") }
    Shortcut { sequences: Keymap.sequences("settings"); onActivated: shell.goSection("settings") }
    Shortcut {
        sequences: Keymap.sequences("fullscreen")
        onActivated: window.visibility === Window.FullScreen ? window.showNormal() : window.showFullScreen()
    }
}
