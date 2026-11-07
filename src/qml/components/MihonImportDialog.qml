import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Before a Mihon backup comes in: the repositories it names that are not
// added here, and the extensions its series came from that are not
// installed. With them, those series come back on their own sources;
// without, they wait to be found by title. Also the extensions of the other
// sources set up in that app, so what was installed there is here too. Opens only when there is
// something to offer (see start()).
Dialog {
    id: dialog
    property url file
    property var task: null
    property var runtimeTask: null
    readonly property var preview: task && task.ok ? task.result : null
    // Repository links ticked to add; extensions installed or not.
    property var chosenRepos: []
    property bool installExtensions: true
    property bool installOthers: true
    readonly property var newRepos: preview ? preview.repositories.filter(r => !r.added) : []
    readonly property var extensions: preview ? preview.extensions.filter(e => e.series > 0) : []
    readonly property var otherExtensions: preview ? preview.extensions.filter(e => e.series === 0) : []
    readonly property int extensionSeries: extensions.reduce((n, e) => n + e.series, 0)
    readonly property bool needsRuntime: App.platform !== "android" && Extensions.runtimeAvailable && !Extensions.runtimeInstalled
    readonly property bool busy: runtimeTask !== null && runtimeTask.loading
    // Called with the import task once it starts.
    signal importStarted(var importTask)

    function start(path) {
        file = path
        runtimeTask = null
        task = Importer.previewMihon(path)
    }

    function begin() {
        const prepare = {
            repositories: dialog.chosenRepos,
            extensions: (dialog.installExtensions ? dialog.extensions : []).concat(dialog.installOthers ? dialog.otherExtensions : [])
                .map(e => ({ kind: e.kind, pkg: e.pkg }))
        }
        // Extensions run in the Extensions runtime on computers: it comes first.
        if (prepare.extensions.length > 0 && dialog.needsRuntime) {
            runtimeTask = Extensions.installRuntime()
            runtimeTask.finished.connect(() => dialog.go(prepare))
            runtimeTask.failed.connect((error) => Router.toast(error, "error"))
            return
        }
        go(prepare)
    }
    function go(prepare) {
        dialog.importStarted(Importer.importMihon(dialog.file, prepare))
        dialog.close()
    }

    Connections {
        target: dialog.task
        function onFinished(result) {
            dialog.chosenRepos = result.repositories.filter(r => !r.added && !r.error).map(r => r.url)
            dialog.installExtensions = true
            dialog.installOthers = true
            // Nothing to offer: straight in. Mihon's downloads are offered
            // until a folder of them is chosen.
            if (result.repositories.every(r => r.added) && result.extensions.length === 0
                    && (folderPicker.linked || result.comics === 0)) dialog.go({})
            else dialog.open()
        }
        function onFailed(error) { Router.toast(error, "error") }
    }

    MihonFolderPicker {
        id: folderPicker
        quiet: true
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, (parent ? parent.width : 480) - 32)
    modal: true
    focus: true
    padding: 24
    closePolicy: dialog.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: Theme.overlay }
    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    // What it offers scrolls; the buttons stay in view (short landscape
    // screens).
    contentItem: Column {
        spacing: 16
    Flickable {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        width: parent.width
        implicitHeight: body.implicitHeight
        // The window's height, less the margin, padding and buttons.
        height: Math.min(body.implicitHeight,
                         (dialog.parent ? dialog.parent.height : 640) - 48 - dialog.topPadding - dialog.bottomPadding - buttons.height - 16)
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScScrollBar {}

        Column {
            id: body
            width: parent.width
            spacing: 16

            Text {
                width: parent.width
                text: qsTr("Bring back everything from %1").arg(dialog.preview ? dialog.preview.app : "Mihon")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                visible: dialog.preview !== null
                text: !dialog.preview ? ""
                    : qsTr("%1 comics and %2 anime.").arg(dialog.preview.comics).arg(dialog.preview.anime)
                      + (dialog.preview.lists.length > 0 ? " " + qsTr("Your lists come along in %1's order: %2.").arg(dialog.preview.app).arg(dialog.preview.lists.join(", ")) : "")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                lineHeight: 1.25
                wrapMode: Text.Wrap
            }

            // Repositories in the backup that are not added here.
            Column {
                width: parent.width
                spacing: 4
                visible: dialog.newRepos.length > 0
                Text {
                    text: qsTr("Repositories in the backup")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs
                }
                Repeater {
                    model: dialog.newRepos
                    Item {
                        required property var modelData
                        width: parent.width
                        height: Math.max(40, repoText.implicitHeight + 12)
                        Column {
                            id: repoText
                            anchors.left: parent.left
                            anchors.right: repoToggle.left
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                width: parent.width
                                text: parent.parent.modelData.name
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: parent.parent.modelData.error
                                    ? parent.parent.modelData.error
                                    : qsTr("%1 extensions").arg(parent.parent.modelData.count || 0)
                                color: parent.parent.modelData.error ? Theme.danger : Theme.textFaint
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textXs
                                wrapMode: Text.Wrap
                            }
                        }
                        Toggle {
                            id: repoToggle
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            enabled: !parent.modelData.error
                            checked: dialog.chosenRepos.indexOf(parent.modelData.url) >= 0
                            onToggled: {
                                const url = parent.modelData.url
                                const next = dialog.chosenRepos.filter(u => u !== url)
                                if (checked) next.push(url)
                                dialog.chosenRepos = next
                            }
                        }
                    }
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

            // Extensions its series came from.
            Item {
                width: parent.width
                height: Math.max(40, extText.implicitHeight + 12)
                visible: dialog.extensions.length > 0
                Column {
                    id: extText
                    anchors.left: parent.left
                    anchors.right: extToggle.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: qsTr("Install the %1 extensions your series came from").arg(dialog.extensions.length)
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        // A few names; the whole list can be long.
                        text: dialog.extensions.slice(0, 6).map(e => e.name).join(", ")
                              + (dialog.extensions.length > 6 ? qsTr(" and %1 more").arg(dialog.extensions.length - 6) : "")
                              + " · " + qsTr("%1 series").arg(dialog.extensionSeries)
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs
                        wrapMode: Text.Wrap
                    }
                }
                Toggle {
                    id: extToggle
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: dialog.installExtensions
                    onToggled: dialog.installExtensions = checked
                }
            }
            // The other extensions installed there.
            Item {
                width: parent.width
                height: Math.max(40, otherText.implicitHeight + 12)
                visible: dialog.otherExtensions.length > 0
                Column {
                    id: otherText
                    anchors.left: parent.left
                    anchors.right: otherToggle.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        readonly property int count: dialog.otherExtensions.length
                        readonly property string app: dialog.preview ? dialog.preview.app : "Mihon"
                        text: dialog.extensions.length > 0
                              ? (count === 1 ? qsTr("Also the other extension you had in %1").arg(app) : qsTr("Also the %1 other extensions you had in %2").arg(count).arg(app))
                              : (count === 1 ? qsTr("Install the extension you had in %1").arg(app) : qsTr("Install the %1 extensions you had in %2").arg(count).arg(app))
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        text: dialog.otherExtensions.slice(0, 6).map(e => e.name).join(", ")
                              + (dialog.otherExtensions.length > 6 ? qsTr(" and %1 more").arg(dialog.otherExtensions.length - 6) : "")
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs
                        wrapMode: Text.Wrap
                    }
                }
                Toggle {
                    id: otherToggle
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: dialog.installOthers
                    onToggled: dialog.installOthers = checked
                }
            }
            Text {
                width: parent.width
                visible: (dialog.installExtensions && dialog.extensions.length > 0 || dialog.installOthers && dialog.otherExtensions.length > 0) && dialog.needsRuntime
                text: qsTr("Extensions run in the Extensions runtime, which downloads first (about 100 MB).")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs
                wrapMode: Text.Wrap
            }
            // The chapters it downloaded, read where they are.
            Item {
                width: parent.width
                height: Math.max(40, folderText.implicitHeight + 12)
                visible: dialog.preview !== null && dialog.preview.comics > 0
                Column {
                    id: folderText
                    anchors.left: parent.left
                    anchors.right: folderButton.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: qsTr("Chapters %1 downloaded").arg(dialog.preview ? dialog.preview.app : "Mihon")
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        text: folderPicker.linking ? qsTr("Looking through the folder…")
                            : folderPicker.linked ? qsTr("Read from its folder after the import, offline and with your progress. Nothing is copied.")
                            : qsTr("Choose its storage folder (More → Settings → Data and storage) to read them here offline. Nothing is copied.")
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs
                        wrapMode: Text.Wrap
                    }
                }
                ScButton {
                    id: folderButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true
                    text: folderPicker.linked ? qsTr("Chosen") : qsTr("Choose")
                    iconName: folderPicker.linked ? "check" : "folder-open"
                    loading: folderPicker.linking
                    onClicked: folderPicker.open()
                }
            }
            Text {
                width: parent.width
                visible: dialog.preview !== null && dialog.preview.unknownSources > 0
                text: qsTr("%1 series come from extensions none of these repositories has; they wait below to be found by title.")
                      .arg(dialog.preview ? dialog.preview.unknownSources : 0)
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs
                wrapMode: Text.Wrap
            }

        }
    }
    Row {
        id: buttons
        anchors.right: parent.right
        spacing: 8
        ScButton {
            text: qsTr("Cancel")
            enabled: !dialog.busy
            onClicked: dialog.close()
        }
        ScButton {
            text: qsTr("Import")
            variant: "primary"
            loading: dialog.busy
            enabled: !dialog.busy
            onClicked: dialog.begin()
        }
    }
    }
}
