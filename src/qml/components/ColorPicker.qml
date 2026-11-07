import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Colour picker: a hue and saturation wheel, a brightness slider (how dark
// the colour is), a hex field and a few swatches. `changed` fires as the
// colour moves, so whatever uses it can preview live; Revert goes back to the
// colour it opened with.
Popup {
    id: picker
    property string title: qsTr("Colour")
    property var swatches: ["#0b0b0f", "#15171d", "#1b2230", "#14231d", "#2a1621", "#231a12",
                            "#f4f1ea", "#eef2f7", "#7c9cff", "#a78bfa", "#34d399", "#fb923c", "#fb7185", "#38bdf8"]
    signal changed(color value)

    property real hue: 0
    property real sat: 0
    property real val: 0.1
    property color original: "black"
    readonly property color current: Qt.hsva(hue, sat, val, 1)
    readonly property string hex: String(current).slice(0, 7)

    // Opens on `c`, remembering it for Revert.
    function edit(c) {
        original = c
        setColor(c)
        open()
    }

    function setColor(c) {
        const parsed = Qt.color(c)
        // Greys have no hue; keep the last one so the wheel does not jump.
        if (parsed.hsvHue >= 0) hue = parsed.hsvHue
        sat = parsed.hsvSaturation
        val = parsed.hsvValue
    }

    onCurrentChanged: if (opened) throttle.restart()
    Timer {
        id: throttle
        interval: 60
        onTriggered: picker.changed(picker.current)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(360, (parent ? parent.width : 360) - 32)
    modal: true
    focus: true
    padding: 20

    Overlay.modal: Rectangle { color: Theme.alpha("#000000", 0.25) }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast } }
    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.palette.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    contentItem: Column {
        spacing: 16

        Item {
            width: parent.width
            height: 30
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: picker.title
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
            }
            IconButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "x"
                diameter: 30
                iconSize: 15
                tip: qsTr("Done")
                onClicked: picker.close()
            }
        }

        // Hue around, saturation outwards; dimmed by the brightness.
        Item {
            id: wheel
            readonly property real side: Math.min(parent.width, 240)
            readonly property real radius: side / 2
            anchors.horizontalCenter: parent.horizontalCenter
            width: side
            height: side

            Image {
                anchors.fill: parent
                source: "image://colorwheel/" + Math.round(wheel.side * Screen.devicePixelRatio)
                sourceSize: Qt.size(Math.round(wheel.side * Screen.devicePixelRatio), Math.round(wheel.side * Screen.devicePixelRatio))
                smooth: true
            }
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "black"
                opacity: 1 - picker.val
            }
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.alpha(Theme.text, 0.12)
            }

            // The picked colour.
            Rectangle {
                id: knob
                readonly property real angle: picker.hue * 2 * Math.PI
                x: wheel.radius + Math.cos(angle) * picker.sat * wheel.radius - width / 2
                y: wheel.radius - Math.sin(angle) * picker.sat * wheel.radius - height / 2
                width: wheelDrag.pressed ? 26 : 22
                height: width
                radius: width / 2
                color: picker.current
                border.width: 3
                border.color: "white"
                Behavior on width { NumberAnimation { duration: Motion.fast } }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.alpha("#000000", 0.35)
                }
            }

            MouseArea {
                id: wheelDrag
                anchors.fill: parent
                preventStealing: true
                function pick(mouse) {
                    const dx = mouse.x - wheel.radius
                    const dy = wheel.radius - mouse.y
                    let h = Math.atan2(dy, dx) / (2 * Math.PI)
                    if (h < 0) h += 1
                    picker.hue = h
                    picker.sat = Math.min(1, Math.sqrt(dx * dx + dy * dy) / wheel.radius)
                }
                onPressed: (mouse) => pick(mouse)
                onPositionChanged: (mouse) => { if (pressed) pick(mouse) }
            }
        }

        // Brightness: from black to the colour at full strength.
        Column {
            width: parent.width
            spacing: 8
            Item {
                width: parent.width
                height: 16
                Text {
                    text: qsTr("Brightness")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                    font.weight: Font.Medium
                }
                Text {
                    anchors.right: parent.right
                    text: Math.round(picker.val * 100) + "%  ·  " + (Theme.isDark(picker.current) ? "dark" : "light")
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                    font.features: { "tnum": 1 }
                }
            }
            Item {
                id: track
                width: parent.width
                height: 22
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    border.width: 1
                    border.color: Theme.alpha(Theme.text, 0.12)
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "black" }
                        GradientStop { position: 1; color: Qt.hsva(picker.hue, picker.sat, 1, 1) }
                    }
                }
                Rectangle {
                    x: picker.val * (track.width - width)
                    anchors.verticalCenter: parent.verticalCenter
                    width: valueDrag.pressed ? 26 : 22
                    height: width
                    radius: width / 2
                    color: picker.current
                    border.width: 3
                    border.color: "white"
                    Behavior on width { NumberAnimation { duration: Motion.fast } }
                }
                MouseArea {
                    id: valueDrag
                    anchors.fill: parent
                    anchors.margins: -6
                    preventStealing: true
                    function pick(mouse) {
                        picker.val = Math.max(0, Math.min(1, (mouse.x - 6 - 11) / (track.width - 22)))
                    }
                    onPressed: (mouse) => pick(mouse)
                    onPositionChanged: (mouse) => { if (pressed) pick(mouse) }
                    onWheel: (wheel) => picker.val = Math.max(0, Math.min(1, picker.val + (wheel.angleDelta.y > 0 ? 0.02 : -0.02)))
                }
            }
        }

        // Before and after, and the hex code.
        Row {
            width: parent.width
            spacing: 10
            Rectangle {
                width: 64
                height: 36
                radius: Theme.radiusMd
                clip: true
                border.width: 1
                border.color: Theme.border
                color: "transparent"
                Row {
                    anchors.fill: parent
                    anchors.margins: 1
                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        color: picker.original
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: picker.setColor(picker.original) }
                    }
                    Rectangle { width: parent.width / 2; height: parent.height; color: picker.current }
                }
            }
            TextField {
                id: hexField
                width: parent.width - 64 - 10 - revert.width - 10
                height: 36
                text: picker.hex
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                font.capitalization: Font.AllUppercase
                color: Theme.text
                selectByMouse: true
                leftPadding: 12
                validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                background: Rectangle {
                    radius: Theme.radiusMd
                    color: Theme.surface2
                    border.width: 1
                    border.color: hexField.activeFocus ? Theme.accent : Theme.border
                }
                onEditingFinished: {
                    const t = text.startsWith("#") ? text : "#" + text
                    if (/^#[0-9a-fA-F]{6}$/.test(t)) picker.setColor(t)
                    // Follow the picker again, typed colour or not.
                    text = Qt.binding(() => picker.hex)
                }
            }
            ScButton {
                id: revert
                text: qsTr("Revert")
                compact: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: picker.setColor(picker.original)
            }
        }

        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: picker.swatches
                AbstractButton {
                    required property string modelData
                    width: 24
                    height: 24
                    hoverEnabled: true
                    onClicked: picker.setColor(modelData)
                    background: Rectangle {
                        radius: 12
                        color: modelData
                        border.width: 1
                        border.color: Theme.alpha(Theme.text, 0.18)
                        scale: parent.hovered ? 1.15 : 1
                        Behavior on scale { NumberAnimation { duration: Motion.fast; easing.type: Easing.OutBack } }
                    }
                }
            }
        }
    }
}
