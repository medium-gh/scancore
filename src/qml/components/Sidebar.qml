import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Collapsible navigation rail. The selection indicator slides between
// items; collapsing animates the width and fades the labels.
// style: "sidebar" (full height), "floating" (a rounded panel inset from the
// window's edges) or "rail" (icons only); side: "left" or "right".
Rectangle {
    id: sidebar
    property string current: "home"
    property string style: "sidebar"
    property string side: "left"
    readonly property bool floating: style === "floating"
    readonly property bool rail: style === "rail"
    property bool collapsed: rail || App.settings.sidebarCollapsed === true
    signal navigate(string section)
    signal openPalette()

    readonly property var sections: [
        { key: "home", label: qsTr("Home"), icon: "house" },
        { key: "comics", label: qsTr("Comics"), icon: "book-open" },
        { key: "novels", label: qsTr("Novels"), icon: "book-text" },
        { key: "anime", label: qsTr("Anime"), icon: "tv" },
        { key: "music", label: qsTr("Music"), icon: "volume-2" },
        { key: "divider" },
        { key: "library", label: qsTr("Library"), icon: "library" },
        { key: "history", label: qsTr("History"), icon: "history" },
        { key: "activity", label: qsTr("Activity"), icon: "activity" },
        { key: "latest", label: qsTr("Latest"), icon: "flame" },
        { key: "downloads", label: qsTr("Downloads"), icon: "download" },
        // Opens the notification area rather than a page.
        { key: "notifications", label: qsTr("Notifications"), icon: "bell" }
    ].filter(s => Router.sectionShown(s.key))

    implicitWidth: collapsed ? 72 : 228
    width: implicitWidth
    color: Theme.sidebar
    radius: floating ? Theme.radiusXl : 0
    border.width: floating ? 1 : 0
    border.color: Theme.border
    Behavior on width { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }

    // The edge towards the content.
    Rectangle {
        visible: !sidebar.floating
        x: sidebar.side === "right" ? 0 : parent.width - 1
        width: 1
        height: parent.height
        color: Theme.border
    }

    // Brand
    Item {
        id: brand
        width: parent.width
        height: 72
        Icon {
            id: logo
            x: sidebar.collapsed ? 18 : 22
            anchors.verticalCenter: parent.verticalCenter
            name: "scancore-mark"
            size: 36
            height: 18
            sourceSize: Qt.size(72 * Screen.devicePixelRatio, 36 * Screen.devicePixelRatio)
            fillMode: Image.PreserveAspectFit
            color: Theme.text
            Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
        }
        Text {
            anchors.left: logo.right
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: "SCANCORE"
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: 15
            font.weight: Font.DemiBold
            font.letterSpacing: 3.2
            opacity: sidebar.collapsed ? 0 : 1
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Motion.fast } }
        }
    }

    // Search / command palette button
    AbstractButton {
        id: searchButton
        x: 12
        y: brand.height
        width: parent.width - 24
        height: 38
        hoverEnabled: true
        onClicked: sidebar.openPalette()
        background: Rectangle {
            radius: Theme.radiusMd
            color: searchButton.hovered ? Theme.surface3 : Theme.surface2
            border.width: 1
            border.color: Theme.border
            Behavior on color { ColorAnimation { duration: Motion.fast } }
        }
        contentItem: Item {
            Icon {
                x: sidebar.collapsed ? (parent.width - width) / 2 : 12
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 16
                color: Theme.textMuted
                Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
            }
            Text {
                x: 38
                width: parent.width - x - kbdBadge.width - 16
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Search everything")
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                opacity: sidebar.collapsed ? 0 : 1
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: Motion.fast } }
            }
            Rectangle {
                id: kbdBadge
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: kbd.implicitWidth + 10
                height: 20
                radius: 5
                color: Theme.surface3
                opacity: sidebar.collapsed ? 0 : 1
                visible: opacity > 0 && kbd.text.length > 0
                Text {
                    id: kbd
                    anchors.centerIn: parent
                    // The first key for search, from Settings → Keyboard.
                    text: Keymap.labels("palette")[0] || ""
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: 11
                }
            }
        }
        ToolTip.visible: sidebar.collapsed && hovered
        ToolTip.text: qsTr("Search") + (Keymap.labels("palette").length ? " (" + Keymap.labels("palette")[0] + ")" : "")
    }

    // Scrolls when the window is too short for every section above the
    // Settings button.
    Flickable {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        WheelScroll {}
        id: navArea
        y: searchButton.y + searchButton.height + 18
        width: parent.width
        height: Math.min(navColumn.height, bottomColumn.y - y - 8)
        contentWidth: width
        contentHeight: navColumn.height
        interactive: contentHeight > height
        // Keep the current section in view.
        function reveal() {
            const item = indicator.target
            if (!item || !interactive) {
                contentY = 0
                return
            }
            if (item.y < contentY) contentY = item.y
            else if (item.y + item.height > contentY + height) contentY = item.y + item.height - height
        }
        onHeightChanged: Qt.callLater(reveal)
        boundsBehavior: Flickable.StopAtBounds
        clip: interactive

        // Sliding selection indicator.
        Rectangle {
            id: indicator
            readonly property Item target: {
                for (let i = 0; i < repeater.count; ++i) {
                    const it = repeater.itemAt(i)
                    if (it && it.key === sidebar.current) return it
                }
                return null
            }
            visible: target !== null
            onTargetChanged: Qt.callLater(navArea.reveal)
            x: 12
            width: parent.width - 24
            height: 40
            y: target ? target.y : 0
            radius: Theme.radiusMd
            color: Theme.alpha(Theme.text, Theme.dark ? 0.08 : 0.07)
            Behavior on y { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
            Rectangle {
                x: sidebar.side === "right" ? parent.width + 9 : -12
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 18
                radius: 2
                color: Theme.accent
                visible: !sidebar.floating
            }
        }

        Column {
            id: navColumn
            width: parent.width
            spacing: 2
            Repeater {
                id: repeater
                model: sidebar.sections
                delegate: Item {
                    required property var modelData
                    readonly property string key: modelData.key
                    width: navColumn.width
                    height: key === "divider" ? 22 : 40

                    Rectangle {
                        visible: parent.key === "divider"
                        anchors.centerIn: parent
                        width: parent.width - 40
                        height: 1
                        color: Theme.border
                    }

                    AbstractButton {
                        id: navButton
                        visible: parent.key !== "divider"
                        x: 12
                        width: parent.width - 24
                        height: 40
                        hoverEnabled: true
                        onClicked: sidebar.navigate(modelData.key)
                        background: Rectangle {
                            radius: Theme.radiusMd
                            color: navButton.hovered && sidebar.current !== modelData.key ? Theme.alpha(Theme.text, 0.05) : Theme.alpha(Theme.text, 0)
                            Behavior on color { ColorAnimation { duration: Motion.fast } }
                        }
                        contentItem: Item {
                            Icon {
                                id: navIcon
                                x: sidebar.collapsed ? (parent.width - width) / 2 : 12
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.key === "notifications" && Notifications.unread > 0 ? "bell-dot" : (modelData.icon || "")
                                size: 18
                                color: sidebar.current === modelData.key ? Theme.text : Theme.textMuted
                                scale: navButton.pressed ? 0.88 : 1
                                Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
                                Behavior on scale { NumberAnimation { duration: Motion.fast } }
                            }
                            Text {
                                x: 44
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label || ""
                                color: sidebar.current === modelData.key ? Theme.text : Theme.textMuted
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textMd
                                font.weight: sidebar.current === modelData.key ? Font.DemiBold : Font.Medium
                                opacity: sidebar.collapsed ? 0 : 1
                                visible: opacity > 0
                                Behavior on opacity { NumberAnimation { duration: Motion.fast } }
                            }
                            Rectangle {
                                readonly property int count: modelData.key === "library" ? Library.unreadUpdates
                                    : modelData.key === "downloads" ? Downloads.pending
                                    : modelData.key === "notifications" ? Notifications.unread : 0
                                visible: count > 0
                                x: sidebar.collapsed ? navIcon.x + 12 : parent.width - width - 8
                                y: sidebar.collapsed ? 4 : (parent.height - height) / 2
                                width: sidebar.collapsed ? 8 : countText.implicitWidth + 12
                                height: sidebar.collapsed ? 8 : 20
                                radius: height / 2
                                color: Theme.accent
                                Text {
                                    id: countText
                                    anchors.centerIn: parent
                                    visible: !sidebar.collapsed
                                    text: parent.count > 99 ? "99+" : parent.count
                                    color: Theme.accentInk
                                    font.family: Theme.fontSans
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                            }
                        }
                        ToolTip.visible: sidebar.collapsed && hovered
                        ToolTip.text: modelData.label || ""
                    }
                }
            }
        }
    }

    Column {
        id: bottomColumn
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: parent.width
        spacing: 2

        AbstractButton {
            id: settingsButton
            x: 12
            width: parent.width - 24
            height: 40
            hoverEnabled: true
            onClicked: sidebar.navigate("settings")
            background: Rectangle {
                radius: Theme.radiusMd
                color: sidebar.current === "settings" ? Theme.alpha(Theme.text, 0.08)
                     : settingsButton.hovered ? Theme.alpha(Theme.text, 0.04) : "transparent"
            }
            contentItem: Item {
                Icon {
                    x: sidebar.collapsed ? (parent.width - width) / 2 : 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "settings"
                    size: 18
                    color: sidebar.current === "settings" ? Theme.text : Theme.textMuted
                    rotation: settingsButton.hovered ? 45 : 0
                    Behavior on rotation { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
                    Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
                }
                Text {
                    x: 44
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Settings")
                    color: sidebar.current === "settings" ? Theme.text : Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.Medium
                    opacity: sidebar.collapsed ? 0 : 1
                    visible: opacity > 0
                    Behavior on opacity { NumberAnimation { duration: Motion.fast } }
                }
            }
        }

        AbstractButton {
            id: collapseButton
            visible: !sidebar.rail
            x: 12
            width: parent.width - 24
            height: 36
            hoverEnabled: true
            onClicked: App.setSetting("sidebarCollapsed", !sidebar.collapsed)
            background: Rectangle {
                radius: Theme.radiusMd
                color: collapseButton.hovered ? Theme.alpha(Theme.text, 0.04) : "transparent"
            }
            contentItem: Item {
                Icon {
                    x: sidebar.collapsed ? (parent.width - width) / 2 : 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "panel-left"
                    size: 17
                    mirror: sidebar.side === "right"
                    color: Theme.textFaint
                    Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
                }
                Text {
                    x: 44
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Collapse")
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    opacity: sidebar.collapsed ? 0 : 1
                    visible: opacity > 0
                }
            }
        }
    }
}
