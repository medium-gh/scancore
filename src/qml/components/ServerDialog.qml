import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Adding or changing one of the reader's own servers (Komga, Kavita,
// Suwayomi, any OPDS catalogue, or Jellyfin for video): its kind, address
// and login, saved and tried at once.
Dialog {
    id: dialog
    property var server: ({})
    property string kind: "komga"
    property var testTask: null
    property string outcome: ""

    function start(existing) {
        server = existing || {}
        kind = server.kind || "komga"
        nameField.text = server.label || ""
        urlField.text = server.url || ""
        userField.text = server.username || ""
        passwordField.text = ""
        keyField.text = ""
        outcome = ""
        testTask = null
        open()
    }
    function save() {
        const id = Comics.saveServer({ id: server.id || "", kind: kind, label: nameField.text, url: urlField.text,
                                       username: userField.text, password: passwordField.text, apiKey: keyField.text })
        server = Object.assign({}, server, { id: id })
        outcome = qsTr("Trying it…")
        testTask = Comics.testServer(id)
    }
    Connections {
        target: dialog.testTask
        function onFinished(r) {
            if (r.video)
                dialog.outcome = r.count === 1 ? qsTr("Connected: 1 show or film found.")
                    : r.more ? qsTr("Connected: more than %1 shows and films found.").arg(r.count)
                    : qsTr("Connected: %1 shows and films found.").arg(r.count)
            else
                dialog.outcome = r.count === 1 ? qsTr("Connected: 1 series found.") : qsTr("Connected: %1 series found.").arg(r.count)
        }
        function onFailed(error) { dialog.outcome = error }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, (parent ? parent.width : 460) - 32)
    modal: true
    focus: true
    padding: 24
    Overlay.modal: Rectangle { color: Theme.overlay }
    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    contentItem: Column {
        spacing: 14
        Text {
            width: parent.width
            text: dialog.server.id ? qsTr("Your server") : qsTr("Add your server")
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.textXl
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            text: dialog.kind === "opds"
                  ? qsTr("Any OPDS catalogue (Calibre-web, Kavita, Komga, Ubooquity…): paste its OPDS address. Comics open under Comics, EPUB books under Novels. The login stays on this device.")
                  : dialog.kind === "jellyfin"
                  ? qsTr("Watch the shows and films on your own Jellyfin server under Anime. Where you stop is kept on the server too, so its other apps carry on from there. The login stays on this device.")
                  : qsTr("Read the comics on your own Komga, Kavita or Suwayomi server. The login stays on this device.")
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
        // Chips, not a segmented row: five kinds do not fit one at 360 px.
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: [{ value: "komga", label: "Komga" }, { value: "kavita", label: "Kavita" }, { value: "suwayomi", label: "Suwayomi" },
                        { value: "opds", label: "OPDS" }, { value: "jellyfin", label: "Jellyfin" }]
                Chip {
                    required property var modelData
                    text: modelData.label
                    selected: dialog.kind === modelData.value
                    onClicked: dialog.kind = modelData.value
                }
            }
        }
        InlineField { id: nameField; width: parent.width; placeholderText: qsTr("Name (optional)") }
        InlineField {
            id: urlField
            width: parent.width
            placeholderText: dialog.kind === "suwayomi" ? "http://192.168.1.20:4567" : dialog.kind === "kavita" ? "http://192.168.1.20:5000"
                           : dialog.kind === "opds" ? "http://192.168.1.20:8083/opds" : dialog.kind === "jellyfin" ? "http://192.168.1.20:8096"
                           : "http://192.168.1.20:25600"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
        }
        InlineField {
            id: userField
            width: parent.width
            placeholderText: dialog.kind === "komga" ? qsTr("Email") : qsTr("User name") + (dialog.kind === "suwayomi" || dialog.kind === "opds" ? qsTr(" (if it asks for one)") : "")
            inputMethodHints: Qt.ImhNoAutoUppercase
        }
        InlineField {
            id: passwordField
            width: parent.width
            echoMode: TextInput.Password
            placeholderText: dialog.server.hasPassword ? qsTr("Password (saved; type to change)") : qsTr("Password")
        }
        InlineField {
            id: keyField
            visible: dialog.kind === "kavita"
            width: parent.width
            echoMode: TextInput.Password
            placeholderText: dialog.server.hasKey ? qsTr("API key (saved; type to change)") : qsTr("Or an API key, instead of the login")
        }
        Text {
            visible: dialog.outcome.length > 0
            width: parent.width
            text: dialog.outcome
            color: dialog.testTask && dialog.testTask.ok ? Theme.success : dialog.testTask && dialog.testTask.loading ? Theme.textMuted : Theme.danger
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
        Row {
            anchors.right: parent.right
            spacing: 8
            ScButton { text: qsTr("Close"); variant: "ghost"; onClicked: dialog.close() }
            ScButton {
                text: qsTr("Save and try")
                variant: "primary"
                enabled: urlField.text.trim().length > 0 && !(dialog.testTask && dialog.testTask.loading)
                onClicked: dialog.save()
            }
        }
    }
}
