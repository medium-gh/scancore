import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Now Playing in the Retro mode, as Windows Phone's music app had it: the
// app's name in small capitals, the anime or album in huge thin letters
// running off the edge, then pivots (now playing, lyrics, queue) swiped or
// tapped. The song is a square tile with its title under it, a flat line of
// how far it is, and ring buttons. Wide windows keep the song on the left
// and the lyrics or queue pivot on the right.
Item {
    id: retro
    // NowPlayingPage: the song, the position and the controls.
    required property Item view
    readonly property var song: view.song
    readonly property bool wide: view.wide
    readonly property string what: song ? song.anime || song.album || MusicQueue.artistsOf(song) || song.title : ""
    function cased(text) { return Retro.upperCase ? text.toUpperCase() : text.toLowerCase() }
    // The song after this one, said under the controls.
    readonly property var upNext: {
        if (!MusicQueue.hasNext || MusicQueue.songs.length < 2) return null
        const at = MusicQueue.order.length ? MusicQueue.order.indexOf(MusicQueue.index) : MusicQueue.index
        const nextAt = (at + 1) % MusicQueue.songs.length
        return MusicQueue.songs[MusicQueue.order.length ? MusicQueue.order[nextAt] : nextAt] || null
    }
    // Phones: the three pivots; wide: the song stays, lyrics or queue pivot.
    readonly property var pivots: wide ? [{ value: "lyrics", label: cased(qsTr("Lyrics")) }, { value: "queue", label: cased(qsTr("Queue")) }]
        : [{ value: "song", label: cased(qsTr("Now playing")) }, { value: "lyrics", label: cased(qsTr("Lyrics")) },
           { value: "queue", label: cased(qsTr("Queue")) }]
    readonly property string pivot: wide && view.tab === "song" ? "lyrics" : view.tab

    // ------------------------------------------------------------ title
    Column {
        id: heading
        x: 24
        y: view.SafeArea.margins.top + 12
        width: parent.width - 24
        spacing: 0
        Row {
            spacing: 12
            AbstractButton {
                width: 28
                height: 28
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: qsTr("Close")
                onClicked: Router.leave(view)
                background: null
                contentItem: Icon { name: "chevron-down"; size: 22; color: Theme.text }
            }
            // Phones: the anime or album here, the pivots being the big words.
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, heading.width - 64)
                elide: Text.ElideRight
                text: retro.wide || !retro.what ? qsTr("SCANCORE · MUSIC") : "SCANCORE · " + retro.what.toUpperCase()
                color: Theme.text
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }
        }
        // Huge and thin, cut by the edge as on the phone.
        Text {
            visible: retro.wide
            width: parent.width
            clip: true
            text: retro.cased(retro.what)
            color: Theme.text
            font.family: Theme.fontSans
            font.pixelSize: retro.wide ? 84 : 60
            font.weight: Font.Light
            wrapMode: Text.NoWrap
            elide: Text.ElideNone
            height: visible ? font.pixelSize * 1.15 : 8
        }
    }

    // ------------------------------------------------------------ the song
    Item {
        id: songPart
        visible: retro.wide || retro.pivot === "song"
        x: 24
        anchors.top: retro.wide ? heading.bottom : pivotHeaders.bottom
        anchors.topMargin: retro.wide ? 12 : 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: view.SafeArea.margins.bottom + 16
        width: (retro.wide ? parent.width * 0.46 : parent.width) - 48

        // The cover tile, or the video in its place.
        Rectangle {
            id: art
            y: Math.max(0, (controls.y - 16 - height) / 2)
            width: Math.min(parent.width, controls.y - 16, view.video ? 900 : 480)
            height: view.video ? width * 9 / 16 : width
            color: Theme.tile
            Cover {
                anchors.fill: parent
                visible: !view.video
                radius: 0
                source: retro.song ? retro.song.cover || "" : ""
                title: retro.what
            }
            Loader {
                anchors.fill: parent
                active: view.video
                onItemChanged: if (active || !item) view.videoItem = item
                sourceComponent: NativePlayer {
                    url: retro.song ? retro.song.video : ""
                    startAt: view.videoStart
                    chromeShown: false
                    Component.onCompleted: player.muted = AudioPlayer.muted
                }
            }
        }

        Column {
            id: controls
            width: parent.width
            y: parent.height - height
            spacing: 12
            Column {
                width: parent.width
                Text {
                    width: parent.width
                    text: retro.song ? retro.song.title : ""
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.compact ? 26 : 30
                    font.weight: Font.Normal
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: retro.song ? [MusicQueue.artistsOf(retro.song), retro.song.label].filter(s => s).join(" · ") : ""
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    elide: Text.ElideRight
                }
            }
            // How far: a flat line in the accent, the times under it.
            Item {
                width: parent.width
                height: 34
                Rectangle {
                    id: track
                    y: 8
                    width: parent.width
                    height: 4
                    color: Theme.surface3
                    Rectangle {
                        height: parent.height
                        width: parent.width * Math.max(0, Math.min(1, (scrub.pressed ? scrub.at : view.position) / Math.max(1, view.duration)))
                        color: Theme.tile
                    }
                }
                Text { y: 18; text: view.formatTime(scrub.pressed ? scrub.at : view.position); color: Theme.textMuted; font.family: Theme.fontSans; font.pixelSize: Theme.textXs }
                Text { y: 18; anchors.right: parent.right; text: view.formatTime(view.duration); color: Theme.textMuted; font.family: Theme.fontSans; font.pixelSize: Theme.textXs }
                // A finger on the line moves it; the song follows on release.
                MouseArea {
                    id: scrub
                    property real at: 0
                    width: parent.width
                    height: 24
                    preventStealing: true
                    Accessible.role: Accessible.Slider
                    Accessible.name: qsTr("Position")
                    function update(x) { at = Math.max(0, Math.min(1, x / width)) * view.duration }
                    onPressed: (mouse) => update(mouse.x)
                    onPositionChanged: (mouse) => update(mouse.x)
                    onReleased: view.seek(at)
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.compact ? 22 : 30
                RingButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "skip-back"
                    diameter: 52
                    Accessible.name: qsTr("Previous")
                    onClicked: MusicQueue.previous()
                }
                RingButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: view.paused ? "play" : "pause"
                    diameter: 72
                    Accessible.name: view.paused ? qsTr("Play") : qsTr("Pause")
                    onClicked: view.toggle()
                    Spinner { anchors.centerIn: parent; visible: !view.videoPlayer && MusicQueue.busy }
                }
                RingButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "skip-forward"
                    diameter: 52
                    enabled: MusicQueue.hasNext
                    Accessible.name: qsTr("Next")
                    onClicked: MusicQueue.next()
                }
            }
            // The app bar's kind of buttons, with their names.
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.compact ? 14 : 24
                RingButton {
                    iconName: "arrow-right-left"
                    diameter: 40
                    label: qsTr("shuffle")
                    lit: MusicQueue.shuffle
                    onClicked: MusicQueue.shuffle = !MusicQueue.shuffle
                }
                RingButton {
                    iconName: "rotate-ccw"
                    diameter: 40
                    label: MusicQueue.repeat === "one" ? qsTr("repeat one") : qsTr("repeat")
                    lit: MusicQueue.repeat !== "off"
                    onClicked: MusicQueue.repeat = ({ off: "all", all: "one", one: "off" })[MusicQueue.repeat]
                }
                RingButton {
                    iconName: "heart"
                    diameter: 40
                    label: view.saved ? qsTr("saved") : qsTr("save")
                    lit: view.saved
                    enabled: !!retro.song
                    onClicked: view.toggleSaved()
                }
                RingButton {
                    visible: !!retro.song && !!retro.song.video
                    iconName: "tv"
                    diameter: 40
                    label: qsTr("video")
                    lit: view.video
                    onClicked: view.showVideo(!view.video)
                }
            }
            Text {
                visible: !!retro.upNext
                width: parent.width
                text: retro.upNext ? qsTr("up next: %1").arg(retro.upNext.title) : ""
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
        }
    }

    // ------------------------------------------------------------ pivots
    PivotHeaders {
        id: pivotHeaders
        x: retro.wide ? parent.width * 0.5 : 24
        anchors.top: heading.bottom
        width: (retro.wide ? parent.width * 0.5 : parent.width) - 24
        options: retro.pivots
        value: retro.pivot
        onPicked: (v) => view.tab = v
    }
    Item {
        id: pivotBody
        x: pivotHeaders.x
        anchors.top: pivotHeaders.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: view.SafeArea.margins.bottom + 8
        width: pivotHeaders.width
        LyricsView {
            visible: retro.pivot === "lyrics"
            anchors.fill: parent
            song: retro.song
            large: true
            ink: Theme.text
            position: view.position
            duration: view.duration
            onSeekRequested: (t) => view.seek(t)
        }
        ListView {
            id: queueList
            visible: retro.pivot === "queue"
            anchors.fill: parent
            clip: true
            model: retro.pivot === "queue" ? MusicQueue.songs : []
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            reuseItems: true
            WheelScroll {}
            delegate: SongRow {
                required property var modelData
                required property int index
                width: queueList.width
                song: modelData
                onPlayed: MusicQueue.go(index)
            }
        }
    }
    // Phones: sideways swipes turn the pivots, as on the phone.
    DragHandler {
        target: null
        enabled: !retro.wide
        yAxis.enabled: false
        property real moved: 0
        onTranslationChanged: if (active) moved = translation.x
        onActiveChanged: {
            if (active) { moved = 0; return }
            if (moved < -60) pivotHeaders.next()
            else if (moved > 60) pivotHeaders.previous()
        }
    }
}
