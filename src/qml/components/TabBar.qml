import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Bottom navigation for phones. The selection pill slides between tabs;
// "More" opens the remaining sections and search.
Rectangle {
    id: bar
    property string current: "home"
    signal navigate(string section)
    signal openPalette()

    // The media the reader chose on the welcome (Settings → Interface).
    readonly property var tabs: [
        { key: "home", label: qsTr("Home"), icon: "house" },
        { key: "comics", label: qsTr("Comics"), icon: "book-open" },
        { key: "novels", label: qsTr("Novels"), icon: "book-text" },
        { key: "anime", label: qsTr("Anime"), icon: "tv" },
        { key: "music", label: qsTr("Music"), icon: "volume-2" },
        { key: "library", label: qsTr("Library"), icon: "library" },
        { key: "more", label: qsTr("More"), icon: "ellipsis" }
    ].filter(t => Router.sectionShown(t.key)
                  // Six tabs at most: Music waits in More while all three
                  // other media show.
                  && (t.key !== "music" || Array.from(App.settings.shownMedia || ["comic", "novel", "anime"]).length < 3))
    readonly property int currentIndex: {
        const i = tabs.findIndex(t => t.key === current)
        return i >= 0 ? i : tabs.length - 1
    }

    color: Theme.sidebar
    Rectangle { width: parent.width; height: 1; color: Theme.border }

    Rectangle {
        id: pill
        readonly property real slot: bar.width / bar.tabs.length
        width: 56
        height: 30
        radius: 15
        y: 8
        x: slot * bar.currentIndex + (slot - width) / 2
        color: Theme.alpha(Theme.text, 0.1)
        Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
    }

    Row {
        anchors.fill: parent
        Repeater {
            model: bar.tabs
            delegate: AbstractButton {
                id: tab
                required property var modelData
                required property int index
                width: bar.width / bar.tabs.length
                height: bar.height
                onClicked: modelData.key === "more" ? more.open() : bar.navigate(modelData.key)
                background: null
                contentItem: Column {
                    topPadding: 11
                    spacing: 6
                    Item {
                        width: tab.width
                        height: 20
                        Icon {
                            anchors.centerIn: parent
                            name: tab.modelData.icon
                            size: 20
                            color: bar.currentIndex === tab.index ? Theme.text : Theme.textMuted
                            scale: tab.pressed ? 0.85 : 1
                            Behavior on scale { NumberAnimation { duration: Motion.fast } }
                        }
                        Rectangle {
                            readonly property int count: tab.modelData.key === "library" ? Library.unreadUpdates
                                : tab.modelData.key === "more" ? Notifications.unread : 0
                            visible: count > 0
                            x: parent.width / 2 + 8
                            y: -2
                            width: 8; height: 8; radius: 4
                            color: Theme.accent
                        }
                    }
                    // Six tabs at 360 px leave 60 px each: long names
                    // (Truyện tranh) shrink a little rather than overlap.
                    Text {
                        width: tab.width
                        height: 14
                        leftPadding: 2
                        rightPadding: 2
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 9
                        elide: Text.ElideRight
                        text: tab.modelData.label
                        color: bar.currentIndex === tab.index ? Theme.text : Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: 11
                        font.weight: bar.currentIndex === tab.index ? Font.DemiBold : Font.Medium
                    }
                }
            }
        }
    }

    Popup {
        id: more
        parent: Overlay.overlay
        x: parent.width - width - 12
        y: parent.height - height - bar.height - 12
        width: 220
        padding: 6
        modal: true
        background: Rectangle { radius: Theme.radiusLg; color: Theme.surface; border.width: 1; border.color: Theme.borderStrong }
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.fast }
                NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            }
        }
        transformOrigin: Item.BottomRight
        contentItem: Column {
            Repeater {
                model: (Router.sectionShown("music") && !bar.tabs.some(t => t.key === "music")
                        ? [{ key: "music", label: qsTr("Music"), icon: "volume-2" }] : []).concat([
                    { key: "search", label: qsTr("Search"), icon: "search" },
                    { key: "notifications", label: qsTr("Notifications"), icon: "bell" },
                    { key: "history", label: qsTr("History"), icon: "history" },
                    { key: "activity", label: qsTr("Activity"), icon: "activity" },
                    { key: "latest", label: qsTr("Latest"), icon: "flame" },
                    { key: "downloads", label: qsTr("Downloads"), icon: "download" },
                    { key: "settings", label: qsTr("Settings"), icon: "settings" }
                ])
                delegate: ItemDelegate {
                    required property var modelData
                    width: 208
                    height: 46
                    onClicked: {
                        more.close()
                        if (modelData.key === "search") bar.openPalette()
                        else bar.navigate(modelData.key)
                    }
                    background: Rectangle { radius: Theme.radiusMd; color: parent.pressed ? Theme.surface3 : "transparent" }
                    contentItem: Row {
                        spacing: 14
                        leftPadding: 8
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: modelData.icon; size: 18; color: Theme.textMuted }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textMd
                        }
                        Rectangle {
                            visible: modelData.key === "notifications" && Notifications.unread > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: unreadText.implicitWidth + 12
                            height: 20
                            radius: 10
                            color: Theme.accent
                            Text {
                                id: unreadText
                                anchors.centerIn: parent
                                text: Notifications.unread > 99 ? "99+" : Notifications.unread
                                color: Theme.accentInk
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.Bold
                            }
                        }
                    }
                }
            }
        }
    }
}
