import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import ScanCore

// Anime player. ReAnime streams are the provider's own player page, shown in
// an embedded browser; ScanCore reads its position for resume, auto-next and
// AniSkip intro skipping.
//
// Desktop: the video fills the page and the top bar and episode list float
// over it, fading out after a few seconds without mouse movement.
// Phone: the system WebView is a native view drawn above everything Qt
// renders, so nothing can overlap it. Portrait puts the controls and
// episodes below a 16:9 player; landscape goes full screen with a slim rail.
Rectangle {
    id: watch
    property var media: ({})
    property var episodes: []
    property int index: 0

    readonly property var episode: episodes[index] || ({})
    readonly property bool hasPrev: index > 0
    readonly property bool hasNext: index < episodes.length - 1
    readonly property var series: Anime.seriesFor(media)
    property var streamsTask: null
    property var skipTask: null
    property string currentUrl: ""
    property real position: 0
    property real duration: 0
    property real resumeAt: 0
    property bool sideOpen: false
    property bool ended: false

    readonly property bool phone: App.platform === "android"
    readonly property bool landscape: width > height
    readonly property bool fullScreen: Window.window !== null && Window.window.visibility === Window.FullScreen

    // Desktop chrome auto-hide.
    property bool idle: false
    readonly property bool chromeShown: !idle || !player.active

    readonly property var streams: streamsTask && streamsTask.ok ? streamsTask.result.streams : []
    readonly property var skips: skipTask && skipTask.ok ? skipTask.result : []
    readonly property var activeSkip: {
        for (const s of skips)
            if (position >= s.start && position < s.end - 1) return s
        return null
    }
    readonly property string skipLabel: activeSkip ? (activeSkip.type.indexOf("ed") >= 0 ? "Skip ending" : activeSkip.type === "recap" ? "Skip recap" : "Skip intro") : ""
    readonly property string episodeLine: "Episode " + (episode.number || "") + (episode.title && !String(episode.title).startsWith("Episode") ? " · " + episode.title : "")

    color: "#000000"
    focus: true

    function load(i) {
        saveProgress(true)
        nextCountdown.stop()
        index = i
        position = 0
        duration = 0
        ended = false
        skipTask = null
        const p = Library.progressMap(series.id)[episodes[i].key]
        resumeAt = p && !p.completed && p.position ? (p.position.seconds || 0) : 0
        currentUrl = ""
        streamsTask = Anime.streams(media.anilistId, episodes[i].number)
        if (phone && !landscape) phoneList.positionViewAtIndex(i, ListView.Contain)
    }

    Component.onCompleted: {
        load(index)
        if (phone && landscape) Window.window.showFullScreen()
        wake()
    }
    Component.onDestruction: {
        saveProgress(true)
        if (phone && fullScreen) Window.window.showNormal()
    }

    // Phones: rotating to landscape is the way into full screen.
    onLandscapeChanged: {
        if (!phone) return
        if (landscape) Window.window.showFullScreen()
        else Window.window.showNormal()
    }

    Connections {
        target: watch.streamsTask
        function onFinished(result) {
            if (result.streams.length > 0) watch.currentUrl = result.streams[0].url
        }
    }

    property real lastSaved: 0
    function saveProgress(force) {
        if (!series.id || !episode.key || duration <= 0) return
        if (!force && Date.now() - lastSaved < 5000) return
        lastSaved = Date.now()
        const percent = Math.min(1, position / duration)
        Library.recordProgress(series, { key: episode.key, title: episode.title, number: String(episode.number) },
                               { type: "video", seconds: position, duration: duration }, percent, percent > 0.9)
    }

    function onPlayerTime(seconds, total, status) {
        if (total > 0 && duration === 0) {
            duration = total
            skipTask = Anime.skipTimes(media.anilistId, episode.number, Math.round(total))
            if (resumeAt > 5 && resumeAt < total - 30 && seconds < resumeAt - 5) {
                player.seek(resumeAt)
                Router.toast("Resumed at " + formatTime(resumeAt))
            }
        }
        if (total > 0) duration = total
        position = seconds
        saveProgress(false)
        if (activeSkip && App.settings.autoSkip === true && (activeSkip.type === "op" || activeSkip.type === "mixed-op" || activeSkip.type === "recap"))
            player.seek(activeSkip.end)
        if ((status === "Ended" || (total > 0 && seconds >= total - 1)) && !ended) {
            ended = true
            saveProgress(true)
            if (App.settings.autoNext !== false && hasNext) {
                nextCountdown.start()
                wake()
            }
        }
    }

    function formatTime(s) {
        s = Math.floor(s)
        const m = Math.floor(s / 60)
        return m + ":" + String(s % 60).padStart(2, "0")
    }

    function toggleFullScreen() {
        if (fullScreen) Window.window.showNormal()
        else Window.window.showFullScreen()
    }

    function wake() {
        idle = false
        idleTimer.restart()
    }

    function exit() {
        saveProgress(true)
        if (fullScreen) Window.window.showNormal()
        Router.back()
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            if (fullScreen) Window.window.showNormal()
            else exit()
            event.accepted = true
        } else if (event.key === Qt.Key_N && hasNext) { load(index + 1); event.accepted = true }
        else if (event.key === Qt.Key_P && hasPrev) { load(index - 1); event.accepted = true }
    }
    // The embedded browser keeps keyboard focus while the video plays.
    Shortcut { sequence: "F"; enabled: !watch.phone; onActivated: watch.toggleFullScreen() }
    Shortcut { sequence: "Esc"; enabled: watch.fullScreen && !watch.phone; onActivated: Window.window.showNormal() }

    // Any mouse movement, including over the video, brings the chrome back.
    // Qt Quick also sends a hover at the same position on every repainted
    // frame, so only a pointer that actually moved counts.
    property point lastPointer: Qt.point(-1, -1)
    HoverHandler {
        enabled: !watch.phone
        onPointChanged: {
            const p = point.scenePosition
            if (Math.abs(p.x - watch.lastPointer.x) + Math.abs(p.y - watch.lastPointer.y) < 3) return
            watch.lastPointer = p
            watch.wake()
        }
        onHoveredChanged: watch.wake()
    }
    Timer {
        id: idleTimer
        interval: 2500
        onTriggered: {
            if (barHover.hovered || sideHover.hovered || nextCountdown.running) restart()
            else watch.idle = true
        }
    }

    // ---------------------------------------------------------------- player
    Item {
        id: stage
        x: 0
        y: watch.phone && !watch.landscape ? bar.height : 0
        width: watch.phone && watch.landscape ? watch.width - rail.width : watch.width
        height: watch.phone && !watch.landscape ? Math.round(Math.min(watch.width * 9 / 16, watch.height * 0.45)) : watch.height

        Loader {
            id: player
            anchors.fill: parent
            active: App.webEngine && watch.currentUrl.length > 0
            source: watch.phone ? "../components/WebPlayerMobile.qml" : "../components/WebPlayer.qml"
            onLoaded: item.url = Qt.binding(() => watch.currentUrl)
            function seek(seconds) { if (item) item.seek(seconds) }
            Connections {
                target: player.item
                function onTime(seconds, total, status) { watch.onPlayerTime(seconds, total, status) }
                function onInteracted() { watch.wake() }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 14
            visible: !player.active
            Spinner {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: watch.streamsTask !== null && watch.streamsTask.loading
                size: 30
                color: "#a1a1aa"
            }
            Text {
                width: Math.min(440, stage.width - 32)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: text.length > 0
                text: !App.webEngine ? "This build of SCANCORE has no embedded browser, which the ReAnime player needs."
                    : watch.streamsTask && !watch.streamsTask.loading
                      ? (watch.streamsTask.ok ? (watch.streamsTask.result.message || "") : watch.streamsTask.error) : ""
                color: "#d4d4d8"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
            }
            ScButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: watch.streamsTask !== null && !watch.streamsTask.loading && watch.streams.length === 0
                text: "Try again"
                iconName: "refresh-cw"
                onClicked: watch.load(watch.index)
            }
        }

        // Desktop only: on phones these would sit under the native WebView.
        ScButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 40
            anchors.bottomMargin: 80
            visible: !watch.phone && watch.activeSkip !== null && !(App.settings.autoSkip === true)
            variant: "primary"
            iconName: "skip-forward"
            text: watch.skipLabel
            onClicked: player.seek(watch.activeSkip.end)
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 40
            width: 320
            height: 96
            radius: Theme.radiusLg
            color: "#141416"
            border.width: 1
            border.color: "#2a2a30"
            visible: !watch.phone && nextCountdown.running
            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Text {
                    text: "Next episode in " + Math.ceil(nextCountdown.remaining) + "s"
                    color: "white"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                }
                Row {
                    spacing: 8
                    ScButton { compact: true; variant: "primary"; text: "Play now"; onClicked: { nextCountdown.stop(); watch.load(watch.index + 1) } }
                    ScButton { compact: true; text: "Cancel"; onClicked: nextCountdown.stop() }
                }
            }
        }
        Timer {
            id: nextCountdown
            property real remaining: 8
            interval: 100
            repeat: true
            onRunningChanged: if (running) remaining = 8
            onTriggered: {
                remaining -= 0.1
                if (remaining <= 0) {
                    stop()
                    if (watch.hasNext) watch.load(watch.index + 1)
                }
            }
        }
    }

    // ---------------------------------------------------------------- top bar
    // Desktop: floats over the video and fades with the rest of the chrome.
    // Phone portrait: a plain header above the player. Hidden in landscape.
    Rectangle {
        id: bar
        width: parent.width
        height: watch.phone ? 52 : 72
        visible: watch.phone ? !watch.landscape : opacity > 0.01
        opacity: watch.phone || watch.chromeShown ? 1 : 0
        Behavior on opacity { enabled: !watch.phone; NumberAnimation { duration: Motion.slow } }
        color: watch.phone ? "#0b0b0c" : "transparent"
        gradient: watch.phone ? null : barShade
        Gradient {
            id: barShade
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.85) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        HoverHandler { id: barHover }

        IconButton {
            id: backButton
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            iconName: "arrow-left"
            iconColor: "white"
            tip: "Close player (Esc)"
            onClicked: watch.exit()
        }
        Column {
            anchors.left: backButton.right
            anchors.leftMargin: 10
            anchors.right: watch.phone ? parent.right : controls.left
            anchors.rightMargin: watch.phone ? 16 : 12
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: watch.media.title || ""
                color: "white"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: !watch.phone
                text: watch.episodeLine
                color: "#a1a1aa"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
        }
        Row {
            id: controls
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            visible: !watch.phone
            Repeater {
                model: watch.streams
                Chip {
                    required property var modelData
                    text: modelData.quality
                    selected: watch.currentUrl === modelData.url
                    onClicked: watch.currentUrl = modelData.url
                }
            }
            Item { width: 8; height: 1 }
            IconButton { iconName: "skip-back"; iconColor: "white"; enabled: watch.hasPrev; tip: "Previous episode (P)"; onClicked: watch.load(watch.index - 1) }
            IconButton { iconName: "skip-forward"; iconColor: "white"; enabled: watch.hasNext; tip: "Next episode (N)"; onClicked: watch.load(watch.index + 1) }
            IconButton { iconName: "list"; iconColor: "white"; active: watch.sideOpen; tip: "Episodes"; onClicked: watch.sideOpen = !watch.sideOpen }
            IconButton {
                iconName: watch.fullScreen ? "minimize" : "maximize"
                iconColor: "white"
                tip: watch.fullScreen ? "Exit full screen (F)" : "Full screen (F)"
                onClicked: watch.toggleFullScreen()
            }
        }
    }

    // ---------------------------------------------------------------- episodes
    component EpisodeRow: AbstractButton {
        id: row
        required property var modelData
        required property int index
        readonly property var state: { Library.revision; return Library.progressMap(watch.series.id)[modelData.key] || null }
        height: 64
        hoverEnabled: true
        onClicked: watch.load(index)
        background: Rectangle {
            radius: Theme.radiusMd
            color: row.index === watch.index ? "#222226" : row.hovered ? "#18181b" : "transparent"
        }
        contentItem: Row {
            spacing: 10
            leftPadding: 6
            Item {
                width: 92
                height: 52
                anchors.verticalCenter: parent.verticalCenter
                Cover { anchors.fill: parent; radius: 6; source: row.modelData.image || ""; title: "EP " + row.modelData.number }
                Rectangle {
                    visible: row.state !== null
                    anchors.bottom: parent.bottom
                    width: parent.width * (row.state ? (row.state.completed ? 1 : row.state.percent) : 0)
                    height: 3
                    color: "white"
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: row.width - 92 - 10 - 6 - 8
                Text {
                    width: parent.width
                    text: "Episode " + row.modelData.number
                    color: row.index === watch.index ? "white" : "#d4d4d8"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: row.modelData.title
                    color: "#8b8b93"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                    elide: Text.ElideRight
                }
            }
        }
    }

    // Desktop: slides over the right edge of the video.
    Rectangle {
        id: side
        visible: !watch.phone && width > 0
        y: bar.height
        height: parent.height - bar.height
        anchors.right: parent.right
        width: watch.sideOpen && watch.chromeShown ? 340 : 0
        color: Qt.rgba(0.055, 0.055, 0.063, 0.94)
        radius: Theme.radiusLg
        clip: true
        Behavior on width { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
        HoverHandler { id: sideHover }

        ListView {
            anchors.fill: parent
            anchors.margins: 8
            width: 324
            clip: true
            model: watch.phone ? [] : watch.episodes
            currentIndex: watch.index
            ScrollBar.vertical: ScScrollBar {}
            Component.onCompleted: positionViewAtIndex(watch.index, ListView.Center)
            delegate: EpisodeRow { width: 324 }
        }
    }

    // Phone portrait: details, skip/next and the episode list under the player.
    Item {
        id: phonePanel
        visible: watch.phone && !watch.landscape
        anchors.top: stage.bottom
        anchors.bottom: parent.bottom
        width: parent.width

        Column {
            id: phoneHead
            width: parent.width
            topPadding: 14
            bottomPadding: 10
            spacing: 12

            Text {
                x: 16
                width: parent.width - 32
                text: watch.episodeLine
                color: "white"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Item {
                x: 16
                width: parent.width - 32
                height: 36
                Flickable {
                    anchors.left: parent.left
                    anchors.right: phoneNav.left
                    anchors.rightMargin: 8
                    height: parent.height
                    contentWidth: chips.width
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    Row {
                        id: chips
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: watch.streams
                            Chip {
                                required property var modelData
                                text: modelData.quality
                                selected: watch.currentUrl === modelData.url
                                onClicked: watch.currentUrl = modelData.url
                            }
                        }
                    }
                }
                Row {
                    id: phoneNav
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    IconButton { iconName: "skip-back"; iconColor: "white"; diameter: 40; enabled: watch.hasPrev; onClicked: watch.load(watch.index - 1) }
                    IconButton { iconName: "skip-forward"; iconColor: "white"; diameter: 40; enabled: watch.hasNext; onClicked: watch.load(watch.index + 1) }
                }
            }
            Row {
                x: 16
                spacing: 8
                visible: (watch.activeSkip !== null && !(App.settings.autoSkip === true)) || nextCountdown.running
                ScButton {
                    visible: watch.activeSkip !== null && !(App.settings.autoSkip === true)
                    variant: "primary"
                    compact: true
                    iconName: "skip-forward"
                    text: watch.skipLabel
                    onClicked: player.seek(watch.activeSkip.end)
                }
                ScButton {
                    visible: nextCountdown.running
                    variant: "primary"
                    compact: true
                    text: "Next in " + Math.ceil(nextCountdown.remaining) + "s"
                    onClicked: { nextCountdown.stop(); watch.load(watch.index + 1) }
                }
                ScButton {
                    visible: nextCountdown.running
                    compact: true
                    text: "Cancel"
                    onClicked: nextCountdown.stop()
                }
            }
            Rectangle { width: parent.width; height: 1; color: "#1d1d21" }
        }

        ListView {
            id: phoneList
            anchors.top: phoneHead.bottom
            anchors.bottom: parent.bottom
            x: 8
            width: parent.width - 16
            clip: true
            model: watch.phone ? watch.episodes : []
            currentIndex: watch.index
            Component.onCompleted: positionViewAtIndex(watch.index, ListView.Contain)
            delegate: EpisodeRow { width: phoneList.width }
        }
    }

    // Phone landscape: the player is full screen; a thin rail keeps the
    // essentials reachable without covering the video. It hides with the
    // rest of the chrome and comes back when the video is tapped.
    Rectangle {
        id: rail
        visible: watch.phone && watch.landscape && watch.chromeShown
        width: visible ? 56 : 0
        height: parent.height
        anchors.right: parent.right
        color: "#0b0b0c"
        Column {
            anchors.centerIn: parent
            spacing: 14
            IconButton { iconName: "arrow-left"; iconColor: "white"; diameter: 44; onClicked: watch.exit() }
            IconButton {
                iconName: "chevron-right"
                iconColor: "white"
                diameter: 44
                filled: true
                visible: watch.activeSkip !== null && !(App.settings.autoSkip === true)
                onClicked: player.seek(watch.activeSkip.end)
            }
            IconButton { iconName: "skip-forward"; iconColor: "white"; diameter: 44; enabled: watch.hasNext; onClicked: { nextCountdown.stop(); watch.load(watch.index + 1) } }
        }
    }
}
