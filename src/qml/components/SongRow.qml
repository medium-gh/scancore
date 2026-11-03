import QtQuick
import QtQuick.Controls.Basic
import Scancore

// One song in a list: its cover, title, who sings it and where it is from
// (an anime's OP1, say). Tapping plays it (the list it is in becomes the
// queue); the menu plays it next, queues it or opens its video. The one
// playing shows moving bars.
AbstractButton {
    id: row
    property var song: ({})
    // The anime is said already (its own page lists its songs).
    property bool showAnime: true
    // Text colour (white over Now Playing's dark backdrop).
    property color ink: Theme.text
    readonly property bool isCurrent: MusicQueue.current !== null && MusicQueue.current.id === song.id
    signal played()

    implicitHeight: 64
    // A finger leaves "hovered" set after it lifts.
    hoverEnabled: !Theme.touch
    focusPolicy: Qt.TabFocus
    Accessible.name: (song.title || "") + ", " + (song.artists || []).join(", ")
    onClicked: played()
    onPressAndHold: openMenu(width - 40, height / 2)
    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onTapped: (point) => row.openMenu(point.position.x, point.position.y)
    }
    function openMenu(px, py) {
        menu.openAt(row, px, py, [
            { text: qsTr("Play next"), icon: "skip-forward", run: () => MusicQueue.playNext(row.song) },
            { text: qsTr("Add to queue"), icon: "list", run: () => MusicQueue.addToQueue(row.song) },
            Music.isSaved(row.song.id)
                ? { text: qsTr("Remove from library"), icon: "heart", run: () => Music.setSaved(row.song, false) }
                : { text: qsTr("Save to library"), icon: "heart", run: () => { Music.setSaved(row.song, true); Router.toast(qsTr("Saved to your library"), "success") } },
            { text: qsTr("Watch the video"), icon: "tv", enabled: !!row.song.video, run: () => Router.openNowPlaying(row.song, true) }
        ])
    }
    ActionMenu { id: menu }

    background: Rectangle {
        radius: Theme.retro ? 0 : Theme.radiusMd
        color: row.isCurrent && !Theme.retro ? Theme.alpha(Theme.accent, Theme.dark ? 0.14 : 0.09)
             : row.hovered || row.activeFocus ? Theme.surface2 : "transparent"
    }
    contentItem: Item {
        Cover {
            id: art
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 48
            height: 48
            radius: Theme.retro ? 0 : Theme.radiusSm
            source: row.song.cover || ""
            title: row.song.anime || row.song.title || ""
            // The one playing: three bars over its cover.
            Rectangle {
                anchors.fill: parent
                visible: row.isCurrent
                radius: parent.radius
                color: Qt.rgba(0, 0, 0, 0.45)
                Row {
                    anchors.centerIn: parent
                    spacing: 3
                    Repeater {
                        model: 3
                        Rectangle {
                            required property int index
                            width: 4
                            radius: 2
                            color: "#ffffff"
                            anchors.bottom: parent.bottom
                            height: 6
                            SequentialAnimation on height {
                                running: row.isCurrent && AudioPlayer.playing && !Motion.reduced
                                loops: Animation.Infinite
                                NumberAnimation { to: [16, 10, 18][index]; duration: 260 + index * 70 }
                                NumberAnimation { to: 6; duration: 260 + index * 70 }
                            }
                        }
                    }
                }
            }
        }
        Column {
            anchors.left: art.right
            anchors.leftMargin: 12
            anchors.right: more.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            // Retro: Windows Phone's lists, the title larger and plain.
            Text {
                width: parent.width
                text: row.song.title || ""
                color: row.isCurrent ? (Theme.retro ? Theme.tile : Theme.accent) : row.ink
                font.family: Theme.fontSans
                font.pixelSize: Theme.retro ? Theme.textLg : Theme.textMd
                font.weight: Theme.retro ? Font.Normal : Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: [(row.song.artists || []).join(", "),
                       row.showAnime ? row.song.anime || row.song.album : "",
                       (row.song.label || "") + (row.song.episodes ? " · " + qsTr("Episodes %1").arg(row.song.episodes) : ""),
                       row.song.local && row.song.duration > 0 ? Math.floor(row.song.duration / 60) + ":" + String(Math.floor(row.song.duration % 60)).padStart(2, "0") : ""].filter(s => s).join(" · ")
                color: Theme.alpha(row.ink, 0.65)
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
        }
        IconButton {
            id: more
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            iconName: "ellipsis"
            tip: qsTr("More")
            onClicked: row.openMenu(more.x, more.y + more.height)
        }
    }
}
