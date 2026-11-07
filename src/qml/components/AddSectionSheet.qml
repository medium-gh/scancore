import QtQuick
import QtQuick.Controls.Basic
import Scancore

// "Add section" while arranging a page: sections taken off it, and shelves
// of an installed extension's own listings (its latest releases or what is
// popular there).
Dialog {
    id: dialog
    property PageArranger arranger
    readonly property string pageKey: arranger ? arranger.page : ""

    readonly property var removed: {
        App.dependOn(PageLayout.revision)
        return arranger ? arranger.sections().filter(s => s.removed).map(s => ({ key: s.key, label: s.label })) : []
    }
    // Only while the sheet is open: two options per installed extension
    // (hundreds), which no page should pay for until asked.
    property string query: ""
    readonly property var listings: {
        if (!dialog.visible) return []
        App.dependOn(PageLayout.revision, App.manifestVersion)
        const taken = PageLayout.extras(pageKey).map(e => e.key)
        const out = []
        const add = (service, media) => {
            for (const s of service.sources()) {
                if (!s.extension || !s.enabled || s.canBrowse === false) continue
                const options = (s.browseLatest ? ["latest"] : []).concat(["popular"])
                for (const listing of options) {
                    const key = "src:" + s.key + ":" + listing
                    if (taken.indexOf(key) >= 0) continue
                    out.push({ key: key, source: s.key, listing: listing, media: media, sourceLabel: s.label,
                               label: s.label + " · " + (listing === "latest" ? qsTr("Latest") : qsTr("Popular")) })
                }
            }
        }
        add(Comics, "comic")
        add(Novels, "novel")
        add(Anime, "anime")
        return out
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, (parent ? parent.width : 480) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 640) - 48)
    modal: true
    focus: true
    padding: 24
    Overlay.modal: Rectangle { color: Theme.overlay }
    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    component Option: AbstractButton {
        id: option
        property string label
        property string detail
        property string iconName: "plus"
        width: parent ? parent.width : 300
        height: 52
        hoverEnabled: true
        background: Rectangle {
            radius: Theme.radiusMd
            color: option.hovered ? Theme.surface3 : Theme.surface2
            border.width: 1
            border.color: Theme.border
        }
        contentItem: Item {
            Icon { id: optionIcon; x: 14; anchors.verticalCenter: parent.verticalCenter; name: option.iconName; size: 16; color: Theme.accent }
            Column {
                anchors.left: optionIcon.right
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: option.label
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: option.detail
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
            }
        }
    }

    contentItem: Flickable {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        implicitHeight: body.height
        contentHeight: body.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll {}
        ScrollBar.vertical: ScScrollBar {}

        Column {
            id: body
            width: parent.width
            spacing: 10

            Row {
                width: parent.width
                Text {
                    width: parent.width - closeButton.width
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Add a section")
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                }
                IconButton { id: closeButton; iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
            }

            Text {
                visible: dialog.removed.length > 0
                text: qsTr("Removed from this page")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                topPadding: 4
            }
            Repeater {
                model: dialog.removed
                Option {
                    required property var modelData
                    label: modelData.label
                    iconName: "rotate-ccw"
                    onClicked: PageLayout.setRemoved(dialog.pageKey, modelData.key, false)
                }
            }

            Text {
                text: qsTr("From your extensions")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                topPadding: 4
            }
            SearchField {
                visible: dialog.listings.length > 12
                width: parent.width
                placeholderText: qsTr("Find an extension")
                text: dialog.query
                onTextChanged: dialog.query = text
            }
            Text {
                visible: dialog.listings.length === 0
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Install an extension that can be browsed to add its latest releases or popular titles here.")
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            Repeater {
                // The first 30, or what the search finds.
                model: {
                    const q = dialog.query.trim().toLowerCase()
                    return q.length ? dialog.listings.filter(l => l.label.toLowerCase().indexOf(q) >= 0).slice(0, 60)
                                    : dialog.listings.slice(0, 30)
                }
                Option {
                    required property var modelData
                    label: modelData.label
                    detail: modelData.media === "anime" ? qsTr("Anime") : modelData.media === "novel" ? qsTr("Novels") : qsTr("Comics")
                    onClicked: {
                        PageLayout.addExtra(dialog.pageKey, { key: modelData.key, source: modelData.source, listing: modelData.listing,
                                                              media: modelData.media, label: modelData.label })
                        Router.toast(qsTr("Added %1").arg(modelData.label), "success")
                    }
                }
            }
            Text {
                visible: !dialog.query.trim().length && dialog.listings.length > 30
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("%1 more: search to find one.").arg(dialog.listings.length - 30)
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
        }
    }
}
