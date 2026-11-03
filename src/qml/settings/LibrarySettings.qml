import QtQuick
import Scancore

SettingsPane {
    id: pane
    title: qsTr("Library")
    subtitle: qsTr("New chapters, notifications and background work")
    iconName: "library"

    SettingsGroup {
        title: qsTr("Updates")
        SettingRow {
            iconName: "refresh-cw"
            title: qsTr("Check for new chapters and episodes")
            description: qsTr("Saved comics, novels and anime are checked in the background while SCANCORE is open.")
            Select {
                model: [{ value: 0, label: qsTr("Never") }, { value: 1, label: qsTr("Every hour") }, { value: 2, label: qsTr("Every 2 hours") },
                        { value: 6, label: qsTr("Every 6 hours") }, { value: 24, label: qsTr("Once a day") }]
                value: App.settings.libraryCheckHours
                onPicked: (v) => App.setSetting("libraryCheckHours", v)
            }
        }
        SettingRow {
            iconName: "bell"
            title: qsTr("Notify me about new chapters and episodes")
            description: qsTr("They are always listed under Notifications; this also shows them in the system's notifications.")
            Toggle {
                checked: App.settings.notifyUpdates !== false
                onToggled: {
                    App.setSetting("notifyUpdates", checked)
                    if (checked && !Notifications.systemAllowed()) Notifications.askPermission()
                }
            }
        }
        // Switched on here but not allowed by the system: nothing would show.
        SettingRow {
            readonly property bool blocked: {
                App.dependOn(Qt.application.state)
                return App.platform === "android" && App.settings.notifyUpdates !== false && !Notifications.systemAllowed()
            }
            visible: blocked
            iconName: "bell"
            iconTint: Theme.danger
            title: qsTr("Android is not showing SCANCORE's notifications")
            description: qsTr("Allow them, or switch them back on in Android's settings for SCANCORE.")
            ScButton {
                text: qsTr("Allow")
                variant: "primary"
                compact: true
                onClicked: Notifications.askPermission()
            }
        }
        SettingRow {
            iconName: "list"
            title: qsTr("One notification per check")
            description: qsTr("Sums up every new chapter a check found, instead of one notification per series.")
            Toggle { checked: App.settings.notifyDigest === true; onToggled: App.setSetting("notifyDigest", checked) }
        }
        SettingRow {
            iconName: "megaphone"
            title: qsTr("Announcements from SCANCORE")
            description: qsTr("News about SCANCORE from scancore.onl, in Notifications as soon as they are posted.")
            Toggle { checked: App.settings.announcements !== false; onToggled: App.setSetting("announcements", checked) }
        }
    }

    // Every list in the library's order (issue #9): the built-in ones can
    // be renamed, moved and hidden; the user's own can be removed too.
    readonly property var allLists: Router.libraryCategories("comic", true)
    readonly property int ownCount: allLists.filter(l => l.custom).length
    ConfirmDialog {
        id: listName
        property string listId: ""
        asksText: true
        heading: listId ? qsTr("Rename list") : qsTr("New list")
        placeholder: qsTr("Name, like Isekai or Weekly")
        confirmText: listId ? qsTr("Rename") : qsTr("Add")
        onConfirmed: listId ? Library.renameCategory(listId, value) : Library.addCategory(value)
        function ask(id, name) { listId = id; value = name; open() }
    }
    ConfirmDialog {
        id: listRemove
        property var list: null
        heading: list ? qsTr("Remove the list %1?").arg(list.label) : ""
        message: qsTr("Its series move to %1; nothing is removed from your library.")
            .arg((pane.allLists.find(c => c.value === "reading") || { label: qsTr("Reading") }).label)
        confirmText: qsTr("Remove")
        destructive: true
        onConfirmed: Library.removeCategory(list.value)
    }

    SettingsGroup {
        title: qsTr("Lists")
        note: qsTr("Rename, reorder or hide any list, and add your own. A hidden list's series stay on it and under All. Reading, Plan to read, On hold, Completed and Dropped keep their meaning for trackers and backups whatever they are called. Mihon and LNReader backups bring their lists along.")
        Repeater {
            model: pane.allLists
            SettingRow {
                required property var modelData
                required property int index
                iconName: modelData.custom ? "list" : "bookmark"
                title: modelData.label
                description: modelData.hidden ? qsTr("Hidden from the library")
                    : modelData.renamed ? qsTr("Built-in list, first called %1").arg(modelData.defaultLabel) : ""
                opacity: modelData.hidden ? 0.7 : 1
                Row {
                    spacing: 2
                    IconButton { iconName: "pencil"; tip: qsTr("Rename"); onClicked: listName.ask(modelData.value, modelData.label) }
                    IconButton {
                        iconName: modelData.hidden ? "eye-off" : "eye"
                        tip: modelData.hidden ? qsTr("Show in the library") : qsTr("Hide from the library")
                        active: modelData.hidden
                        onClicked: Library.setCategoryHidden(modelData.value, !modelData.hidden)
                    }
                    IconButton { iconName: "chevron-up"; tip: qsTr("Move up"); enabled: index > 0; opacity: enabled ? 1 : 0.35; onClicked: Library.moveCategory(modelData.value, -1) }
                    IconButton { iconName: "chevron-down"; tip: qsTr("Move down"); enabled: index < pane.allLists.length - 1; opacity: enabled ? 1 : 0.35; onClicked: Library.moveCategory(modelData.value, 1) }
                    // Built-in lists cannot go: the space keeps the columns.
                    IconButton {
                        iconName: modelData.custom ? "trash" : "rotate-ccw"
                        tip: modelData.custom ? qsTr("Remove list") : qsTr("Use the name %1").arg(modelData.defaultLabel)
                        enabled: modelData.custom || modelData.renamed
                        opacity: enabled ? 1 : 0
                        onClicked: {
                            if (modelData.custom) { listRemove.list = modelData; listRemove.open() }
                            else Library.renameCategory(modelData.value, "")
                        }
                    }
                }
            }
        }
        SettingRow {
            iconName: "plus"
            title: pane.ownCount ? qsTr("Add another list") : qsTr("Add a list")
            ScButton { text: qsTr("New list"); iconName: "plus"; onClicked: listName.ask("", "") }
        }
    }

    // Library lists as chips that switch a list setting's members.
    component ListChips: Flow {
        id: chips
        property string setting
        readonly property var chosen: App.settings[setting] || []
        width: Math.min(520, pane.width - 120)
        spacing: 6
        Repeater {
            model: Router.libraryCategories("comic", true)
            Chip {
                required property var modelData
                text: modelData.label
                selected: chips.chosen.indexOf(modelData.value) >= 0
                onClicked: {
                    const next = chips.chosen.filter(v => v !== modelData.value)
                    if (!selected) next.push(modelData.value)
                    App.setSetting(chips.setting, next)
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Which series are checked")
        SettingRow {
            iconName: "list-checks"
            title: qsTr("Lists")
            description: qsTr("Only series on these lists of your library are checked for new chapters.")
            wide: true
            ListChips { setting: "updateLists" }
        }
        SettingRow {
            iconName: "check"
            title: qsTr("Skip finished series")
            description: qsTr("Series their site marks as completed.")
            Toggle { checked: App.settings.updateSkipFinished !== false; onToggled: App.setSetting("updateSkipFinished", checked) }
        }
        SettingRow {
            iconName: "book-open"
            title: qsTr("Skip series with chapters left to read")
            description: qsTr("Checked again once you catch up.")
            Toggle { checked: App.settings.updateSkipUnread === true; onToggled: App.setSetting("updateSkipUnread", checked) }
        }
        SettingRow {
            iconName: "clock"
            title: qsTr("Skip series you have not started")
            Toggle { checked: App.settings.updateSkipNotStarted === true; onToggled: App.setSetting("updateSkipNotStarted", checked) }
        }
        SettingRow {
            iconName: "wifi-off"
            title: qsTr("Check by itself only on Wi-Fi")
            description: qsTr("Not on mobile data or a metered connection. Checking by hand always works.")
            Toggle { checked: App.settings.updateOnlyUnmetered === true; onToggled: App.setSetting("updateOnlyUnmetered", checked) }
        }
        SettingRow {
            visible: App.platform === "android"
            iconName: "battery-warning"
            title: qsTr("Check by itself only while charging")
            Toggle { checked: App.settings.updateOnlyCharging === true; onToggled: App.setSetting("updateOnlyCharging", checked) }
        }
    }

    SettingsGroup {
        title: qsTr("Downloads")
        SettingRow {
            iconName: "download"
            title: qsTr("Download new chapters")
            description: (App.settings.autoDownloadLists || []).length
                ? qsTr("New chapters of series on these lists download as soon as they are found.")
                : qsTr("Pick lists whose new chapters download as soon as they are found.")
            wide: true
            ListChips { setting: "autoDownloadLists" }
        }
        SettingRow {
            iconName: "trash"
            title: qsTr("Delete chapters once read")
            description: qsTr("A downloaded chapter goes when you finish the next one.")
            Toggle { checked: App.settings.deleteAfterRead === true; onToggled: App.setSetting("deleteAfterRead", checked) }
        }
    }

    SettingsGroup {
        visible: App.platform !== "android"
        title: qsTr("Background")
        SettingRow {
            iconName: "minimize"
            title: qsTr("Keep running in the tray when the window closes")
            description: qsTr("Lets update checks and downloads continue in the background.")
            Toggle { checked: App.settings.closeToTray === true; onToggled: App.setSetting("closeToTray", checked) }
        }
    }

    SettingsGroup {
        visible: App.platform === "android"
        title: qsTr("Background")
        SettingRow {
            iconName: App.batteryRestricted ? "battery-warning" : "check"
            title: App.batteryRestricted ? qsTr("Battery optimisation stops SCANCORE in the background")
                                         : qsTr("SCANCORE can run in the background")
            description: App.batteryRestricted
                ? qsTr("Android closes it soon after you leave it, so new chapters are not checked, downloads pause and reading aloud stops.")
                : qsTr("Update checks, downloads and reading aloud carry on when you leave the app.")
            ScButton {
                visible: App.batteryRestricted
                text: qsTr("Allow")
                onClicked: App.askBatteryUnrestricted()
            }
        }
    }
}
