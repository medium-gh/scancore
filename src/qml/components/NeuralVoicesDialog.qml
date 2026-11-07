import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Neural voices for reading aloud on computers (Piper): the catalogue, the
// app's language first, each voice downloaded or removed here. The first
// download also brings the voice program.
Dialog {
    id: dialog
    property var listTask: null
    property var installTask: null
    property string installing: ""
    property string status: ""
    property string query: ""
    readonly property var items: listTask && listTask.ok ? listTask.result : []
    readonly property var shown: {
        const q = query.trim().toLowerCase()
        if (!q) return items
        return items.filter(v => (v.name + " " + v.languageName + " " + v.language).toLowerCase().includes(q))
    }

    function start() {
        if (!listTask || !listTask.ok) listTask = Piper.catalogue()
        open()
    }
    function download(key) {
        installing = key
        status = qsTr("Starting…")
        installTask = Piper.install(key)
    }

    Connections {
        target: dialog.installTask
        function onProgressed(partial) { if (partial && partial.status) dialog.status = partial.status }
        function onFinished() {
            Router.toast(qsTr("Voice ready: choose it under Voice."), "success")
            dialog.installing = ""
            dialog.listTask = Piper.catalogue()
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
            text: qsTr("Neural voices")
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
                text: qsTr("Natural voices that read on this computer, with no internet once downloaded. The first one also brings the voice program (Piper, about 25 MB).")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                wrapMode: Text.Wrap
            }
            SearchField {
                width: parent.width
                placeholderText: qsTr("Find a voice or language")
                text: dialog.query
                onTextChanged: dialog.query = text
            }
        }
        Spinner {
            anchors.centerIn: parent
            visible: !!dialog.listTask && dialog.listTask.loading
        }
        Text {
            anchors.centerIn: parent
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: !!dialog.listTask && dialog.listTask.done && !dialog.listTask.ok
            text: dialog.listTask ? dialog.listTask.error : ""
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
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
                readonly property bool busy: dialog.installing === modelData.key
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
                        text: row.modelData.name + " · " + row.modelData.languageName
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: row.busy ? dialog.status
                              : ({ high: qsTr("Best quality"), medium: qsTr("Good quality"), low: qsTr("Smaller"), x_low: qsTr("Smallest") }[row.modelData.quality] || row.modelData.quality)
                                + " · " + row.modelData.size
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
                        if (row.modelData.installed) {
                            Piper.remove(row.modelData.key)
                            dialog.listTask = Piper.catalogue()
                        } else
                            dialog.download(row.modelData.key)
                    }
                }
            }
        }
    }
}
