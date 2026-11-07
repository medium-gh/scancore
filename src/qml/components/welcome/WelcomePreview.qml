import QtQuick
import Scancore

// The app in small for the welcome's look step: Standard's Home or Retro's
// hub, in the theme, accent and glass being picked (it reads Theme, so it
// changes the moment they do). Phone-shaped on phones.
Rectangle {
    id: pv
    property WelcomeCoverSource covers
    // A phone's Home (tabs, no sidebar) on phones.
    property bool phone: false
    readonly property bool phoneShape: phone || width < height * 1.1
    // Drawn at a fixed design size and scaled, so text stays in proportion.
    readonly property real u: width / (phoneShape ? 300 : 620)
    radius: Theme.retro ? 0 : 14
    color: Theme.base
    border.width: 1
    border.color: Theme.borderStrong
    clip: true
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Preview of the look you picked")

    // Standard: sidebar (wide), Home with covers and new chapters.
    Item {
        anchors.fill: parent
        visible: !Theme.retro
        Rectangle {
            visible: !pv.phoneShape
            width: 120 * pv.u
            height: parent.height
            color: Theme.sidebar
            Column {
                x: 14 * pv.u
                y: 18 * pv.u
                spacing: 12 * pv.u
                Icon { name: "scancore-mark"; size: 18 * pv.u; height: 9 * pv.u; color: Theme.text }
                Repeater {
                    model: ["house", "book-open", "book-text", "tv", "library", "download"]
                    Row {
                        required property string modelData
                        required property int index
                        spacing: 7 * pv.u
                        Icon { name: modelData; size: 12 * pv.u; color: index === 0 ? Theme.text : Theme.textMuted }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: [34, 40, 38, 32, 40, 52][index] * pv.u; height: 5 * pv.u; radius: 2
                            color: index === 0 ? Theme.text : Theme.alpha(Theme.text, 0.25)
                        }
                    }
                }
            }
        }
        Column {
            x: (pv.phoneShape ? 14 : 138) * pv.u
            y: 18 * pv.u
            width: parent.width - x - 14 * pv.u
            spacing: 10 * pv.u
            Text { text: qsTr("Home"); color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: 20 * pv.u; font.weight: Font.Bold }
            Text { text: qsTr("Continue reading"); color: Theme.textMuted; font.family: Theme.fontSans; font.pixelSize: 9 * pv.u; font.weight: Font.DemiBold }
            Row {
                spacing: 8 * pv.u
                Repeater {
                    model: pv.phoneShape ? 3 : 5
                    Column {
                        required property int index
                        spacing: 4 * pv.u
                        WelcomeArt {
                            width: 76 * pv.u; height: 108 * pv.u
                            seed: index * 3 + 1
                            source: pv.covers ? pv.covers.pick(index * 3 + 1) : ""
                        }
                        Rectangle {
                            width: 76 * pv.u; height: 3 * pv.u; radius: 2; color: Theme.alpha(Theme.text, 0.15)
                            Rectangle { width: parent.width * [0.7, 0.3, 0.9, 0.5, 0.15][index]; height: parent.height; radius: 2; color: Theme.accent }
                        }
                    }
                }
            }
            Text { text: qsTr("New chapters"); color: Theme.textMuted; font.family: Theme.fontSans; font.pixelSize: 9 * pv.u; font.weight: Font.DemiBold; topPadding: 4 * pv.u }
            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    width: parent.width
                    height: 34 * pv.u
                    radius: 8 * pv.u
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    WelcomeArt {
                        x: 5 * pv.u; y: 4 * pv.u; width: 18 * pv.u; height: 26 * pv.u
                        seed: 7 + index
                        source: pv.covers ? pv.covers.pick(7 + index) : ""
                    }
                    Rectangle { x: 30 * pv.u; y: 9 * pv.u; width: [90, 70, 110][index] * pv.u; height: 5 * pv.u; radius: 2; color: Theme.text }
                    Rectangle { x: 30 * pv.u; y: 19 * pv.u; width: 50 * pv.u; height: 4 * pv.u; radius: 2; color: Theme.textFaint }
                    Rectangle {
                        anchors.right: parent.right; anchors.rightMargin: 8 * pv.u; anchors.verticalCenter: parent.verticalCenter
                        width: 22 * pv.u; height: 12 * pv.u; radius: 6 * pv.u; color: Theme.accent
                    }
                }
            }
        }
        // A menu over the page: see-through with glass, solid without.
        Rectangle {
            x: parent.width - width - 18 * pv.u
            y: (pv.phoneShape ? 150 : 170) * pv.u
            width: 130 * pv.u
            height: 86 * pv.u
            radius: 12 * pv.u
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderStrong
            Column {
                x: 12 * pv.u; y: 12 * pv.u
                spacing: 9 * pv.u
                Repeater {
                    model: [qsTr("Add to library"), qsTr("Download"), qsTr("Share")]
                    Text { required property string modelData; text: modelData; color: Theme.text; font.family: Theme.fontSans; font.pixelSize: 10 * pv.u }
                }
            }
        }
    }

    // Retro: the hub, its name running off the edge, pivot words and tiles.
    Item {
        anchors.fill: parent
        visible: Theme.retro
        Text {
            x: 10 * pv.u
            y: 4 * pv.u
            text: "scancore"
            color: Theme.text
            font.family: Theme.fontSans
            font.pixelSize: 64 * pv.u
            font.weight: Font.Light
        }
        Row {
            x: 14 * pv.u
            y: 84 * pv.u
            spacing: 12 * pv.u
            Repeater {
                model: [qsTr("home"), qsTr("comics"), qsTr("anime")]
                Text {
                    required property string modelData
                    required property int index
                    text: modelData
                    color: index === 0 ? Theme.text : Theme.alpha(Theme.text, 0.4)
                    font.family: Theme.fontSans
                    font.pixelSize: 22 * pv.u
                    font.weight: Font.Light
                }
            }
        }
        Grid {
            id: tiles
            x: 14 * pv.u
            y: 122 * pv.u
            columns: pv.phoneShape ? 4 : 8
            spacing: 6 * pv.u
            readonly property real cell: (pv.width - 28 * pv.u - (columns - 1) * spacing) / columns
            Repeater {
                model: pv.phoneShape ? 20 : 24
                Rectangle {
                    required property int index
                    readonly property bool cover: [1, 4, 7, 9, 14, 16, 19, 22].indexOf(index) >= 0
                    width: tiles.cell
                    height: tiles.cell
                    color: [0, 5, 6, 11, 13, 18].indexOf(index) >= 0 ? Theme.alpha(Theme.tile, 0.7) : Theme.tile
                    WelcomeArt {
                        visible: parent.cover
                        anchors.fill: parent
                        corner: 0
                        seed: parent.index
                        source: pv.covers && parent.cover ? pv.covers.pick(parent.index) : ""
                    }
                    Icon {
                        visible: [2, 10, 17].indexOf(parent.index) >= 0
                        anchors.centerIn: parent
                        name: ["book-open", "tv", "library"][parent.index % 3]
                        size: 14 * pv.u
                        color: Theme.tileInk
                    }
                }
            }
        }
    }
}
