import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Music in the Retro mode: a hub like Windows Phone's music one, panned
// sideways under a huge title. "collection" lists your music by artist,
// album and song, the anime songs by kind, the songs you saved and the
// music extensions; "new" is this season's anime songs as tiles; "now
// playing" is what plays, or a way to start. Each link opens a list
// (RetroMusicList).
Item {
    id: hub

    // Your files (from the folders chosen on the full music page) and the
    // songs you saved.
    readonly property var folders: Array.from(App.settings.musicFolders || [])
    property var localTask: folders.length ? Music.localSongs() : null
    readonly property var local: localTask && localTask.ok ? localTask.result : []
    readonly property var saved: { App.dependOn(Music.libraryRevision); return Music.library() }
    readonly property var collection: {
        const seen = {}, out = []
        for (const s of local.concat(saved))
            if (s && !seen[s.id]) { seen[s.id] = true; out.push(s) }
        return out
    }
    // This season's anime songs, one list.
    property var seasonTask: Music.animeSongsAvailable ? Music.seasonSongs() : null
    readonly property var season: {
        if (!seasonTask || !seasonTask.ok) return []
        const out = []
        for (const a of seasonTask.result) for (const s of a.songs) out.push(s)
        return out
    }
    readonly property var sources: { App.dependOn(Extensions.revision); return Music.sources() }

    function count(list, key) {
        const seen = {}
        for (const s of list) {
            const values = key === "artists" ? (s.artists || []) : [s.album || ""]
            for (const v of values) if (v) seen[v] = true
        }
        return Object.keys(seen).length
    }
    function open(kind, extra) {
        Router.push("retro/RetroMusicList.qml", Object.assign({ kind: kind }, extra || {}))
    }
    // The full music page, from one source: anime songs, files or extensions.
    function full(from) {
        if (from) App.setSetting("musicFrom", from)
        Router.push("pages/MusicPage.qml", {})
    }

    Panorama {
        id: pano
        anchors.fill: parent
        focus: true
        title: qsTr("music")
        gradientTitle: Retro.gradientTitle
        upperCase: Retro.upperCase
        sections: [{ key: "collection", label: qsTr("collection") },
                   { key: "new", label: qsTr("new") },
                   { key: "playing", label: qsTr("now playing") }]
        pages: ({ collection: collectionPage, new: newPage, playing: playingPage })
    }

    // A big light link, as the hub's lists are.
    component Link: AbstractButton {
        id: link
        property string label
        property string detail
        width: parent ? parent.width : 300
        height: linkText.implicitHeight + 12
        background: null
        contentItem: Row {
            spacing: 10
            Text {
                id: linkText
                text: link.label
                color: link.pressed ? Theme.tile : link.enabled ? Theme.text : Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.compact ? 30 : 32
                font.weight: Font.Light
            }
            Text {
                visible: link.detail.length > 0
                anchors.baseline: linkText.baseline
                text: link.detail
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
        }
    }

    Component {
        id: collectionPage
        Item {
            property string sectionKey
            property bool current
            SmoothFlickable {
                anchors.fill: parent
                contentHeight: links.height + 120
                Column {
                    id: links
                    width: parent.width
                    spacing: 2
                    Link { label: qsTr("artists"); detail: hub.collection.length ? String(hub.count(hub.collection, "artists")) : ""; onClicked: hub.open("artists") }
                    Link { label: qsTr("albums"); detail: hub.collection.length ? String(hub.count(hub.collection, "albums")) : ""; onClicked: hub.open("albums") }
                    Link { label: qsTr("songs"); detail: hub.collection.length ? String(hub.collection.length) : ""; onClicked: hub.open("songs") }
                    Link { visible: Music.animeSongsAvailable; label: qsTr("openings & endings"); onClicked: hub.open("kinds") }
                    Link { label: qsTr("saved"); detail: hub.saved.length ? String(hub.saved.length) : ""; onClicked: hub.open("saved") }
                    Link { visible: hub.sources.length > 0; label: qsTr("radio"); detail: String(hub.sources.length); onClicked: hub.full("extensions") }
                    Item { width: 1; height: 18 }
                    // Nothing of your own yet: where folders are chosen.
                    Text {
                        visible: hub.collection.length === 0
                        width: parent.width
                        text: qsTr("Add your music folders on the full music page to see your artists, albums and songs here.")
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.WordWrap
                    }
                    Link {
                        label: qsTr("all music")
                        onClicked: hub.full("")
                    }
                }
            }
        }
    }

    Component {
        id: newPage
        Item {
            id: fresh
            property string sectionKey
            property bool current
            readonly property real tileSize: Math.floor((width - 8) / 2)
            SmoothFlickable {
                anchors.fill: parent
                contentHeight: grid.height + 120
                Flow {
                    id: grid
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: hub.season.slice(0, 12)
                        AbstractButton {
                            id: songTile
                            required property var modelData
                            required property int index
                            width: fresh.tileSize
                            height: fresh.tileSize
                            background: Rectangle { color: Theme.tile }
                            onClicked: MusicQueue.playList(hub.season, index)
                            contentItem: Item {
                                Image {
                                    anchors.fill: parent
                                    source: songTile.modelData.cover ? App.image(songTile.modelData.cover) : ""
                                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    opacity: 0.85
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        GradientStop { position: 0.45; color: "transparent" }
                                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.7) }
                                    }
                                }
                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 8
                                    text: songTile.modelData.title
                                    color: Theme.tileInk
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.textSm
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: hub.season.length === 0
                    width: parent.width
                    text: hub.seasonTask && hub.seasonTask.loading ? qsTr("Loading…")
                        : hub.seasonTask && !hub.seasonTask.ok ? hub.seasonTask.error : qsTr("Nothing new this season yet.")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Component {
        id: playingPage
        Item {
            id: playing
            property string sectionKey
            property bool current
            Column {
                width: parent.width
                spacing: 12
                AbstractButton {
                    visible: MusicQueue.active
                    width: Math.min(parent.width, 320)
                    height: width
                    background: Rectangle { color: Theme.tile }
                    onClicked: Router.openNowPlaying()
                    contentItem: Image {
                        source: MusicQueue.current && MusicQueue.current.cover ? App.image(MusicQueue.current.cover) : ""
                        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
                Text {
                    width: parent.width
                    text: MusicQueue.active ? MusicQueue.current.title : qsTr("nothing playing")
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.compact ? 26 : 28
                    font.weight: Font.Light
                    elide: Text.ElideRight
                }
                Text {
                    visible: MusicQueue.active
                    width: parent.width
                    text: MusicQueue.artistsOf(MusicQueue.current)
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    elide: Text.ElideRight
                }
                Link {
                    label: MusicQueue.active ? qsTr("open now playing") : qsTr("shuffle my music")
                    enabled: MusicQueue.active || hub.collection.length > 0 || hub.season.length > 0
                    onClicked: {
                        if (MusicQueue.active) { Router.openNowPlaying(); return }
                        const list = hub.collection.length ? hub.collection : hub.season
                        MusicQueue.shuffle = true
                        MusicQueue.playList(list, Math.floor(Math.random() * list.length))
                    }
                }
            }
        }
    }
}
