import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Everything read or watched recently, grouped by day.
Item {
    id: page
    property string media: ""
    property string filter: ""
    property var task: Library.history("", 300)

    function search(text) { filter = text }

    // The last result, kept while a refresh runs.
    property var all: []
    Connections {
        target: page.task
        function onFinished() { page.all = Router.keepTask(page.all, page.task) }
    }

    // Library changes come every couple of seconds while reading (progress):
    // a page out of sight (another section, under a reader or the player)
    // only notes them and reloads once it shows again.
    property bool libraryStale: false
    onVisibleChanged: if (visible && libraryStale) { libraryStale = false; refresh.restart() }
    Connections {
        target: Library
        function onChanged() { if (page.visible) refresh.restart(); else page.libraryStale = true }
    }
    Timer { id: refresh; interval: 300; onTriggered: page.task = Library.history("", 300) }

    function dayLabel(seconds) {
        const d = new Date(seconds * 1000)
        const today = new Date()
        today.setHours(0, 0, 0, 0)
        const diff = Math.floor((today - new Date(d.getFullYear(), d.getMonth(), d.getDate())) / 86400000)
        return diff <= 0 ? qsTr("Today") : diff === 1 ? qsTr("Yesterday") : diff < 7 ? Qt.formatDate(d, "dddd") : Qt.formatDate(d, "d MMMM yyyy")
    }

    readonly property var rows: {
        const f = filter.trim().toLowerCase()
        const all = page.all
            .filter(e => (!media || e.media === media) && (!f || (e.title || "").toLowerCase().includes(f)))
        const out = []
        let last = ""
        for (const e of all) {
            const label = dayLabel(e.progressAt)
            if (label !== last) {
                out.push({ header: label })
                last = label
            }
            out.push({ entry: e })
        }
        return out
    }

    ListView {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        WheelScroll {}
        id: list
        anchors.fill: parent
        model: page.rows
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: 40
        ScrollBar.vertical: ScScrollBar {}
        // The list arrives after the header is laid out, and a new model
        // left the view below the title (the page opened scrolled past it):
        // until the reader scrolls, stay at the very top.
        property bool touched: false
        onMovementStarted: touched = true
        onModelChanged: if (!touched) Qt.callLater(() => { if (!list.touched) list.positionViewAtBeginning() })

        header: Column {
            width: list.width
            onHeightChanged: if (!list.touched) Qt.callLater(() => { if (!list.touched) list.positionViewAtBeginning() })
            spacing: 18
            PageHeader {
                title: qsTr("History")
                subtitle: qsTr("What you read and watched, newest first")
                ScButton {
                    text: qsTr("Clear history")
                    iconName: "trash"
                    variant: "danger"
                    enabled: page.all.length > 0
                    onClicked: confirm.open()
                }
            }
            Flow {
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                spacing: 12
                Segmented {
                    visible: !Theme.retro
                    options: [{ value: "", label: qsTr("All") }, { value: "comic", label: qsTr("Comics"), icon: "book-open" },
                              { value: "novel", label: qsTr("Novels"), icon: "book-text" }, { value: "anime", label: qsTr("Anime"), icon: "tv" }]
                    value: page.media
                    onPicked: (v) => page.media = v
                }
                // Retro: the same as pivot headers.
                PivotHeaders {
                    visible: Theme.retro
                    width: parent.width
                    height: visible ? implicitHeight : 0
                    options: [{ value: "", label: qsTr("All") }, { value: "comic", label: qsTr("Comics") },
                              { value: "novel", label: qsTr("Novels") }, { value: "anime", label: qsTr("Anime") }]
                    value: page.media
                    onPicked: (v) => page.media = v
                }
                OutsideList {
                    view: list
                    outside: page
                    width: Theme.compact ? parent.width : 260
                    height: 40
                    SearchField {
                        width: parent.width
                        placeholderText: qsTr("Search history")
                        onTextChanged: page.filter = text
                    }
                }
            }
            StateView {
                width: list.width
                height: visible ? 300 : 0
                task: page.all.length === 0 ? page.task : null
                empty: page.task && page.task.ok && page.rows.length === 0
                emptyTitle: page.filter || page.media ? qsTr("Nothing matches") : qsTr("Nothing read yet")
                emptyText: qsTr("Chapters and episodes you open show up here, with your place saved.")
                emptyIcon: "history"
                onRetry: page.task = Library.history("", 300)
            }
            Item { width: 1; height: 4 }
        }

        delegate: Loader {
            required property var modelData
            width: list.width
            sourceComponent: modelData.header ? headerRow : entryRow
            property var row: modelData
        }

        Component {
            id: headerRow
            Item {
                height: 44
                Text {
                    x: Theme.gutter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    text: parent.parent ? parent.parent.row.header : ""
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.2
                }
            }
        }

        Component {
            id: entryRow
            AbstractButton {
                id: item
                readonly property var e: parent ? parent.row.entry : ({})
                height: 76
                hoverEnabled: true
                onClicked: Router.resume(e)
                onPressAndHold: Router.seriesMenu(e, item, item.width / 2, item.height / 2)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: (point) => Router.seriesMenu(item.e, item, point.position.x, point.position.y)
                }
                background: Rectangle {
                    x: Theme.gutter - 12
                    width: item.width - 2 * Theme.gutter + 24
                    radius: Theme.radiusLg
                    color: item.hovered ? Theme.surface2 : "transparent"
                    Behavior on color { ColorAnimation { duration: Motion.fast } }
                }
                contentItem: Item {
                    Cover {
                        id: cover
                        x: Theme.gutter
                        anchors.verticalCenter: parent.verticalCenter
                        width: 42
                        height: 60
                        radius: 7
                        source: item.e.cover || ""
                        title: item.e.title || ""
                    }
                    Column {
                        anchors.left: cover.right
                        anchors.leftMargin: 16
                        anchors.right: actions.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text {
                            width: parent.width
                            text: item.e.title || ""
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textMd
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: (item.e.media === "anime" ? qsTr("Episode ") + (item.e.unitNumber || item.e.unitKey) : (item.e.unitTitle || ""))
                                + " · " + (item.e.completed ? "finished" : Math.round((item.e.percent || 0) * 100) + "%")
                                + " · " + App.relativeTime(item.e.progressAt)
                            color: Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textSm
                            elide: Text.ElideRight
                        }
                    }
                    Row {
                        id: actions
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.gutter - 6
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: item.hovered || Theme.touch ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Motion.fast } }
                        IconButton { iconName: "info"; tip: qsTr("Details"); onClicked: Router.openSeries(item.e, cover) }
                        IconButton { iconName: "x"; tip: qsTr("Remove from history"); onClicked: Library.forget(item.e.id) }
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirm
        heading: qsTr("Clear ") + (page.media ? page.media + " " : "") + qsTr("history?")
        message: qsTr("Your place in every series will be forgotten. Your library and downloads are kept.")
        confirmText: qsTr("Clear")
        destructive: true
        onConfirmed: Library.clearHistory(page.media)
    }

    BackToTop { flick: list }
}
