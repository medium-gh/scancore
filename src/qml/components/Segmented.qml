import QtQuick
import Scancore

// Segmented control with a sliding selection pill.
Rectangle {
    id: root
    property var options: []          // [{value, label, icon}]
    property var value
    // Options share the whole width (phones), instead of each its own.
    property bool fill: false
    signal picked(var value)

    readonly property int currentIndex: {
        for (let i = 0; i < options.length; ++i)
            if (options[i].value === value) return i
        return 0
    }

    implicitHeight: 36
    implicitWidth: row.implicitWidth + 8
    radius: Theme.pill(height)
    // Glass: a capsule of glass; the choice a soft white fill on it.
    color: Theme.glass ? "transparent" : Theme.surface2
    border.width: Theme.glass ? 0 : 1
    border.color: Theme.border
    GlassCard { radius: root.radius; tint: 0.25 }

    Rectangle {
        id: pill
        y: 4
        height: parent.height - 8
        radius: Theme.pill(height)
        color: Theme.glass ? Theme.alpha(Theme.text, 0.16) : Theme.surface
        border.width: Theme.glass ? 0 : 1
        border.color: Theme.borderStrong
        readonly property Item target: repeater.count > root.currentIndex ? repeater.itemAt(root.currentIndex) : null
        x: target ? target.x + 4 : 4
        width: target ? target.width : 0
        Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
        Behavior on width { NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
    }

    Row {
        id: row
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            id: repeater
            model: root.options
            delegate: Item {
                required property var modelData
                required property int index
                // Icon-only options ({value, icon, tip}) are square-ish.
                width: root.fill ? (root.width - 8) / Math.max(1, root.options.length)
                    : (label.visible ? label.implicitWidth + (glyph.visible ? 21 : 0) : glyph.width) + 26
                height: root.height - 8
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Icon {
                        id: glyph
                        visible: !!modelData.icon
                        name: modelData.icon || ""
                        size: 15
                        color: index === root.currentIndex ? Theme.text : Theme.textMuted
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: label
                        // Sharing the width, a long label (a translation) ends in "…".
                        width: root.fill ? Math.min(implicitWidth, root.width / Math.max(1, root.options.length) - 20 - (glyph.visible ? 21 : 0))
                                         : implicitWidth
                        elide: Text.ElideRight
                        visible: text.length > 0
                        text: modelData.label || ""
                        color: index === root.currentIndex ? Theme.text : Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                        Behavior on color { ColorAnimation { duration: Motion.fast } }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.picked(modelData.value)
                }
            }
        }
    }
}
