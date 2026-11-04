import QtQuick
import Scancore

// One "Continue the story" card on Home: an anime season you finished and
// where its comic or novel goes on, or a comic you caught up on whose novel
// goes further.
Rectangle {
    id: card
    property var entry: ({})
    signal open(var entry)

    readonly property bool fromAnime: entry.kind === "anime"
    readonly property string kind: {
        const type = String(entry.type || "")
        if (type === "Manhwa") return qsTr("Manhwa")
        if (type === "Manhua") return qsTr("Manhua")
        if (type === "Manga") return qsTr("Manga")
        return entry.media === "novel" ? qsTr("Novel") : qsTr("Comic")
    }
    readonly property string where: entry.volume > 0 ? qsTr("volume %1").arg(entry.volume) : qsTr("chapter %1").arg(Number(entry.chapter))

    width: Theme.compact ? 300 : 360
    height: Theme.compact ? 132 : 144
    radius: Theme.radiusXl
    color: hover.hovered ? Theme.surface2 : Theme.surface
    border.width: 1
    border.color: Theme.border

    HoverHandler { id: hover }
    TapHandler { onTapped: card.open(card.entry) }

    Cover {
        id: cover
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height - 24
        width: Math.round(height / 1.5)
        radius: Theme.radiusMd
        source: card.entry.cover || ""
    }

    Column {
        anchors.left: cover.right
        anchors.leftMargin: 14
        anchors.right: parent.right
        anchors.rightMargin: 36
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Text {
            width: parent.width
            text: card.fromAnime ? qsTr("You finished %1").arg(card.entry.animeTitle) : qsTr("Caught up on %1").arg(card.entry.comicTitle)
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: card.fromAnime ? qsTr("%1 continues from %2").arg(card.kind).arg(card.where)
                                 : qsTr("Its novel goes further")
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.textLg
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: card.fromAnime && card.entry.done > 0 ? qsTr("You are at chapter %1").arg(card.entry.done) : (card.entry.title || "")
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            elide: Text.ElideRight
        }
    }

    IconButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        diameter: 28
        iconSize: 14
        iconName: "x"
        tip: qsTr("Not now")
        onClicked: Story.dismiss(card.entry.key)
    }
}
