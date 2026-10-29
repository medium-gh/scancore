import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Scancore

// Sync: a scancore.onl account, an account on a SCANCORE sync server you host,
// a WebDAV folder (Nextcloud, a NAS) or a folder another program keeps in
// step. One at a time; account sync rests while WebDAV or folder sync is on.
SettingsPane {
    id: pane
    title: qsTr("Sync")
    subtitle: qsTr("Your library, progress and activity on every device")
    iconName: "cloud"

    readonly property string active: Sync.kind !== "" ? Sync.kind : Account.customSite ? "server" : "scancore"
    // The tab shown; starts on the one in use.
    property string view: active
    // Phones: labels only, so the tabs fit beside each other.
    readonly property var methods: [
        { value: "scancore", label: "scancore.onl", icon: Theme.compact ? "" : "cloud" },
        { value: "server", label: qsTr("Your server"), icon: Theme.compact ? "" : "server" },
        { value: "webdav", label: "WebDAV", icon: Theme.compact ? "" : "globe" }
    ].concat(App.platform === "android" ? [] : [{ value: "folder", label: qsTr("Folder"), icon: Theme.compact ? "" : "folder-open" }])

    readonly property string name: Account.user.username || Account.user.email || ""
    readonly property string avatarUrl: {
        const a = Account.user.avatar || ""
        return a.startsWith("/") ? Account.site + a : a
    }
    readonly property string host: String(Account.site).replace(/^https?:\/\//, "")
    readonly property string syncLine: Account.syncing ? qsTr("Syncing…")
        : Account.lastSync ? qsTr("Synced ") + App.relativeTime(Number(Account.lastSync))
        : qsTr("Not synced yet")

    function report(task) {
        task.finished.connect((r) => Router.toast(qsTr("Synced · ") + r.libraryImported + qsTr(" saved, ") + r.historyImported + qsTr(" progress updates in, ")
                                                  + r.pushed + qsTr(" sent"), "success"))
        task.failed.connect((error) => Router.toast(error, "error"))
    }

    // Scrolls sideways when the tabs are wider than the pane.
    Flickable {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        width: parent.width
        height: methodTabs.height
        contentWidth: methodTabs.width
        interactive: contentWidth > width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Segmented {
            id: methodTabs
            options: pane.methods
            value: pane.view
            onPicked: (v) => pane.view = v
        }
    }

    // ------------------------------------------------------------ accounts

    // Signed in elsewhere than the tab shows.
    Notice {
        visible: (pane.view === "scancore" && Account.signedIn && Account.customSite)
            || (pane.view === "server" && Account.signedIn && !Account.customSite)
        text: qsTr("You are signed in to ") + pane.host + qsTr(". Sign out to use ") + (pane.view === "scancore" ? "scancore.onl." : qsTr("another website."))
    }
    Notice {
        visible: (pane.view === "scancore" || pane.view === "server") && Sync.kind !== ""
        text: qsTr("Sync is using ") + (Sync.kind === "webdav" ? "WebDAV" : qsTr("a folder")) + qsTr(" now. Stop that first to sync through an account.")
    }

    // Signed in: who, when it last synced, and the actions.
    Rectangle {
        visible: Account.signedIn && Sync.kind === ""
            && ((pane.view === "scancore" && !Account.customSite) || (pane.view === "server" && Account.customSite))
        width: parent.width
        height: profile.height + 36
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        Flow {
            id: profile
            x: 18
            y: 18
            width: parent.width - 36
            spacing: 16

            Row {
                spacing: 14
                width: Theme.compact ? profile.width : Math.max(0, profile.width - actions.width - 16)
                Rectangle {
                    width: 52
                    height: 52
                    radius: 26
                    color: Theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: pane.name.slice(0, 1).toUpperCase()
                        color: Theme.accentInk
                        font.family: Theme.fontDisplay
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                    Cover {
                        anchors.fill: parent
                        visible: pane.avatarUrl.length > 0 && ready
                        source: pane.avatarUrl
                        radius: 26
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    width: parent.width - 66
                    Text {
                        width: parent.width
                        text: pane.name
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textLg
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Row {
                        spacing: 6
                        Spinner { visible: Account.syncing; size: 13; anchors.verticalCenter: parent.verticalCenter }
                        Icon { visible: !Account.syncing; name: "cloud-check"; size: 14; color: Theme.success; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: pane.syncLine + " · " + pane.host
                            color: Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textSm
                        }
                    }
                }
            }
            Row {
                id: actions
                spacing: 8
                ScButton {
                    variant: "primary"
                    text: qsTr("Sync now")
                    iconName: "refresh-cw"
                    loading: Account.syncing
                    enabled: !Account.syncing
                    onClicked: pane.report(Account.sync())
                }
                ScButton {
                    text: qsTr("Sign out")
                    iconName: "log-out"
                    variant: "ghost"
                    loading: pane.signingOut !== null
                    enabled: pane.signingOut === null
                    onClicked: signOutConfirm.open()
                }
            }
        }
    }

    // Signing out takes the account's library, progress and lists off this
    // device (they stay in the account), after a last sync so nothing
    // unsent is lost; or keeps them here.
    ConfirmDialog {
        id: signOutConfirm
        heading: qsTr("Sign out of ") + pane.host + "?"
        message: optionOn ? qsTr("Your library, reading progress and lists stay on this device and stop syncing. Signing in to another account later asks what to do with them.")
                          : qsTr("Your library, reading progress and lists stay in your account and leave this device. Anything not synced yet is sent first.")
        option: qsTr("Keep them on this device")
        onOpened: optionOn = false
        confirmText: qsTr("Sign out")
        destructive: true
        onConfirmed: {
            if (optionOn) { Account.signOut(true); return }
            pane.lastSync(Account.syncing)
        }
    }
    // A sync already running may have started before the latest changes:
    // it ends first, then one more sends everything.
    function lastSync(again) {
        signingOut = Account.sync()
        signingOut.finished.connect(() => {
            if (again) { pane.lastSync(false); return }
            pane.signingOut = null
            Account.signOut(false)
        })
        signingOut.failed.connect((error) => { pane.signingOut = null; pane.unsentError = error; unsentConfirm.open() })
    }
    property var signingOut: null
    property string unsentError: ""
    ConfirmDialog {
        id: unsentConfirm
        heading: qsTr("Some changes have not reached your account")
        message: pane.unsentError + "\n\n" + qsTr("Signing out now removes them from this device too.")
        confirmText: qsTr("Sign out anyway")
        destructive: true
        onConfirmed: Account.signOut(false)
    }

    // Your own website: its address first.
    Rectangle {
        visible: pane.view === "server" && !Account.signedIn && Sync.kind === ""
        width: parent.width
        height: serverColumn.height + 40
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        Column {
            id: serverColumn
            x: 20
            y: 20
            width: parent.width - 40
            spacing: 14
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("The SCANCORE sync server keeps your account, library, progress and activity on a computer, NAS or server of your own. ")
                    + qsTr("Start it with one Docker command (see How to set it up), then enter its address and sign in here.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            FormField {
                id: siteField
                width: Math.min(parent.width, 420)
                label: qsTr("Server address")
                placeholder: "http://192.168.1.20:8787"
                text: Account.customSite ? Account.site : ""
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                problem: ""
                onAccepted: useSite.clicked()
            }
            Row {
                spacing: 8
                ScButton {
                    id: useSite
                    variant: "primary"
                    text: Account.customSite ? qsTr("Change server") : qsTr("Use this server")
                    enabled: siteField.text.trim().length > 0
                    onClicked: {
                        const problem = Account.setSite(siteField.text)
                        siteField.problem = problem
                        if (!problem) Router.toast(qsTr("Sign in to ") + pane.host, "success")
                    }
                }
                ScButton {
                    visible: Account.customSite
                    text: qsTr("Use scancore.onl")
                    variant: "ghost"
                    onClicked: { Account.setSite(""); pane.view = "scancore" }
                }
                ScButton {
                    text: qsTr("How to set it up")
                    iconName: "external-link"
                    variant: "ghost"
                    onClicked: App.openUrl("https://github.com/medium-gh/scancore-web#readme")
                }
            }
        }
    }

    // Signed out: what an account is for, then the form.
    Rectangle {
        visible: !Account.signedIn && Sync.kind === ""
            && ((pane.view === "scancore" && !Account.customSite) || (pane.view === "server" && Account.customSite))
        width: parent.width
        height: signIn.height + 40
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        Column {
            id: signIn
            x: 20
            y: 20
            width: parent.width - 40
            spacing: 18
            Row {
                spacing: 14
                width: parent.width
                Rectangle {
                    width: 44
                    height: 44
                    radius: 13
                    color: Theme.alpha(Theme.accent, 0.12)
                    Icon { anchors.centerIn: parent; name: Account.customSite ? "server" : "cloud"; size: 20; color: Theme.accent }
                }
                Column {
                    width: parent.width - 58
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        width: parent.width
                        text: qsTr("Sync with ") + pane.host
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textLg
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("Keep your library, progress and activity the same on every device")
                            + (Account.customSite ? "." : qsTr(" and on the website. Everything works without an account."))
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                    }
                }
            }
            AuthForm {
                width: Math.min(parent.width, 420)
                onDone: Router.toast(qsTr("Signed in"), "success")
            }
        }
    }

    // ------------------------------------------------------------ files

    Notice {
        visible: (pane.view === "webdav" || pane.view === "folder") && Sync.kind === "" && Account.signedIn
        text: qsTr("Syncing here pauses your ") + pane.host + qsTr(" account sync until you stop.")
    }

    // WebDAV or a folder in use: where, the devices, and the actions.
    Rectangle {
        visible: Sync.kind !== "" && pane.view === Sync.kind
        width: parent.width
        height: fileStatus.height + 36
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        Column {
            id: fileStatus
            x: 18
            y: 18
            width: parent.width - 36
            spacing: 16
            Flow {
                width: parent.width
                spacing: 16
                Row {
                    spacing: 14
                    width: Theme.compact ? parent.width : Math.max(0, parent.width - fileActions.width - 16)
                    Rectangle {
                        width: 44
                        height: 44
                        radius: 13
                        color: Theme.alpha(Theme.accent, 0.12)
                        Icon { anchors.centerIn: parent; name: Sync.kind === "webdav" ? "globe" : "folder-open"; size: 20; color: Theme.accent }
                    }
                    Column {
                        width: parent.width - 58
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            width: parent.width
                            text: Sync.location
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textMd
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        Text {
                            width: parent.width
                            text: Sync.error ? Sync.error
                                : Sync.syncing ? qsTr("Syncing…")
                                : Sync.lastSync ? qsTr("Synced ") + App.relativeTime(Number(Sync.lastSync)) : qsTr("Not synced yet")
                            color: Sync.error ? Theme.danger : Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textSm
                            elide: Text.ElideRight
                        }
                    }
                }
                Row {
                    id: fileActions
                    spacing: 8
                    ScButton {
                        variant: "primary"
                        text: qsTr("Sync now")
                        iconName: "refresh-cw"
                        loading: Sync.syncing
                        enabled: !Sync.syncing
                        onClicked: {
                            const task = Sync.sync()
                            task.finished.connect((r) => Router.toast(r.merged > 0 ? qsTr("Synced · ") + r.merged + qsTr(" changes from other devices") : qsTr("Everything is up to date"), "success"))
                            task.failed.connect((e) => Router.toast(e, "error"))
                        }
                    }
                    ScButton { text: qsTr("Stop syncing"); variant: "ghost"; onClicked: stopConfirm.open() }
                }
            }
            DeviceList { width: parent.width; devices: Sync.devices }
        }
    }

    ConfirmDialog {
        id: stopConfirm
        heading: qsTr("Stop syncing?")
        message: qsTr("This device stops reading and writing ") + (Sync.kind === "webdav" ? qsTr("the WebDAV folder") : qsTr("the folder"))
            + qsTr(". What is there stays, so other devices keep their copy.")
        confirmText: qsTr("Stop syncing")
        onConfirmed: Sync.stop()
    }

    // WebDAV: the address and sign-in.
    Rectangle {
        visible: pane.view === "webdav" && Sync.kind !== "webdav"
        width: parent.width
        height: davColumn.height + 40
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        Column {
            id: davColumn
            x: 20
            y: 20
            width: parent.width - 40
            spacing: 14
            property var task: null
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Keep a SCANCORE folder on any WebDAV server, such as Nextcloud, ownCloud, Synology or Koofr. ")
                    + qsTr("Every device you connect to it syncs with the others, no account needed.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            FormField {
                id: davUrl
                width: Math.min(parent.width, 460)
                label: qsTr("WebDAV address")
                placeholder: "https://cloud.example.com/remote.php/dav/files/you/"
                hint: qsTr("Nextcloud: Files → Settings → WebDAV shows yours.")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            }
            FormField {
                id: davUser
                width: Math.min(parent.width, 460)
                label: qsTr("User name")
                inputMethodHints: Qt.ImhNoAutoUppercase
            }
            FormField {
                id: davPassword
                width: Math.min(parent.width, 460)
                label: qsTr("Password")
                echoMode: TextInput.Password
                hint: qsTr("Kept on this device. An app password, if your server offers them, is best.")
                onAccepted: davConnect.clicked()
            }
            Text {
                visible: davColumn.task !== null && !davColumn.task.loading && !davColumn.task.ok
                width: parent.width
                wrapMode: Text.Wrap
                text: davColumn.task ? davColumn.task.error : ""
                color: Theme.danger
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            ScButton {
                id: davConnect
                variant: "primary"
                text: qsTr("Connect")
                iconName: "link"
                loading: davColumn.task !== null && davColumn.task.loading
                enabled: davUrl.text.trim().length > 0 && !(davColumn.task !== null && davColumn.task.loading)
                onClicked: {
                    davColumn.task = Sync.useWebDav(davUrl.text, davUser.text, davPassword.text)
                    davColumn.task.finished.connect(() => {
                        davPassword.text = ""
                        Router.toast(qsTr("Syncing with WebDAV"), "success")
                    })
                }
            }
        }
    }

    // A folder: pick it.
    Rectangle {
        visible: pane.view === "folder" && Sync.kind !== "folder"
        width: parent.width
        height: folderColumn.height + 40
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        Column {
            id: folderColumn
            x: 20
            y: 20
            width: parent.width - 40
            spacing: 14
            property var task: null
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Choose a folder that Syncthing, Dropbox, OneDrive, iCloud Drive or Nextcloud keeps the same on your computers. ")
                    + qsTr("SCANCORE keeps a small file for each device in it and reads the others'.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            Text {
                visible: folderColumn.task !== null && !folderColumn.task.loading && !folderColumn.task.ok
                width: parent.width
                wrapMode: Text.Wrap
                text: folderColumn.task ? folderColumn.task.error : ""
                color: Theme.danger
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            ScButton {
                variant: "primary"
                text: qsTr("Choose folder")
                iconName: "folder-open"
                loading: folderColumn.task !== null && folderColumn.task.loading
                onClicked: folderDialog.open()
            }
        }
    }
    FolderDialog {
        id: folderDialog
        onAccepted: {
            folderColumn.task = Sync.useFolder(selectedFolder)
            folderColumn.task.finished.connect(() => Router.toast(qsTr("Syncing with the folder"), "success"))
        }
    }

    // Devices that synced through the account's website.
    DeviceList {
        visible: Account.signedIn && Sync.kind === "" && devices.length > 1
            && ((pane.view === "scancore" && !Account.customSite) || (pane.view === "server" && Account.customSite))
        width: parent.width
        devices: Sync.devices
    }

    // A line of explanation in a soft box.
    component Notice: Rectangle {
        property alias text: noticeText.text
        width: parent ? parent.width : 400
        height: noticeText.implicitHeight + 24
        radius: Theme.radiusMd
        color: Theme.alpha(Theme.warning, 0.1)
        border.width: 1
        border.color: Theme.alpha(Theme.warning, 0.25)
        Row {
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Icon { name: "info"; size: 16; color: Theme.warning; anchors.verticalCenter: parent.verticalCenter }
            Text {
                id: noticeText
                width: parent.parent.width - 54
                wrapMode: Text.Wrap
                color: Theme.text
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
        }
    }

    // The devices that sync together, this one first.
    component DeviceList: Column {
        property var devices: []
        spacing: 8
        Text {
            text: qsTr("Devices")
            color: Theme.textFaint
            font.family: Theme.fontSans
            font.pixelSize: Theme.textXs
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.1
        }
        Repeater {
            model: devices
            Row {
                id: deviceRow
                required property var modelData
                // Open right now, so live progress reaches it in seconds.
                readonly property bool live: !modelData.self && Live.devices.some(d => d.id === modelData.id)
                spacing: 10
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: modelData.platform === "android" ? "smartphone" : "monitor"
                    size: 16
                    color: Theme.textMuted
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name + (modelData.self ? qsTr(" (this device)") : "")
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: deviceRow.live || (!modelData.self && Number(modelData.updatedAt) > 0)
                    text: deviceRow.live ? qsTr("· open now") : qsTr("· updated ") + App.relativeTime(Number(modelData.updatedAt))
                    color: deviceRow.live ? Theme.success : Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
            }
        }
    }
}
