import QtQuick
import Scancore

// Large page title with an optional subtitle and trailing controls. On
// phones, or whenever they would leave the title too little room, the
// controls go below the title, which would otherwise be squeezed to nothing
// beside them.
Item {
    id: header
    property string title
    property string subtitle
    property bool showBack: false
    // Keep the controls beside the title on phones too (icon buttons).
    property bool keepBeside: false
    default property alias trailing: trailingRow.data

    readonly property real titleX: showBack && !Theme.retro ? Theme.gutter + 34 : Theme.gutter
    readonly property bool stacked: !keepBeside && trailingRow.width > 0
        && (Theme.compact || width - titleX - Theme.gutter - trailingRow.width - 12 < 240)
    width: parent ? parent.width : 800
    implicitHeight: column.implicitHeight + 28 + (stacked ? trailingRow.height + 14 : 0)

    IconButton {
        id: back
        // Retro has its back button in the app bar.
        visible: header.showBack && !Theme.retro
        x: Theme.gutter - 8
        y: 26
        glassOrb: true
        iconName: "arrow-left"
        tip: qsTr("Back")
        onClicked: Router.back()
    }

    Column {
        id: column
        x: header.titleX
        y: 24
        width: parent.width - x - Theme.gutter - (header.stacked ? 0 : trailingRow.width + 12)
        spacing: 4
        // Retro: the app's name small above a big, thin title, as Windows
        // Phone's pages had.
        Text {
            visible: Theme.retro
            text: "SCANCORE"
            color: Theme.text
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            text: header.title
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.retro ? Math.round(Theme.text3xl * 1.55) : Theme.text3xl
            font.weight: Theme.retro ? Font.Light : Font.Bold
            font.letterSpacing: Theme.retro ? -1 : -0.6
            font.capitalization: Theme.retro ? (Retro.upperCase ? Font.MixedCase : Font.AllLowercase) : Font.MixedCase
            elide: Text.ElideRight
        }
        Text {
            visible: text.length > 0
            width: parent.width
            text: header.subtitle
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textMd
            elide: Text.ElideRight
        }
    }

    Row {
        id: trailingRow
        x: header.stacked ? Theme.gutter : parent.width - width - Theme.gutter
        y: header.stacked ? column.y + column.height + 14 : 30
        spacing: Theme.glass ? 10 : 8
        // Glass: icon buttons here are glass orbs as tall as the buttons
        // beside them (the drafts' header and filter buttons).
        onChildrenChanged: {
            for (const child of children) {
                if (child.glassOrb === undefined || child.headerOrb) continue
                child.headerOrb = true
                child.glassOrb = true
                if (child.diameter === 34) child.diameter = Qt.binding(() => Theme.glass ? 38 : 34)
            }
        }
    }
}
