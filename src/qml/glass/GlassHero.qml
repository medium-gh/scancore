import QtQuick
import QtQuick.Effects
import Scancore

// Home in the Glass mode: what was read or watched last, as the hero. Its
// art fills the top of the page, and a glass card on it says where to pick
// up (title, progress, chapter or episode, when) with a Continue button.
// On phones a glass search capsule and an account orb float at the top.
// The glass here shows the hero's art (it is inside the page, which the
// window's glass stage holds). Texts come from ResumeCard's (translated).
Item {
    id: hero
    property var entry: ({})
    signal editPage()

    readonly property bool anime: entry.media === "anime"
    readonly property real percent: entry.completed ? 1 : (entry.percent || 0)
    readonly property real unitNumber: Number(entry.unitNumber)
    readonly property bool upNext: anime && entry.completed && unitNumber > 0
    readonly property string unitLine: upNext ? qsTranslate("ResumeCard", "Up next: episode %1").arg(Math.floor(unitNumber) + 1)
                                       : anime ? qsTranslate("ResumeCard", "Episode ") + (entry.unitNumber || entry.unitKey || "")
                                       : (entry.unitTitle || "")
    // Phones: the tall cover fits a tall hero; computers: the wide banner.
    readonly property string art: Theme.compact ? (entry.cover || (entry.extra && entry.extra.banner) || "")
                                                : ((entry.extra && entry.extra.banner) || entry.cover || "")

    width: parent ? parent.width : 400
    implicitHeight: Theme.compact ? 470 : 430

    // ---------------------------------------------------------------- the art
    // A layer: the glass on it shows it.
    Item {
        id: artLayer
        anchors.fill: parent
        layer.enabled: true
        layer.textureSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
        Image {
            id: artImage
            anchors.fill: parent
            visible: false
            source: hero.art ? App.image(hero.art, hero.entry.referer || "") : ""
            sourceSize.width: Math.min(1400, Math.ceil(hero.width * Screen.devicePixelRatio))
            fillMode: Image.PreserveAspectCrop
            verticalAlignment: Image.AlignTop
            asynchronous: true
        }
        // The art melts into the page (to transparent: the wallpaper below
        // shows through, no seam): along the bottom, and on computers along
        // the side by the sidebar too. Two masks, one after the other. Soft
        // masks: alpha ramps over 0..1 (threshold 0.5, spread 1; by default
        // MultiEffect cuts a mask to a hard edge).
        Rectangle {
            id: fadeDown
            anchors.fill: parent
            visible: false
            layer.enabled: true
            gradient: Gradient {
                GradientStop { position: 0; color: "white" }
                GradientStop { position: 0.45; color: "white" }
                GradientStop { position: 1; color: "transparent" }
            }
        }
        Rectangle {
            id: fadeSide
            anchors.fill: parent
            visible: false
            layer.enabled: true
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.compact ? "white" : "transparent" }
                GradientStop { position: Theme.compact ? 0.01 : Math.min(0.4, 260 / Math.max(1, hero.width)); color: "white" }
                GradientStop { position: 1; color: "white" }
            }
        }
        MultiEffect {
            id: fadedDown
            anchors.fill: parent
            visible: false
            layer.enabled: true
            source: artImage
            maskEnabled: true
            maskSource: fadeDown
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
        MultiEffect {
            anchors.fill: parent
            source: fadedDown
            maskEnabled: true
            maskSource: fadeSide
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
            opacity: artImage.status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Motion.hero } }
        }
    }

    // ---------------------------------------------------------------- motion
    // The card pours in when Home opens; presses squash.
    LiquidSpring { id: rise; stiffness: 170; damping: 15 }
    Component.onCompleted: { rise.snap(60); rise.target = 0 }
    LiquidSpring { id: cardPress; stiffness: 520; damping: 14; target: cardTap.pressed ? 1 : 0 }
    LiquidSpring { id: goPress; stiffness: 560; damping: 13; target: goTap.pressed ? 1 : 0 }
    LiquidSpring { id: searchPress; stiffness: 520; damping: 14; target: searchTap.pressed ? 1 : 0 }
    LiquidSpring { id: orbPress; stiffness: 520; damping: 14; target: orbTap.pressed ? 1 : 0 }

    // ---------------------------------------------------------------- the card
    Item {
        id: card
        readonly property real squash: cardPress.value
        x: Theme.compact ? 16 : Theme.gutter
        width: Theme.compact ? hero.width - 32 : Math.min(580, hero.width - 2 * Theme.gutter)
        height: 200
        y: hero.height - 24 - height + rise.value
        opacity: Math.max(0, 1 - rise.value / 60)
        transform: Scale { origin.x: card.width / 2; origin.y: card.height / 2
                           xScale: 1 + 0.015 * card.squash; yScale: 1 - 0.025 * card.squash }

        GlassSurface {
            anchors.fill: parent
            backdrop: artLayer
            shapes: [{ x: 0, y: 0, w: card.width, h: card.height, r: 30, lift: card.squash + Math.min(1, Math.abs(rise.velocity) / 300) }]
        }
        GlassInk {
            anchors.fill: parent
            backdrop: artLayer
            Column {
                x: 20; y: 18
                width: parent.width - 40
                spacing: 6
                Text {
                    id: heroLabel
                    text: hero.anime ? qsTranslate("ResumeCard", "Continue watching") : qsTranslate("ResumeCard", "Continue reading")
                    color: "#d9ffffff"
                    font.family: Theme.fontSans
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.4
                }
                Text {
                    id: heroTitle
                    width: parent.width
                    text: hero.entry.title || ""
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    color: "white"
                    font.family: Theme.fontDisplay
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    lineHeight: 0.95
                }
                Item { width: 1; height: 2 }
                // The progress: inked, like the text.
                Rectangle {
                    width: parent.width; height: 4; radius: 2; color: "#55ffffff"
                    Rectangle { width: parent.width * Math.max(0.03, hero.percent); height: parent.height; radius: 2; color: "white" }
                }
            }
            Column {
                x: 20
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 18
                width: parent.width - 40 - goButton.width - 12
                Text {
                    width: parent.width
                    text: hero.unitLine
                    elide: Text.ElideRight
                    color: "white"
                    font.family: Theme.fontSans
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: [hero.upNext ? "" : hero.entry.completed ? qsTranslate("ResumeCard", "Finished") : Math.round(hero.percent * 100) + "%",
                           hero.entry.progressAt ? App.relativeTime(hero.entry.progressAt) : ""].filter(s => s).join("  ·  ")
                    elide: Text.ElideRight
                    color: "#b3ffffff"
                    font.family: Theme.fontSans
                    font.pixelSize: 12
                }
            }
        }
        // The card opens the series (the button continues it).
        MouseArea {
            id: cardTap
            anchors.fill: parent
            onClicked: Router.openSeries(hero.entry, null)
            onPressAndHold: Router.seriesMenu(hero.entry, card, mouseX, mouseY)
        }
        // Continue: a filled accent capsule (its own colours, not inked).
        Rectangle {
            id: goButton
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 18
            width: goRow.width + 34
            height: 44
            radius: 22
            color: Theme.accent
            scale: 1 - 0.06 * goPress.value
            Row {
                id: goRow
                anchors.centerIn: parent
                spacing: 7
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "play"; size: 16; color: Theme.accentInk }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("ResumeCard", "Continue")
                    color: Theme.accentInk
                    font.family: Theme.fontSans
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
            }
            MouseArea {
                id: goTap
                anchors.fill: parent
                onClicked: Router.resume(hero.entry)
            }
        }
    }

    // ---------------------------------------------------------------- phones: search and account
    Item {
        id: topRow
        // Phones: search and the account orb; computers: the orb alone (the
        // sidebar has search).
        x: Theme.compact ? 16 : hero.width - Theme.gutter - orbD
        y: 14
        width: Theme.compact ? hero.width - 32 : orbD
        height: 48
        readonly property real orbD: 48
        readonly property real searchW: Theme.compact ? width - orbD - 10 : 0

        GlassSurface {
            anchors.fill: parent
            backdrop: artLayer
            readonly property real sq: 1 - 0.06 * searchPress.value
            readonly property real oq: 1 - 0.1 * orbPress.value
            // A press squashes: a little wider, a little lower, about its centre.
            readonly property real sw: topRow.searchW * (2 - sq)
            readonly property real ow: topRow.orbD * (2 - oq)
            shapes: [
                { x: (topRow.searchW - sw) / 2, y: topRow.height * (1 - sq) / 2, w: sw, h: topRow.height * sq,
                  r: topRow.height * sq / 2, lift: searchPress.value },
                { x: topRow.width - topRow.orbD / 2 - ow / 2, y: topRow.height * (1 - oq) / 2, w: ow, h: topRow.orbD * oq,
                  r: topRow.orbD * oq / 2, lift: orbPress.value }
            ]
        }
        GlassInk {
            anchors.fill: parent
            backdrop: artLayer
            Row {
                visible: Theme.compact
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 20; color: "white" }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("Sidebar", "Search everything")
                    color: "#e6ffffff"
                    font.family: Theme.fontSans
                    font.pixelSize: 15
                    font.weight: Font.Medium
                }
            }
            Icon {
                x: topRow.width - topRow.orbD / 2 - width / 2
                anchors.verticalCenter: parent.verticalCenter
                name: "user-round"
                size: 22
                color: "white"
            }
        }
        MouseArea {
            id: searchTap
            width: topRow.searchW
            height: parent.height
            onClicked: if (Router.shell) Router.shell.openPalette()
        }
        MouseArea {
            id: orbTap
            x: topRow.width - topRow.orbD
            width: topRow.orbD
            height: parent.height
            onClicked: accountMenu.openAt(orbTap, 0, orbTap.height + 6, [
                { text: qsTranslate("HomePage", "Edit page"), icon: "layout-dashboard", run: () => hero.editPage() },
                { text: qsTranslate("Sidebar", "Settings"), icon: "settings", run: () => Router.go("settings") }
            ])
        }
        ActionMenu { id: accountMenu }
    }
}
