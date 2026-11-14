import QtQuick
import Scancore

// "Continue on another device": sends where you are to another open app on
// the same account (Live), which offers to open it there. Shown only while
// one is open.
IconButton {
    id: root
    property string seriesId: ""
    // Called first, so what goes out is the position on screen.
    property var save: null

    visible: Live.devices.length > 0 && seriesId !== "" && !App.settings.incognito
    iconName: "arrow-right-left"
    tip: qsTr("Continue on another device")
    onClicked: menu.openAt(root, 0, root.height, Live.devices.map(device => ({
        text: device.name,
        icon: (device.platform === "android" || device.platform === "ios") ? "smartphone" : "monitor",
        run: () => root.send(device)
    })))

    function send(device) {
        if (root.save) root.save()
        if (Live.handoff(device.id, root.seriesId)) Router.toast(qsTr("Sent to %1").arg(device.name), "success")
        else Router.toast(qsTr("Nothing to send yet: read or watch a little first."), "info")
    }

    ActionMenu { id: menu }
}
