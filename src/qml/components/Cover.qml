import QtQuick
import Scancore

// Rounded cover art. Shows a soft placeholder while loading, then fades and
// settles the image in. The image is drawn by one shader straight from its
// texture (crop, zoom and rounded corners in one pass), so a screen of
// covers costs no offscreen buffers.
Item {
    id: root
    property string source
    property string referer
    property string title
    property int radius: Theme.radiusMd
    property real zoom: 1
    property color tint: Theme.surface2
    readonly property bool ready: image.status === Image.Ready
    readonly property bool failed: image.status === Image.Error

    Rectangle {
        id: placeholder
        anchors.fill: parent
        radius: root.radius
        color: root.tint
        visible: !root.ready
        Text {
            anchors.fill: parent
            anchors.margins: 12
            visible: image.status === Image.Error || root.source.length === 0
            text: root.title
            textFormat: Text.PlainText
            color: Theme.textFaint
            font.family: Theme.fontDisplay
            font.pixelSize: 13
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Shimmer {
            anchors.fill: parent
            radius: root.radius
            visible: image.status === Image.Loading
        }
    }

    // Only a texture source for the shader.
    Image {
        id: image
        visible: false
        // Nothing loads before the cover has its size, so each image is
        // decoded once, at the size it is shown.
        source: root.source && root.width > 0 ? App.image(root.source, root.referer) : ""
        sourceSize.width: Math.max(1, Math.ceil(root.width * Screen.devicePixelRatio * 1.15))
        asynchronous: true
        cache: true
        smooth: true
        mipmap: false
    }

    ShaderEffect {
        id: effect
        anchors.fill: parent
        visible: root.ready && width > 0 && height > 0
        readonly property real imageAspect: image.implicitHeight > 0 ? image.implicitWidth / image.implicitHeight : 1
        readonly property real itemAspect: height > 0 ? width / height : 1
        property variant source: image
        property size size: Qt.size(width, height)
        property size crop: imageAspect > itemAspect ? Qt.size(itemAspect / imageAspect, 1) : Qt.size(1, imageAspect / itemAspect)
        property real radius: Math.min(root.radius, width / 2, height / 2)
        property real zoom: root.zoom
        fragmentShader: "qrc:/shaders/cover.frag.qsb"
        opacity: root.ready ? 1 : 0
        scale: root.ready ? 1 : 1.03
        Behavior on opacity { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: Motion.page; easing.type: Easing.OutCubic } }
    }
}
