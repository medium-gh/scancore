import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Connect and Disconnect for one tracker, with the step each one needs:
// pasting the token AniList or Bangumi shows, a user name and password
// (Kitsu, MangaUpdates), a sign-in page (MyAnimeList, Comick, Shikimori) or
// a code to enter (Simkl). Used in Settings → Connections and on the
// first-run tracker step.
Column {
    id: root
    property string service: "anilist"
    readonly property var info: Trackers.services.find(s => s.key === service) || ({ key: service, label: service, auth: "oauth" })
    // Signed out by the service (its token was revoked or ran out).
    readonly property bool expired: info.expired === true
    readonly property bool connected: info.connected === true
    readonly property string label: info.label
    readonly property string auth: info.auth
    // The token or password step is showing.
    property bool entering: false
    // Android: AniList's sign-in screen is open; its last address carries
    // the token.
    property bool awaitingAniList: false
    Connections {
        target: App
        enabled: root.awaitingAniList
        function onSiteChecked(address) {
            root.awaitingAniList = false
            if (String(address).indexOf("access_token=") < 0) return
            root.track(Trackers.anilistUseToken(address))
        }
    }
    property var task: null
    readonly property bool waiting: task !== null && task.loading
    // What is happening, for the row's description; empty when idle.
    readonly property string status: expired ? qsTr("%1 signed SCANCORE out, so progress is not reaching it. Connect again.").arg(label)
        : connected ? qsTr("Connected as ") + info.name
        : auth === "password" && entering ? qsTr("Your password goes only to %1.").arg(label)
        : auth === "token" && entering ? qsTr("Sign in on the page that opened, then paste the token it shows.")
        : waiting && auth === "pin" ? (Trackers.pin.code ? qsTr("Enter %1 on the page that opened.").arg(Trackers.pin.code) : qsTr("Asking Simkl for a code…"))
        : waiting && auth === "oauth" ? (service === "comick" ? qsTr("Waiting for you to allow SCANCORE on the page that opened…")
                                                              : qsTr("Waiting for you to sign in on the page that opened…"))
        : waiting ? qsTr("Signing in…")
        : ""

    spacing: 10

    function track(t) {
        task = t
        const name = label
        t.finished.connect((r) => {
            root.entering = false
            userField.text = ""
            secretField.text = ""
            Router.toast(qsTr("Connected to %1 as %2").arg(name).arg(r.name), "success")
        })
        t.failed.connect((error) => { if (error !== "Cancelled") Router.toast(error, "error") })
    }

    function connect() {
        if (waiting) {
            task.cancel()
            return
        }
        if (auth === "password") {
            entering = true
            userField.forceActiveFocus()
            return
        }
        if (service === "anilist") {
            // Inside SCANCORE, the token is taken by itself; otherwise in the
            // browser, and pasted.
            if (App.platform === "android") {
                awaitingAniList = true
                if (App.openSiteCheck(Trackers.anilistAuthorizeUrl(), qsTr("Sign in to AniList and allow SCANCORE; this closes by itself."), qsTr("Cancel")))
                    return
                awaitingAniList = false
            } else if (App.embeddedBrowser) {
                Router.openAniListSignIn()
                return
            }
        }
        if (auth === "token") {
            App.openUrl(Trackers.tokenPage(service))
            entering = true
            return
        }
        track(Trackers.signIn(service))
    }

    Row {
        anchors.right: parent.right
        spacing: 8
        ScButton {
            visible: !root.connected && !(root.entering && root.auth === "password")
            variant: root.entering ? "ghost" : "primary"
            text: root.entering ? qsTr("Open again") : root.waiting ? qsTr("Cancel") : root.expired ? qsTr("Connect again") : qsTr("Connect")
            onClicked: root.connect()
        }
        ScButton {
            visible: root.connected
            text: qsTr("Disconnect")
            variant: "ghost"
            onClicked: Trackers.signOut(root.service)
        }
    }

    // The token the service shows, or a user name and password: one line on
    // wide windows, stacked on phones.
    Grid {
        anchors.right: parent.right
        visible: root.entering && !root.connected
        columns: Theme.compact ? 1 : root.auth === "password" ? 3 : 2
        horizontalItemAlignment: Grid.AlignRight
        verticalItemAlignment: Grid.AlignVCenter
        spacing: 8
        InlineField {
            id: userField
            visible: root.auth === "password"
            width: Theme.compact ? 200 : 180
            placeholderText: root.service === "kitsu" ? qsTr("Email") : qsTr("User name")
            onAccepted: secretField.forceActiveFocus()
        }
        InlineField {
            id: secretField
            width: Theme.compact ? 200 : 180
            placeholderText: root.auth === "password" ? qsTr("Password") : qsTr("Paste the token")
            echoMode: TextInput.Password
            onAccepted: if (submit.enabled) submit.clicked()
        }
        ScButton {
            id: submit
            variant: "primary"
            text: root.auth === "password" ? qsTr("Sign in") : qsTr("Use token")
            loading: root.waiting
            enabled: secretField.text.length > 0 && (root.auth !== "password" || userField.text.trim().length > 0)
            onClicked: root.track(root.auth === "password" ? Trackers.passwordSignIn(root.service, userField.text, secretField.text)
                                                          : Trackers.useToken(root.service, secretField.text))
        }
    }
}
