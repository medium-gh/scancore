import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Translating on this device (OnDevice): the languages that can be
// translated into the one chosen (Translator.target), each downloaded or
// removed here. The first download also brings ONNX Runtime.
Dialog {
    id: dialog
    property var installTask: null
    property string installing: ""
    property string status: ""
    property string query: ""
    readonly property var items: { App.dependOn(OnDevice.revision); return OnDevice.languages(Translator.target) }
    readonly property var shown: {
        const q = query.trim().toLowerCase()
        if (!q) return items
        return items.filter(v => (v.label + " " + v.language).toLowerCase().includes(q))
    }

    function start() { open() }
    function download(language) {
        installing = language
        status = qsTr("Starting…")
        installTask = OnDevice.install(language, Translator.target)
    }

    Connections {
        target: dialog.installTask
        function onProgressed(partial) { if (partial && partial.status) dialog.status = partial.status }
        function onFinished() {
            Router.toast(App.platform === "android" ? qsTr("Ready: pages in this language can be translated on this device.")
                                                    : qsTr("Ready: pages in this language can be translated on this computer."), "success")
            dialog.installing = ""
        }
        function onFailed(error) {
            Router.toast(error, "error")
            dialog.installing = ""
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(600, (parent ? parent.width : 600) - 32)
    height: (parent ? parent.height : 720) - 64
    modal: true
    focus: true
    padding: 24
    topPadding: 4
    title: ""

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

    header: Item {
        implicitHeight: 64
        Text {
            x: 24
            y: 22
            width: parent.width - x - 64
            text: App.platform === "android" ? qsTr("Translate on this device") : qsTr("Translate on this computer")
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.textLg
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        IconButton { anchors.right: parent.right; anchors.rightMargin: 14; y: 14; iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
    }

    contentItem: Item {
        Column {
            id: top
            width: parent.width
            spacing: 12
            bottomPadding: 12
            Text {
                width: parent.width
                text: qsTr("Each language brings what reading its pages and translating them into %1 needs: about 140 MB. Nothing is sent anywhere. ONNX Runtime (Microsoft), PaddleOCR and Opus-MT do the work.").arg(Qt.locale(Translator.target).nativeLanguageName || Translator.target)
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                wrapMode: Text.Wrap
            }
            SearchField {
                width: parent.width
                placeholderText: qsTr("Find a language")
                text: dialog.query
                onTextChanged: dialog.query = text
            }
        }
        ListView {
            id: list
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            anchors.fill: parent
            anchors.topMargin: top.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.shown
            WheelScroll {}
            ScrollBar.vertical: ScScrollBar {}
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool busy: dialog.installing === modelData.language
                width: list.width
                height: 60
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.right: action.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: row.modelData.label
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: row.busy ? dialog.status
                              : row.modelData.installed ? (row.modelData.through ? qsTr("Ready · through English") : qsTr("Ready"))
                              : (row.modelData.size > 0 ? qsTr("About %1 MB").arg(row.modelData.size) : qsTr("Nothing more to download"))
                                + (row.modelData.through ? qsTr(" · through English") : "")
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs + 1
                        elide: Text.ElideRight
                    }
                }
                ScButton {
                    id: action
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true
                    loading: row.busy
                    enabled: row.modelData.installed || dialog.installing === ""
                    variant: row.modelData.installed ? "ghost" : "secondary"
                    text: row.modelData.installed ? qsTr("Remove") : qsTr("Download")
                    onClicked: {
                        if (row.modelData.installed)
                            OnDevice.remove(row.modelData.language, Translator.target)
                        else
                            dialog.download(row.modelData.language)
                    }
                }
            }
        }
    }
}
