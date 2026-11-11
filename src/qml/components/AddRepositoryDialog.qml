import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Every way of adding a repository ends here: a one-tap link, a pasted
// address, the first-run step. Reads the repository first, says what it is
// and what will be installed, and adds it only when the user agrees.
Dialog {
    id: dialog
    property string link: ""
    property string kind: ""
    property var task: null
    property var addTask: null
    readonly property var preview: task && task.ok ? task.result : null
    readonly property bool adding: addTask ? addTask.loading : false
    property bool withInstall: false
    // Several links pasted at once go one after another: the ones still to
    // come, and how many there were.
    property var queue: []
    property int queueTotal: 0

    // Opens the sheet for `text` (a link or an address); with no text it
    // asks for one.
    function start(text, kindHint) {
        queue = []
        queueTotal = 0
        show(text, kindHint)
    }
    function show(text, kindHint) {
        link = text || ""
        kind = kindHint || ""
        addTask = null
        task = link ? Extensions.previewRepository(link, kind) : null
        field.text = ""
        open()
        if (!link) Qt.callLater(() => field.forceActiveFocus())
    }
    // The next pasted link, or false when there is none.
    function next() {
        if (queue.length === 0) return false
        const rest = queue.slice()
        const item = rest.shift()
        queue = rest
        show(item.url, item.kind)
        return true
    }

    function check() {
        const links = field.text.split(/[\s,]+/).map(t => Extensions.repositoryLink(t)).filter(p => !!p.url)
        if (links.length === 0) {
            Router.toast(qsTr("That is not a repository link."), "error")
            return
        }
        queue = links.slice(1)
        queueTotal = links.length
        show(links[0].url, links[0].kind)
    }

    function kindName(media, count) {
        if (media === "anime") return count === 1 ? qsTr("anime extension") : qsTr("anime extensions")
        if (media === "novel") return count === 1 ? qsTr("novel plugin") : qsTr("novel plugins")
        if (media === "music") return count === 1 ? qsTr("music extension") : qsTr("music extensions")
        return count === 1 ? qsTr("comic extension") : qsTr("comic extensions")
    }

    Connections {
        target: dialog.addTask
        function onFinished(result) {
            if (!dialog.next()) dialog.close()
            let message = "“" + result.name + "”" + qsTr(" added")
            if (result.queued > 0)
                message += qsTr("; installing %1 in the background").arg(result.queued)
            Router.toast(message, "success")
        }
        function onFailed(error) { Router.toast(error, "error") }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, (parent ? parent.width : 460) - 32)
    modal: true
    focus: true
    padding: 24
    title: ""
    closePolicy: dialog.adding ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: Theme.overlay }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast }
            NumberAnimation { property: "scale"; to: 0.97; duration: Motion.fast }
        }
    }

    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    contentItem: Column {
        spacing: 16

        Row {
            spacing: 12
            Rectangle {
                width: 40
                height: 40
                radius: 12
                color: Theme.alpha(Theme.accent, 0.14)
                Icon { anchors.centerIn: parent; name: "puzzle"; size: 20; color: Theme.accent }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: dialog.availableWidth - 52
                text: (dialog.preview ? qsTr("Add “") + dialog.preview.name + qsTr("” from ") + dialog.preview.domain + "?"
                    : qsTr("Add a repository"))
                    + (dialog.queueTotal > 1 ? "  " + qsTr("(%1 of %2)").arg(dialog.queueTotal - dialog.queue.length).arg(dialog.queueTotal) : "")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
        }

        // No link yet: ask for one.
        Column {
            width: parent.width
            spacing: 12
            visible: !dialog.task
            Text {
                width: parent.width
                text: qsTr("Paste the link from the repository's page, or the link to its index, repo.json or plugin list; several at once work too. SCANCORE reads Mihon, Tachiyomi, Aniyomi, CloudStream, Echo and LNReader repositories, and CloudStream and Echo codes.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                lineHeight: 1.25
                wrapMode: Text.Wrap
            }
            InlineField {
                id: field
                width: parent.width
                placeholderText: qsTr("Repository link")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: dialog.check()
            }
        }

        // Reading it.
        Row {
            visible: dialog.task !== null && dialog.task.loading
            spacing: 10
            Spinner { size: 16; anchors.verticalCenter: parent.verticalCenter }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Reading the repository…")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
        }
        Text {
            width: parent.width
            visible: dialog.task !== null && !dialog.task.loading && !dialog.task.ok
            text: dialog.task ? dialog.task.error : ""
            color: Theme.danger
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }

        // What it is and what happens.
        Column {
            width: parent.width
            spacing: 10
            visible: dialog.preview !== null
            Text {
                width: parent.width
                text: !dialog.preview ? ""
                    : dialog.preview.count + " " + dialog.kindName(dialog.preview.media, dialog.preview.count)
                      + (dialog.preview.languages.length > 1 ? qsTr(" in ") + dialog.preview.languages.length + qsTr(" languages") : "")
                      + "."
                color: Theme.text
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: !dialog.preview ? ""
                    : dialog.preview.added ? qsTr("It is already added. Adding it again reads it afresh.")
                    : dialog.preview.autoInstall.length > 0
                      ? qsTr("Install the %1 in %2 now, or add it only and pick extensions under Available. Installing goes on in the background.")
                            .arg(dialog.preview.autoInstall.length).arg(dialog.preview.languageName)
                    : qsTr("None of them is in ") + dialog.preview.languageName + qsTr(", so nothing is installed yet. Pick extensions under Available.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                lineHeight: 1.25
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: qsTr("Extensions are other people's code. Add repositories you trust.")
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs
                wrapMode: Text.Wrap
            }
        }

        // Right-aligned, and onto a second line when they do not fit (on
        // a phone "Cancel" went off the dialog's left edge).
        Flow {
            width: parent.width
            layoutDirection: Qt.RightToLeft
            spacing: 8
            // Adding is quick; any installs carry on in the background.
            readonly property int toInstall: dialog.preview && !dialog.preview.added ? dialog.preview.autoInstall.length : 0
            ScButton {
                visible: parent.toInstall > 0
                text: qsTr("Add and install %1").arg(parent.toInstall)
                variant: "primary"
                loading: dialog.adding && dialog.withInstall
                enabled: !dialog.adding
                onClicked: { dialog.withInstall = true; dialog.addTask = Extensions.addRepository(dialog.preview.url, true) }
            }
            ScButton {
                visible: dialog.preview !== null
                text: parent.toInstall > 0 ? qsTr("Add only") : qsTr("Add")
                variant: parent.toInstall > 0 ? "secondary" : "primary"
                loading: dialog.adding && !dialog.withInstall
                enabled: !dialog.adding
                onClicked: { dialog.withInstall = false; dialog.addTask = Extensions.addRepository(dialog.preview.url, false) }
            }
            ScButton {
                visible: dialog.task !== null && !dialog.task.loading && !dialog.task.ok
                text: qsTr("Try again")
                onClicked: dialog.show(dialog.link, dialog.kind)
            }
            ScButton {
                visible: !dialog.task
                text: qsTr("Continue")
                variant: "primary"
                enabled: field.text.trim().length > 0
                onClicked: dialog.check()
            }
            ScButton {
                visible: dialog.queue.length > 0
                text: qsTr("Skip")
                enabled: !dialog.adding
                onClicked: dialog.next()
            }
            ScButton {
                text: qsTr("Cancel")
                enabled: !dialog.adding
                onClicked: dialog.close()
            }
        }
    }
}
