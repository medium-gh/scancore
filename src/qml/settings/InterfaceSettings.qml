import QtQuick
import QtQuick.Controls.Basic
import Scancore

SettingsPane {
    id: pane
    title: qsTr("Interface")
    subtitle: qsTr("Navigation, size and motion")
    iconName: "layout-grid"

    readonly property string navStyle: ["sidebar", "floating", "rail", "dock"].indexOf(App.settings.navStyle) >= 0
        ? App.settings.navStyle : "sidebar"
    readonly property bool rightSide: App.settings.navSide === "right"
    readonly property bool topSide: App.settings.dockSide === "top"
    readonly property int tileWidth: Theme.compact || width < 600 ? Math.floor((width - 36) / 2)
                                                                 : Math.min(168, Math.floor((width - 60) / 4))

    // A small window showing where navigation sits.
    component LayoutPreview: Item {
        id: preview
        property string kind: "sidebar"
        anchors.fill: parent
        readonly property color panel: Theme.surface3
        readonly property color line: Theme.borderStrong
        readonly property bool mirrored: pane.rightSide
        Rectangle { anchors.fill: parent; color: Theme.surface2 }
        // Content lines.
        Column {
            x: preview.kind === "dock" ? 14 : preview.mirrored ? 10 : (preview.kind === "rail" ? 30 : 52)
            y: preview.kind === "dock" && pane.topSide ? 30 : 12
            spacing: 6
            Rectangle { width: 46; height: 6; radius: 3; color: preview.line }
            Row {
                spacing: 5
                Repeater {
                    model: preview.kind === "dock" ? 5 : 3
                    Rectangle { width: 18; height: 26; radius: 3; color: preview.panel }
                }
            }
        }
        // Sidebars.
        Rectangle {
            visible: preview.kind !== "dock"
            readonly property int inset: preview.kind === "floating" ? 5 : 0
            x: preview.mirrored ? parent.width - width - inset : inset
            y: inset
            width: preview.kind === "rail" ? 18 : 40
            height: parent.height - 2 * inset
            radius: preview.kind === "floating" ? 6 : 0
            color: preview.panel
            border.width: preview.kind === "floating" ? 1 : 0
            border.color: preview.line
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 8
                spacing: 5
                Repeater {
                    model: 4
                    Rectangle {
                        width: preview.kind === "rail" ? 8 : 26 - index * 3
                        height: preview.kind === "rail" ? 8 : 4
                        radius: preview.kind === "rail" ? 4 : 2
                        color: index === 0 ? Theme.text : preview.line
                    }
                }
            }
        }
        // Dock.
        Rectangle {
            visible: preview.kind === "dock"
            anchors.horizontalCenter: parent.horizontalCenter
            y: pane.topSide ? 5 : parent.height - height - 5
            width: parent.width * 0.62
            height: 14
            radius: 7
            color: preview.panel
            border.width: 1
            border.color: preview.line
            Row {
                anchors.centerIn: parent
                spacing: 5
                Repeater {
                    model: 6
                    Rectangle { width: 5; height: 5; radius: 2.5; color: index === 1 ? Theme.text : preview.line }
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Language")
        note: qsTr("Help translate SCANCORE: the translations are the .ts files in the i18n folder of the repository.")
        SettingRow {
            iconName: "globe"
            title: qsTr("Language")
            description: qsTr("Titles and chapter names stay as the sites write them.")
            Select {
                model: [{ value: "system", label: qsTr("System") }].concat(Customize.languages)
                value: Customize.language
                onPicked: (v) => Customize.setLanguage(v)
            }
        }
    }

    // Standard or Retro, drawn small.
    component LookPreview: Item {
        id: look
        property bool retro: false
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: look.retro ? "#101012" : Theme.surface2 }
        // Standard: a sidebar and covers.
        Rectangle { visible: !look.retro; width: 40; height: parent.height; color: Theme.surface3 }
        Row {
            visible: !look.retro
            x: 52; y: 22; spacing: 5
            Repeater { model: 3; Rectangle { width: 18; height: 26; radius: 3; color: Theme.borderStrong } }
        }
        // Retro: the big thin title, a header and tiles in the accent.
        Text {
            visible: look.retro
            x: 6; y: -8
            text: "scancore"
            color: Theme.tile
            font.family: "Selawik"
            font.pixelSize: 30
            font.weight: Font.Light
        }
        Grid {
            visible: look.retro
            x: 8; y: 34; columns: 4; spacing: 3
            Repeater {
                model: [1, 0, 1, 1, 0, 1, 1, 0]
                Rectangle { width: 15; height: 15; color: modelData ? Theme.tile : "#3a3a40" }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Look")
        note: qsTr("Retro: a panorama hub with tiles, big type and an app bar, in the manner of Windows Phone.")
        Flow {
            x: 12
            width: parent.width - 24
            topPadding: 12
            bottomPadding: 12
            spacing: 12
            Repeater {
                model: [{ value: "standard", label: qsTr("Standard"), detail: qsTr("Pages and navigation") },
                        { value: "retro", label: qsTr("Retro"), detail: qsTr("Tiles and panoramas") }]
                ChoiceTile {
                    required property var modelData
                    width: pane.tileWidth
                    label: modelData.label
                    detail: modelData.detail
                    selected: (Theme.retro ? "retro" : "standard") === modelData.value
                    onClicked: App.setSetting("uiMode", modelData.value)
                    LookPreview { retro: modelData.value === "retro" }
                }
            }
        }
    }

    SettingsGroup {
        visible: Theme.retro
        title: qsTr("Retro")
        note: qsTr("Hold a tile on the hub to resize, move or unpin it. Series pages have Pin to the hub.")
        SettingRow {
            iconName: "type"
            title: qsTr("Hub title")
            description: qsTr("The big word across the top of the hub.")
            InlineField {
                width: 180
                text: Retro.title
                placeholderText: "scancore"
                maximumLength: 24
                onEditingFinished: App.setSetting("retroTitle", text.trim())
            }
        }
        SettingRow {
            iconName: "palette"
            title: qsTr("Title colour")
            Segmented {
                options: [{ value: "gradient", label: qsTr("Accent blend") }, { value: "plain", label: qsTr("Plain") }]
                value: Retro.gradientTitle ? "gradient" : "plain"
                onPicked: (v) => App.setSetting("retroTitleColor", v)
            }
        }
        SettingRow {
            iconName: "layers"
            title: qsTr("Background")
            description: qsTr("Behind the hub, moving slower than the tiles.")
            Segmented {
                options: [{ value: "recent", label: qsTr("Last read") }, { value: "theme", label: qsTr("Theme picture") },
                          { value: "none", label: qsTr("None") }]
                value: Retro.background
                onPicked: (v) => App.setSetting("retroBackground", v)
            }
        }
        SettingRow {
            iconName: "type"
            title: qsTr("Headers")
            Segmented {
                options: [{ value: "upper", label: qsTr("UPPERCASE") }, { value: "lower", label: qsTr("lowercase") }]
                value: Retro.upperCase ? "upper" : "lower"
                onPicked: (v) => App.setSetting("retroCase", v)
            }
        }
        SettingRow {
            iconName: "refresh-cw"
            title: qsTr("Live tiles")
            description: qsTr("Tiles turn over now and then to show new chapters and where you are.")
            Toggle { checked: Retro.liveTiles; onToggled: App.setSetting("retroLiveTiles", checked) }
        }
        SettingRow {
            iconName: "columns-2"
            title: qsTr("Hub sections")
            description: qsTr("Which sections the hub shows, in this order. Four or five are easiest to find your way in.")
        }
        Repeater {
            model: (Retro.sectionList, Retro.sectionKeys.filter(k => k !== "music" || Retro.musicOn).sort((a, b) => {
                const order = Retro.sectionList.map(s => s.key)
                const ia = order.indexOf(a), ib = order.indexOf(b)
                return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib)
            }))
            SettingRow {
                id: sectionRow
                required property string modelData
                readonly property var shown: Retro.sectionList.map(s => s.key)
                readonly property int at: shown.indexOf(modelData)
                title: Retro.sectionLabel(modelData)
                Row {
                    spacing: 4
                    IconButton {
                        iconName: "chevron-up"
                        tip: qsTr("Earlier")
                        enabled: sectionRow.at > 0
                        onClicked: {
                            const list = sectionRow.shown.slice()
                            list.splice(sectionRow.at, 1)
                            list.splice(sectionRow.at - 1, 0, sectionRow.modelData)
                            Retro.setSections(list)
                        }
                    }
                    IconButton {
                        iconName: "chevron-down"
                        tip: qsTr("Later")
                        enabled: sectionRow.at >= 0 && sectionRow.at < sectionRow.shown.length - 1
                        onClicked: {
                            const list = sectionRow.shown.slice()
                            list.splice(sectionRow.at, 1)
                            list.splice(sectionRow.at + 1, 0, sectionRow.modelData)
                            Retro.setSections(list)
                        }
                    }
                    Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: sectionRow.at >= 0
                        enabled: !(checked && sectionRow.shown.length <= 1)
                        onToggled: {
                            const list = sectionRow.shown.filter(k => k !== sectionRow.modelData)
                            if (checked) list.push(sectionRow.modelData)
                            Retro.setSections(list)
                        }
                    }
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Sections")
        SettingRow {
            iconName: "layers"
            title: qsTr("What you read and watch")
            description: qsTr("The tabs, Home and Retro's hub show these. A link still opens the others.")
            Row {
                spacing: 6
                Repeater {
                    model: [{ key: "comic", label: qsTr("Comics") }, { key: "novel", label: qsTr("Novels") }, { key: "anime", label: qsTr("Anime") }]
                    Chip {
                        required property var modelData
                        readonly property var shown: { const s = Array.from(App.settings.shownMedia || []); return s.length ? s : ["comic", "novel", "anime"] }
                        text: modelData.label
                        selected: shown.indexOf(modelData.key) >= 0
                        iconName: selected ? "check" : ""
                        // One always stays.
                        enabled: !(selected && shown.length === 1)
                        onClicked: Router.setShownMedia(selected ? shown.filter(m => m !== modelData.key) : shown.concat([modelData.key]))
                    }
                }
            }
        }
        SettingRow {
            iconName: "volume-2"
            title: qsTr("Music (preview)")
            description: qsTr("Anime openings and endings and your own music, with lyrics, and a player that keeps going while you read. Still being built.")
            Toggle {
                checked: App.settings.musicSection === true
                onToggled: Router.setMusicShown(checked)
            }
        }
    }

    SettingsGroup {
        visible: App.platform !== "android" && !Theme.retro
        title: qsTr("Navigation")
        note: qsTr("Narrow windows and phones always use tabs along the bottom.")
        Flow {
            x: 12
            width: parent.width - 24
            topPadding: 12
            bottomPadding: 12
            spacing: 12
            Repeater {
                model: [{ value: "sidebar", label: qsTr("Sidebar"), detail: qsTr("Full height") },
                        { value: "floating", label: qsTr("Floating"), detail: qsTr("Inset panel") },
                        { value: "rail", label: qsTr("Rail"), detail: qsTr("Icons only") },
                        { value: "dock", label: qsTr("Dock"), detail: qsTr("A bar of icons") }]
                ChoiceTile {
                    required property var modelData
                    width: pane.tileWidth
                    label: modelData.label
                    detail: modelData.detail
                    selected: pane.navStyle === modelData.value
                    onClicked: App.setSetting("navStyle", modelData.value)
                    LayoutPreview { kind: modelData.value }
                }
            }
        }
        SettingRow {
            iconName: "panel-left"
            title: pane.navStyle === "dock" ? qsTr("Dock position") : qsTr("Sidebar position")
            Segmented {
                visible: pane.navStyle !== "dock"
                options: [{ value: "left", label: qsTr("Left") }, { value: "right", label: qsTr("Right") }]
                value: pane.rightSide ? "right" : "left"
                onPicked: (v) => App.setSetting("navSide", v)
            }
            Segmented {
                visible: pane.navStyle === "dock"
                options: [{ value: "bottom", label: qsTr("Bottom") }, { value: "top", label: qsTr("Top") }]
                value: pane.topSide ? "top" : "bottom"
                onPicked: (v) => App.setSetting("dockSide", v)
            }
        }
    }

    SettingsGroup {
        title: qsTr("Size")
        SettingRow {
            iconName: "type"
            title: qsTr("Text size")
            description: qsTr("Titles, labels and descriptions throughout the app.")
            Segmented {
                options: [{ value: 0.92, label: qsTr("Small") }, { value: 1, label: qsTr("Default") },
                          { value: 1.1, label: qsTr("Large") }, { value: 1.2, label: qsTr("Larger") }]
                value: Theme.textScale
                onPicked: (v) => App.setSetting("textScale", v)
            }
        }
        SettingRow {
            iconName: "layout-grid"
            title: qsTr("Cover size")
            description: qsTr("How big covers are in grids and rows. Smaller fits more on screen.")
            Segmented {
                options: [{ value: "small", label: qsTr("Small") }, { value: "default", label: qsTr("Default") }, { value: "large", label: qsTr("Large") }]
                value: App.settings.coverSize || "default"
                onPicked: (v) => App.setSetting("coverSize", v)
            }
        }
    }

    SettingsGroup {
        title: qsTr("Pages")
        note: qsTr("Home, Comics, Novels and Anime each have an Edit page button: move, hide and restyle their sections there.")
        // Home's greeting: the time of day's, with a name, or your own words.
        SettingRow {
            iconName: "user"
            title: qsTr("Your name on Home")
            description: qsTr("Said after the greeting: “Good evening, Sam”.")
            InlineField {
                width: 180
                text: App.settings.homeName || ""
                placeholderText: qsTr("No name")
                maximumLength: 32
                onEditingFinished: App.setSetting("homeName", text.trim())
            }
        }
        SettingRow {
            iconName: "type"
            title: qsTr("Your own greeting")
            description: qsTr("Shown on Home instead of “Good morning”; {time} puts that back in, {name} your name.")
            InlineField {
                width: 220
                text: App.settings.homeGreeting || ""
                placeholderText: qsTr("Time of day")
                maximumLength: 60
                onEditingFinished: App.setSetting("homeGreeting", text.trim())
            }
        }
        SettingRow {
            iconName: "rows-3"
            title: qsTr("Spacing")
            description: qsTr("The room between a page's sections.")
            Segmented {
                options: [{ value: "compact", label: qsTr("Compact") }, { value: "default", label: qsTr("Default") }, { value: "spacious", label: qsTr("Spacious") }]
                value: App.settings.pageSpacing || "default"
                onPicked: (v) => App.setSetting("pageSpacing", v)
            }
        }
        SettingRow {
            iconName: "square"
            title: qsTr("Corners")
            description: qsTr("How round covers, cards, buttons and panels are.")
            Segmented {
                options: [{ value: "square", label: qsTr("Square") }, { value: "subtle", label: qsTr("Subtle") },
                          { value: "default", label: qsTr("Default") }, { value: "round", label: qsTr("Round") }]
                value: App.settings.roundness || "default"
                onPicked: (v) => App.setSetting("roundness", v)
            }
        }
        SettingRow {
            iconName: "rotate-ccw"
            title: qsTr("Page arrangements")
            description: qsTr("Put every page's sections back in their first order and look.")
            ScButton {
                text: qsTr("Reset all")
                variant: "ghost"
                onClicked: ["home", "comics", "comics-top", "novels", "novels-top", "anime"].forEach(p => PageLayout.reset(p))
            }
        }
    }

    SettingsGroup {
        title: qsTr("Motion")
        SettingRow {
            iconName: "gauge"
            title: qsTr("Animation speed")
            description: qsTr("How quickly pages, menus and covers move.")
            enabled: !Motion.reduced
            opacity: enabled ? 1 : 0.45
            Segmented {
                options: [{ value: "relaxed", label: qsTr("Relaxed") }, { value: "default", label: qsTr("Default") }, { value: "snappy", label: qsTr("Snappy") }]
                value: App.settings.motionSpeed || "default"
                onPicked: (v) => App.setSetting("motionSpeed", v)
            }
        }
        SettingRow {
            visible: App.platform !== "android"
            iconName: "move-vertical"
            title: qsTr("Scroll speed")
            description: qsTr("How far the mouse wheel and touchpad move pages.")
            Segmented {
                options: [{ value: 0.7, label: qsTr("Slow") }, { value: 1, label: qsTr("Default") },
                          { value: 1.5, label: qsTr("Fast") }, { value: 2.2, label: qsTr("Faster") }]
                value: Number(App.settings.scrollSpeed) || 1
                onPicked: (v) => App.setSetting("scrollSpeed", v)
            }
        }
        SettingRow {
            visible: App.platform === "android"
            iconName: "gauge"
            title: qsTr("Highest refresh rate")
            description: qsTr("Scroll and animate at your screen's fastest rate, such as 120 Hz, instead of the rate the phone picks for apps. Uses a little more battery.")
            Toggle { checked: App.settings.highRefresh !== false; onToggled: App.setSetting("highRefresh", checked) }
        }
        SettingRow {
            iconName: "hand"
            title: qsTr("Fling glide")
            description: App.platform === "android" ? qsTr("How long lists keep moving after a swipe.")
                                                    : qsTr("How long lists keep moving after a touchpad or touch screen swipe.")
            Segmented {
                options: [{ value: "short", label: qsTr("Short") }, { value: "default", label: qsTr("Default") }, { value: "long", label: qsTr("Long") }]
                value: App.settings.flingGlide || "default"
                onPicked: (v) => App.setSetting("flingGlide", v)
            }
        }
        SettingRow {
            iconName: "sparkles"
            title: qsTr("Reduce motion")
            description: qsTr("Turns off page transitions, hover lifts, animated backgrounds and other animations.")
            Toggle { checked: App.settings.reducedMotion === true; onToggled: App.setSetting("reducedMotion", checked) }
        }
        SettingRow {
            iconName: "book-open"
            title: qsTr("E-ink screen")
            description: qsTr("For Boox, Kobo and other e-paper devices: black on white, no animation, comics in pages and novels turned page by page with a tap on the sides.")
            Toggle {
                checked: App.settings.eink === true
                onToggled: {
                    App.setSetting("eink", checked)
                    if (checked) App.setSetting("readerMode", "paged")
                }
            }
        }
        SettingRow {
            visible: App.settings.eink === true
            iconName: "square"
            title: qsTr("Flash on page turns")
            description: qsTr("A moment of black when a comic page turns clears the last page's ghost.")
            Toggle { checked: App.settings.einkFlash !== false; onToggled: App.setSetting("einkFlash", checked) }
        }
    }

    SettingsGroup {
        title: qsTr("Start")
        SettingRow {
            iconName: "house"
            title: qsTr("Open SCANCORE on")
            description: qsTr("The page you see when the app starts.")
            Select {
                model: [{ value: "home", label: "Home" }, { value: "comics", label: qsTr("Comics") }, { value: "novels", label: qsTr("Novels") },
                        { value: "anime", label: qsTr("Anime") }, { value: "library", label: qsTr("Library") }, { value: "history", label: qsTr("History") }]
                value: App.settings.startSection || "home"
                onPicked: (v) => App.setSetting("startSection", v)
            }
        }
    }
}
