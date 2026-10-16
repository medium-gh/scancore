import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import ScanCore

ApplicationWindow {
    id: window
    width: 1360
    height: 860
    minimumWidth: 360
    minimumHeight: 560
    visible: true
    title: "SCANCORE"
    color: Theme.bg
    font.family: Theme.fontSans

    readonly property bool immersiveOpen: immersive.depth > 0


    Component.onCompleted: {
        Theme.compact = Qt.binding(() => window.width < 720)
        Router.shell = shell
        const start = App.startAction
        if (start.size) {
            window.width = start.size[0]
            window.height = start.size[1]
        }
        if (start.screenshot) screenshotTimer.start()
        if (start.section) shell.goSection(start.section)
        if (start.siteCheck) Qt.callLater(() => Router.openSiteCheck(start.siteCheck))
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
        target: App
        function onActivateRequested() {
            window.show()
            window.raise()
            window.requestActivate()
        }
    }
    Connections {
        target: Library
        function onUpdatesFound(count, titles) {
            shell.toast(count + " new chapter" + (count === 1 ? "" : "s") + " in your library", "success")
        }
    }
    Connections {
        target: Downloads
        function onFinished(title) { shell.toast("Downloaded " + title, "success") }
    }

    function handleAndroidBack() {
        const stack = shell.stackFor(shell.section)
        if (immersive.depth > 0 || (stack && stack.depth > 1)) {
            shell.back()
            return true
        }
        if (shell.section !== "home") {
            shell.goSection("home")
            return true
        }
        return false
    }

    // Android back: close the reader, pop the page, then return to Home.
    onClosing: (close) => {
        if (App.platform !== "android") {
            if (App.settings.closeToTray === true) {
                close.accepted = false
                window.hide()
            }
            return
        }
        if (handleAndroidBack()) {
            close.accepted = false
        }
    }

    // ------------------------------------------------------------------ shell

    QtObject {
        id: shell
        property string section: "home"
        property var visited: ({ home: true })

        function stackFor(name) {
            for (let i = 0; i < sectionRepeater.count; ++i) {
                const loader = sectionRepeater.itemAt(i)
                if (loader && loader.key === name) return loader.item
            }
            return null
        }

        function goSection(name) {
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
            Router.section = name
        }

        function push(path, props, fromCover, coverSource) {
            if (immersive.depth > 0) immersive.clear()
            const stack = stackFor(section)
            if (!stack) return
            if (fromCover && coverSource) hero.launch(fromCover, coverSource)
            stack.push(Qt.resolvedUrl(path), props)
        }

        function openImmersive(path, props) {
            immersive.push(Qt.resolvedUrl(path), props)
        }

        function back() {
            if (immersive.depth > 0) {
                immersive.clear()
                return
            }
            const stack = stackFor(section)
            if (stack && stack.depth > 1) stack.pop()
        }

        function toast(message, kind) { toasts.show(message, kind) }

        function land(target) { hero.land(target) }

        function resume(entry) {
            const props = { url: entry.sourceUrl, summary: entry, autoResume: true }
            if (entry.media === "anime") {
                const extra = entry.extra || {}
                push("pages/AnimeDetails.qml", { anilistId: extra.anilistId || 0, summary: entry, autoResume: true })
            } else if (entry.media === "novel") {
                push("pages/NovelDetails.qml", props)
            } else {
                push("pages/ComicDetails.qml", props)
            }
        }

        function openPalette() { palette.open() }

        function showWelcome() { onboarding.active = true }
    }

    Item {
        id: shellView
        anchors.fill: parent
        // The parent's safe area, so these margins do not feed back into it.
        anchors.topMargin: parent.SafeArea.margins.top
        anchors.bottomMargin: parent.SafeArea.margins.bottom
        enabled: !window.immersiveOpen
        opacity: window.immersiveOpen ? 0 : 1
        scale: window.immersiveOpen ? 0.985 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        Behavior on scale { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }

        Sidebar {
            id: sidebar
            visible: !Theme.compact
            width: Theme.compact ? 0 : implicitWidth
            height: parent.height
            current: shell.section
            onNavigate: (name) => shell.goSection(name)
            onOpenPalette: palette.open()
        }

        Item {
            id: content
            anchors.left: Theme.compact ? parent.left : sidebar.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: Theme.compact ? tabBar.top : parent.bottom
            clip: true

            Repeater {
                id: sectionRepeater
                model: ["home", "comics", "novels", "anime", "library", "history", "downloads", "settings"]
                delegate: Loader {
                    id: sectionLoader
                    required property string modelData
                    readonly property string key: modelData
                    readonly property bool current: shell.section === key
                    anchors.fill: parent
                    active: shell.visited[key] === true
                    visible: opacity > 0.001
                    opacity: current ? 1 : 0
                    z: current ? 1 : 0
                    transform: Translate { y: sectionLoader.current ? 0 : 14; Behavior on y { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate } } }
                    Behavior on opacity { NumberAnimation { duration: sectionLoader.current ? Motion.slow : Motion.fast; easing.type: Easing.OutCubic } }

                    sourceComponent: PageStack {
                        initial: {
                            switch (sectionLoader.key) {
                            case "home": return "pages/HomePage.qml"
                            case "comics": return "pages/ComicsPage.qml"
                            case "novels": return "pages/NovelsPage.qml"
                            case "anime": return "pages/AnimePage.qml"
                            case "library": return "pages/LibraryPage.qml"
                            case "history": return "pages/HistoryPage.qml"
                            case "downloads": return "pages/DownloadsPage.qml"
                            default: return "pages/SettingsPage.qml"
                            }
                        }
                    }
                }
            }
        }
    }

    // Phone layout: bottom tabs instead of the sidebar.
    TabBar {
        id: tabBar
        parent: shellView
        visible: Theme.compact
        height: Theme.compact ? 62 : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        current: shell.section
        onNavigate: (name) => shell.goSection(name)
        onOpenPalette: palette.open()
    }

    // Readers and the player cover the whole window.
    StackView {
        id: immersive
        anchors.fill: parent
        // The parent's safe area, so these margins do not feed back into it.
        anchors.topMargin: parent.SafeArea.margins.top
        anchors.bottomMargin: parent.SafeArea.margins.bottom
        visible: depth > 0 || busy
        z: 10
        pushEnter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.slow; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: 1.03; to: 1; duration: Motion.page; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
            }
        }
        pushExit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast } }
        popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal } }
        popExit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: Motion.normal }
                NumberAnimation { property: "scale"; to: 1.02; duration: Motion.normal }
            }
        }
        replaceEnter: pushEnter
        replaceExit: pushExit
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

    CommandPalette {
        id: palette
        z: 30
    }

    UpdateBanner {
        z: 35
    }
    Toasts {
        id: toasts
        z: 40
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

    Shortcut { sequences: ["Ctrl+K", "Ctrl+P"]; onActivated: palette.open() }
    Shortcut { sequences: ["Alt+Left", StandardKey.Back]; onActivated: shell.back() }
    Shortcut { sequence: "Ctrl+B"; onActivated: App.setSetting("sidebarCollapsed", !(App.settings.sidebarCollapsed === true)) }
    Shortcut { sequence: "Ctrl+1"; onActivated: shell.goSection("home") }
    Shortcut { sequence: "Ctrl+2"; onActivated: shell.goSection("comics") }
    Shortcut { sequence: "Ctrl+3"; onActivated: shell.goSection("novels") }
    Shortcut { sequence: "Ctrl+4"; onActivated: shell.goSection("anime") }
    Shortcut { sequence: "Ctrl+5"; onActivated: shell.goSection("library") }
    Shortcut { sequence: "Ctrl+,"; onActivated: shell.goSection("settings") }
    Shortcut {
        sequence: "F11"
        onActivated: window.visibility === Window.FullScreen ? window.showNormal() : window.showFullScreen()
    }
}
