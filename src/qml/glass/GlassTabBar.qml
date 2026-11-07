import QtQuick
import QtQuick.Controls.Basic
import Scancore

// The Glass mode's bottom navigation on phones: one liquid surface, floating
// over the page. A capsule of tabs whose selection is a droplet (it flows to
// the tab tapped, stretching as it goes), a search orb beside it, and an
// accessory above for music that is playing. Scrolling down draws the bar
// in to the current tab alone, and the accessory pours down beside it a
// moment later; scrolling up lifts the accessory off first, then the bar
// grows back. Everything moves on velocity-keeping springs (LiquidSpring).
Item {
    id: bar
    property string current: "home"
    signal navigate(string section)
    signal openPalette()

    readonly property var tabs: [
        { key: "home", label: qsTr("Home"), icon: "house" },
        { key: "comics", label: qsTr("Comics"), icon: "book-open" },
        { key: "novels", label: qsTr("Novels"), icon: "book-text" },
        { key: "anime", label: qsTr("Anime"), icon: "tv" },
        { key: "music", label: qsTr("Music"), icon: "volume-2" },
        { key: "library", label: qsTr("Library"), icon: "library" }
    ].filter(t => Router.sectionShown(t.key)
                  && (t.key !== "music" || Array.from(App.settings.shownMedia || ["comic", "novel", "anime"]).length < 3))
    readonly property int currentIndex: {
        const i = tabs.findIndex(t => t.key === current)
        return i >= 0 ? i : tabs.length - 1
    }

    // Five tabs at most (Home, your media, Library): everything else is in
    // search (the orb opens the command palette, which lists every section).
    // Sizes (the draft's): a 64 px bar and orb, 26 px from the sides and 20 from the
    // bottom; the accessory 52 px, 12 above.
    readonly property real side: 26
    readonly property real gap: 12
    readonly property real barH: 64
    readonly property real accH: 52
    readonly property real baseY: height - 20
    readonly property real fullW: width - 2 * side - barH - gap
    readonly property real slot: (fullW - 8) / Math.max(1, tabs.length)
    readonly property bool accessory: MusicQueue.active
    readonly property bool minimized: GlassScene.barMinimized
    // The accessory follows the bar a beat later (and leaves a beat earlier).
    property bool accDown: false
    onMinimizedChanged: accBeat.restart()
    Timer { id: accBeat; interval: bar.minimized ? 180 : 0; onTriggered: bar.accDown = bar.minimized }

    // What the pages leave clear below them.
    readonly property real clearance: barH + 20 + 12 + (accessory && !accDown ? accH + gap : 0)
    onClearanceChanged: GlassScene.clearance = clearance
    Component.onCompleted: GlassScene.clearance = clearance
    Component.onDestruction: GlassScene.clearance = 0
    onVisibleChanged: GlassScene.clearance = visible ? clearance : 0

    // ---------------------------------------------------------------- springs
    LiquidSpring { id: barW; target: bar.minimized ? bar.barH : bar.fullW }
    LiquidSpring { id: lensX; stiffness: 300; damping: 18; target: bar.minimized ? bar.side + 4 : bar.side + 4 + bar.currentIndex * bar.slot }
    LiquidSpring { id: lensW; stiffness: 300; damping: 18; target: bar.minimized ? bar.barH - 8 : bar.slot }
    LiquidSpring { id: labels; stiffness: 280; damping: 22; target: bar.minimized ? 0 : 1 }
    LiquidSpring { id: accX; target: bar.accDown ? bar.side + bar.barH + bar.gap : bar.side }
    LiquidSpring { id: accW; target: !bar.accessory ? 0 : bar.accDown ? bar.width - 2 * bar.side - 2 * (bar.barH + bar.gap) : bar.width - 2 * bar.side }
    LiquidSpring { id: accY; target: bar.accDown ? bar.baseY - bar.barH + (bar.barH - bar.accH) / 2 : bar.baseY - bar.barH - bar.gap - bar.accH }
    LiquidSpring { id: press; stiffness: 520; damping: 14 }
    LiquidSpring { id: orbPress; stiffness: 520; damping: 14 }

    function speed(s, scale) { return Math.min(1, Math.abs(s.velocity) / scale) }

    // ---------------------------------------------------------------- glass
    GlassSurface {
        id: surface
        anchors.fill: parent
        merge: 22
        readonly property real barStretch: 1 + 0.18 * bar.speed(barW, 2200)
        readonly property real lensStretch: 1 + 0.45 * bar.speed(lensX, 1600)
        readonly property real lensH: (bar.barH - 8) / Math.sqrt(lensStretch) * (1 - 0.08 * press.value)
        readonly property real lensWide: lensW.value * lensStretch * (1 + 0.04 * press.value)
        readonly property real orbS: 1 - 0.1 * orbPress.value
        shapes: [
            { x: bar.side, y: bar.baseY - bar.barH / barStretch, w: barW.value, h: bar.barH / barStretch, r: bar.barH / 2,
              lift: bar.speed(barW, 1500) },
            { x: lensX.value + lensW.value / 2 - lensWide / 2, y: bar.baseY - bar.barH / 2 - lensH / 2, w: lensWide, h: lensH, r: lensH / 2,
              lift: Math.min(1, bar.speed(lensX, 900) + press.value), inset: true },
            { x: bar.width - bar.side - bar.barH * (2 - orbS), y: bar.baseY - bar.barH / 2 - bar.barH * orbS / 2,
              w: bar.barH * (2 - orbS), h: bar.barH * orbS, r: bar.barH * orbS / 2, lift: orbPress.value },
            { x: accX.value, y: accY.value, w: accW.value, h: bar.accH, r: bar.accH / 2, lift: bar.speed(accY, 900) }
        ]
    }

    // ---------------------------------------------------------------- what is on it
    GlassInk {
        anchors.fill: parent
        // The current tab is in the accent, the accessory shows a cover.
        keepColour: true
        // Tabs: every one while open; drawn in, only the current one,
        // centred in the small bar.
        Repeater {
            model: bar.tabs
            Item {
                id: tabFace
                required property var modelData
                required property int index
                readonly property bool isCurrent: index === bar.currentIndex
                readonly property real openX: bar.side + 4 + index * bar.slot
                x: isCurrent ? labels.value * openX + (1 - labels.value) * (bar.side + 4) : openX
                width: isCurrent ? labels.value * bar.slot + (1 - labels.value) * (bar.barH - 8) : bar.slot
                y: bar.baseY - bar.barH
                height: bar.barH
                opacity: isCurrent ? 1 : labels.value
                visible: opacity > 0.01
                Column {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: (1 - labels.value) * 7
                    spacing: 3
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: tabFace.modelData.icon
                        size: 22
                        color: tabFace.isCurrent ? Theme.accent : "white"
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: bar.slot - 4
                        horizontalAlignment: Text.AlignHCenter
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 9
                        elide: Text.ElideRight
                        opacity: labels.value
                        text: tabFace.modelData.label
                        color: tabFace.isCurrent ? Theme.accent : "#b3ffffff"
                        font.family: Theme.fontSans
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
                Rectangle {
                    readonly property int count: tabFace.modelData.key === "library" ? Library.unreadUpdates : 0
                    visible: count > 0
                    x: parent.width / 2 + 9
                    y: 12
                    width: 8; height: 8; radius: 4
                    color: Theme.accent
                }
            }
        }
        Icon {
            x: bar.width - bar.side - bar.barH / 2 - width / 2
            y: bar.baseY - bar.barH / 2 - height / 2
            name: "search"
            size: 24
            color: "white"
        }
        // Unread notifications: a dot on the orb (they are in search).
        Rectangle {
            visible: Notifications.unread > 0
            x: bar.width - bar.side - bar.barH / 2 + 9
            y: bar.baseY - bar.barH / 2 - 15
            width: 8; height: 8; radius: 4
            color: Theme.accent
        }
        // The accessory: what is playing.
        Item {
            x: accX.value; y: accY.value; width: accW.value; height: bar.accH
            clip: true
            opacity: Math.max(0, Math.min(1, (accW.value - 110) / 70))
            visible: opacity > 0.01
            Rectangle {
                id: art
                x: 8; anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 36; radius: 18
                color: Theme.alpha(Theme.accent, 0.8)
                clip: true
                Image {
                    anchors.fill: parent
                    source: MusicQueue.current && MusicQueue.current.cover ? App.image(MusicQueue.current.cover) : ""
                    sourceSize.width: 72
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }
            Column {
                anchors.left: art.right; anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 46 - 96
                Text { width: parent.width; text: MusicQueue.current ? MusicQueue.current.title : ""; elide: Text.ElideRight
                       color: "white"; font.family: Theme.fontSans; font.pixelSize: 14; font.weight: Font.DemiBold }
                Text { width: parent.width; text: MusicQueue.artistsOf(MusicQueue.current); elide: Text.ElideRight
                       color: "#b3ffffff"; font.family: Theme.fontSans; font.pixelSize: 12 }
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 18
                Icon { name: AudioPlayer.paused ? "play" : "pause"; size: 22; color: "white" }
                Icon { name: "skip-forward"; size: 22; color: "white" }
            }
        }
    }

    // ---------------------------------------------------------------- input
    // Taps go to plain areas over each part (the glass itself takes none).
    Repeater {
        model: bar.minimized ? 0 : bar.tabs.length
        MouseArea {
            required property int index
            x: bar.side + 4 + index * bar.slot; y: bar.baseY - bar.barH
            width: bar.slot; height: bar.barH
            onPressedChanged: if (index === bar.currentIndex) press.target = pressed ? 1 : 0
            onClicked: bar.navigate(bar.tabs[index].key)
        }
    }
    MouseArea {
        // Drawn in: the small bar opens it again.
        visible: bar.minimized
        x: bar.side; y: bar.baseY - bar.barH; width: bar.barH; height: bar.barH
        onClicked: GlassScene.barMinimized = false
    }
    MouseArea {
        x: bar.width - bar.side - bar.barH; y: bar.baseY - bar.barH; width: bar.barH; height: bar.barH
        onPressedChanged: orbPress.target = pressed ? 1 : 0
        onClicked: bar.openPalette()
    }
    MouseArea {
        visible: bar.accessory && accW.value > 110
        x: accX.value; y: accY.value; width: accW.value - 96; height: bar.accH
        onClicked: Router.openNowPlaying()
    }
    MouseArea {
        visible: bar.accessory && accW.value > 110
        x: accX.value + accW.value - 96; y: accY.value; width: 48; height: bar.accH
        onClicked: AudioPlayer.toggle()
    }
    MouseArea {
        visible: bar.accessory && accW.value > 110
        x: accX.value + accW.value - 48; y: accY.value; width: 48; height: bar.accH
        onClicked: MusicQueue.next()
    }

}
