import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Plays a video from an anime extension with SCANCORE's own player (libmpv,
// MpvPlayer), with controls along the bottom: play, episodes, seek, time,
// volume, the settings panel (WatchPage's PlayerPanel, from
// settingsSections()) and full screen. It shows the same face to WatchPage
// as the web players (url, seek(), time(), interacted(), key()).
// Phones: a tap shows or hides the controls; a double tap seeks 10 s on the
// sides and pauses in the middle; holding plays at 2×; swiping up or down
// sets the brightness (left half) or the volume (right half), and sideways
// seeks (Settings → Anime → Gestures).
Item {
    id: root
    property string url
    // The video as Anime.videos() gives it: headers, subtitles, audio,
    // mpvArgs.
    property var video: ({})
    property real startAt: 0
    // WatchPage's chrome: the controls show and hide with it.
    property bool chromeShown: true
    // The same server's other videos ([{id, label, height, current}]),
    // offered beside the stream's own variants under Quality.
    property var alternatives: []
    readonly property bool phone: App.mobile
    readonly property alias player: mpv
    // Playing now (WatchPage keeps the screen on meanwhile).
    readonly property bool playing: mpv.loaded && !mpv.paused && !mpv.ended
    // The settings panel is open (WatchPage): the controls stay.
    property bool panelOpen: false
    property bool hasPrev: false
    // Full screen now (WatchPage: phones in landscape, desktops' window).
    property bool fullScreen: false
    // How far the picture may reach past the sides (a phone's cutout, kept
    // clear by the page): Stretch and Crop to fill use it, so they fill
    // the whole screen; the controls stay clear of it.
    property real bleedLeft: 0
    property real bleedRight: 0
    readonly property bool fills: ["stretch", "crop"].includes(App.settings.playerFit)
    property bool hasNext: false
    readonly property bool controlsShown: chromeShown || mpv.paused || mpv.buffering || seekBar.pressed || panelOpen

    signal time(real seconds, real total, string status)
    signal interacted()
    signal key(var press)
    signal unsupported()
    signal failed(string message)
    signal fullScreenRequested()
    signal alternativePicked(string id)
    signal settingsRequested()
    signal previousRequested()
    signal nextRequested()
    // A tap on the picture (phones): WatchPage shows or hides its chrome.
    signal tapped()

    function seek(seconds) { mpv.seek(seconds) }
    function togglePause() { mpv.togglePause(); interacted() }
    // A watch party's host playing or paused.
    function setPlaying(on) { if (mpv.loaded && !mpv.ended && on === mpv.paused) mpv.togglePause() }
    function seekBy(seconds) { mpv.seekBy(seconds); interacted() }
    function changeVolume(delta) {
        const v = Math.max(0, Math.min(130, Math.round(mpv.volume + delta)))
        mpv.volume = v
        App.setSetting("playerVolume", v)
        interacted()
    }

    // Quality, Audio, Subtitles, Speed and Fit for the settings panel
    // (PlayerPanel's sections), as they are now.
    function settingsSections() {
        const out = []
        const variants = mpv.tracks.filter(t => t.type === "video" && t.height > 0)
            .sort((a, b) => b.height - a.height || (b.bitrate || 0) - (a.bitrate || 0))
        const others = root.alternatives.length > 1 ? root.alternatives : []
        if (variants.length > 1 || others.length > 0) {
            const rate = (t) => t.bitrate > 0 ? (t.bitrate >= 1e6 ? (t.bitrate / 1e6).toFixed(1) + " Mb/s" : Math.round(t.bitrate / 1e3) + " kb/s") : ""
            const choices = variants.length > 1 ? variants.map(t => ({ label: t.height + "p", detail: rate(t), selected: t.selected, pick: () => mpv.setTrack("video", t.id) })) : []
            for (const o of others)
                choices.push({ label: o.label, selected: o.current, pick: () => root.alternativePicked(o.id) })
            const now = choices.find(c => c.selected)
            out.push({ key: "quality", icon: "sliders-horizontal", label: qsTr("Quality"), value: now ? now.label : "", choices: choices })
        }
        const audio = mpv.tracks.filter(t => t.type === "audio")
        if (audio.length > 1) {
            const name = (t) => t.title || t.lang || qsTr("Track ") + t.id
            const now = audio.find(t => t.selected)
            out.push({ key: "audio", icon: "languages", label: qsTr("Audio"), value: now ? name(now) : "",
                       choices: audio.map(t => ({ label: name(t), selected: t.selected, pick: () => mpv.setTrack("audio", t.id) })) })
        }
        const subs = mpv.tracks.filter(t => t.type === "sub")
        if (subs.length > 0) {
            const name = (t) => t.title || t.lang || qsTr("Track ") + t.id
            const now = subs.find(t => t.selected)
            out.push({ key: "subtitles", icon: "captions", label: qsTr("Subtitles"), value: now ? name(now) : qsTr("Off"),
                       choices: [{ label: qsTr("Off"), selected: !now, pick: () => mpv.setTrack("sub", 0) }]
                           .concat(subs.map(t => ({ label: name(t), selected: t.selected, pick: () => mpv.setTrack("sub", t.id) }))) })
        }
        const speeds = [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
        const speedName = (v) => Math.abs(v - 1) < 0.01 ? qsTr("Normal") : v + "×"
        out.push({ key: "speed", icon: "gauge", label: qsTr("Speed"), value: speedName(mpv.speed),
                   choices: speeds.map(v => ({ label: speedName(v), selected: Math.abs(mpv.speed - v) < 0.01,
                                               pick: () => { mpv.speed = v; App.setSetting("playerSpeed", v) } })) })
        const fits = [{ value: "fit", label: qsTr("Fit"), detail: qsTr("The whole picture") },
                      { value: "crop", label: qsTr("Crop to fill"), detail: qsTr("No black bars") },
                      { value: "stretch", label: qsTr("Stretch"), detail: qsTr("Fills the screen, out of shape") },
                      { value: "original", label: qsTr("Original size"), detail: qsTr("Its own pixels") }]
        const fit = App.settings.playerFit || "fit"
        out.push({ key: "fit", icon: "crop", label: qsTr("Fit"), value: (fits.find(f => f.value === fit) || fits[0]).label,
                   choices: fits.map(f => ({ label: f.label, detail: f.detail, selected: f.value === fit, pick: () => App.setSetting("playerFit", f.value) })) })
        // Anime4K, to compare while watching (also in Settings → Anime).
        const sharpens = [{ value: "off", label: qsTr("Off") },
                          { value: "fast", label: qsTr("Fast"), detail: root.phone ? "" : qsTr("Most graphics cards") }]
            .concat(root.phone ? [] : [{ value: "hq", label: qsTr("Best"), detail: qsTr("Needs a strong graphics card") }])
        const sharpen = root.phone && App.settings.animeSharpen === "hq" ? "fast" : App.settings.animeSharpen || "off"
        out.push({ key: "sharpen", icon: "sparkles", label: qsTr("Sharpen (Anime4K)"), value: (sharpens.find(s => s.value === sharpen) || sharpens[0]).label,
                   choices: sharpens.map(s => ({ label: s.label, detail: s.detail || "", selected: s.value === sharpen, pick: () => App.setSetting("animeSharpen", s.value) })) })
        return out
    }

    function keyHint(action) {
        const keys = Keymap.labels(action)
        return keys.length ? " (" + keys[0] + ")" : ""
    }

    function formatTime(s) {
        s = Math.max(0, Math.floor(s || 0))
        const h = Math.floor(s / 3600)
        const m = Math.floor(s / 60) % 60
        const sec = String(s % 60).padStart(2, "0")
        return h > 0 ? h + ":" + String(m).padStart(2, "0") + ":" + sec : m + ":" + sec
    }

    MpvPlayer {
        id: mpv
        anchors.fill: parent
        anchors.leftMargin: root.fills ? -root.bleedLeft : 0
        anchors.rightMargin: root.fills ? -root.bleedRight : 0
        source: root.url
        headers: root.video.headers || ({})
        options: root.video.mpvArgs || ({})
        subtitles: root.video.subtitles || []
        look: ({ subtitleScale: Number(App.settings.subtitleScale) || 1, subtitleOutline: Number(App.settings.subtitleOutline),
                 subtitleColor: App.settings.subtitleColor || "#ffffff", subtitleBox: App.settings.subtitleBox === true,
                 sharpen: App.settings.animeSharpen || "off", fit: App.settings.playerFit || "fit" })
        audio: root.video.audio || []
        startAt: root.startAt
        // Settings → Anime: the variant and audio language to start with.
        preferredHeight: VideoPrefs.height
        audioLanguages: VideoPrefs.audioLanguages
        // Above the controls while they show.
        subtitlePosition: root.controlsShown && bar.visible ? Math.round(100 - (bar.height + 6) / Math.max(1, root.height) * 100) : 100
        Component.onCompleted: {
            volume = Number(App.settings.playerVolume ?? 100)
            speed = Number(App.settings.playerSpeed || 1)
        }
        onPositionChanged: root.time(position, duration, ended ? "Ended" : paused ? "Paused" : "Playing")
        onFinished: root.time(duration, duration, "Ended")
        onErrorChanged: if (error) root.failed(error)
    }

    // Desktop: a click pauses, a double click goes full screen. Phones: the
    // gestures above. The first tap on hidden controls only shows them.
    MouseArea {
        id: touch
        anchors.fill: parent
        anchors.bottomMargin: bar.visible ? bar.height : 0
        acceptedButtons: Qt.LeftButton
        // What a press turned into: "" (still a tap), "seek", "brightness",
        // "volume" or "hold" (2× while held).
        property string gesture: ""
        property point start
        property real startLevel: 0
        property real level: 0
        property real seekTo: 0
        property real speedBefore: 1
        readonly property bool swipes: root.phone && App.settings.playerSwipes !== false
        Timer { id: clickDelay; interval: 240; property real x: 0; onTriggered: root.phone ? root.tapped() : root.togglePause() }
        Timer {
            id: holdTimer
            interval: 450
            onTriggered: {
                if (!root.phone || App.settings.playerHoldSpeed === false || touch.gesture !== "" || !mpv.loaded || mpv.paused) return
                touch.gesture = "hold"
                touch.speedBefore = mpv.speed
                mpv.speed = 2
                clickDelay.stop()
            }
        }
        onPressed: (mouse) => {
            gesture = ""
            start = Qt.point(mouse.x, mouse.y)
            holdTimer.restart()
        }
        onPositionChanged: (mouse) => {
            const dx = mouse.x - start.x, dy = mouse.y - start.y
            if (gesture === "" && swipes && Math.max(Math.abs(dx), Math.abs(dy)) > 18) {
                holdTimer.stop()
                if (Math.abs(dx) > Math.abs(dy)) {
                    if (mpv.duration > 0) gesture = "seek"
                } else if (start.x < width / 2 && App.screenBrightness() >= 0) {
                    gesture = "brightness"
                    startLevel = App.screenBrightness()
                } else {
                    gesture = "volume"
                    startLevel = App.mediaVolume()
                }
            }
            if (gesture === "seek") {
                // The width of the screen is a minute and a half.
                seekTo = Math.max(0, Math.min(mpv.duration, mpv.position + dx / width * 90))
            } else if (gesture === "brightness" || gesture === "volume") {
                level = Math.max(0, Math.min(1, startLevel - dy / (height * 0.8)))
                if (gesture === "brightness") App.setScreenBrightness(level)
                else App.setMediaVolume(level)
            }
        }
        onReleased: {
            holdTimer.stop()
            if (gesture === "seek") mpv.seek(seekTo)
            if (gesture === "hold") mpv.speed = speedBefore
            if (gesture !== "") root.interacted()
        }
        onCanceled: {
            holdTimer.stop()
            if (gesture === "hold") mpv.speed = speedBefore
            gesture = ""
        }
        onClicked: (mouse) => {
            if (gesture !== "") { gesture = ""; return }
            clickDelay.x = mouse.x
            clickDelay.restart()
        }
        onDoubleClicked: (mouse) => {
            clickDelay.stop()
            if (gesture !== "") { gesture = ""; return }
            if (!root.phone) { root.fullScreenRequested(); return }
            const third = width / 3
            if (mouse.x > third && mouse.x < 2 * third) root.togglePause()
            else if (App.settings.playerDoubleTapSeek !== false) root.seekBy(mouse.x < third ? -10 : 10)
            else root.togglePause()
        }
    }

    // What a gesture does, while it does it.
    Rectangle {
        anchors.centerIn: parent
        visible: touch.gesture === "seek"
        width: seekText.implicitWidth + 32
        height: 52
        radius: 26
        color: Qt.rgba(0, 0, 0, 0.6)
        Text {
            id: seekText
            anchors.centerIn: parent
            readonly property real delta: touch.seekTo - mpv.position
            text: (delta < 0 ? "−" : "+") + root.formatTime(Math.abs(delta)) + "   " + root.formatTime(touch.seekTo) + " / " + root.formatTime(mpv.duration)
            color: "white"
            font.family: Theme.fontSans
            font.pixelSize: Theme.textMd
            font.weight: Font.DemiBold
            font.features: { "tnum": 1 }
        }
    }
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: touch.gesture === "brightness" ? 32 : parent.width - width - 32
        visible: touch.gesture === "brightness" || touch.gesture === "volume"
        width: 44
        height: Math.min(200, parent.height * 0.6)
        radius: 22
        color: Qt.rgba(0, 0, 0, 0.6)
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: levelIcon.top
            anchors.bottomMargin: 10
            width: 4
            height: parent.height - 60
            radius: 2
            color: Qt.rgba(1, 1, 1, 0.25)
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: parent.height * touch.level
                radius: 2
                color: "white"
            }
        }
        Icon {
            id: levelIcon
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            name: touch.gesture === "brightness" ? "sun" : touch.level === 0 ? "volume-x" : "volume-2"
            size: 18
            color: "white"
        }
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 24
        visible: touch.gesture === "hold"
        width: holdText.implicitWidth + 28
        height: 36
        radius: 18
        color: Qt.rgba(0, 0, 0, 0.6)
        Text {
            id: holdText
            anchors.centerIn: parent
            text: qsTr("2× speed")
            color: "white"
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
        }
    }

    Spinner {
        anchors.centerIn: parent
        visible: mpv.buffering && !mpv.error
        size: 34
        color: "#e4e4e7"
    }

    // A tap target in the middle while paused (phones above all).
    IconButton {
        anchors.centerIn: parent
        visible: mpv.paused && !mpv.buffering && mpv.loaded && !mpv.ended
        diameter: 64
        iconSize: 30
        iconName: "play"
        iconColor: "white"
        background: Rectangle { radius: 32; color: Qt.rgba(0, 0, 0, 0.55) }
        onClicked: root.togglePause()
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 48)
        spacing: 10
        visible: mpv.error.length > 0 || !mpv.available
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "tv-minimal"
            size: 30
            color: "#a1a1aa"
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: !mpv.available ? qsTr("This copy of SCANCORE has no video player for anime from extensions.") : mpv.error
            color: "#d4d4d8"
            font.family: Theme.fontSans
            font.pixelSize: Theme.textMd
        }
    }

    // ------------------------------------------------------------ controls
    Rectangle {
        id: bar
        anchors.bottom: parent.bottom
        width: parent.width
        height: root.phone ? 64 : 76
        visible: opacity > 0.01 && mpv.available
        opacity: root.controlsShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Motion.slow } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
        }
        HoverHandler { id: barHover }

        // Seek bar with what is downloaded already.
        Slider {
            id: seekBar
            x: 12
            width: parent.width - 24
            y: root.phone ? 6 : 12
            height: 22
            from: 0
            to: Math.max(1, mpv.duration)
            hoverEnabled: true
            // Follows the video unless it is being dragged.
            Binding on value { when: !seekBar.pressed; value: mpv.position }
            onPressedChanged: if (!pressed) mpv.seek(value)
            background: Rectangle {
                x: seekBar.leftPadding
                y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                width: seekBar.availableWidth
                height: seekBar.hovered || seekBar.pressed ? 6 : 4
                radius: height / 2
                color: Qt.rgba(1, 1, 1, 0.18)
                Rectangle {
                    width: Math.min(1, (mpv.position + mpv.buffered) / Math.max(1, mpv.duration)) * parent.width
                    height: parent.height
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, 0.28)
                }
                Rectangle {
                    width: seekBar.visualPosition * parent.width
                    height: parent.height
                    radius: height / 2
                    color: Theme.accent
                }
            }
            handle: Rectangle {
                x: seekBar.leftPadding + seekBar.visualPosition * (seekBar.availableWidth - width)
                y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                width: seekBar.pressed || seekBar.hovered ? 14 : 0
                height: width
                radius: width / 2
                color: "white"
                Behavior on width { NumberAnimation { duration: Motion.fast } }
            }
            // The time under the pointer.
            Rectangle {
                visible: seekBar.hovered && !root.phone && mpv.duration > 0
                readonly property real at: hoverPoint.point.position.x / seekBar.width
                x: Math.max(0, Math.min(seekBar.width - width, hoverPoint.point.position.x - width / 2))
                y: -height - 4
                width: hoverTime.implicitWidth + 12
                height: 22
                radius: 6
                color: "#18181b"
                Text {
                    id: hoverTime
                    anchors.centerIn: parent
                    text: root.formatTime(parent.at * mpv.duration)
                    color: "white"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                }
            }
            HoverHandler { id: hoverPoint }
        }

        Row {
            x: 8
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.phone ? 4 : 8
            spacing: 2
            IconButton {
                iconName: mpv.paused || mpv.ended ? "play" : "pause"
                iconColor: "white"
                tip: (mpv.paused ? qsTr("Play") : qsTr("Pause")) + root.keyHint("player.pause")
                onClicked: mpv.ended ? mpv.seek(0) : root.togglePause()
            }
            IconButton {
                iconName: "skip-back"
                iconColor: "white"
                enabled: root.hasPrev
                tip: qsTr("Previous episode") + root.keyHint("player.prev")
                onClicked: root.previousRequested()
            }
            IconButton {
                iconName: "skip-forward"
                iconColor: "white"
                enabled: root.hasNext
                tip: qsTr("Next episode") + root.keyHint("player.next")
                onClicked: root.nextRequested()
            }
            IconButton {
                visible: !root.phone
                iconName: mpv.muted || mpv.volume === 0 ? "volume-x" : "volume-2"
                iconColor: "white"
                tip: (mpv.muted ? qsTr("Unmute") : qsTr("Mute")) + root.keyHint("player.mute")
                onClicked: mpv.muted = !mpv.muted
            }
            Slider {
                id: volumeBar
                visible: !root.phone
                width: 90
                height: 34
                from: 0
                to: 100
                Binding on value { when: !volumeBar.pressed; value: mpv.muted ? 0 : Math.min(100, mpv.volume) }
                onMoved: {
                    mpv.muted = false
                    mpv.volume = value
                }
                onPressedChanged: if (!pressed) App.setSetting("playerVolume", Math.round(value))
                background: Rectangle {
                    x: volumeBar.leftPadding
                    y: volumeBar.topPadding + volumeBar.availableHeight / 2 - 2
                    width: volumeBar.availableWidth
                    height: 4
                    radius: 2
                    color: Qt.rgba(1, 1, 1, 0.2)
                    Rectangle { width: volumeBar.visualPosition * parent.width; height: 4; radius: 2; color: "white" }
                }
                handle: Rectangle {
                    x: volumeBar.leftPadding + volumeBar.visualPosition * (volumeBar.availableWidth - width)
                    y: volumeBar.topPadding + volumeBar.availableHeight / 2 - height / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: "white"
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 8
                text: root.formatTime(seekBar.pressed ? seekBar.value : mpv.position) + " / " + root.formatTime(mpv.duration)
                color: "#e4e4e7"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                font.features: { "tnum": 1 }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.phone ? 4 : 8
            spacing: 2
            // Quality, audio, subtitles, speed, fit and more: one panel.
            IconButton {
                iconName: "settings"
                iconColor: "white"
                tip: qsTr("Settings")
                onClicked: { root.settingsRequested(); root.interacted() }
            }
            // Phones: landscape is full screen (WatchPage turns the phone).
            IconButton {
                readonly property bool full: root.fullScreen
                iconName: full ? "minimize" : "maximize"
                iconColor: "white"
                tip: full ? qsTr("Exit full screen") : qsTr("Full screen")
                onClicked: { root.fullScreenRequested(); root.interacted() }
            }
        }
    }

    // The keys from Settings → Keyboard (player).
    Shortcut { sequences: Keymap.sequences("player.pause"); enabled: root.visible && mpv.available; onActivated: root.togglePause() }
    // A TV: the D-pad is WatchPage's (seeking, or moving to the controls),
    // and the remote has its own volume.
    Shortcut { sequences: Keymap.sequences("player.forward"); enabled: root.visible && mpv.available && !App.tv; onActivated: root.seekBy(10) }
    Shortcut { sequences: Keymap.sequences("player.backward"); enabled: root.visible && mpv.available && !App.tv; onActivated: root.seekBy(-10) }
    Shortcut { sequences: Keymap.sequences("player.volumeUp"); enabled: root.visible && mpv.available && !App.tv; onActivated: root.changeVolume(5) }
    Shortcut { sequences: Keymap.sequences("player.volumeDown"); enabled: root.visible && mpv.available && !App.tv; onActivated: root.changeVolume(-5) }
    Shortcut { sequences: Keymap.sequences("player.mute"); enabled: root.visible && mpv.available; onActivated: mpv.muted = !mpv.muted }
}
