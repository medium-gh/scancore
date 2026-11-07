import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Crash reports and diagnostics, ready to send to the developer: after a
// crash it opens by itself on the newest report; Settings → About lists them.
// Copy, share (Android), or start a GitHub issue with it filled in.
Dialog {
    id: dialog
    // The report shown ("" for the list), and whether this opened after a crash.
    property string file: ""
    property bool afterCrash: false
    property var reports: []
    readonly property string text: file ? App.crashReport(file) : ""

    function showLatest() {
        const unseen = App.unseenCrashReports()
        if (unseen.length === 0) return
        unseen.forEach(f => App.markCrashReportSeen(f))
        afterCrash = true
        file = unseen[0]
        open()
    }
    function showAll() {
        afterCrash = false
        file = ""
        reports = App.crashReports()
        open()
    }
    function send(content, subject) {
        if (App.platform === "android" && App.shareText(content, subject)) return
        App.copy(content)
        Router.toast(qsTr("Copied. Paste it in a message to the developer, on Discord or GitHub."), "success")
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, (parent ? parent.width : 560) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 64)
    modal: true
    focus: true
    padding: 22
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
            text: dialog.afterCrash ? qsTr("SCANCORE closed unexpectedly") : dialog.file ? qsTr("Crash report") : qsTr("Crash reports and diagnostics")
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.textLg
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            text: dialog.file ? qsTr("Sending this report to the developer helps fix it. It holds the app's version, your system, where it stopped and the app's recent log: no library, history or account details.")
                : qsTr("Reports from times SCANCORE closed unexpectedly. Diagnostics hold the app's recent log, for problems that are not crashes.")
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
        // One report.
        Rectangle {
            visible: dialog.file !== ""
            width: parent.width
            height: Math.min(260, reportText.implicitHeight + 20)
            radius: Theme.radiusMd
            color: Theme.surface2
            Flickable {
                maximumFlickVelocity: Motion.flickVelocity
                flickDeceleration: Motion.flickDeceleration
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                contentHeight: reportText.implicitHeight
                Text {
                    id: reportText
                    width: parent.width
                    text: dialog.text
                    color: Theme.textMuted
                    font.family: "monospace"
                    font.pixelSize: Theme.textXs
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
        // The list.
        Column {
            visible: dialog.file === ""
            width: parent.width
            spacing: 6
            Repeater {
                model: dialog.reports
                AbstractButton {
                    id: row
                    required property var modelData
                    width: parent.width
                    height: 46
                    hoverEnabled: true
                    onClicked: dialog.file = modelData.file
                    background: Rectangle { radius: Theme.radiusMd; color: row.hovered ? Theme.alpha(Theme.text, 0.07) : Theme.surface2 }
                    contentItem: Column {
                        leftPadding: 12
                        topPadding: 6
                        Text { text: App.relativeTime(row.modelData.when); color: Theme.text; font.family: Theme.fontSans; font.pixelSize: Theme.textSm; font.weight: Font.Medium }
                        Text { width: row.width - 24; text: row.modelData.summary; elide: Text.ElideRight; color: Theme.textFaint; font.family: Theme.fontSans; font.pixelSize: Theme.textXs }
                    }
                }
            }
            Text {
                visible: dialog.reports.length === 0
                text: qsTr("No crash reports. Good news.")
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
        }
        Flow {
            width: parent.width
            spacing: 8
            layoutDirection: Qt.RightToLeft
            ScButton { text: qsTr("Close"); variant: "ghost"; onClicked: dialog.close() }
            ScButton {
                visible: dialog.file !== ""
                text: App.platform === "android" ? qsTr("Share") : qsTr("Copy")
                iconName: App.platform === "android" ? "external-link" : "copy"
                variant: "primary"
                onClicked: dialog.send(dialog.text, qsTr("SCANCORE crash report"))
            }
            ScButton {
                visible: dialog.file !== ""
                text: qsTr("Report on GitHub")
                iconName: "external-link"
                onClicked: App.openUrl(App.reportIssueUrl(dialog.text))
            }
            ScButton {
                visible: dialog.file !== "" && !dialog.afterCrash
                text: qsTr("Delete")
                iconName: "trash"
                variant: "ghost"
                onClicked: { App.removeCrashReport(dialog.file); dialog.showAll() }
            }
            ScButton {
                visible: dialog.file === ""
                text: qsTr("Copy diagnostics")
                iconName: "copy"
                onClicked: dialog.send(App.diagnostics(), qsTr("SCANCORE diagnostics"))
            }
        }
    }
}
