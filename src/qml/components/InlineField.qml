import QtQuick
import QtQuick.Controls.Basic
import Scancore

// A small single-line input for a settings row.
TextField {
    implicitHeight: 38
    leftPadding: 14
    rightPadding: 14
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    font.family: Theme.fontSans
    font.pixelSize: Theme.textMd
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: Theme.pill(height)
        // Glass: a field of glass (its edge only while typing in it).
        color: Theme.glass ? "transparent" : parent.activeFocus ? Theme.surface : Theme.surface2
        border.width: Theme.glass && !parent.activeFocus ? 0 : 1
        GlassCard { radius: parent.radius; tint: 0.3 }
        border.color: parent.activeFocus ? Theme.alpha(Theme.accent, 0.55) : Theme.border
    }
}
