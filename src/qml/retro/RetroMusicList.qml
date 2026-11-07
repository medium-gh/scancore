import QtQuick
import QtQuick.Controls.Basic
import Scancore

// A list from the Retro music hub: your artists or albums (each opening its
// songs), your songs (or one artist's, one album's), the anime songs of one
// kind, the kinds themselves, or the songs you saved.
//   kind: "artists" | "albums" | "songs" | "kinds" | "saved"
//   artist / album / songKind: what "songs" is narrowed to.
Item {
    id: page
    property string kind: "songs"
    property string artist: ""
    property string album: ""
    property string songKind: ""

    readonly property var folders: Array.from(App.settings.musicFolders || [])
    property var localTask: folders.length && (kind === "artists" || kind === "albums" || kind === "songs") && !songKind ? Music.localSongs() : null
    readonly property var saved: { App.dependOn(Music.libraryRevision); return Music.library() }
    property var seasonTask: kind === "kinds" || songKind ? Music.seasonSongs() : null
    readonly property var season: {
        if (!seasonTask || !seasonTask.ok) return []
        const out = []
        for (const a of seasonTask.result) for (const s of a.songs) out.push(s)
        return out
    }
    readonly property var collection: {
        const seen = {}, out = []
        for (const s of (localTask && localTask.ok ? localTask.result : []).concat(saved))
            if (s && !seen[s.id]) { seen[s.id] = true; out.push(s) }
        return out
    }
    function kindLabel(type) {
        const t = String(type || "").toUpperCase()
        return t.startsWith("OP") ? qsTr("openings") : t.startsWith("ED") ? qsTr("endings") : qsTr("inserts")
    }
    // [{ label, detail, cover, songs }] for artists, albums and kinds.
    readonly property var groups: {
        const map = {}
        const add = (key, song) => {
            if (!key) return
            if (!map[key]) map[key] = { label: key, cover: song.cover || "", songs: [] }
            map[key].songs.push(song)
        }
        if (kind === "artists") for (const s of collection) for (const a of (s.artists || [])) add(a, s)
        if (kind === "albums") for (const s of collection) add(s.album, s)
        if (kind === "kinds") for (const s of season) add(kindLabel(s.type), s)
        return Object.values(map).sort((a, b) => a.label.localeCompare(b.label))
    }
    readonly property var songs: {
        let list = kind === "saved" ? saved : songKind ? season.filter(s => kindLabel(s.type) === songKind) : collection
        if (artist) list = list.filter(s => (s.artists || []).indexOf(artist) >= 0)
        if (album) list = list.filter(s => s.album === album)
        return list.slice().sort((a, b) => String(a.title).localeCompare(String(b.title)))
    }
    readonly property bool grouped: kind === "artists" || kind === "albums" || kind === "kinds"
    readonly property string heading: artist || album || songKind
        || (kind === "artists" ? qsTr("artists") : kind === "albums" ? qsTr("albums") : kind === "kinds" ? qsTr("openings & endings")
            : kind === "saved" ? qsTr("saved") : qsTr("songs"))
    readonly property bool loading: (localTask && localTask.loading) || (seasonTask && seasonTask.loading)

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        Glide { flick: list }
        bottomMargin: 120
        header: Column {
            width: list.width
            spacing: 8
            PageHeader {
                title: page.heading
                subtitle: page.grouped ? page.groups.length + "" : page.songs.length + ""
                showBack: true
                ScButton {
                    visible: !page.grouped && page.songs.length > 0
                    text: qsTr("Shuffle")
                    iconName: "shuffle"
                    onClicked: {
                        MusicQueue.shuffle = true
                        MusicQueue.playList(page.songs, Math.floor(Math.random() * page.songs.length))
                    }
                }
            }
            Text {
                visible: !page.loading && (page.grouped ? page.groups.length === 0 : page.songs.length === 0)
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                text: page.seasonTask && !page.seasonTask.ok && !page.seasonTask.loading ? page.seasonTask.error
                    : page.kind === "saved" ? qsTr("Songs you save show here.")
                    : page.kind === "kinds" ? qsTr("Nothing this season yet.")
                    : qsTr("Add your music folders on the full music page to see your music here.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                wrapMode: Text.WordWrap
            }
        }
        model: page.grouped ? page.groups : page.songs
        delegate: Loader {
            required property var modelData
            required property int index
            width: list.width
            sourceComponent: page.grouped ? groupRow : songRow
            property var entry: modelData
            property int row: index
        }
    }

    Component {
        id: groupRow
        AbstractButton {
            id: group
            readonly property var g: parent ? parent.entry : ({})
            width: list.width
            height: 64
            background: null
            onClicked: Router.push("retro/RetroMusicList.qml", page.kind === "artists" ? { kind: "songs", artist: g.label }
                                   : page.kind === "albums" ? { kind: "songs", album: g.label } : { kind: "songs", songKind: g.label })
            contentItem: Row {
                x: Theme.gutter
                spacing: 14
                Rectangle {
                    visible: page.kind === "albums"
                    width: 52
                    height: 52
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.tile
                    Image {
                        anchors.fill: parent
                        source: group.g.cover ? App.image(group.g.cover) : ""
                        sourceSize.width: 104
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: group.g.label || ""
                        color: group.pressed ? Theme.tile : Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.compact ? 24 : 26
                        font.weight: Font.Light
                        elide: Text.ElideRight
                        width: list.width - 2 * Theme.gutter - 66
                    }
                    Text {
                        text: (group.g.songs || []).length === 1 ? qsTr("1 song") : qsTr("%1 songs").arg((group.g.songs || []).length)
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                    }
                }
            }
        }
    }

    Component {
        id: songRow
        SongRow {
            width: list.width
            song: parent ? parent.entry : ({})
            onPlayed: MusicQueue.playList(page.songs, parent ? parent.row : 0)
        }
    }
}
