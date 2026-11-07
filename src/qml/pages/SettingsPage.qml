import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Settings. Wide windows list the categories beside the one being shown;
// phones show the categories as a list and open each on its own page
// (SettingsDetail.qml).
Item {
    id: page

    // keywords: what the category list's search also finds it by.
    readonly property var allSections: [
        { key: "account", file: "AccountSettings.qml", title: qsTr("Sync"), subtitle: qsTr("scancore.onl, your server, WebDAV or a folder"), icon: "cloud", tint: "sky",
          keywords: "account sign in sign out sync scancore.onl website login server self host own database webdav nextcloud owncloud synology folder syncthing dropbox onedrive devices" },
        { key: "connections", file: "ConnectionsSettings.qml", title: qsTr("Connections"), subtitle: qsTr("AniList, MyAnimeList, Comick and imports"), icon: "link", tint: "emerald",
          keywords: "anilist myanimelist mal comick tracking tracker list discord rich presence status import mihon tachiyomi tachibk backup export xml bring library" },
        { key: "theme", file: "ThemeSettings.qml", title: qsTr("Theme"), subtitle: qsTr("Colours, background and transparency"), icon: "palette", tint: "violet",
          keywords: "dark light black system custom colour color accent wheel background picture image gif wallpaper transparency transparent opacity panels export import sct share" },
        { key: "interface", file: "InterfaceSettings.qml", title: qsTr("Interface"), subtitle: qsTr("Navigation, size and motion"), icon: "layout-grid", tint: "indigo",
          keywords: "language translation idioma langue bahasa idioma sidebar dock rail floating navigation left right top bottom text size font cover animation motion speed reduce start page" },
        { key: "keyboard", file: "KeyboardSettings.qml", title: qsTr("Keyboard"), subtitle: qsTr("Shortcuts"), icon: "keyboard", tint: "mono",
          keywords: "keys shortcuts keybinds hotkeys bindings key profiles per series", desktop: true },
        { key: "reading", file: "ReadingSettings.qml", title: qsTr("Reading"), subtitle: qsTr("Comic and novel readers"), icon: "book-open", tint: "ember",
          keywords: "comic novel reader strip pages direction width gap font size line spacing justify sepia paper" },
        { key: "anime", file: "AnimeSettings.qml", title: qsTr("Anime"), subtitle: qsTr("Playback and skipping"), icon: "tv", tint: "rose",
          keywords: "player next episode autoplay countdown skip intro ending recap" },
        { key: "library", file: "LibrarySettings.qml", title: qsTr("Library"), subtitle: qsTr("New chapters and notifications"), icon: "library", tint: "green",
          keywords: "updates new chapters check notifications tray background" },
        { key: "privacy", file: "PrivacySettings.qml", title: qsTr("Privacy"), subtitle: qsTr("Incognito mode and the app lock"), icon: "lock", tint: "rose",
          keywords: "incognito private history lock pin password fingerprint face biometric security" },
        { key: "sources", file: "SourcesSettings.qml", title: qsTr("Sources"), subtitle: qsTr("The sites SCANCORE reads from"), icon: "globe", tint: "amber",
          keywords: "sites nsfw manifest novel pack extensions" },
        { key: "storage", file: "StorageSettings.qml", title: qsTr("Storage"), subtitle: qsTr("Cache, downloads and backups"), icon: "hard-drive", tint: "mono",
          keywords: "cache downloads folder backup restore export data" },
        { key: "about", file: "AboutSettings.qml", title: qsTr("About"), subtitle: qsTr("Version, updates and help"), icon: "info", tint: "mono",
          keywords: "version updates help licence discord welcome" }
    ]
    readonly property var sections: allSections.filter(s => !s.desktop || App.platform !== "android")

    function sectionFor(key) {
        for (const s of sections)
            if (s.key === key) return s
        return null
    }

    function tint(name) {
        if (name === "green") return Theme.success
        if (name === "amber") return Theme.warning
        if (name === "mono" || !Theme.accents[name]) return Theme.textMuted
        return Theme.dark ? Theme.accents[name].dark : Theme.accents[name].light
    }

    // One line under each category on the phone list.
    function summary(key) {
        App.dependOn(App.settings)
        switch (key) {
        case "theme": {
            const theme = { system: qsTr("System"), light: qsTr("Light"), dark: qsTr("Dark"), black: qsTr("Black"), custom: qsTr("Custom") }[App.settings.theme] || qsTr("Dark")
            const accent = String(App.settings.accent || "mono").replace(/^retro-/, "")
            return theme + qsTr(" theme · ") + accent.charAt(0).toUpperCase() + accent.slice(1) + qsTr(" accent")
        }
        case "interface":
            if (Theme.retro) return qsTr("Retro") + " · " + ({ relaxed: "relaxed", snappy: "snappy" }[App.settings.motionSpeed] || "default") + qsTr(" motion")
            return ({ sidebar: qsTr("Sidebar"), floating: qsTr("Floating sidebar"), rail: qsTr("Icon rail"), dock: qsTr("Dock") }[App.settings.navStyle] || qsTr("Sidebar"))
                + " · " + ({ relaxed: "relaxed", snappy: "snappy" }[App.settings.motionSpeed] || "default") + qsTr(" motion")
        case "keyboard": {
            const changed = Object.keys(Keymap.changed).length
            return changed ? changed + (changed === 1 ? qsTr(" shortcut changed") : qsTr(" shortcuts changed")) : qsTr("Default shortcuts")
        }
        case "reading":
            return (App.settings.readerMode === "paged" ? qsTr("Pages") : qsTr("Vertical strip")) + " · "
                + ({ "Literata": qsTr("Serif"), "DM Sans": qsTr("Sans"), "Space Grotesk": qsTr("Grotesk") }[App.settings.novelFont] || qsTr("Serif")) + qsTr(" text")
        case "anime":
            return qsTr("Auto-next ") + (App.settings.autoNext !== false ? "on" : "off") + qsTr(" · Auto-skip ")
                + (App.settings.autoSkip === true || App.settings.autoSkipEndings === true ? "on" : "off")
        case "privacy":
            App.dependOn(Lock.enabled)
            return [App.settings.incognito === true ? qsTr("Incognito on") : qsTr("Incognito off"),
                    Lock.enabled ? qsTr("locked with a PIN") : qsTr("no app lock")].join(" · ")
        case "library": {
            const hours = App.settings.libraryCheckHours
            return hours > 0 ? qsTr("Checks for new chapters every ") + (hours === 1 ? "hour" : hours === 24 ? "day" : hours + qsTr(" hours")) : qsTr("Update checks off")
        }
        case "connections": {
            const on = Trackers.services.filter(s => s.connected).map(s => s.key === "comick" ? qsTr("Comick") : s.label).concat([
                        Presence.available && App.settings.discordPresence === true ? qsTr("Discord") : ""]).filter(s => s)
            return on.length ? on.join(" · ") + qsTr(" connected") : qsTr("Tracking and imports")
        }
        case "sources": return qsTr("Source list version ") + App.manifestVersion
        case "storage": return qsTr("Cache, downloads and backup")
        case "about": return qsTr("Version ") + App.version
        }
        return ""
    }

    function open(section) {
        Router.shell.push("pages/SettingsDetail.qml", { section: section, tint: page.tint(section.tint) })
    }
    // Router.openSettings(category) on a phone: straight to that category,
    // once this page is the one showing (its stack is still being made on
    // the first visit).
    function jump() {
        const s = Theme.compact && Router.settingsJump ? sectionFor(Router.settingsJump) : null
        Router.settingsJump = ""
        // Its own stack: Router.shell.push goes to the section showing, and
        // on the first visit this stack is not the section's item yet.
        if (s && page.StackView.view) Qt.callLater(() => page.StackView.view.push(Qt.resolvedUrl("SettingsDetail.qml"), { section: s, tint: page.tint(s.tint) }))
        else if (s) open(s)
    }
    StackView.onActivated: jump()
    Connections {
        target: Router
        function onSettingsJumpChanged() {
            if (!Router.settingsJump) return
            if (page.StackView.status === StackView.Active || !Theme.compact) page.jump()
            // Under another category: back to the list, which then opens it.
            else if (page.StackView.view) page.StackView.view.pop(page)
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: Theme.compact ? phoneLayout : wideLayout
    }

    // ------------------------------------------------------------ wide

    Component {
        id: wideLayout
        Item {
            id: wide
            readonly property int navWidth: width >= 1000 ? 240 : 204
            property string currentKey: page.sectionFor(App.settings.settingsCategory) ? App.settings.settingsCategory : "account"
            readonly property var current: page.sectionFor(currentKey) || page.sections[0]
            property string query: ""
            readonly property var matches: {
                const q = query.trim().toLowerCase()
                if (!q) return page.sections
                return page.sections.filter(s => (s.title + " " + s.subtitle + " " + s.keywords).toLowerCase().includes(q))
            }

            function show(key) {
                if (key === currentKey) return
                currentKey = key
                App.setSetting("settingsCategory", key)
            }

            // Over a background picture the list sits on a panel, like the
            // sidebar, so it stays readable.
            Rectangle {
                visible: Theme.layered
                x: navFlick.x - 12
                y: nav.y - 14
                width: nav.width + 24
                height: Math.min(nav.height + 28, wide.height - y - 10)
                radius: Theme.radiusXl
                color: Theme.sidebar
                border.width: 1
                border.color: Theme.border
            }

            // Scrolls when the categories do not fit (a TV's screen is only
            // about 540 points high), with the mouse, a pointer remote or the
            // D-pad (SpatialFocus scrolls to the focused one).
            SmoothFlickable {
                id: navFlick
                x: Theme.gutter
                width: wide.navWidth
                height: wide.height
                contentHeight: nav.y + nav.height + 24

                Column {
                    id: nav
                    y: 24
                    width: wide.navWidth
                    spacing: 2
                    Text {
                        text: qsTr("Settings")
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.text3xl
                        font.weight: Font.Bold
                        font.letterSpacing: -0.6
                    }
                    Text {
                        bottomPadding: 16
                        text: qsTr("Stored on this computer")
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                    }
                    SearchField {
                        width: parent.width
                        implicitHeight: 36
                        placeholderText: qsTr("Find a setting")
                        onTextChanged: wide.query = text
                        onSubmitted: if (wide.matches.length > 0) wide.show(wide.matches[0].key)
                    }
                    Item { width: 1; height: 12 }
                    Repeater {
                        model: wide.matches
                        AbstractButton {
                            id: item
                            required property var modelData
                            readonly property bool active: wide.currentKey === modelData.key
                            width: nav.width
                            height: 40
                            hoverEnabled: true
                            onClicked: wide.show(modelData.key)
                            background: Rectangle {
                                radius: Theme.radiusMd
                                color: item.active ? Theme.surface2 : item.hovered ? Theme.alpha(Theme.text, 0.04) : Theme.alpha(Theme.text, 0)
                                border.width: item.active ? 1 : 0
                                border.color: Theme.border
                                Behavior on color { ColorAnimation { duration: Motion.fast } }
                            }
                            contentItem: Row {
                                leftPadding: 8
                                spacing: 11
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 26
                                    height: 26
                                    radius: 8
                                    color: Theme.alpha(page.tint(item.modelData.tint), item.active ? 0.2 : 0.12)
                                    Icon { anchors.centerIn: parent; name: item.modelData.icon; size: 14; color: page.tint(item.modelData.tint) }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: nav.width - 60
                                    elide: Text.ElideRight
                                    text: item.modelData.title
                                    color: item.active ? Theme.text : Theme.textMuted
                                    font.family: Theme.fontSans
                                    font.pixelSize: Theme.textMd
                                    font.weight: item.active ? Font.DemiBold : Font.Medium
                                }
                            }
                        }
                    }
                    Text {
                        visible: wide.matches.length === 0
                        topPadding: 8
                        leftPadding: 8
                        text: qsTr("Nothing matches")
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                    }
                }
            }

            // The chosen category, sliding in when it changes.
            SmoothFlickable {
                id: flick
                anchors.fill: parent
                anchors.leftMargin: navFlick.x + navFlick.width + 28
                contentHeight: pane.y + pane.height + 80

                // Over a background picture: a panel behind the category, so
                // its headings and notes stay readable.
                Rectangle {
                    visible: Theme.layered
                    x: pane.x - 20
                    y: pane.y - 20
                    width: pane.width + 40
                    height: pane.height + 40
                    radius: Theme.radiusXl
                    color: Theme.sidebar
                    border.width: 1
                    border.color: Theme.border
                    opacity: pane.opacity
                }

                Loader {
                    id: pane
                    x: Theme.layered ? 24 : 4
                    y: Theme.layered ? 44 : 30
                    width: Math.min(flick.width - x - Theme.gutter - (Theme.layered ? 20 : 0), 780)
                    property string key: ""
                    onLoaded: reveal.restart()
                    Component.onCompleted: load()
                    function load() {
                        key = wide.current.key
                        setSource(Qt.resolvedUrl("../settings/" + wide.current.file), { tint: page.tint(wide.current.tint) })
                    }
                    Connections {
                        target: wide
                        function onCurrentKeyChanged() {
                            flick.scrollTo(0, false)
                            pane.load()
                        }
                    }
                    transform: Translate { id: shift }
                    ParallelAnimation {
                        id: reveal
                        NumberAnimation { target: pane; property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
                        NumberAnimation { target: shift; property: "y"; from: 14; to: 0; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.decelerate }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ phone

    Component {
        id: phoneLayout
        SmoothFlickable {
            contentHeight: list.height + 40

            Column {
                id: list
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                spacing: 22

                PageHeader {
                    x: -Theme.gutter
                    width: list.width + 2 * Theme.gutter
                    title: qsTr("Settings")
                    subtitle: qsTr("Stored on this device")
                }

                // Account summary; opens the account page.
                AbstractButton {
                    id: accountCard
                    width: parent.width
                    height: 76
                    onClicked: page.open(page.sections[0])
                    background: Rectangle {
                        radius: Theme.radiusLg
                        color: accountCard.pressed ? Theme.surface2 : Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    contentItem: Item {
                        Rectangle {
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 46
                            height: 46
                            radius: 23
                            color: Account.signedIn ? Theme.accent : Theme.alpha(page.tint("sky"), 0.14)
                            Text {
                                anchors.centerIn: parent
                                visible: Account.signedIn
                                text: String(Account.user.username || Account.user.email || "?").slice(0, 1).toUpperCase()
                                color: Theme.accentInk
                                font.family: Theme.fontDisplay
                                font.pixelSize: 20
                                font.weight: Font.Bold
                            }
                            Icon { anchors.centerIn: parent; visible: !Account.signedIn; name: "user-round"; size: 20; color: page.tint("sky") }
                            // The profile photo; the letter shows until it loads, or without one.
                            Cover {
                                anchors.fill: parent
                                readonly property string url: String(Account.user.avatar || "")
                                visible: Account.signedIn && url.length > 0 && ready
                                source: Account.signedIn && url.length > 0 ? (url.startsWith("/") ? Account.site + url : url) : ""
                                radius: 23
                            }
                        }
                        Column {
                            x: 74
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - x - 40
                            spacing: 3
                            Text {
                                width: parent.width
                                text: Sync.kind === "webdav" ? qsTr("Syncing with WebDAV") : Sync.kind === "folder" ? qsTr("Syncing with a folder")
                                    : Account.signedIn ? (Account.user.username || Account.user.email) : qsTr("Set up sync")
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textLg
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: Sync.kind !== "" ? (Sync.syncing ? qsTr("Syncing…") : Sync.lastSync ? qsTr("Synced ") + App.relativeTime(Number(Sync.lastSync)) : Sync.location)
                                    : Account.signedIn ? (Account.syncing ? qsTr("Syncing…") : Account.lastSync ? qsTr("Synced ") + App.relativeTime(Number(Account.lastSync)) : String(Account.site).replace(/^https?:\/\//, "") + qsTr(" account"))
                                    : qsTr("scancore.onl, your server, WebDAV or a folder")
                                color: Theme.textMuted
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                            }
                        }
                        Icon {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron-right"
                            size: 18
                            color: Theme.textFaint
                        }
                    }
                }

                Repeater {
                    model: [["theme", "interface", "keyboard"], ["reading", "anime", "library"], ["connections", "sources", "storage"], ["privacy", "about"]]
                    SettingsGroup {
                        required property var modelData
                        Repeater {
                            model: modelData.filter(key => page.sectionFor(key) !== null)
                            SettingRow {
                                required property string modelData
                                readonly property var section: page.sectionFor(modelData)
                                iconName: section.icon
                                iconTint: page.tint(section.tint)
                                title: section.title
                                description: page.summary(section.key)
                                clickable: true
                                onClicked: page.open(section)
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("SCANCORE ") + App.version + qsTr(" · Medium Dev")
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                }
            }
        }
    }
}
