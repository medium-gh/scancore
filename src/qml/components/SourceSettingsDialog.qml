import QtQuick
import QtQuick.Controls.Basic
import Scancore

// An extension source's own settings, as its extension describes them
// (switches, lists, several choices, text). Each change is saved at once;
// the source may then show other settings.
Dialog {
    id: dialog
    property string key: ""
    property string name: ""
    property var task: null
    property var settings: []
    property bool saving: false

    function start(sourceKey, sourceName) {
        key = sourceKey
        name = sourceName
        settings = []
        task = Extensions.sourceSettings(sourceKey)
        open()
    }

    function save(setting, value) {
        saving = true
        const t = Extensions.setSourceSetting(key, setting, value)
        t.finished.connect((list) => { dialog.saving = false; dialog.settings = list })
        t.failed.connect((error) => { dialog.saving = false; Router.toast(error, "error") })
    }

    Connections {
        target: dialog.task
        function onFinished(list) { dialog.settings = list }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, (parent ? parent.width : 520) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 640) - 64)
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
            width: parent.width - 80
            text: dialog.name + qsTr(" settings")
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.textLg
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Spinner { visible: dialog.saving; size: 16; anchors.right: closeButton.left; anchors.rightMargin: 8; y: 26 }
        IconButton { id: closeButton; anchors.right: parent.right; anchors.rightMargin: 14; y: 14; iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
    }

    contentItem: Flickable {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        implicitHeight: Math.max(80, body.implicitHeight)
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll {}
        ScrollBar.vertical: ScScrollBar {}

        Column {
            id: body
            width: parent.width
            spacing: 14

            StateView {
                width: parent.width
                height: visible ? 120 : 0
                visible: dialog.task !== null && (dialog.task.loading || !dialog.task.ok)
                compact: true
                task: dialog.task
            }
            Text {
                width: parent.width
                visible: dialog.task !== null && dialog.task.ok && dialog.settings.length === 0
                text: qsTr("This source has no settings.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }

            Repeater {
                model: dialog.settings
                delegate: Column {
                    id: setting
                    required property var modelData
                    readonly property string label: {
                        const i = (modelData.values || []).indexOf(String(modelData.value))
                        return i >= 0 ? (modelData.entries || [])[i] || "" : ""
                    }
                    width: body.width
                    spacing: 8
                    opacity: modelData.enabled === false ? 0.5 : 1
                    enabled: modelData.enabled !== false

                    Item {
                        width: parent.width
                        height: Math.max(labels.implicitHeight, toggle.visible ? toggle.height : 0)
                        Column {
                            id: labels
                            width: parent.width - (toggle.visible ? toggle.width + 12 : 0)
                            spacing: 3
                            Text {
                                width: parent.width
                                text: setting.modelData.title || setting.modelData.key
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textMd
                                font.weight: Font.Medium
                                wrapMode: Text.Wrap
                            }
                            Text {
                                width: parent.width
                                visible: text.length > 0
                                // Android's "%s" stands for the chosen entry.
                                text: (setting.modelData.summary || "").replace("%s", setting.label)
                                color: Theme.textMuted
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                                wrapMode: Text.Wrap
                            }
                        }
                        Toggle {
                            id: toggle
                            visible: setting.modelData.type === "switch"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            checked: setting.modelData.value === true
                            onToggled: dialog.save(setting.modelData.key, checked)
                        }
                    }

                    // One of a list.
                    Select {
                        visible: setting.modelData.type === "list"
                        width: Math.min(parent.width, 320)
                        model: (setting.modelData.values || []).map((v, i) => ({ value: v, label: (setting.modelData.entries || [])[i] || v }))
                        value: setting.modelData.value
                        onPicked: (v) => dialog.save(setting.modelData.key, v)
                    }

                    // Several of a list.
                    Flow {
                        visible: setting.modelData.type === "multiselect"
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: setting.modelData.type === "multiselect" ? setting.modelData.values : []
                            delegate: Chip {
                                required property string modelData
                                required property int index
                                readonly property var chosen: Array.from(setting.modelData.value || [])
                                text: (setting.modelData.entries || [])[index] || modelData
                                checkable: true
                                checked: chosen.indexOf(modelData) >= 0
                                onClicked: {
                                    const next = chosen.filter(v => v !== modelData)
                                    if (checked) next.push(modelData)
                                    dialog.save(setting.modelData.key, next)
                                }
                            }
                        }
                    }

                    // Text, saved on Enter or when leaving the field.
                    InlineField {
                        id: textField
                        visible: setting.modelData.type === "text"
                        width: parent.width
                        text: setting.modelData.value === null || setting.modelData.value === undefined ? "" : String(setting.modelData.value)
                        placeholderText: setting.modelData.dialogTitle || ""
                        onEditingFinished: if (text !== String(setting.modelData.value || "")) dialog.save(setting.modelData.key, text)
                    }
                }
            }
        }
    }
}
