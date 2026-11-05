import QtQuick
import QtQuick.Effects
import Scancore

// The last thing read or watched, large, on Home: its cover over a blurred
// copy of itself, how far along it is, and one click to carry on.
Item {
    id: card
    property var entry: ({})
    readonly property bool anime: entry.media === "anime"
    readonly property real percent: entry.completed ? 1 : (entry.percent || 0)
    // A finished episode: the next one is what continues.
    readonly property real unitNumber: Number(entry.unitNumber)
    readonly property bool upNext: anime && entry.completed && unitNumber > 0
    readonly property string unitLine: upNext ? qsTr("Up next: episode %1").arg(Math.floor(unitNumber) + 1)
                                       : anime ? qsTr("Episode ") + (entry.unitNumber || entry.unitKey || "")
                                       : (entry.unitTitle || "")

    width: parent ? parent.width : 800
    implicitHeight: Theme.compact ? 148 : 176

    // Entrance: rise and fade in.
    opacity: 0
    transform: Translate { id: rise; y: 18 }
    Component.onCompleted: reveal.start()
    ParallelAnimation {
        id: reveal
        NumberAnimation { target: card; property: "opacity"; to: 1; duration: Motion.slow; easing.type: Easing.OutCubic }
        NumberAnimation { target: rise; property: "y"; to: 0; duration: Motion.page; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
    }

    Rectangle {
        id: panel
        x: Theme.gutter
        width: parent.width - 2 * Theme.gutter
        height: parent.height
        radius: Theme.radiusXl
        color: Theme.surface

        // Blurred cover across the card.
        Image {
            id: backdrop
            anchors.fill: parent
            source: card.entry.cover ? App.image(card.entry.cover) : ""
            sourceSize.width: 240
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: false
        }
        // The card's rounded shape, for the backdrop.
        Rectangle {
            id: shape
            anchors.fill: parent
            radius: panel.radius
            visible: false
            layer.enabled: true
        }
        // Blurred within the card's own bounds: by default the blur adds
        // padding around the item, which spilled a glow outside the card and
        // stretched the rounded mask over the larger area.
        Item {
            id: blurred
            anchors.fill: parent
            visible: false
            layer.enabled: true
            MultiEffect {
                anchors.fill: parent
                source: backdrop
                autoPaddingEnabled: false
                blurEnabled: true
                blur: 1
                blurMax: 48
                saturation: 0.2
            }
        }
        MultiEffect {
            anchors.fill: parent
            source: blurred
            maskEnabled: true
            maskSource: shape
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
            opacity: backdrop.status === Image.Ready ? (Theme.dark ? 0.5 : 0.35) : 0
            Behavior on opacity { NumberAnimation { duration: 700 } }
        }
        Rectangle {
            anchors.fill: parent
            radius: panel.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.alpha(Theme.surface, 0.35) }
                GradientStop { position: 0.7; color: Theme.alpha(Theme.surface, 0.92) }
                GradientStop { position: 1.0; color: Theme.surface }
            }
        }

        Cover {
            id: cover
            x: 18
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height - 36
            width: height / 1.5
            radius: Theme.radiusMd
            source: card.entry.cover || ""
            title: card.entry.title || ""
            scale: hover.hovered ? 1.03 : 1
            Behavior on scale { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } }
        }

        Column {
            anchors.left: cover.right
            anchors.leftMargin: Theme.compact ? 16 : 22
            anchors.right: Theme.compact ? parent.right : actions.left
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                text: card.anime ? qsTr("Continue watching") : qsTr("Continue reading")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.4
            }
            Text {
                width: parent.width
                text: card.entry.title || ""
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.compact ? Theme.textXl : Theme.text2xl
                font.weight: Font.Bold
                font.letterSpacing: -0.5
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: card.unitLine + (card.entry.progressAt ? "  ·  " + App.relativeTime(card.entry.progressAt) : "")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
            Row {
                topPadding: 6
                spacing: 10
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(220, card.width * 0.3)
                    height: 4
                    radius: 2
                    color: Theme.alpha(Theme.text, 0.12)
                    Rectangle {
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                        width: parent.width * Math.max(0.03, card.percent)
                        Behavior on width { NumberAnimation { duration: Motion.page; easing.type: Easing.OutCubic } }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.upNext ? "" : card.entry.completed ? qsTr("Finished") : Math.round(card.percent * 100) + "%"
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                    font.features: { "tnum": 1 }
                }
            }
        }

        Row {
            id: actions
            visible: !Theme.compact
            anchors.right: parent.right
            anchors.rightMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            ScButton {
                text: qsTr("Details")
                onClicked: Router.openSeries(card.entry, cover)
            }
            ScButton {
                variant: "primary"
                iconName: "play"
                text: qsTr("Continue")
                onClicked: Router.resume(card.entry)
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: panel.radius
            color: "transparent"
            border.width: 1
            border.color: hover.hovered ? Theme.borderStrong : Theme.border
            Behavior on border.color { ColorAnimation { duration: Motion.fast } }
        }

        HoverHandler { id: hover }
        // Phones: the whole card continues. A long press anywhere: its
        // options (remove from history…), as on every cover.
        TapHandler {
            id: tap
            onTapped: if (Theme.compact) Router.resume(card.entry)
            onLongPressed: Router.seriesMenu(card.entry, panel, tap.point.position.x, tap.point.position.y)
        }
        // A right click: the same options.
        TapHandler {
            acceptedButtons: Qt.RightButton
            // Mice only: touch taps ignore acceptedButtons (a tap would open it).
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onTapped: (point) => Router.seriesMenu(card.entry, panel, point.position.x, point.position.y)
        }
    }
}
