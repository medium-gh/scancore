import QtQuick
import QtQuick.Controls.Basic
import Scancore

// The Retro mode's app bar along the bottom: the multi-purpose button on
// the left, "• • •" on the right. The dots open the bar: round buttons with
// their names, then a menu of words. The multi-purpose button does what the
// screen in view is for (search on Explore, check for new chapters on
// Library, carry on on Recent…); held, it opens a few more.
Item {
    id: bar
    // The shell's section and whether a page can go back (desktop shows a
    // back button; phones have their own Back).
    property string section: "home"
    property bool canGoBack: false
    property bool expanded: false
    property bool moreOpen: false
    signal back()

    readonly property int barHeight: 56
    implicitHeight: barHeight

    // What the button does here.
    readonly property var primary: {
        if (section === "home" && Retro.onHub) {
            switch (Retro.hubSection) {
            case "recent": return { icon: "play", label: qsTr("Continue"), run: () => bar.continueLast() }
            case "library": return { icon: "refresh-cw", label: qsTr("Check for new chapters"), run: () => bar.checkLibrary() }
            case "comics": return { icon: "search", label: qsTr("Search comics"), run: () => Router.go("comics") }
            case "novels": return { icon: "search", label: qsTr("Search novels"), run: () => Router.go("novels") }
            case "anime": return { icon: "search", label: qsTr("Search anime"), run: () => Router.go("anime") }
            }
        }
        if (section === "library") return { icon: "refresh-cw", label: qsTr("Check for new chapters"), run: () => bar.checkLibrary() }
        return { icon: "search", label: qsTr("Search everything"), run: () => Router.shell.openPalette() }
    }

    // Held here so a check or a lookup is not dropped when the bar closes.
    property var checkTask: null
    function checkLibrary() {
        Router.toast(qsTr("Checking your library for new chapters…"))
        checkTask = Library.checkUpdates()
    }
    Connections {
        target: bar.checkTask
        function onFinished(result) { if (result.newUnits === 0) Router.toast(qsTr("Everything is up to date")) }
        function onFailed(error) { Router.toast(error, "error") }
    }
    property var lastTask: null
    function continueLast() { lastTask = Library.continueItems("", 1) }
    Connections {
        target: bar.lastTask
        function onFinished(result) {
            bar.lastTask = null
            if (result.length) Router.resume(result[0])
            else Router.toast(qsTr("Nothing to continue yet"), "info")
        }
    }

    readonly property var buttons: [
        { icon: "search", label: qsTr("search"), run: () => Router.shell.openPalette() },
        { icon: "refresh-cw", label: qsTr("refresh"), run: () => bar.checkLibrary() },
        { icon: "bell", label: qsTr("alerts"), count: Notifications.unread, run: () => Router.go("notifications") },
        { icon: "settings", label: qsTr("settings"), run: () => Router.go("settings") }
    ]
    readonly property var menu: [
        { label: qsTr("history"), run: () => Router.go("history") },
        { label: qsTr("downloads"), run: () => Router.go("downloads") },
        { label: qsTr("activity"), run: () => Router.go("activity") },
        { label: qsTr("latest"), run: () => Router.go("latest") },
        { label: qsTr("extensions"), run: () => Router.openExtensions() },
        { label: App.settings.incognito === true ? qsTr("turn incognito off") : qsTr("go incognito"),
          run: () => Router.setIncognito(App.settings.incognito !== true) },
        { label: qsTr("arrange the hub"), run: () => Router.openSettings("interface") }
    ]
    function run(action) {
        expanded = false
        moreOpen = false
        action.run()
    }

    // Behind the open bar: a tap anywhere closes it.
    Rectangle {
        parent: bar.parent
        anchors.fill: parent
        z: bar.z - 0.5
        visible: opacity > 0
        opacity: bar.expanded || bar.moreOpen ? 1 : 0
        // Darker for the held button's labels, which sit over the page.
        color: Qt.rgba(0, 0, 0, bar.moreOpen ? 0.75 : 0.45)
        Behavior on opacity { NumberAnimation { duration: Motion.fast } }
        TapHandler { onTapped: { bar.expanded = false; bar.moreOpen = false } }
    }

    // The open part grows upwards from the bar.
    Rectangle {
        id: sheet
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: bar.barHeight + (bar.expanded ? openPart.implicitHeight : 0)
        color: Theme.sidebar
        clip: true
        Behavior on height { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } }

        // Above the strip, so the bar's own clip hides it while closed.
        Column {
            id: openPart
            anchors.bottom: strip.top
            anchors.bottomMargin: -8
            width: parent.width
            bottomPadding: 12
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.compact ? 18 : 28
                Repeater {
                    model: bar.buttons
                    AbstractButton {
                        id: roundButton
                        required property var modelData
                        width: 64
                        height: 76
                        background: null
                        onClicked: bar.run(modelData)
                        contentItem: Column {
                            spacing: 6
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 44
                                height: 44
                                radius: 22
                                color: roundButton.pressed ? Theme.text : "transparent"
                                border.width: 2
                                border.color: Theme.text
                                Icon {
                                    anchors.centerIn: parent
                                    name: roundButton.modelData.icon
                                    size: 20
                                    color: roundButton.pressed ? Theme.base : Theme.text
                                }
                                Rectangle {
                                    visible: (roundButton.modelData.count || 0) > 0
                                    x: parent.width - 10
                                    y: -2
                                    width: 12; height: 12; radius: 6
                                    color: Theme.tile
                                }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: roundButton.modelData.label
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textXs + 1
                            }
                        }
                    }
                }
            }
            Item { width: 1; height: 8 }
            Repeater {
                model: bar.menu
                AbstractButton {
                    id: menuItem
                    required property var modelData
                    width: openPart.width
                    height: 46
                    background: Rectangle { color: menuItem.pressed ? Theme.surface3 : "transparent" }
                    onClicked: bar.run(modelData)
                    contentItem: Text {
                        leftPadding: 24
                        verticalAlignment: Text.AlignVCenter
                        text: menuItem.modelData.label
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXl
                        font.weight: 350
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // The bar itself, always at the bottom.
        Item {
            id: strip
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: bar.barHeight

            Row {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                // Desktops: back, where a page can go back (phones use Back).
                AbstractButton {
                    id: backButton
                    visible: bar.canGoBack && !Theme.touch
                    width: 40
                    height: 40
                    Accessible.name: qsTr("Back")
                    onClicked: bar.back()
                    background: Rectangle {
                        radius: 20
                        color: backButton.pressed ? Theme.text : "transparent"
                        border.width: 2
                        border.color: Theme.text
                    }
                    contentItem: Item {
                        Icon { anchors.centerIn: parent; name: "arrow-left"; size: 18; color: backButton.pressed ? Theme.base : Theme.text }
                    }
                }

                // The multi-purpose button.
                AbstractButton {
                    id: primaryButton
                    width: 44
                    height: 44
                    Accessible.name: bar.primary.label
                    hoverEnabled: true
                    ToolTip.visible: hovered && !Theme.touch
                    ToolTip.text: bar.primary.label + qsTr(" (hold for more)")
                    onClicked: bar.run(bar.primary)
                    onPressAndHold: { bar.expanded = false; bar.moreOpen = true }
                    background: Rectangle {
                        radius: 22
                        color: primaryButton.pressed ? Theme.tile : Theme.alpha(Theme.text, 0.16)
                        Behavior on color { ColorAnimation { duration: Motion.instant } }
                    }
                    contentItem: Item {
                        Icon {
                            anchors.centerIn: parent
                            name: bar.primary.icon
                            size: 20
                            color: primaryButton.pressed ? Theme.tileInk : Theme.text
                        }
                    }
                    scale: pressed && !Motion.reduced ? 0.92 : 1
                    Behavior on scale { NumberAnimation { duration: Motion.instant } }
                }
            }

            // "• • •"
            AbstractButton {
                id: dots
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 56
                height: bar.barHeight
                Accessible.name: bar.expanded ? qsTr("Close the menu") : qsTr("Open the menu")
                onClicked: { bar.moreOpen = false; bar.expanded = !bar.expanded }
                background: null
                contentItem: Row {
                    spacing: 5
                    topPadding: (dots.height - 5) / 2
                    leftPadding: (dots.width - 3 * 5 - 2 * 5) / 2
                    Repeater {
                        model: 3
                        Rectangle { width: 5; height: 5; radius: 2.5; color: Theme.text }
                    }
                }
            }
        }
    }

    // Held: the button's other jobs, rising above it.
    Column {
        id: moreColumn
        x: 12 + (backButton.visible ? 50 : 0)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: bar.barHeight + 4
        spacing: 10
        visible: opacity > 0
        opacity: bar.moreOpen ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Motion.fast } }
        transform: Translate { y: bar.moreOpen ? 0 : 16; Behavior on y { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } } }
        Repeater {
            model: [
                { icon: "search", label: qsTr("Search everything"), run: () => Router.shell.openPalette() },
                { icon: "play", label: qsTr("Continue"), run: () => bar.continueLast() },
                { icon: "refresh-cw", label: qsTr("Check for new chapters"), run: () => bar.checkLibrary() },
                { icon: "venetian-mask", label: App.settings.incognito === true ? qsTr("Incognito off") : qsTr("Incognito"),
                  run: () => Router.setIncognito(App.settings.incognito !== true) },
                { icon: "user-round", label: qsTr("Account"), run: () => Router.openSettings("account") }
            ].reverse()
            AbstractButton {
                id: moreButton
                required property var modelData
                height: 44
                width: 44 + moreLabel.implicitWidth + 12
                background: null
                onClicked: bar.run(modelData)
                contentItem: Row {
                    spacing: 12
                    Rectangle {
                        width: 44
                        height: 44
                        radius: 22
                        color: moreButton.pressed ? Theme.tileInk : Theme.tile
                        Icon { anchors.centerIn: parent; name: moreButton.modelData.icon; size: 20; color: moreButton.pressed ? Theme.tile : Theme.tileInk }
                    }
                    Text {
                        id: moreLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: moreButton.modelData.label
                        color: "white"
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textLg
                        style: Text.Outline
                        styleColor: Qt.rgba(0, 0, 0, 0.4)
                    }
                }
            }
        }
    }
}
