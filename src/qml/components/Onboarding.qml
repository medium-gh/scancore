import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Scancore

// First-run welcome, six steps:
//   start   the opening (covers drifting behind the mark), then what the
//           reader reads and watches;
//   coming  coming from Mihon, Tachiyomi, Komikku, Aniyomi or LNReader:
//           the backup, the downloads folder and the extensions at once;
//   look    layout, theme, accent and glass beside a live preview;
//   reader  a sample chapter to try the reading mode and progress bar on;
//   extras  trackers, the scancore.onl account and notifications;
//   done    what was chosen, with ways back, and where to go first.
// Swipe, the dots, the arrow keys and Back move between steps; a TV's
// D-pad moves between controls. Settings can show it again.
Rectangle {
    id: root
    property int step: Math.max(0, Math.min(steps - 1, App.startAction.onboardingStep || 0))
    readonly property var stepKeys: ["start", "coming", "look", "reader", "extras", "done"]
    readonly property int steps: stepKeys.length
    signal finished()

    color: Theme.base
    focus: true

    // Layout: phones in portrait are compact; phones on their side low.
    readonly property bool wide: width >= 720
    readonly property bool low: height < 520
    readonly property int gutter: wide ? 40 : 20
    readonly property real columnWidth: Math.min(width - 2 * gutter, 1100)
    readonly property real columnX: Math.max(gutter, (width - columnWidth) / 2)
    readonly property real topInset: parent ? parent.SafeArea.margins.top : 0
    readonly property real bottomInset: parent ? parent.SafeArea.margins.bottom : 0

    // ------------------------------------------------------------ steps
    function go(next) {
        if (next < 0 || next >= steps || next === step) return
        step = next
    }
    // Back (Android's Back, Alt+Left): the previous step; false on the first.
    function back() {
        if (leave.running) return true
        if (step === 0) return false
        go(step - 1)
        return true
    }
    // The tabs and Home rebuild for the media chosen (~60 ms on a laptop):
    // once, as the welcome goes, never while a step slides (FrameTimes).
    function finish() {
        applyMedia()
        App.setSetting("onboarded", true)
        leave.start()
    }

    // ------------------------------------------------------------ media
    // What the reader reads and watches: the tabs, Home and Retro's hub
    // follow; Settings → Interface brings any back.
    property var media: {
        const shown = Array.from(App.settings.shownMedia || [])
        return shown.length ? shown : ["comic", "novel", "anime"]
    }
    // Music is apart from the three (a section of its own, off until chosen).
    property bool music: App.settings.musicSection === true
    function hasMedia(kind) { return kind === "music" ? music : media.indexOf(kind) >= 0 }
    function toggleMedia(kind) {
        if (kind === "music") { music = !music; return }
        const next = media.filter(m => m !== kind)
        if (next.length === media.length) next.push(kind)
        if (next.length === 0) return  // one stays
        media = ["comic", "novel", "anime"].filter(m => next.indexOf(m) >= 0)
    }
    function applyMedia() {
        Router.setShownMedia(media)
        Router.setMusicShown(music)
    }

    // ------------------------------------------------------------ coming from
    property string from: ""
    property var importTask: null
    readonly property var importResult: importTask && importTask.ok ? importTask.result : null
    readonly property var preview: mihonImport.preview
    function importSummary(r) {
        if (!r) return ""
        const parts = [r.imported === 1 ? qsTr("1 series added") : qsTr("%1 series added").arg(r.imported)]
        if (r.progress > 0) parts.push(qsTr("%1 chapters and episodes marked").arg(r.progress))
        if (r.extensionsInstalled > 0) parts.push(qsTr("%1 extensions installed").arg(r.extensionsInstalled))
        if (r.downloadsLinked > 0) parts.push(qsTr("%1 downloaded chapters linked").arg(r.downloadsLinked))
        if (r.unmatched > 0) parts.push(qsTr("%1 waiting to be found").arg(r.unmatched))
        return parts.join(" · ")
    }

    MihonImportDialog {
        id: mihonImport
        onImportStarted: (task) => root.importTask = task
    }
    MihonFolderPicker { id: mihonFolder; quiet: true }
    FileDialog {
        id: backupDialog
        fileMode: FileDialog.OpenFile
        nameFilters: root.from === "lnreader"
            ? [qsTr("LNReader backup (*.zip)"), qsTr("All files (*)")]
            : [qsTr("Mihon, Komikku, TachiyomiSY, Tachiyomi or Aniyomi backup (*.tachibk *.proto.gz)"), qsTr("All files (*)")]
        onAccepted: {
            root.importTask = null
            if (root.from === "lnreader") root.importTask = Importer.importLNReader(selectedFile)
            else mihonImport.start(selectedFile)
        }
    }
    Connections {
        target: root.importTask
        function onFailed(error) { Router.toast(error, "error") }
    }

    // ------------------------------------------------------------ covers
    WelcomeCoverSource { id: coverSource; active: root.visible }

    // Nothing behind the welcome (the tabs, the page) takes taps or wheels.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // ------------------------------------------------------------ opening
    // The covers fill the screen while the mark draws in, then make room:
    // they stay as a band at the top of the first step.
    property bool settled: Motion.reduced || step !== 0
    property real band: settled ? (wide ? 0.42 : 0.3) : 1
    Behavior on band { NumberAnimation { duration: Motion.reduced ? 0 : 700; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }

    WelcomeWall {
        id: wall
        anchors.fill: parent
        covers: coverSource
        moving: root.step === 0
        visible: opacity > 0.01
        opacity: root.step !== 0 ? 0 : root.settled ? 0.28 : 0.5
        Behavior on opacity { NumberAnimation { duration: Motion.slow } }
    }
    // The covers fade into the page below the band.
    Rectangle {
        anchors.fill: parent
        visible: wall.visible
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.alpha(Theme.base, root.settled ? 0.25 : 0.55) }
            GradientStop { position: Math.max(0.01, root.band * 0.55); color: Theme.alpha(Theme.base, root.settled ? 0.55 : 0.8) }
            GradientStop { position: root.band; color: Theme.alpha(Theme.base, root.settled ? 1 : 0.55) }
            GradientStop { position: 1; color: Theme.alpha(Theme.base, root.settled ? 1 : 0.55) }
        }
    }

    // The mark draws in with a scan line (opening only).
    Item {
        id: introMark
        anchors.centerIn: parent
        width: root.wide ? 300 : 220
        height: width / 2.02
        visible: opacity > 0.01
        opacity: root.settled ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        property real drawn: 0
        Icon {
            width: parent.width; height: parent.height
            name: "scancore-mark"
            size: parent.width
            sourceSize: Qt.size(parent.width * 2, parent.height * 2)
            fillMode: Image.PreserveAspectFit
            color: Theme.text
            opacity: 0.12
        }
        Item {
            width: parent.width
            height: parent.height * introMark.drawn
            clip: true
            Icon {
                width: introMark.width; height: introMark.height
                name: "scancore-mark"
                size: introMark.width
                sourceSize: Qt.size(introMark.width * 2, introMark.height * 2)
                fillMode: Image.PreserveAspectFit
                color: Theme.text
            }
        }
        Rectangle {
            x: -parent.width * 0.1
            y: parent.height * introMark.drawn - 1
            width: parent.width * 1.2
            height: 3
            radius: 2
            opacity: introMark.drawn > 0 && introMark.drawn < 1 ? 1 : 0
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.5; color: Theme.retro ? Theme.tile : Theme.text }
                GradientStop { position: 1; color: "transparent" }
            }
        }
    }
    SequentialAnimation {
        id: intro
        running: !root.settled
        PauseAnimation { duration: 150 }
        NumberAnimation { target: introMark; property: "drawn"; from: 0; to: 1; duration: 900; easing.type: Easing.InOutCubic }
        PauseAnimation { duration: 150 }
        ScriptAction { script: root.settled = true }
    }

    // ------------------------------------------------------------ top bar
    Item {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: root.topInset
        height: 56
        opacity: root.settled ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Motion.slow } }
        Row {
            x: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Icon { anchors.verticalCenter: parent.verticalCenter; name: "scancore-mark"; size: 30; height: 15; color: Theme.text }
            Text {
                visible: root.width >= 400
                anchors.verticalCenter: parent.verticalCenter
                text: Theme.retro ? "scancore" : "SCANCORE"
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: 14
                font.weight: Theme.retro ? Font.Normal : Font.DemiBold
                font.letterSpacing: Theme.retro ? 0 : 2
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: root.gutter - 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            // The interface language first, so the rest is in it.
            Select {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(170, root.width * 0.42)
                implicitHeight: 34
                Accessible.name: qsTr("Language")
                model: [{ value: "system", label: qsTr("System") }].concat(Customize.languages)
                value: Customize.language
                onPicked: (v) => Customize.setLanguage(v)
            }
            ScButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.step < root.steps - 1
                variant: "plain"
                compact: true
                text: Theme.retro ? qsTr("skip") : qsTr("Skip")
                Accessible.description: qsTr("Close the welcome and start using SCANCORE")
                onClicked: root.finish()
            }
        }
    }

    // ------------------------------------------------------------ stage
    Item {
        id: stage
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topBar.bottom
        anchors.bottom: footer.top
        opacity: root.settled ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Motion.slow } }
        // The finger's pull while swiping between steps.
        property real pull: 0
        readonly property bool pulling: swipe.active

        Repeater {
            model: root.stepKeys
            delegate: Loader {
                id: page
                required property string modelData
                required property int index
                readonly property bool current: root.step === index
                width: stage.width
                height: stage.height
                active: Math.abs(root.step - index) <= 1
                // A step made ready beside the current one is built a bit
                // at a time; going back to the first step made the start
                // step at once and held a swipe for ~60 ms (FrameTimes).
                asynchronous: !current
                visible: active && (Motion.reduced ? opacity > 0.01 : Math.abs(x) < stage.width)
                enabled: current
                sourceComponent: ({ start: startStep, coming: comingStep, look: lookStep, reader: readerStep,
                                    extras: extrasStep, done: doneStep })[modelData]
                // Slides with the finger and settles; reduce motion fades.
                x: Motion.reduced ? 0 : (index - root.step) * stage.width + stage.pull
                opacity: Motion.reduced ? (current ? 1 : 0) : 1
                Behavior on x {
                    enabled: !stage.pulling && !Motion.reduced
                    NumberAnimation { duration: Motion.page; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized }
                }
                Behavior on opacity { NumberAnimation { duration: Motion.normal } }
            }
        }

        // Swipe between steps on touch screens (mice use the buttons).
        DragHandler {
            id: swipe
            target: null
            enabled: !Motion.reduced
            xAxis.enabled: true
            yAxis.enabled: false
            acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Stylus
            property real last: 0
            onTranslationChanged: {
                last = translation.x
                const atEnd = (translation.x > 0 && root.step === 0) || (translation.x < 0 && root.step === root.steps - 1)
                // A rubber band at either end.
                stage.pull = atEnd ? translation.x * 0.3 : translation.x
            }
            onActiveChanged: {
                if (active) return
                const moved = last
                const speed = centroid.velocity.x
                stage.pull = 0
                if ((moved < -stage.width * 0.22 || speed < -700) && root.step < root.steps - 1) root.go(root.step + 1)
                else if ((moved > stage.width * 0.22 || speed > 700) && root.step > 0) root.go(root.step - 1)
                last = 0
            }
        }
    }

    // ------------------------------------------------------------ footer
    Item {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomInset
        height: Theme.retro ? (root.low ? 60 : 72) : 68
        opacity: root.settled ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Motion.slow } }
        readonly property string nextLabel: root.step === 1 && !root.from ? (Theme.retro ? qsTr("not now") : qsTr("Not now"))
            : Theme.retro ? qsTr("next") : qsTr("Continue")

        Rectangle {
            visible: Theme.retro
            anchors.fill: parent
            anchors.bottomMargin: -root.bottomInset
            color: Theme.surface2
        }
        // The dots: where you are, and a tap away from any step.
        Row {
            id: dots
            x: root.gutter - 6
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: root.steps
                AbstractButton {
                    id: dot
                    required property int index
                    width: (index === root.step ? 30 : 14) + 8
                    height: 36
                    focusPolicy: Qt.TabFocus
                    Accessible.name: qsTr("Step %1 of %2").arg(index + 1).arg(root.steps)
                    onClicked: root.go(index)
                    Behavior on width { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
                    background: null
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: dot.width - 14
                            height: 8
                            radius: Theme.retro ? 0 : 4
                            color: dot.index === root.step ? (Theme.retro ? Theme.tile : Theme.text)
                                 : Theme.alpha(Theme.text, dot.hovered ? 0.6 : dot.index < root.step ? 0.45 : 0.2)
                            Behavior on color { ColorAnimation { duration: Motion.normal } }
                        }
                    }
                }
            }
        }
        // Retro: Windows Phone's app bar, round buttons with a word under.
        Row {
            visible: Theme.retro
            anchors.right: parent.right
            anchors.rightMargin: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 24
            Repeater {
                model: (root.step > 0 ? [{ icon: "chevron-left", label: qsTr("back"), next: false }] : [])
                    .concat(root.step < root.steps - 1 ? [{ icon: "chevron-right", label: footer.nextLabel, next: true }] : [])
                AbstractButton {
                    id: appButton
                    required property var modelData
                    width: 56
                    height: footer.height
                    focusPolicy: Qt.TabFocus
                    Accessible.name: modelData.label
                    onClicked: root.go(root.step + (modelData.next ? 1 : -1))
                    background: null
                    contentItem: Column {
                        spacing: 2
                        topPadding: (appButton.height - 40 - 16) / 2
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 40; height: 40; radius: 20
                            color: appButton.pressed ? Theme.text : "transparent"
                            border.width: 2
                            border.color: Theme.text
                            Icon { anchors.centerIn: parent; name: appButton.modelData.icon; size: 20; color: appButton.pressed ? Theme.base : Theme.text }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: appButton.modelData.label
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
        Row {
            visible: !Theme.retro
            anchors.right: parent.right
            anchors.rightMargin: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            ScButton {
                visible: root.step > 0 && root.width >= 520
                variant: "ghost"
                text: qsTr("Back")
                onClicked: root.go(root.step - 1)
            }
            ScButton {
                visible: root.step < root.steps - 1
                variant: "primary"
                text: footer.nextLabel
                iconName: "chevron-right"
                iconAfter: true
                onClicked: root.go(root.step + 1)
            }
        }
    }

    // ------------------------------------------------------------ keys
    // Arrows between steps (not while typing); a TV's D-pad between
    // controls, Select presses.
    Keys.onPressed: (event) => {
        if (App.tv) {
            const dirs = { [Qt.Key_Left]: "left", [Qt.Key_Right]: "right", [Qt.Key_Up]: "up", [Qt.Key_Down]: "down" }
            if (dirs[event.key] !== undefined) event.accepted = spatial.move(dirs[event.key])
            else if (event.key === Qt.Key_Select || event.key === Qt.Key_Enter || event.key === Qt.Key_Return) event.accepted = spatial.activate()
            return
        }
        const focused = Window.activeFocusItem
        if (focused && focused instanceof TextInput) return
        if (event.key === Qt.Key_Right) { root.go(root.step + 1); event.accepted = true }
        else if (event.key === Qt.Key_Left) { root.go(root.step - 1); event.accepted = true }
        else if (event.key === Qt.Key_Escape && root.step > 0) { root.go(root.step - 1); event.accepted = true }
    }
    SpatialFocus {
        id: spatial
        scope: root
    }
    Timer {
        // A TV starts with the focus on the first control.
        running: App.tv && root.settled
        interval: 400
        onTriggered: if (!spatial.current || !spatial.isStop(spatial.current)) spatial.move("down")
    }

    // ------------------------------------------------------------ pieces
    component Kicker: Text {
        color: Theme.retro ? Theme.tile : Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textXs
        font.weight: Theme.retro ? Font.DemiBold : Font.Bold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: Theme.retro ? 0.5 : 2
    }
    component Title: Text {
        color: Theme.text
        font.family: Theme.fontDisplay
        font.pixelSize: root.low ? (Theme.retro ? 32 : 26) : Theme.retro ? (root.wide ? 54 : 38) : (root.wide ? 42 : 29)
        font.weight: Theme.retro ? Font.Light : Font.Bold
        font.letterSpacing: -1
        font.capitalization: Theme.retro ? Font.AllLowercase : Font.MixedCase
        wrapMode: Text.Wrap
        lineHeight: Theme.retro ? 0.95 : 1.04
        Accessible.role: Accessible.Heading
    }
    component Body: Text {
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: root.wide ? Theme.textLg : Theme.textMd
        wrapMode: Text.Wrap
        lineHeight: 1.35
    }
    component Heading: Column {
        property string kicker
        property string title
        property string body
        spacing: 10
        Kicker { visible: text.length > 0; text: parent.kicker }
        Title { width: parent.width; text: parent.title }
        Body { visible: text.length > 0; width: parent.width; text: parent.body }
    }
    // A label above a control.
    component Label: Text {
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
        font.weight: Font.DemiBold
        font.capitalization: Theme.retro ? Font.AllLowercase : Font.MixedCase
    }
    // A scrolling step with its column centred on wide screens.
    component Page: Flickable {
        id: pageFlick
        default property alias content: column.data
        property real columnWidth: root.columnWidth
        contentHeight: Math.max(height, column.height + (root.wide ? 48 : 24))
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        WheelScroll {}
        Column {
            id: column
            x: Math.max(0, (pageFlick.width - width) / 2)
            y: root.wide ? Math.max(24, (pageFlick.height - height) / 2 - 8) : 16
            width: pageFlick.columnWidth
            spacing: root.wide ? 24 : 18
        }
    }
    // An optional card on the extras step.
    component Card: Rectangle {
        id: card
        property string iconName
        property string title
        property string body
        default property alias content: cardColumn.data
        radius: Theme.retro ? 0 : Theme.radiusXl
        color: Theme.retro ? Theme.surface2 : Theme.surface
        border.width: Theme.retro ? 0 : 1
        border.color: Theme.border
        implicitHeight: cardColumn.implicitHeight + 40
        Column {
            id: cardColumn
            x: 20
            y: 20
            width: card.width - 40
            spacing: 12
            Row {
                width: parent.width
                spacing: 12
                Rectangle {
                    width: 40; height: 40; radius: Theme.retro ? 0 : 11
                    color: Theme.retro ? Theme.tile : Theme.surface2
                    Icon { anchors.centerIn: parent; name: card.iconName; size: 19; color: Theme.retro ? Theme.tileInk : Theme.text }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 52
                    text: card.title
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.retro ? 22 : Theme.textLg
                    font.weight: Theme.retro ? Font.Light : Font.DemiBold
                    font.capitalization: Theme.retro ? Font.AllLowercase : Font.MixedCase
                    wrapMode: Text.Wrap
                }
            }
            Text {
                width: parent.width
                text: card.body
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                wrapMode: Text.Wrap
                lineHeight: 1.3
            }
        }
    }

    // ------------------------------------------------------------ 1. start
    Component {
        id: startStep
        Item {
            id: start
            readonly property bool side: root.low && root.wide
            Heading {
                id: startHeading
                x: root.columnX
                y: start.side ? 8 : root.wide ? Math.max(24, start.height * 0.14) : 12
                width: start.side ? root.width * 0.42 - root.gutter : Math.min(root.columnWidth, 760)
                kicker: qsTr("Welcome")
                title: qsTr("What do you read, watch and listen to?")
                body: qsTr("Pick any. The tabs and Home follow, and Settings → Interface brings back the rest.")
            }
            Item {
                id: toggles
                x: start.side ? root.width * 0.42 + 16 : root.columnX
                y: start.side ? 8 : startHeading.y + startHeading.height + (root.wide ? 32 : 20)
                width: start.side ? root.width * 0.58 - 16 - root.gutter : root.columnWidth
                height: start.height - y - 12
                // Wide Standard: four tall cards in a row. Phones and the
                // side layout: four rows. Retro: one big tile, two squares
                // and a strip (phones), or the big tile beside three.
                readonly property bool row: root.wide && !Theme.retro && !start.side
                readonly property bool tiles: Theme.retro && !start.side
                readonly property real rowHeight: start.side ? (height - 24) / 4 : Math.max(72, Math.min(124, (height - 32) / 4))
                readonly property real cardHeight: Math.min(380, height)
                readonly property real half: (width - 12) / 2
                // Retro on phones: the big tile, two squares, the strip.
                readonly property real bigHeight: Math.min(height * 0.42, width * 0.55)
                readonly property real squareHeight: Math.min(half, (height - bigHeight - 24) * 0.6)
                readonly property real stripHeight: Math.max(56, Math.min(110, height - bigHeight - squareHeight - 24))
                // Retro wide: three stacked beside the big tile.
                readonly property real third: (cardHeight - 24) / 3
                Repeater {
                    model: [{ kind: "comic", icon: "book-open", label: qsTr("Comics"), detail: qsTr("Manhwa, manga and webtoons") },
                            { kind: "novel", icon: "book-text", label: qsTr("Novels"), detail: qsTr("Web novels and your EPUB books") },
                            { kind: "anime", icon: "tv", label: qsTr("Anime"), detail: qsTr("Episodes that resume where you stopped") },
                            { kind: "music", icon: "volume-2", label: qsTr("Music"), detail: qsTr("Anime songs and your own, with their lyrics") }]
                    WelcomeMedia {
                        required property var modelData
                        required property int index
                        kind: modelData.kind
                        iconName: modelData.icon
                        text: modelData.label
                        detail: modelData.detail
                        covers: coverSource
                        on: root.hasMedia(modelData.kind)
                        onClicked: root.toggleMedia(modelData.kind)
                        x: toggles.row ? index * (toggles.width + 16) / 4
                           : toggles.tiles && root.wide ? [0, toggles.half + 12, toggles.half + 12, toggles.half + 12][index]
                           : toggles.tiles ? [0, 0, toggles.half + 12, 0][index] : 0
                        y: toggles.row ? 0
                           : toggles.tiles && root.wide ? [0, 0, toggles.third + 12, 2 * (toggles.third + 12)][index]
                           : toggles.tiles ? [0, toggles.bigHeight + 12, toggles.bigHeight + 12, toggles.bigHeight + toggles.squareHeight + 24][index]
                           : index * (toggles.rowHeight + 8)
                        width: toggles.row ? (toggles.width - 48) / 4
                               : toggles.tiles ? ((index === 0 || index === 3) && !root.wide ? toggles.width : toggles.half)
                               : toggles.width
                        height: toggles.row ? toggles.cardHeight
                                : toggles.tiles && root.wide ? (index === 0 ? toggles.cardHeight : toggles.third)
                                : toggles.tiles ? [toggles.bigHeight, toggles.squareHeight, toggles.squareHeight, toggles.stripHeight][index]
                                : toggles.rowHeight
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ 2. coming from
    Component {
        id: comingStep
        Page {
            id: coming
            readonly property bool mihonLike: ["mihon", "tachiyomi", "komikku", "aniyomi"].indexOf(root.from) >= 0
            readonly property string appName: ({ mihon: "Mihon", tachiyomi: "Tachiyomi", komikku: "Komikku", aniyomi: "Aniyomi", lnreader: "LNReader" })[root.from] || ""
            readonly property bool previewing: mihonImport.task !== null && mihonImport.task.loading
            readonly property bool importing: root.importTask !== null && root.importTask.loading
            Heading {
                width: parent.width
                kicker: qsTr("Switching?")
                title: qsTr("Coming from another app?")
                body: root.wide ? qsTr("Your library, your downloads and your extensions come over in one go.")
                                : qsTr("Library, downloads and extensions, in one go.")
            }
            Flow {
                width: parent.width
                spacing: 8
                Repeater {
                    model: [{ key: "mihon", label: "Mihon" }, { key: "tachiyomi", label: "Tachiyomi" }, { key: "komikku", label: "Komikku" },
                            { key: "aniyomi", label: "Aniyomi" }, { key: "lnreader", label: "LNReader" }, { key: "new", label: qsTr("I'm new here") }]
                    Chip {
                        required property var modelData
                        text: modelData.label
                        selected: root.from === modelData.key
                        iconName: selected ? "check" : ""
                        implicitHeight: root.wide ? 40 : 36
                        onClicked: root.from = root.from === modelData.key ? "" : modelData.key
                    }
                }
            }
            // What comes over, counted as it is read.
            Rectangle {
                visible: root.from.length > 0
                width: parent.width
                height: details.implicitHeight + 40
                radius: Theme.retro ? 0 : Theme.radiusXl
                color: Theme.retro ? Theme.surface2 : Theme.surface
                border.width: Theme.retro ? 0 : 1
                border.color: Theme.border
                Column {
                    id: details
                    x: 20
                    y: 20
                    width: parent.width - 40
                    spacing: 14
                    Text {
                        width: parent.width
                        text: root.from === "new" ? qsTr("Welcome aboard") : qsTr("From %1").arg(coming.appName)
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.retro ? 28 : Theme.textXl
                        font.weight: Theme.retro ? Font.Light : Font.DemiBold
                        font.capitalization: Theme.retro ? Font.AllLowercase : Font.MixedCase
                    }
                    Text {
                        visible: coming.mihonLike || root.from === "lnreader"
                        width: parent.width
                        text: root.from === "lnreader" ? qsTr("In LNReader: More → Backup → Create backup. Then choose the file here.")
                            : qsTr("In %1: More → Settings → Backup and restore → Create backup. Then choose the file here.").arg(coming.appName)
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    // The backup.
                    Row {
                        visible: root.from !== "new"
                        width: parent.width
                        spacing: 12
                        Column {
                            width: parent.width - backupButton.width - 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Label { text: qsTr("Backup") }
                            Text {
                                width: parent.width
                                text: coming.importing ? qsTr("Bringing your library over…")
                                    : root.importResult ? root.importSummary(root.importResult)
                                    : coming.previewing ? qsTr("Reading the backup…")
                                    : root.preview && coming.mihonLike ? qsTr("%1 comics and %2 anime · %3 extensions to install")
                                        .arg(root.preview.comics).arg(root.preview.anime).arg(root.preview.extensions.length)
                                    : qsTr("Your series, lists, progress and trackers.")
                                color: root.importResult ? Theme.success : Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                wrapMode: Text.Wrap
                            }
                        }
                        ScButton {
                            id: backupButton
                            anchors.verticalCenter: parent.verticalCenter
                            variant: root.importResult ? "ghost" : "primary"
                            iconName: "folder-open"
                            loading: coming.previewing || coming.importing
                            text: root.importResult ? qsTr("Another") : qsTr("Choose backup")
                            onClicked: backupDialog.open()
                        }
                    }
                    // Mihon's downloads, read where they are.
                    Row {
                        visible: ["mihon", "tachiyomi", "komikku"].indexOf(root.from) >= 0
                        width: parent.width
                        spacing: 12
                        Column {
                            width: parent.width - folderButton.width - 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Label { text: qsTr("Downloads folder") }
                            Text {
                                width: parent.width
                                text: mihonFolder.summary
                                color: mihonFolder.linked ? Theme.success : Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                wrapMode: Text.Wrap
                            }
                        }
                        ScButton {
                            id: folderButton
                            anchors.verticalCenter: parent.verticalCenter
                            variant: "ghost"
                            iconName: "folder-open"
                            loading: mihonFolder.linking
                            text: mihonFolder.linked ? qsTr("Change") : qsTr("Choose")
                            onClicked: mihonFolder.open()
                        }
                    }
                    // Extensions come with the backup.
                    Text {
                        visible: coming.mihonLike
                        width: parent.width
                        text: qsTr("The extensions and repositories your series came from are offered with the backup, so they open on the same sources.")
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    // New here: the built-in sources work; a link is optional.
                    Text {
                        visible: root.from === "new"
                        width: parent.width
                        text: qsTr("SCANCORE comes with its own sources for comics, novels and anime. If you have a link to an extension repository, it can go here; the Extensions page takes one any time.")
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    Row {
                        visible: root.from === "new"
                        width: parent.width
                        spacing: 10
                        SearchField {
                            id: linkField
                            width: parent.width - addLink.width - 10
                            placeholderText: qsTr("Repository link")
                            onSubmitted: (text) => { if (text.trim()) Router.addRepository(text.trim()) }
                        }
                        ScButton {
                            id: addLink
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Add")
                            iconName: "plus"
                            enabled: linkField.text.trim().length > 0
                            onClicked: Router.addRepository(linkField.text.trim())
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ 3. look
    Component {
        id: lookStep
        Item {
            id: look
            readonly property bool besides: root.wide
            WelcomePreview {
                id: preview
                covers: coverSource
                phone: !look.besides
                x: look.besides ? choices.x + choices.width + 40 : root.gutter
                // Short screens (phones on their side) keep more for the choices.
                width: look.besides ? Math.min(look.width - x - root.gutter, root.low ? look.width * 0.38 : 900) : look.width - 2 * root.gutter
                height: look.besides ? Math.min(look.height - 40, width * 0.68) : Math.min(look.height * 0.4, 290)
                y: look.besides ? (look.height - height) / 2 : 8
            }
            Page {
                id: choices
                x: look.besides ? Math.max(root.gutter, (look.width - width - 40 - Math.min(look.width * (root.low ? 0.38 : 0.53), 900)) / 2) : 0
                y: look.besides ? 0 : preview.y + preview.height + 8
                width: look.besides ? Math.min(look.width * (root.low ? 0.62 : 0.47) - root.gutter - 40, 520) : look.width
                height: look.height - y
                columnWidth: look.besides ? width : width - 2 * root.gutter
                Heading {
                    visible: look.besides
                    width: parent.width
                    kicker: qsTr("Make it yours")
                    title: qsTr("Pick a look")
                }
                // The layout, as two pictures.
                Row {
                    width: parent.width
                    spacing: 12
                    Repeater {
                        model: [{ value: "standard", label: qsTr("Standard") }, { value: "retro", label: qsTr("Retro tiles") }]
                        AbstractButton {
                            id: layoutChoice
                            required property var modelData
                            readonly property bool on: (modelData.value === "retro") === Theme.retro
                            width: (parent.width - 12) / 2
                            height: picture.height + 28
                            focusPolicy: Qt.TabFocus
                            Accessible.name: modelData.label
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: on
                            onClicked: App.setSetting("uiMode", modelData.value)
                            background: null
                            contentItem: Column {
                                spacing: 6
                                Rectangle {
                                    id: picture
                                    width: layoutChoice.width
                                    height: root.wide ? 88 : 62
                                    radius: Theme.retro ? 0 : 12
                                    color: Theme.surface
                                    border.width: layoutChoice.on ? 2 : 1
                                    border.color: layoutChoice.on ? (Theme.retro ? Theme.tile : Theme.accent) : Theme.border
                                    clip: true
                                    Row {
                                        visible: layoutChoice.modelData.value === "standard"
                                        x: 10; y: 10
                                        spacing: 6
                                        Rectangle { width: 16; height: picture.height - 20; radius: 4; color: Theme.surface3 }
                                        Repeater {
                                            model: 3
                                            WelcomeArt {
                                                required property int index
                                                width: (picture.width - 56) / 3.4
                                                height: width * 1.4
                                                seed: index + 2
                                                source: coverSource.pick(index + 2)
                                            }
                                        }
                                    }
                                    Text {
                                        visible: layoutChoice.modelData.value === "retro"
                                        x: 8; y: -4
                                        text: "scancore"
                                        color: Theme.text
                                        font.family: "Selawik"
                                        font.weight: Font.Light
                                        font.pixelSize: root.wide ? 28 : 21
                                    }
                                    Grid {
                                        visible: layoutChoice.modelData.value === "retro"
                                        x: 10
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 8
                                        columns: 6
                                        spacing: 4
                                        Repeater {
                                            model: 12
                                            Rectangle {
                                                required property int index
                                                width: root.wide ? 17 : 12; height: width
                                                color: index % 5 === 1 ? Theme.alpha(Theme.tile, 0.6) : Theme.tile
                                            }
                                        }
                                    }
                                }
                                Text {
                                    text: layoutChoice.modelData.label
                                    color: layoutChoice.on ? Theme.text : Theme.textMuted
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.textSm
                                    font.weight: layoutChoice.on ? Font.DemiBold : Font.Normal
                                    font.capitalization: Theme.retro ? Font.AllLowercase : Font.MixedCase
                                }
                            }
                        }
                    }
                }
                // Theme, as four swatches.
                Row {
                    spacing: 10
                    Repeater {
                        model: [{ v: "system", l: qsTr("System"), c: ["#f7f7f8", "#0c0c0d"] }, { v: "light", l: qsTr("Light"), c: ["#f7f7f8", "#f7f7f8"] },
                                { v: "dark", l: qsTr("Dark"), c: ["#1c1c1f", "#1c1c1f"] }, { v: "black", l: qsTr("Black"), c: ["#000000", "#000000"] }]
                        AbstractButton {
                            id: swatch
                            required property var modelData
                            readonly property bool on: modelData.v === (App.settings.theme || "system")
                            width: Math.max(52, swatchLabel.implicitWidth)
                            height: (root.wide ? 50 : 44) + 22
                            focusPolicy: Qt.TabFocus
                            Accessible.name: modelData.l
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: on
                            onClicked: App.setSetting("theme", modelData.v)
                            background: null
                            contentItem: Column {
                                spacing: 5
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: root.wide ? 50 : 44; height: width
                                    radius: Theme.retro ? 0 : width / 2
                                    border.width: swatch.on ? 2 : 1
                                    border.color: swatch.on ? (Theme.retro ? Theme.tile : Theme.accent) : Theme.borderStrong
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.499; color: swatch.modelData.c[0] }
                                        GradientStop { position: 0.5; color: swatch.modelData.c[1] }
                                    }
                                }
                                Text {
                                    id: swatchLabel
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: swatch.modelData.l
                                    color: swatch.on ? Theme.text : Theme.textMuted
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.textXs
                                    font.capitalization: Theme.retro ? Font.AllLowercase : Font.MixedCase
                                }
                            }
                        }
                    }
                }
                // Accent: Retro's own colours in Retro.
                Flow {
                    width: parent.width
                    spacing: root.wide ? 10 : 8
                    Repeater {
                        model: Theme.retro ? ["retro-lime", "retro-cyan", "retro-cobalt", "retro-violet", "retro-magenta", "retro-red", "retro-orange", "retro-amber"]
                                           : ["mono", "violet", "indigo", "sky", "emerald", "amber", "ember", "rose"]
                        AbstractButton {
                            id: accentDot
                            required property string modelData
                            readonly property bool on: App.settings.accent === modelData || (!App.settings.accent && modelData === "mono")
                            width: root.wide ? 34 : 30
                            height: width
                            focusPolicy: Qt.TabFocus
                            Accessible.name: modelData.replace("retro-", "")
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: on
                            onClicked: App.setSetting("accent", modelData)
                            background: Rectangle {
                                radius: Theme.retro ? 0 : width / 2
                                color: Theme.retro ? Theme.retroAccents[accentDot.modelData]
                                     : (Theme.dark ? Theme.accents[accentDot.modelData].dark : Theme.accents[accentDot.modelData].light)
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    radius: Theme.retro ? 0 : width / 2
                                    color: "transparent"
                                    border.width: 2
                                    border.color: Theme.text
                                    visible: accentDot.on
                                }
                            }
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: 12
                    Text {
                        width: parent.width - motionToggle.width - 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Reduce motion")
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                        font.weight: Font.DemiBold
                        font.capitalization: Theme.retro ? Font.AllLowercase : Font.MixedCase
                    }
                    Toggle {
                        id: motionToggle
                        anchors.verticalCenter: parent.verticalCenter
                        checked: App.settings.reducedMotion === true
                        Accessible.name: qsTr("Reduce motion")
                        onToggled: App.setSetting("reducedMotion", checked)
                    }
                }
                Column {
                    visible: root.hasMedia("comic")
                    width: parent.width
                    spacing: 8
                    Label { text: qsTr("Comic sources in") }
                    Select {
                        width: Math.min(parent.width, 320)
                        Accessible.name: qsTr("Comic sources in")
                        model: [{ value: "", label: qsTr("All languages") }].concat(Comics.languages())
                        value: App.settings.sourceLanguages || ""
                        onPicked: (v) => App.setSetting("sourceLanguages", v)
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ 4. reader
    Component {
        id: readerStep
        Item {
            id: reader
            readonly property bool besides: root.wide
            WelcomeStrip {
                id: strip
                x: reader.besides ? Math.max(root.gutter, (reader.width - width - 48 - readerChoices.width) / 2) : root.gutter
                y: reader.besides ? (reader.height - height) / 2 : 8
                height: reader.besides ? reader.height - 32 : Math.min(reader.height * 0.42, 340)
                width: reader.besides ? Math.min(height * 0.6, reader.width * 0.4) : reader.width - 2 * root.gutter
            }
            Page {
                id: readerChoices
                x: reader.besides ? strip.x + strip.width + 48 : 0
                y: reader.besides ? 0 : strip.y + strip.height + 8
                width: reader.besides ? Math.min(560, reader.width - strip.width - 48 - 2 * root.gutter) : reader.width
                height: reader.height - y
                columnWidth: reader.besides ? width : width - 2 * root.gutter
                Heading {
                    visible: reader.besides
                    width: parent.width
                    kicker: qsTr("Reading comics")
                    title: qsTr("Try the reader")
                    body: qsTr("Scroll the sample, tap the bar, and pick what suits you. All of it is in Settings → Reading too.")
                }
                Column {
                    width: parent.width
                    spacing: 8
                    Label { text: qsTr("Comics open as") }
                    Segmented {
                        options: [{ value: "strip", label: qsTr("Vertical strip"), icon: "move-vertical" }, { value: "paged", label: qsTr("Pages"), icon: "layers" }]
                        value: App.settings.readerMode === "paged" ? "paged" : "strip"
                        onPicked: (v) => App.setSetting("readerMode", v)
                    }
                }
                Column {
                    width: parent.width
                    spacing: 8
                    Label { text: qsTr("Progress bar") }
                    ProgressBarPicker {
                        width: parent.width
                        tileWidth: Math.min(root.wide ? 120 : 150, Math.floor((width - 3 * spacing) / 4))
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ 5. extras
    Component {
        id: extrasStep
        Page {
            id: extras
            readonly property var trackers: Trackers.services.filter(t => ["anilist", "mal", "comick"].indexOf(t.key) >= 0)
            readonly property bool allowed: { App.dependOn(Qt.application.state); return Notifications.systemAllowed() }
            property bool signingIn: false
            Heading {
                width: parent.width
                kicker: qsTr("Optional")
                title: qsTr("A few extras")
                body: qsTr("Each takes a tap now, or later in Settings.")
            }
            Grid {
                id: cards
                width: parent.width
                columns: root.wide ? 3 : 1
                columnSpacing: 16
                rowSpacing: 16
                readonly property real cell: (width - (columns - 1) * columnSpacing) / columns
                Card {
                    visible: extras.trackers.length > 0
                    width: cards.cell
                    iconName: "list-checks"
                    title: qsTr("Keep your lists in step")
                    body: qsTr("Finishing a chapter or episode moves it forward on your trackers.")
                    Repeater {
                        model: extras.trackers.map(t => ({ service: t.key }))
                        Item {
                            required property var modelData
                            width: parent.width
                            height: Math.max(40, connector.height)
                            Text {
                                anchors.left: parent.left
                                anchors.right: connector.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: connector.label
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textMd
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            TrackerConnect {
                                id: connector
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                service: modelData.service
                            }
                        }
                    }
                }
                Card {
                    width: cards.cell
                    iconName: "cloud"
                    title: Account.signedIn ? qsTr("You're signed in") : qsTr("A scancore.onl account")
                    body: Account.signedIn ? qsTr("Signed in as %1. Your library and progress sync.").arg(Account.user.username || Account.user.email)
                        : qsTr("Your progress on every device, friends, shared lists and reactions. Everything works without one too.")
                    ScButton {
                        visible: !Account.signedIn && !extras.signingIn
                        variant: "primary"
                        iconName: "user"
                        text: qsTr("Sign in or create one")
                        onClicked: extras.signingIn = true
                    }
                    Loader {
                        width: parent.width
                        active: extras.signingIn && !Account.signedIn
                        visible: active
                        sourceComponent: AuthForm {
                            onDone: extras.signingIn = false
                        }
                    }
                }
                Card {
                    width: cards.cell
                    iconName: "bell"
                    title: qsTr("Get notified")
                    body: qsTr("New chapters of what you follow, and news from SCANCORE.")
                    Repeater {
                        model: [{ key: "notifyUpdates", title: qsTr("New chapters and episodes") }, { key: "announcements", title: qsTr("News from SCANCORE") }]
                        Row {
                            required property var modelData
                            width: parent.width
                            spacing: 12
                            Text {
                                width: parent.width - notifyToggle.width - 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.title
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textMd
                                wrapMode: Text.Wrap
                            }
                            Toggle {
                                id: notifyToggle
                                anchors.verticalCenter: parent.verticalCenter
                                checked: App.settings[modelData.key] !== false
                                Accessible.name: modelData.title
                                onToggled: App.setSetting(modelData.key, checked)
                            }
                        }
                    }
                    ScButton {
                        visible: !extras.allowed
                        variant: "primary"
                        iconName: "bell"
                        text: qsTr("Allow notifications")
                        onClicked: {
                            App.setSetting("notificationsAsked", true)
                            Notifications.askPermission()
                        }
                    }
                    Row {
                        visible: extras.allowed
                        spacing: 8
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "check"; size: 16; color: Theme.success }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: App.platform === "android" ? qsTr("Notifications are allowed") : qsTr("Your system shows them")
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textSm
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ 6. done
    Component {
        id: doneStep
        Page {
            id: done
            columnWidth: Math.min(root.columnWidth, 640)
            readonly property var mediaNames: ({ comic: qsTr("Comics"), novel: qsTr("Novels"), anime: qsTr("Anime"), music: qsTr("Music") })
            readonly property var connected: Trackers.services.filter(t => t.connected).map(t => t.label)
            Rectangle {
                width: 64; height: 64; radius: Theme.retro ? 0 : 32
                color: Theme.retro ? Theme.tile : Theme.accent
                scale: 0.4
                Component.onCompleted: pop.start()
                NumberAnimation on scale { id: pop; running: false; to: 1; duration: Motion.reduced ? 0 : Motion.page; easing.type: Easing.OutBack }
                Icon { anchors.centerIn: parent; name: "check"; size: 30; color: Theme.retro ? Theme.tileInk : Theme.accentInk }
            }
            Heading {
                width: parent.width
                title: qsTr("You're all set")
                body: qsTr("Here is what you picked. Tap one to change it.")
            }
            // The answers, each a way back to its step.
            Column {
                width: parent.width
                Repeater {
                    model: [
                        { step: 0, icon: "layers", label: qsTr("Reading and watching"), value: root.media.concat(root.music ? ["music"] : []).map(m => done.mediaNames[m]).join(", ") },
                        { step: 1, icon: "download", label: qsTr("Coming from"),
                          value: root.importResult ? root.importSummary(root.importResult)
                               : root.from === "new" || !root.from ? qsTr("Starting fresh") : ({ mihon: "Mihon", tachiyomi: "Tachiyomi", komikku: "Komikku", aniyomi: "Aniyomi", lnreader: "LNReader" })[root.from] },
                        { step: 2, icon: "palette", label: qsTr("Look"),
                          value: [Theme.retro ? qsTr("Retro tiles") : qsTr("Standard"),
                                  ({ system: qsTr("System"), light: qsTr("Light"), dark: qsTr("Dark"), black: qsTr("Black") })[App.settings.theme || "system"] || ""].filter(s => s).join(" · ") },
                        { step: 3, icon: "book-open", label: qsTr("Reader"),
                          value: [App.settings.readerMode === "paged" ? qsTr("Pages") : qsTr("Vertical strip"),
                                  App.settings.readerProgressBar === false ? qsTr("No progress bar")
                                  : ({ segments: qsTr("Pages bar"), line: qsTr("Line"), floating: qsTr("Floating bar") })[App.settings.readerProgressLook || "segments"] || ""].join(" · ") },
                        { step: 4, icon: "sparkles", label: qsTr("Extras"),
                          value: [Account.signedIn ? qsTr("Signed in") : "", done.connected.length ? done.connected.join(", ") : "",
                                  App.settings.notifyUpdates !== false ? qsTr("Notifications on") : qsTr("Notifications off")].filter(s => s).join(" · ") }]
                    AbstractButton {
                        id: answer
                        required property var modelData
                        width: parent.width
                        height: Math.max(56, answerText.implicitHeight + 20)
                        focusPolicy: Qt.TabFocus
                        hoverEnabled: true
                        Accessible.name: modelData.label + ": " + modelData.value
                        onClicked: root.go(modelData.step)
                        background: Rectangle {
                            radius: Theme.retro ? 0 : Theme.radiusMd
                            color: answer.hovered || answer.activeFocus ? Theme.surface2 : "transparent"
                        }
                        contentItem: Row {
                            spacing: 14
                            leftPadding: 8
                            Icon { anchors.verticalCenter: parent.verticalCenter; name: answer.modelData.icon; size: 18; color: Theme.textMuted }
                            Column {
                                id: answerText
                                anchors.verticalCenter: parent.verticalCenter
                                width: answer.width - 84
                                Text { text: answer.modelData.label; color: Theme.textMuted; font.family: Theme.fontSans; font.pixelSize: Theme.textXs; font.weight: Font.DemiBold }
                                Text { width: parent.width; text: answer.modelData.value; color: Theme.text; font.family: Theme.fontSans; font.pixelSize: Theme.textMd; wrapMode: Text.Wrap }
                            }
                            Icon { anchors.verticalCenter: parent.verticalCenter; name: "pencil"; size: 14; color: Theme.textFaint }
                        }
                    }
                }
            }
            // Where to go first, from the answers.
            Flow {
                width: parent.width
                spacing: 10
                ScButton {
                    variant: "primary"
                    iconName: "chevron-right"
                    iconAfter: true
                    text: qsTr("Start exploring")
                    onClicked: root.finish()
                }
                ScButton {
                    visible: !!root.importResult && root.importResult.imported > 0
                    iconName: "library"
                    text: qsTr("Open your library")
                    onClicked: { root.finish(); Router.go("library") }
                }
                Repeater {
                    model: [{ kind: "comic", section: "comics", icon: "book-open", label: qsTr("Browse comics") },
                            { kind: "anime", section: "anime", icon: "tv", label: qsTr("Watch anime") },
                            { kind: "novel", section: "novels", icon: "book-text", label: qsTr("Read novels") },
                            { kind: "music", section: "music", icon: "volume-2", label: qsTr("Play music") }].filter(a => root.hasMedia(a.kind))
                    ScButton {
                        required property var modelData
                        variant: "ghost"
                        iconName: modelData.icon
                        text: modelData.label
                        onClicked: { root.finish(); Router.go(modelData.section) }
                    }
                }
            }
        }
    }

    ParallelAnimation {
        id: leave
        NumberAnimation { target: root; property: "opacity"; to: 0; duration: Motion.slow; easing.type: Easing.OutCubic }
        NumberAnimation { target: root; property: "scale"; to: 1.04; duration: Motion.slow; easing.type: Easing.OutCubic }
        onFinished: root.finished()
    }
}
