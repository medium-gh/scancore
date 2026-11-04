import QtQuick
import Scancore

// One setting inside a SettingsGroup: an optional icon, a title and
// description, and its control on the right. On phones a wide control drops
// below the text. A row without a control can be made clickable, which adds
// a chevron (or an external-link mark) and a hover wash.
Item {
    id: row
    property string title
    property string description
    property string iconName
    property color iconTint: Theme.textMuted
    property bool clickable: false
    property bool external: false
    property bool danger: false
    signal clicked()
    default property alias control: holder.data

    readonly property int pad: 16
    readonly property int textX: pad + (iconName ? 32 + 14 : 0)
    // Its control under the text on every screen (wide controls such as a
    // row of chips); any control that would squeeze the text into a narrow
    // column goes there too (a TV's settings pane is narrow as well), and on
    // phones one taking much of the width.
    property bool wide: false
    readonly property real besideWidth: width - textX - pad - (holder.width > 0 ? holder.width + 16 : 0) - trailing
    readonly property bool stacked: wide || besideWidth < 160 || (Theme.compact && holder.width > width * 0.42)
    readonly property real trailing: clickable ? 22 : 0

    width: parent ? parent.width : 600
    implicitHeight: stacked
        ? 14 + texts.implicitHeight + 12 + holder.height + 16
        : Math.max(texts.implicitHeight, holder.height, iconName ? 32 : 0) + 28

    // Inset divider between rows of the same group.
    Rectangle {
        x: row.textX
        width: parent.width - x
        height: 1
        color: Theme.border
        visible: row.y > 0
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.radiusMd
        color: Theme.alpha(Theme.text, press.pressed ? 0.08 : 0.04)
        visible: row.clickable && (press.containsMouse || press.pressed)
    }

    Rectangle {
        visible: row.iconName.length > 0
        x: row.pad
        y: row.stacked ? 14 : (parent.height - height) / 2
        width: 32
        height: 32
        radius: 9
        color: Theme.alpha(row.iconTint, 0.14)
        Icon {
            anchors.centerIn: parent
            name: row.iconName
            size: 16
            color: row.iconTint
        }
    }

    Column {
        id: texts
        x: row.textX
        y: row.stacked ? 14 : (parent.height - height) / 2
        width: row.stacked ? parent.width - x - row.pad : row.besideWidth
        spacing: 3
        Text {
            width: parent.width
            text: row.title
            textFormat: Text.PlainText
            color: row.danger ? Theme.danger : Theme.text
            font.family: Theme.fontSans
            font.pixelSize: Theme.textMd
            font.weight: Font.Medium
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: row.description
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            lineHeight: 1.15
            wrapMode: Text.Wrap
        }
    }

    Item {
        id: holder
        // Under the text; one wider than the text column starts under the
        // icon instead, so it stays inside the card.
        x: !row.stacked ? parent.width - width - row.pad - row.trailing
            : width > parent.width - row.textX - row.pad ? row.pad : row.textX
        y: row.stacked ? texts.y + texts.implicitHeight + 12 : (parent.height - height) / 2
        width: childrenRect.width
        height: childrenRect.height
    }

    Icon {
        visible: row.clickable
        anchors.right: parent.right
        anchors.rightMargin: row.pad - 2
        anchors.verticalCenter: parent.verticalCenter
        name: row.external ? "external-link" : "chevron-right"
        size: row.external ? 15 : 18
        color: Theme.textFaint
    }

    MouseArea {
        id: press
        anchors.fill: parent
        enabled: row.clickable
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }
}
