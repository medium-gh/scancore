import QtQuick
import QtQuick.Controls.Basic
import Scancore

SettingsPane {
    id: pane
    title: qsTr("Privacy")
    subtitle: qsTr("Incognito mode and the app lock")
    iconName: "lock"

    SettingsGroup {
        title: qsTr("Incognito")
        note: qsTr("Incognito reading and watching leaves nothing behind: no history, no place kept, no reading time, no tracker updates and no Discord status. Saving series and downloads still work.")
        SettingRow {
            iconName: "venetian-mask"
            title: qsTr("Incognito mode")
            description: qsTr("A badge shows while it is on.")
            Toggle { checked: App.settings.incognito === true; onToggled: Router.setIncognito(checked) }
        }
    }

    SettingsGroup {
        title: qsTr("App lock")
        note: Lock.enabled ? "" : qsTr("Ask for a PIN when SCANCORE starts and after it has been away for a while.")
            + (App.platform === "android" ? qsTr(" Fingerprint or face can unlock it too.") : "")
        SettingRow {
            iconName: Lock.enabled ? "lock" : "lock-open"
            title: qsTr("Lock with a PIN")
            description: Lock.enabled ? qsTr("On · %1 digits").arg(Lock.length) : qsTr("Off")
            Row {
                spacing: 8
                ScButton {
                    text: Lock.enabled ? qsTr("Change PIN") : qsTr("Set a PIN")
                    variant: Lock.enabled ? "secondary" : "primary"
                    onClicked: pinDialog.start(Lock.enabled ? "current" : "new", "change")
                }
                ScButton {
                    visible: Lock.enabled
                    text: qsTr("Turn off")
                    variant: "ghost"
                    onClicked: pinDialog.start("current", "remove")
                }
            }
        }
        SettingRow {
            visible: Lock.enabled
            iconName: "clock"
            title: qsTr("Lock again")
            description: qsTr("After SCANCORE has been in the background this long.")
            Select {
                title: qsTr("Lock again")
                model: [{ value: -1, label: qsTr("Only when it starts") }, { value: 0, label: qsTr("Right away") },
                        { value: 1, label: qsTr("After 1 minute") }, { value: 5, label: qsTr("After 5 minutes") },
                        { value: 15, label: qsTr("After 15 minutes") }]
                value: App.settings.lockAfter === undefined ? 1 : App.settings.lockAfter
                onPicked: (v) => App.setSetting("lockAfter", v)
            }
        }
        SettingRow {
            visible: Lock.enabled && Lock.biometricAvailable
            iconName: "fingerprint"
            title: qsTr("Unlock with fingerprint or face")
            description: qsTr("The phone's screen lock works there too, if you forget the PIN.")
            Toggle { checked: App.settings.lockBiometric !== false; onToggled: App.setSetting("lockBiometric", checked) }
        }
        SettingRow {
            visible: Lock.enabled
            iconName: "lock"
            title: qsTr("Lock now")
            clickable: true
            onClicked: Lock.lockNow()
        }
    }

    // Asks for the current PIN (to change or remove it), then a new one twice.
    Dialog {
        id: pinDialog
        property string step: "new"      // "current", "new" or "repeat"
        property string purpose: "change" // "change" or "remove"
        property string first: ""
        property string message: ""
        property var task: null

        function start(firstStep, why) {
            step = firstStep
            purpose = why
            first = ""
            message = ""
            open()
        }
        function entered(pin) {
            if (step === "current") {
                task = Lock.check(pin)
            } else if (step === "new") {
                first = pin
                step = "repeat"
                message = ""
                pad.clear()
            } else if (pin !== first) {
                message = qsTr("The PINs do not match. Start again.")
                step = "new"
                first = ""
                pad.shake()
            } else {
                task = Lock.setPin(pin)
            }
        }
        Connections {
            target: pinDialog.task
            function onFinished(ok) {
                const step = pinDialog.step
                pinDialog.task = null
                if (step === "current") {
                    if (!ok) { pinDialog.message = qsTr("Wrong PIN"); pad.shake(); return }
                    if (pinDialog.purpose === "remove") {
                        Lock.removePin()
                        Router.toast(qsTr("App lock is off"), "info")
                        pinDialog.close()
                        return
                    }
                    pinDialog.step = "new"
                    pinDialog.message = ""
                    pad.clear()
                } else if (ok) {
                    Router.toast(qsTr("App lock is on"), "success")
                    pinDialog.close()
                }
            }
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(380, (parent ? parent.width : 380) - 32)
        modal: true
        focus: true
        padding: 24
        onOpened: pad.forceActiveFocus()
        Overlay.modal: Rectangle { color: Theme.overlay }
        background: SurfacePanel {
            radius: Theme.radiusXl
            fill: Theme.surface
            border.width: 1
            border.color: Theme.borderStrong
        }
        contentItem: Column {
            spacing: 22
            Column {
                width: parent.width
                spacing: 6
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: pinDialog.step === "current" ? qsTr("Enter your current PIN")
                        : pinDialog.step === "new" ? qsTr("Choose a PIN") : qsTr("Enter it again")
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: pinDialog.message || (pinDialog.step === "new" ? qsTr("4 to 8 digits, then the tick.") : " ")
                    color: pinDialog.message ? Theme.danger : Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
            }
            PinPad {
                id: pad
                anchors.horizontalCenter: parent.horizontalCenter
                length: pinDialog.step === "current" ? Lock.length : pinDialog.step === "repeat" ? pinDialog.first.length : 0
                busy: pinDialog.task !== null
                onEntered: (pin) => pinDialog.entered(pin)
            }
            ScButton {
                anchors.horizontalCenter: parent.horizontalCenter
                variant: "ghost"
                text: qsTr("Cancel")
                onClicked: pinDialog.close()
            }
        }
    }
}
