import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Opens a site so the reader can pass its browser check once. SCANCORE then
// uses the cookies the site sets for its own requests to that site.
Rectangle {
    id: page
    property string url
    readonly property string host: url.replace(/^https?:\/\//, "").replace(/\/.*$/, "")
    color: Theme.base
    focus: true
    // A shortcut, because the embedded browser keeps keys once it has focus.
    Shortcut { sequence: "Esc"; onActivated: Router.back() }

    Rectangle {
        id: bar
        width: parent.width
        height: 64
        color: Theme.surface
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        Row {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            IconButton { iconName: "arrow-left"; tip: qsTr("Close"); onClicked: Router.back() }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: qsTr("Browser check for ") + page.host
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Complete any check the site shows. When its normal page appears, press Done.")
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            IconButton { iconName: "refresh-cw"; tip: qsTr("Reload"); onClicked: if (loader.item) loader.item.reload() }
            ScButton {
                variant: "primary"
                text: qsTr("Done")
                iconName: "check"
                onClicked: page.finish()
            }
        }
    }

    // Extensions that read this site get the cookies too; pages waiting on
    // it try again (App.siteChecked, and Main.qml says so).
    property bool finished: false
    // Closed without passing: a request waiting on it gives up.
    Component.onDestruction: if (!finished) App.cancelSiteCheck(page.url)
    function finish() {
        if (finished) return
        finished = true
        App.finishSiteCheck(page.url)
        Router.back()
    }
    // The site's clearance arrived: the check is passed, no need for Done.
    Connections {
        target: App
        function onSiteCleared(domain) {
            if (page.host === domain || page.host.endsWith("." + domain)) clearedDelay.restart()
        }
    }
    // A moment for the site's own page to load behind the check.
    Timer { id: clearedDelay; interval: 1200; onTriggered: page.finish() }

    Loader {
        id: loader
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        active: App.embeddedBrowser
        source: "../components/WebCheckView.qml"
        onLoaded: item.startUrl = page.url
    }
    Text {
        anchors.centerIn: parent
        visible: !App.embeddedBrowser
        text: qsTr("This build has no embedded browser. Open ") + page.host + qsTr(" in your browser instead.")
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textMd
    }
}
