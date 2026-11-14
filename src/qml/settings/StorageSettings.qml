import QtQuick
import QtQuick.Dialogs
import Scancore

SettingsPane {
    id: pane
    title: qsTr("Storage")
    subtitle: qsTr("Cache, downloads and backups")
    iconName: "hard-drive"

    property var cacheTask: App.cacheSize()
    readonly property real cacheBytes: cacheTask && cacheTask.ok ? Number(cacheTask.result) : 0
    readonly property real cacheLimit: (App.settings.imageCacheMB || 1024) * 1024 * 1024

    function report(task, success) {
        task.finished.connect(() => Router.toast(typeof success === "function" ? success(task.result) : success, "success"))
        task.failed.connect((error) => Router.toast(error, "error"))
    }

    SettingsGroup {
        title: qsTr("On this device")
        SettingRow {
            iconName: "layers"
            title: qsTr("Image cache")
            description: qsTr("Covers and pages you have viewed, kept so they load instantly next time. The oldest are removed first when it is full.")
            Row {
                spacing: 8
                Select {
                    model: [{ value: 256, label: qsTr("256 MB") }, { value: 512, label: qsTr("512 MB") }, { value: 1024, label: qsTr("1 GB") },
                            { value: 2048, label: qsTr("2 GB") }, { value: 4096, label: qsTr("4 GB") }]
                    value: App.settings.imageCacheMB || 1024
                    onPicked: (v) => {
                        App.setSetting("imageCacheMB", v)
                        refreshCache.start()
                    }
                }
                ScButton {
                    text: qsTr("Clear")
                    onClicked: {
                        App.clearCache()
                        Router.toast(qsTr("Cache cleared"))
                        refreshCache.start()
                    }
                }
            }
        }
        // Usage meter for the cache.
        Item {
            width: parent.width
            height: 46
            Rectangle {
                x: 62
                y: 2
                width: parent.width - x - 16
                height: 6
                radius: 3
                color: Theme.surface3
                Rectangle {
                    width: Math.max(height, parent.width * Math.min(1, pane.cacheBytes / pane.cacheLimit))
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
                }
            }
            Text {
                x: 62
                y: 16
                text: (pane.cacheTask && pane.cacheTask.ok ? App.formatBytes(pane.cacheBytes) : qsTr("Measuring…"))
                    + qsTr(" of ") + App.formatBytes(pane.cacheLimit)
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs + 1
            }
        }
        SettingRow {
            iconName: "download"
            title: qsTr("Downloaded chapters")
            description: Downloads.sizeOnDisk() + (!App.mobile ? " · " + Downloads.folder() : "")
            ScButton {
                visible: !App.mobile
                text: qsTr("Open folder")
                iconName: "folder-open"
                onClicked: App.openFolder(Downloads.folder())
            }
        }
        SettingRow {
            visible: !App.mobile
            iconName: "hard-drive"
            title: qsTr("App data")
            description: App.dataFolder
            ScButton { text: qsTr("Open folder"); iconName: "folder-open"; onClicked: App.openFolder(App.dataFolder) }
        }
    }
    Timer { id: refreshCache; interval: 800; onTriggered: pane.cacheTask = App.cacheSize() }

    SettingsGroup {
        title: qsTr("Backup")
        note: qsTr("Restoring replaces what is on this device when SCANCORE next starts.")
        SettingRow {
            iconName: "shield-check"
            title: qsTr("Back up everything")
            description: qsTr("Your library, history, reading positions and settings in one file.")
            Row {
                spacing: 8
                ScButton { text: qsTr("Export"); iconName: "download"; onClicked: exportDialog.open() }
                ScButton { text: qsTr("Restore"); variant: "ghost"; onClicked: importDialog.open() }
            }
        }
    }

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "scancore"
        nameFilters: [qsTr("SCANCORE backup (*.scancore)")]
        selectedFile: "scancore-backup-" + Qt.formatDate(new Date(), "yyyy-MM-dd") + ".scancore"
        onAccepted: pane.report(App.exportBackup(selectedFile), qsTr("Backup saved"))
    }
    FileDialog {
        id: importDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("SCANCORE backup (*.scancore)"), qsTr("All files (*)")]
        onAccepted: pane.report(App.importBackup(selectedFile), (r) => r)
    }
}
