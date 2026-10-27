import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Sign in, create an account or recover a forgotten password on the
// scancore.onl account (or the site set in Settings → Sync). Used by the
// welcome flow and Settings. Every step stays in place: the parts that come
// and go ease their height and fade (Reveal), so the form never jumps.
// Emits done() after a successful sign in or sign up.
Column {
    id: form
    property string mode: "signin"           // "signin" | "signup" | "forgot" | "sent" | "newpass" | "done"
    property var task: null
    property string error: ""
    readonly property bool busy: task !== null && task.loading
    readonly property bool account: mode === "signin" || mode === "signup"
    readonly property bool recovering: !account
    // The one-time token a correct reset code was swapped for.
    property string resetToken: ""
    property bool codeWrong: false
    // The password being chosen: a new account's, or the reset one.
    readonly property string chosen: mode === "newpass" ? newPassword.text : password.text
    readonly property var checks: Account.passwordChecks(chosen)
    readonly property int passed: checks.filter(c => c.passed).length
    readonly property bool passwordOk: passed === checks.length
    // 0 empty, 1 weak, 2 medium, 3 strong (as on the website).
    readonly property int score: chosen.length === 0 ? 0 : passed <= 2 ? 1 : passed < checks.length ? 2 : 3
    readonly property color scoreColor: score === 1 ? Theme.danger : score === 2 ? Theme.warning : Theme.success
    readonly property bool emailOk: /^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(email.text.trim())
    readonly property string siteName: String(Account.site).replace(/^https?:\/\//, "").replace(/\/+$/, "")
    // The site's sign-up rules (an access code on scancore.onl, or closed)
    // and whether it resets forgotten passwords by email.
    property var signupRules: ({ open: true, accessCode: false, codeLength: 6, help: "", recovery: !Account.customSite })
    property var rulesTask: null
    readonly property bool recovery: signupRules.recovery === true
    readonly property bool needsCode: mode === "signup" && signupRules.accessCode === true
    readonly property bool signupClosed: mode === "signup" && signupRules.open === false
    readonly property bool canSubmit: mode === "done" ? true
        : mode === "sent" ? !busy && resetCode.complete
        : mode === "newpass" ? !busy && passwordOk && resetToken.length > 0
        : !busy && emailOk && (mode === "forgot" ? recovery
        : mode === "signin" ? password.text.length > 0
        : !signupClosed && passwordOk && username.text.trim().length >= 3 && !Account.usernameProblem(username.text)
          && (!needsCode || code.complete))
    // Seconds before "Send again" works after a reset email.
    property int cooldown: 0
    signal done()

    function loadRules() {
        rulesTask = Account.signupOptions()
        rulesTask.finished.connect((rules) => form.signupRules = rules)
    }
    function go(next) {
        mode = next
        error = ""
        stepIn.restart()
    }
    Component.onCompleted: loadRules()
    onModeChanged: if (mode === "signup") loadRules()
    Connections {
        target: Account
        // The site can change in Settings → Sync while the form is open.
        function onChanged() { if (!Account.signedIn) form.loadRules() }
    }
    Timer {
        running: form.cooldown > 0
        interval: 1000
        repeat: true
        onTriggered: form.cooldown -= 1
    }

    spacing: 0
    width: 380

    function sendReset() {
        error = ""
        task = Account.requestPasswordReset(email.text)
        task.finished.connect(() => {
            form.cooldown = 30
            form.codeWrong = false
            resetCode.clear()
            if (form.mode !== "sent") form.go("sent")
            resetCode.focusInput()
        })
        task.failed.connect((message) => form.error = message)
    }

    // The code from the email, typed here: swapped for a one-time token,
    // then the same new-password step the emailed link opens on the site.
    function checkCode(code) {
        if (busy) return
        error = ""
        codeWrong = false
        task = Account.exchangeResetCode(email.text, code)
        task.finished.connect((token) => {
            form.resetToken = token
            form.go("newpass")
            newPassword.focusInput()
        })
        task.failed.connect((message) => {
            form.error = message
            form.codeWrong = true
            resetCode.clear()
            resetCode.focusInput()
        })
    }

    function saveNewPassword() {
        error = ""
        task = Account.completePasswordReset(resetToken, newPassword.text)
        task.finished.connect(() => {
            form.resetToken = ""
            newPassword.text = ""
            password.text = ""
            form.go("done")
        })
        task.failed.connect((message) => {
            // A spent or expired reset: start again from the email step.
            if (/invalid or expired/i.test(message)) {
                form.resetToken = ""
                form.go("forgot")
                form.error = qsTr("That reset has expired. Ask for a new email.")
            } else {
                form.error = message
            }
        })
    }

    function submit() {
        if (!canSubmit) return
        if (mode === "done") return go("signin")
        if (mode === "sent") return checkCode(resetCode.text)
        if (mode === "newpass") return saveNewPassword()
        if (mode === "forgot") return sendReset()
        error = ""
        task = mode === "signin" ? Account.signIn(email.text, password.text)
                                 : Account.signUp(email.text, username.text, password.text, needsCode ? code.text : "")
        task.finished.connect(() => {
            password.text = ""
            form.done()
        })
        task.failed.connect((message) => form.error = message)
    }

    // A quiet note with an info mark.
    component Note: Rectangle {
        property alias text: noteText.text
        width: parent ? parent.width : 380
        height: noteText.implicitHeight + 22
        radius: Theme.radiusMd
        color: Theme.surface2
        border.width: 1
        border.color: Theme.border
        Icon {
            x: 12
            y: 12
            name: "info"
            size: 15
            color: Theme.textMuted
        }
        Text {
            id: noteText
            x: 36
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 48
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
        }
    }

    // A new step slides up into place.
    ParallelAnimation {
        id: stepIn
        NumberAnimation { target: form; property: "opacity"; from: 0.25; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
        NumberAnimation { target: stepShift; property: "y"; from: 8; to: 0; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized }
    }
    transform: Translate { id: stepShift }

    // ------------------------------------------------------------ recovery header

    Reveal {
        open: form.recovering
        Column {
            width: parent.width
            spacing: 10
            Rectangle {
                width: 44
                height: 44
                radius: Theme.radiusLg
                border.width: 1
                color: form.mode === "done" ? Theme.alpha(Theme.success, 0.12) : Theme.surface2
                border.color: form.mode === "done" ? Theme.alpha(Theme.success, 0.3) : Theme.border
                Icon {
                    anchors.centerIn: parent
                    name: form.mode === "sent" ? "mail" : form.mode === "done" ? "circle-check" : "key-round"
                    size: 20
                    color: form.mode === "done" ? Theme.success : Theme.text
                }
            }
            Text {
                width: parent.width
                topPadding: 4
                text: form.mode === "sent" ? qsTr("Check your email")
                    : form.mode === "newpass" ? qsTr("Choose a new password")
                    : form.mode === "done" ? qsTr("Password updated") : qsTr("Reset your password")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textXl
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: form.mode === "sent"
                    ? qsTr("If an account uses %1, we sent it a 6-digit code. Enter it here, or open the link in the same email on any device.")
                        .arg("<b>" + email.text.trim().replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b>")
                    : form.mode === "newpass" ? qsTr("Use at least 8 characters. Every device signs out once it's saved.")
                    : form.mode === "done" ? qsTr("Your new password is set, and every device was signed out. Sign in with it now.")
                    : qsTr("Enter the email on your account. We'll send a code to enter here, and a link that works on any device.")
                textFormat: Text.StyledText
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                lineHeight: 1.2
                wrapMode: Text.Wrap
            }
        }
    }

    // ------------------------------------------------------------ account

    Reveal {
        open: form.account
        Segmented {
            options: [{ value: "signin", label: qsTr("Sign in") }, { value: "signup", label: qsTr("Create account") }]
            value: form.mode
            onPicked: (v) => {
                form.mode = v
                form.error = ""
            }
        }
    }

    Reveal {
        open: form.account || form.mode === "forgot"
        gap: 18
        FormField {
            id: email
            label: qsTr("Email")
            placeholder: "you@example.com"
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            problem: text.length > 3 && !form.emailOk ? qsTr("Enter a valid email address") : ""
            onAccepted: form.mode === "forgot" ? form.submit()
                : form.mode === "signup" ? username.focusInput() : password.focusInput()
        }
    }

    Reveal {
        open: form.mode === "signup"
        gap: 16
        FormField {
            id: username
            label: qsTr("Username")
            placeholder: qsTr("Shown on comments and reviews")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            problem: Account.usernameProblem(text)
            hint: qsTr("3–32 letters, numbers or underscores")
            onAccepted: password.focusInput()
        }
    }

    Reveal {
        open: form.account
        gap: 16
        FormField {
            id: password
            label: qsTr("Password")
            // The reset step; on a sync server without email it says who to ask.
            action: form.mode === "signin" ? qsTr("Forgot password?") : ""
            onActionClicked: form.go("forgot")
            placeholder: form.mode === "signin" ? qsTr("Your password") : qsTr("At least 8 characters")
            echoMode: TextInput.Password
            onAccepted: form.needsCode && !code.complete ? code.focusInput() : form.submit()
        }
    }

    Reveal {
        open: form.mode === "sent"
        gap: 20
        Column {
            width: parent.width
            spacing: 8
            Text {
                text: qsTr("Code from the email")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                font.weight: Font.Medium
            }
            CodeBoxes {
                id: resetCode
                digits: true
                invalid: form.codeWrong
                label: qsTr("6-digit code from the email")
                enabled: !form.busy
                onTextChanged: if (text.length > 0) form.codeWrong = false
                onCompleted: (code) => form.checkCode(code)
                onAccepted: form.submit()
            }
        }
    }

    Reveal {
        open: form.mode === "newpass"
        gap: 18
        FormField {
            id: newPassword
            label: qsTr("New password")
            placeholder: qsTr("At least 8 characters")
            echoMode: TextInput.Password
            onAccepted: form.submit()
        }
    }

    // Strength of a new password, and what is still missing.
    Reveal {
        open: (form.mode === "signup" && password.text.length > 0) || form.mode === "newpass"
        gap: 12
        Column {
            width: parent.width
            spacing: 10
            Item {
                width: parent.width
                height: strengthLabel.implicitHeight
                Row {
                    id: bars
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - strengthLabel.implicitWidth - 12
                    spacing: 4
                    Repeater {
                        model: 3
                        Rectangle {
                            required property int index
                            width: (bars.width - 8) / 3
                            height: 4
                            radius: 2
                            color: index < form.score ? form.scoreColor : Theme.border
                            Behavior on color { ColorAnimation { duration: Motion.normal } }
                        }
                    }
                }
                Text {
                    id: strengthLabel
                    anchors.right: parent.right
                    text: form.score === 0 ? "" : form.score === 1 ? qsTr("Weak") : form.score === 2 ? qsTr("Medium") : qsTr("Strong")
                    color: form.scoreColor
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                    font.weight: Font.DemiBold
                    Behavior on color { ColorAnimation { duration: Motion.normal } }
                }
            }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: form.checks
                    Row {
                        required property var modelData
                        width: (parent.width - 6) / 2
                        spacing: 6
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "check"
                            size: 12
                            color: modelData.passed ? Theme.success : Theme.textFaint
                            opacity: modelData.passed ? 1 : 0.5
                            Behavior on opacity { NumberAnimation { duration: Motion.fast } }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 18
                            elide: Text.ElideRight
                            text: modelData.label
                            color: modelData.passed ? Theme.text : Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textXs + 1
                            Behavior on color { ColorAnimation { duration: Motion.fast } }
                        }
                    }
                }
            }
            Row {
                width: parent.width
                spacing: 6
                Icon {
                    name: "shield-check"
                    size: 13
                    color: Theme.textFaint
                }
                Text {
                    width: parent.width - 19
                    wrapMode: Text.Wrap
                    text: qsTr("Passwords found in known data breaches are refused. The check is private: your password is never shared.")
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs
                }
            }
        }
    }

    Reveal {
        open: form.needsCode
        gap: 16
        Column {
            width: parent.width
            spacing: 6
            Text {
                text: qsTr("Access code")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                font.weight: Font.Medium
            }
            CodeBoxes {
                id: code
                length: form.signupRules.codeLength || 6
                enabled: !form.busy
                onAccepted: form.submit()
            }
            Text {
                width: parent.width
                // scancore.onl's usual line is shown in the app's language.
                readonly property string standard: "Contact logesh@mediumdev.com for access"
                text: form.signupRules.help && form.signupRules.help !== standard ? form.signupRules.help
                                                                                  : qsTr("Contact logesh@mediumdev.com for access")
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs + 1
                elide: Text.ElideRight
            }
        }
    }

    Reveal {
        open: form.signupClosed
        gap: 16
        Note { text: qsTr("This server is not taking new accounts.") }
    }

    Reveal {
        open: form.mode === "forgot" && !form.recovery
        gap: 16
        Note { text: qsTr("%1 doesn't send password emails. Ask whoever runs it to reset your password.").arg(form.siteName) }
    }

    // ------------------------------------------------------------ result

    Reveal {
        open: form.error.length > 0
        gap: 16
        Rectangle {
            width: parent.width
            height: errorRow.implicitHeight + 22
            radius: Theme.radiusMd
            color: Theme.alpha(Theme.danger, 0.1)
            border.width: 1
            border.color: Theme.alpha(Theme.danger, 0.3)
            Row {
                id: errorRow
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 24
                spacing: 9
                Icon {
                    name: "circle-alert"
                    size: 16
                    color: Theme.danger
                }
                Text {
                    id: errorText
                    width: parent.width - 25
                    color: Theme.danger
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                }
            }
        }
    }
    // Keeps the last message while its box fades out.
    onErrorChanged: if (error) errorText.text = error

    Reveal {
        open: true
        gap: 20
        ScButton {
            width: parent.width
            implicitHeight: 44
            variant: "primary"
            iconName: "arrow-right"
            iconAfter: true
            text: ({ forgot: qsTr("Send code"), sent: qsTr("Continue"), newpass: qsTr("Save new password"),
                     done: qsTr("Sign in"), signin: qsTr("Sign in"), signup: qsTr("Create account") })[form.mode]
            loading: form.busy
            enabled: form.canSubmit
            onClicked: form.submit()
        }
    }

    // ------------------------------------------------------------ email sent

    Reveal {
        open: form.mode === "sent"
        gap: 16
        Column {
            width: parent.width
            spacing: 12
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("The code works once and expires in 30 minutes. Nothing there? Look in spam, or send it again.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            Flow {
                width: parent.width
                spacing: 10
                ScButton {
                    variant: "secondary"
                    iconName: "send"
                    loading: form.busy && form.resetToken === "" && !resetCode.complete
                    enabled: !form.busy && form.cooldown === 0
                    text: form.cooldown > 0 ? qsTr("Send again in %1 s").arg(form.cooldown) : qsTr("Send a new code")
                    onClicked: form.sendReset()
                }
                ScButton {
                    variant: "plain"
                    text: qsTr("Use a different email")
                    onClicked: form.go("forgot")
                }
            }
        }
    }

    Reveal {
        open: form.mode === "forgot" || form.mode === "sent"
        gap: 14
        ScButton {
            variant: "plain"
            compact: true
            iconName: "arrow-left"
            text: qsTr("Back to sign in")
            onClicked: form.go("signin")
        }
    }

    Reveal {
        open: form.account
        gap: 16
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: (Account.customSite ? qsTr("Your account lives on %1.") : qsTr("Your account lives on scancore.onl."))
                .arg(form.siteName) + " " + qsTr("Your password is sent only there and is never stored by this app.")
            color: Theme.textFaint
            font.family: Theme.fontSans
            font.pixelSize: Theme.textXs + 1
        }
    }
}
