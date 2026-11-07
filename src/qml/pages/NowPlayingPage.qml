import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import Scancore

// The song playing, full screen: its cover over a wash of its colours,
// the controls, the lyrics following the song, and the queue. "Video"
// swaps the cover for the song's own video (an anime's creditless opening)
// from the same moment, and back.
Rectangle {
    id: page
    property bool startWithVideo: false
    // Glass on this page shows its blurred cover (GlassScene.backdropFor).
    property Item glassBackdrop: null
    readonly property var song: MusicQueue.current
    readonly property bool wide: width >= 760 && width > height
    // Phones: one of the song, its lyrics or the queue at a time.
    property string tab: "song"
    property bool video: false
    // Retro follows the theme (Windows Phone's black or white); the usual
    // look is always dark over the cover's colours.
    readonly property bool systemBarsDark: !Theme.retro || Theme.dark
    color: Theme.retro ? Theme.base : "#0b0b0d"

    Component.onCompleted: if (startWithVideo && song && song.video) showVideo(true)
    onSongChanged: if (video) showVideo(false)

    function showVideo(on) {
        if (on === video || !song || !song.video) return
        if (on) {
            videoStart = AudioPlayer.position
            AudioPlayer.pause()
            video = true
        } else {
            const at = videoItem ? videoItem.player.position : AudioPlayer.position
            video = false
            AudioPlayer.seek(at)
            AudioPlayer.play()
        }
    }
    property real videoStart: 0
    // The song's video, in whichever look is showing.
    property var videoItem: null
    // Whichever is playing: the song, or its video.
    readonly property var videoPlayer: video && videoItem ? videoItem.player : null
    readonly property bool saved: { App.dependOn(Music.libraryRevision); return !!song && Music.isSaved(song.id) }
    function toggleSaved() { if (song) Music.setSaved(song, !saved) }

    readonly property real position: videoPlayer ? videoPlayer.position : AudioPlayer.position
    readonly property real duration: videoPlayer ? videoPlayer.duration : AudioPlayer.duration
    readonly property bool paused: videoPlayer ? videoPlayer.paused : AudioPlayer.paused
    function seek(t) { videoPlayer ? videoPlayer.seek(t) : AudioPlayer.seek(t) }
    function toggle() { if (videoPlayer) videoPlayer.paused = !videoPlayer.paused; else AudioPlayer.toggle() }
    function handleBack() {
        if (video) { showVideo(false); return true }
        return false
    }
    function formatTime(s) {
        s = Math.max(0, Math.floor(s || 0))
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    Loader {
        anchors.fill: parent
        active: Theme.retro
        sourceComponent: Component { RetroNowPlaying { view: page } }
    }
    Loader {
        anchors.fill: parent
        active: !Theme.retro
        sourceComponent: classicLook
    }
    Component {
        id: classicLook
        Item {

            Item {
                id: backdrop
                anchors.fill: parent
                Component.onCompleted: page.glassBackdrop = backdrop
                Component.onDestruction: if (page.glassBackdrop === backdrop) page.glassBackdrop = null
                // Glass: the controls' card shows this (a layer).
                layer.enabled: Theme.glass
                layer.textureSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
                // The cover's colours, washed out: a tiny copy stretched over the page
                // (no blur effect to run, it is just a 24-pixel picture made big).
                Image {
                    id: wash
                    anchors.fill: parent
                    visible: !Theme.glass
                    source: page.song ? page.song.cover || "" : ""
                    sourceSize.width: Theme.glass ? 400 : 24
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                    asynchronous: true
                    opacity: status === Image.Ready ? 0.55 : 0
                    Behavior on opacity { NumberAnimation { duration: Motion.slow } }
                }
                // Glass: the cover itself, blurred, as the draft (its shapes
                // still show, a little darker).
                MultiEffect {
                    anchors.fill: parent
                    visible: Theme.glass && wash.status === Image.Ready
                    source: wash
                    autoPaddingEnabled: false
                    blurEnabled: true
                    blur: 1
                    blurMax: 64
                    saturation: 0.3
                    brightness: -0.12
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.rgba(0.04, 0.04, 0.05, Theme.glass ? 0.1 : 0.35) }
                        GradientStop { position: 1; color: Qt.rgba(0.04, 0.04, 0.05, Theme.glass ? 0.55 : 0.9) }
                    }
                }
            }

            // ------------------------------------------------------------ top
            Item {
                id: top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: page.SafeArea.margins.top
                height: 56
                IconButton {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "chevron-down"
                    iconColor: "#ffffff"
                    tip: qsTr("Close")
                    onClicked: Router.leave(page)
                }
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 140
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Now playing")
                        color: Qt.rgba(1, 1, 1, 0.6)
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1.5
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: page.song ? page.song.anime || page.song.album || "" : ""
                        color: "#ffffff"
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        elide: Text.ElideRight
                    }
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !!page.song && !!page.song.video
                    iconName: "tv"
                    iconColor: "#ffffff"
                    active: page.video
                    tip: page.video ? qsTr("Back to the song") : qsTr("Watch the video")
                    onClicked: page.showVideo(!page.video)
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: page.song && page.song.video ? 60 : 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !!page.song
                    iconName: "heart"
                    iconColor: page.saved ? Theme.accent : "#ffffff"
                    active: page.saved
                    tip: page.saved ? qsTr("Remove from library") : qsTr("Save to library")
                    onClicked: page.toggleSaved()
                }
            }

            // ------------------------------------------------------------ the song
            Item {
                id: songSide
                anchors.left: parent.left
                anchors.top: top.bottom
                anchors.bottom: parent.bottom
                anchors.bottomMargin: page.SafeArea.margins.bottom
                width: page.wide ? parent.width * 0.48 : parent.width

                // The cover, or the video. Phones on the lyrics tab keep a small
                // row instead, so the words have the room.
                Item {
                    id: picture
                    readonly property bool small: !page.wide && page.tab === "lyrics"
                    visible: page.wide || page.tab !== "queue"
                    x: small ? 20 : (parent.width - width) / 2
                    y: small ? 8 : Math.max(8, (controls.y - height) / 2 - 10)
                    width: small ? 56 : Math.min(parent.width - 48, controls.y - 40, page.video ? 900 : 520)
                    height: page.video && !small ? width * 9 / 16 : width
                    Behavior on width { enabled: !Motion.reduced; NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
                    Cover {
                        anchors.fill: parent
                        visible: !page.video || picture.small
                        radius: Theme.retro ? 0 : (picture.small ? Theme.radiusSm : Theme.radiusXl)
                        source: page.song ? page.song.cover || "" : ""
                        title: page.song ? page.song.anime || page.song.album || "" : ""
                        scale: !page.paused || picture.small ? 1 : 0.94
                        Behavior on scale { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutBack } }
                    }
                    Loader {
                        id: videoLoader
                        anchors.fill: parent
                        active: page.video && !picture.small && !Theme.retro
                        onItemChanged: if (active || !item) page.videoItem = item
                        sourceComponent: NativePlayer {
                            url: page.song ? page.song.video : ""
                            startAt: page.videoStart
                            chromeShown: false
                            Component.onCompleted: player.muted = AudioPlayer.muted
                        }
                    }
                }
                // Lyrics under the small cover on phones.
                LyricsView {
                    visible: !page.wide && page.tab === "lyrics"
                    x: 20
                    y: picture.y + picture.height + 12
                    width: parent.width - 40
                    height: controls.y - y - 8
                    song: page.song
                    large: true
                    ink: "#ffffff"
                    position: page.position
                    duration: page.duration
                    onSeekRequested: (t) => page.seek(t)
                }

                // Glass: the position and the buttons on a card of glass over
                // the blurred cover (as the draft); the play button keeps its
                // own white.
                GlassSurface {
                    visible: Theme.glass
                    anchors.fill: parent
                    backdrop: backdrop
                    readonly property real cardTop: controls.y + where.y - 22
                    readonly property real cardBottom: controls.y + buttons.y + buttons.height + 20
                    shapes: [{ x: 24, y: cardTop, w: parent.width - 48, h: Math.max(1, cardBottom - cardTop), r: 36 }]
                }
                Column {
                    id: controls
                    // Glass: inside the card, with room around it.
                    x: Theme.glass ? 46 : 24
                    width: parent.width - 2 * x
                    y: parent.height - height - (Theme.glass ? 36 : 16)
                    spacing: Theme.glass ? 22 : 14
                    // Title and who sings it.
                    Column {
                        visible: page.tab !== "lyrics" || page.wide
                        width: parent.width
                        spacing: 4
                        Text {
                            width: parent.width
                            text: page.song ? page.song.title : ""
                            color: "#ffffff"
                            font.family: Theme.fontDisplay
                            font.pixelSize: Theme.compact ? 24 : 28
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: page.song ? [MusicQueue.artistsOf(page.song), page.song.label].filter(s => s).join(" · ") : ""
                            color: Qt.rgba(1, 1, 1, 0.7)
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textMd
                            elide: Text.ElideRight
                        }
                    }
                    // Glass: room between the title and the card (a positioner skips an
                    // item of no height, so it has some).
                    Item { visible: Theme.glass; width: 1; height: 6 }
                    // Where it is.
                    Column {
                        id: where
                        width: parent.width
                        spacing: 2
                        ScSlider {
                            id: scrub
                            width: parent.width
                            from: 0
                            to: Math.max(1, page.duration)
                            value: pressed ? value : page.position
                            onMoved: if (!pressed) page.seek(value)
                            onPressedChanged: if (!pressed) page.seek(value)
                            Accessible.name: qsTr("Position")
                        }
                        Item {
                            width: parent.width
                            height: 16
                            Text { text: page.formatTime(scrub.pressed ? scrub.value : page.position); color: Qt.rgba(1, 1, 1, 0.6); font.family: Theme.fontSans; font.pixelSize: Theme.textXs }
                            Text { anchors.right: parent.right; text: page.formatTime(page.duration); color: Qt.rgba(1, 1, 1, 0.6); font.family: Theme.fontSans; font.pixelSize: Theme.textXs }
                        }
                    }
                    // The buttons.
                    Row {
                        id: buttons
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.compact ? 14 : 22
                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "arrow-right-left"
                            iconColor: MusicQueue.shuffle ? Theme.accent : "#ffffff"
                            active: MusicQueue.shuffle
                            tip: MusicQueue.shuffle ? qsTr("Shuffle on") : qsTr("Shuffle off")
                            onClicked: MusicQueue.shuffle = !MusicQueue.shuffle
                        }
                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "skip-back"
                            iconColor: "#ffffff"
                            iconSize: 24
                            diameter: 48
                            tip: qsTr("Previous")
                            onClicked: MusicQueue.previous()
                        }
                        // Play and pause, big.
                        AbstractButton {
                            width: 68
                            height: 68
                            focusPolicy: Qt.TabFocus
                            Accessible.name: page.paused ? qsTr("Play") : qsTr("Pause")
                            onClicked: page.toggle()
                            background: Rectangle {
                                radius: Theme.retro ? 0 : width / 2
                                color: "#ffffff"
                                scale: parent.pressed ? 0.94 : 1
                                Behavior on scale { NumberAnimation { duration: Motion.fast } }
                            }
                            contentItem: Item {
                                Spinner { anchors.centerIn: parent; visible: !page.videoPlayer && MusicQueue.busy }
                                Icon {
                                    anchors.centerIn: parent
                                    anchors.horizontalCenterOffset: page.paused ? 2 : 0
                                    visible: page.videoPlayer || !MusicQueue.busy
                                    name: page.paused ? "play" : "pause"
                                    size: 28
                                    color: "#0b0b0d"
                                }
                            }
                        }
                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "skip-forward"
                            iconColor: "#ffffff"
                            iconSize: 24
                            diameter: 48
                            enabled: MusicQueue.hasNext
                            tip: qsTr("Next")
                            onClicked: MusicQueue.next()
                        }
                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "rotate-ccw"
                            iconColor: MusicQueue.repeat !== "off" ? Theme.accent : "#ffffff"
                            active: MusicQueue.repeat !== "off"
                            tip: MusicQueue.repeat === "one" ? qsTr("Repeat this song") : MusicQueue.repeat === "all" ? qsTr("Repeat the queue") : qsTr("Repeat off")
                            onClicked: MusicQueue.repeat = ({ off: "all", all: "one", one: "off" })[MusicQueue.repeat]
                            Text {
                                visible: MusicQueue.repeat === "one"
                                anchors.right: parent.right
                                anchors.top: parent.top
                                text: "1"
                                color: Theme.accent
                                font.family: Theme.fontSans
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }
                    }
                    // Glass: room between the card and the chips.
                    Item { visible: Theme.glass && !page.wide; width: 1; height: 6 }
                    // Phones: what the space above shows.
                    Row {
                        visible: !page.wide
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 8
                        Repeater {
                            model: [{ key: "song", label: qsTr("Song"), icon: "circle-play" }, { key: "lyrics", label: qsTr("Lyrics"), icon: "type" },
                                    { key: "queue", label: qsTr("Queue"), icon: "list" }]
                            Chip {
                                required property var modelData
                                text: modelData.label
                                iconName: modelData.icon
                                selected: page.tab === modelData.key
                                onClicked: page.tab = modelData.key
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------------------ lyrics / queue
            Item {
                id: sidePanel
                anchors.right: parent.right
                anchors.top: top.bottom
                anchors.bottom: parent.bottom
                anchors.bottomMargin: page.SafeArea.margins.bottom
                width: page.wide ? parent.width * 0.52 - 24 : parent.width
                visible: page.wide || page.tab === "queue"
                // Wide: lyrics or queue, picked here.
                Row {
                    id: sideTabs
                    visible: page.wide
                    x: 8
                    spacing: 8
                    Repeater {
                        model: [{ key: "lyrics", label: qsTr("Lyrics"), icon: "type" }, { key: "queue", label: qsTr("Queue"), icon: "list" }]
                        Chip {
                            required property var modelData
                            text: modelData.label
                            iconName: modelData.icon
                            selected: (page.tab === "queue") === (modelData.key === "queue")
                            onClicked: page.tab = modelData.key
                        }
                    }
                }
                LyricsView {
                    visible: page.wide && page.tab !== "queue"
                    anchors.fill: parent
                    anchors.topMargin: 48
                    anchors.rightMargin: 24
                    anchors.bottomMargin: 24
                    song: page.song
                    large: true
                    ink: "#ffffff"
                    position: page.position
                    duration: page.duration
                    onSeekRequested: (t) => page.seek(t)
                }
                ListView {
                    id: queueList
                    visible: page.tab === "queue"
                    anchors.fill: parent
                    anchors.topMargin: page.wide ? 48 : 0
                    anchors.bottomMargin: page.wide ? 24 : controls.height + 32
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    clip: true
                    model: MusicQueue.songs
                    maximumFlickVelocity: Motion.flickVelocity
                    flickDeceleration: Motion.flickDeceleration
                    WheelScroll {}
                    header: Text {
                        width: queueList.width
                        bottomPadding: 8
                        text: MusicQueue.songs.length === 1 ? qsTr("1 song in the queue") : qsTr("%1 songs in the queue").arg(MusicQueue.songs.length)
                        color: Qt.rgba(1, 1, 1, 0.6)
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                    }
                    delegate: SongRow {
                        required property var modelData
                        required property int index
                        width: queueList.width
                        song: modelData
                        ink: "#ffffff"
                        onPlayed: MusicQueue.go(index)
                    }
                }
            }
        }
    }
}
