import QtQuick
import QtQuick.Effects
import Scancore

// The Glass mode's wallpaper behind the pages: the art of what was last
// opened, of the section's own kind (Comics: the last comic, Novels: the
// last novel, Anime: the last anime; Home and the rest: whatever was
// last), as the desktop draft has it: blurred, darker and a little more
// colourful (its shapes still show), with no veil; in a light theme
// lighter instead. A new section's art fades in over the old one. With
// no history yet, a trending anime's banner.
Item {
    id: wall
    property string section: "home"
    readonly property string media: ({ comics: "comic", novels: "novel", anime: "anime" })[section] || ""

    // What was last opened, of this section's kind (or of any, when none).
    property var task: null
    property var anyTask: null
    function reload() {
        if (!visible) return
        task = Library.continueItems(media, 1)
        if (media !== "" && !anyTask) anyTask = Library.continueItems("", 1)
    }
    onMediaChanged: reload()
    onVisibleChanged: reload()
    Component.onCompleted: reload()
    Connections {
        target: Library
        // The art follows what was opened last, without reloading on every
        // progress save.
        function onChanged() { if (wall.visible) refresh.restart() }
    }
    Timer { id: refresh; interval: 1500; onTriggered: { wall.anyTask = null; wall.reload() } }

    function first(t) { return t && t.ok && t.result.length ? t.result[0] : null }
    readonly property var entry: first(task) || first(anyTask)
    // Nothing opened yet (a new install, a TV): what is trending, so the
    // glass has something to show and is not plain grey over black.
    readonly property bool waiting: (!task || task.ok) && (!anyTask || anyTask.ok) && !entry
    property var trending: waiting && visible ? Anime.home() : null
    readonly property string trendingArt: {
        if (!trending || !trending.ok) return ""
        for (const it of trending.result.trending || [])
            if (it && !it.isAdult && (it.banner || it.cover)) return it.banner || it.cover
        return ""
    }
    readonly property string art: entry ? ((entry.extra && entry.extra.banner) || entry.cover || "") : trendingArt
    readonly property string url: art ? App.image(art, entry ? entry.referer || "" : "") : ""

    // Two pictures: the one showing, and the next one loading behind it.
    property int front: 0
    onUrlChanged: {
        const back = front === 0 ? pictureB : pictureA
        if (url === (front === 0 ? pictureA : pictureB).source.toString()) return
        back.source = url
        if (back.status === Image.Ready) front = 1 - front
    }
    component Picture: Image {
        anchors.fill: parent
        visible: false
        sourceSize.width: 600
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    Picture { id: pictureA; onStatusChanged: if (status === Image.Ready && wall.front !== 0 && source.toString() === wall.url) wall.front = 0 }
    Picture { id: pictureB; onStatusChanged: if (status === Image.Ready && wall.front !== 1 && source.toString() === wall.url) wall.front = 1 }

    component Blurred: MultiEffect {
        property bool shown: false
        anchors.fill: parent
        visible: opacity > 0
        opacity: shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Motion.hero; easing.type: Easing.InOutQuad } }
        autoPaddingEnabled: false
        blurEnabled: true
        blur: 1
        blurMax: 40
        brightness: Theme.dark ? -0.3 : 0.42
        saturation: Theme.dark ? 0.35 : 0.05
    }
    Blurred { source: pictureA; shown: wall.front === 0 && pictureA.status === Image.Ready && wall.url !== "" }
    Blurred { source: pictureB; shown: wall.front === 1 && pictureB.status === Image.Ready && wall.url !== "" }
    // Light art (a pale banner) would still leave the ground pale: black,
    // not the theme's grey, takes it down further and keeps its colour.
    Rectangle {
        anchors.fill: parent
        visible: Theme.dark && wall.url !== ""
        color: "black"
        opacity: 0.4
    }
}
