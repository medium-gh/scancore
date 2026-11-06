import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Offline chapters: queue, progress, failures and stored copies.
Item {
    id: page
    property var task: Downloads.items()
    property var sizeText: Downloads.sizeOnDisk()

    Connections {
        target: Downloads
        function onChanged() { refresh.restart() }
    }
    Timer {
        id: refresh
        interval: 250
        onTriggered: page.task = Downloads.items()
    }

    // Running downloads refresh this list several times a second. The rows
    // are replaced only when downloads come, go or change state; page counts
    // are read from `progress`, so rows (and the scroll position) stay put.
    property var items: []
    property var progress: ({})
    property string shape: ""
    Connections {
        target: page.task
        function onFinished(result) {
            const counts = {}
            for (const i of result) counts[i.id] = { done: i.done, total: i.total }
            page.progress = counts
            const shape = JSON.stringify(result.map(i => [i.id, i.status, i.error]))
            if (shape !== page.shape) {
                page.shape = shape
                page.items = result
            }
        }
    }
    // Measuring walks the whole downloads folder, so only when what is saved
    // changes, not on every page of a running download.
    // Chapters read from Mihon's folder come as one row per series.
    readonly property int savedCount: items.reduce((n, i) => n + (i.status === "done" ? (i.linked || 1) : 0), 0)
    onSavedCountChanged: sizeText = Downloads.sizeOnDisk()
    readonly property int linkedCount: items.reduce((n, i) => n + (i.linked || 0), 0)
    readonly property int failedCount: items.filter(i => i.status === "failed").length

    // One row per series: its chapters or episodes open under it. Series
    // with something running or waiting come first, as their downloads do.
    property var expanded: ({})
    function toggle(seriesId) {
        const next = Object.assign({}, expanded)
        if (next[seriesId]) delete next[seriesId]
        else next[seriesId] = true
        expanded = next
    }
    // Chapter and episode order, by the first number in their titles.
    function unitOrder(a, b) {
        const na = parseFloat(((a.unitTitle || "").match(/\d+(\.\d+)?/) || ["NaN"])[0])
        const nb = parseFloat(((b.unitTitle || "").match(/\d+(\.\d+)?/) || ["NaN"])[0])
        if (!isNaN(na) && !isNaN(nb) && na !== nb) return na - nb
        return (a.unitTitle || "") < (b.unitTitle || "") ? -1 : (a.unitTitle || "") > (b.unitTitle || "") ? 1 : 0
    }
    readonly property var groups: {
        const list = []
        const bySeries = {}
        for (const i of items) {
            // Mihon's folder is already one row per series.
            const key = i.linked ? "linked:" + i.seriesId : i.seriesId
            let g = bySeries[key]
            if (!g) {
                g = bySeries[key] = { seriesId: i.seriesId, first: i, items: [] }
                list.push(g)
            }
            g.items.push(i)
        }
        for (const g of list) g.items.sort(unitOrder)
        return list
    }
    readonly property var rows: {
        const out = []
        for (const g of groups) {
            if (g.items.length === 1) {
                out.push({ kind: "item", item: g.items[0], nested: false })
                continue
            }
            const open = !!expanded[g.seriesId]
            out.push({ kind: "group", group: g, open: open })
            if (open)
                for (const i of g.items) out.push({ kind: "item", item: i, nested: true })
        }
        return out
    }
    property var removing: null
    ConfirmDialog {
        id: removeAll
        heading: page.removing ? qsTr("Delete %1 downloads?").arg(page.removing.items.length) : ""
        message: !page.removing ? ""
            : page.removing.first.media === "anime" ? qsTr("Every downloaded episode of %1 is deleted from this device.").arg(page.removing.first.seriesTitle || "")
            : qsTr("Every downloaded chapter of %1 is deleted from this device.").arg(page.removing.first.seriesTitle || "")
        confirmText: qsTr("Delete")
        destructive: true
        onConfirmed: {
            for (const i of page.removing.items) Downloads.remove(i.id)
            page.removing = null
        }
        onDeclined: page.removing = null
    }

    ListView {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        WheelScroll {}
        id: list
        anchors.fill: parent
        model: page.rows
        clip: true
        bottomMargin: 40
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScScrollBar {}

        header: Column {
            width: list.width
            spacing: 14
            PageHeader {
                title: qsTr("Downloads")
                subtitle: (page.savedCount - page.linkedCount) + qsTr(" saved · ") + page.sizeText
                    + (page.linkedCount > 0 ? " · " + qsTr("%1 in Mihon's folder").arg(page.linkedCount) : "")
                Row {
                    spacing: 8
                    ScButton { visible: page.failedCount > 0; text: qsTr("Retry failed"); iconName: "refresh-cw"; onClicked: Downloads.retryFailed() }
                    ScButton { visible: page.failedCount > 0; text: qsTr("Clear failed"); variant: "ghost"; onClicked: Downloads.clearFinished() }
                    ScButton { visible: App.platform !== "android"; text: qsTr("Open folder"); iconName: "folder-open"; onClicked: App.openFolder(Downloads.folder()) }
                }
            }
            // Android pauses downloads soon after the app leaves the screen
            // while battery optimisation is on.
            Rectangle {
                visible: App.platform === "android" && App.batteryRestricted
                x: Theme.gutter
                width: list.width - 2 * Theme.gutter
                height: visible ? batteryRow.implicitHeight + 24 : 0
                radius: Theme.radiusLg
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                Row {
                    id: batteryRow
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 28
                    spacing: 12
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "battery-warning"; color: Theme.warning; size: 20 }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 32 - allowBattery.width - 24
                        text: qsTr("Battery optimisation pauses downloads when you leave SCANCORE.")
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                    ScButton { id: allowBattery; anchors.verticalCenter: parent.verticalCenter; compact: true; text: qsTr("Allow"); onClicked: App.askBatteryUnrestricted() }
                }
            }
            StateView {
                width: list.width
                height: visible ? 320 : 0
                task: page.items.length === 0 ? page.task : null
                empty: page.task && page.task.ok && page.items.length === 0
                emptyTitle: qsTr("No downloads yet")
                emptyText: qsTr("On a comic, novel or anime page, use the download button on a chapter or episode, or select several at once. Downloads open without a connection.")
                emptyIcon: "download"
            }
        }

        delegate: Item {
            id: entry
            required property var modelData
            width: list.width
            height: modelData.kind === "group" ? 74 : modelData.nested ? 58 : 74

            Rectangle {
                x: Theme.gutter - 12
                width: parent.width - 2 * Theme.gutter + 24
                height: parent.height - 6
                radius: Theme.radiusLg
                color: hover.hovered ? Theme.surface2 : "transparent"
                Behavior on color { ColorAnimation { duration: Motion.fast } }
            }
            HoverHandler { id: hover }

            // A series with several downloads: tap to see them.
            Item {
                id: group
                readonly property var g: entry.modelData.group || null
                readonly property var first: g ? g.first : ({})
                readonly property int active: g ? g.items.filter(i => i.status === "running" || i.status === "queued").length : 0
                readonly property int failed: g ? g.items.filter(i => i.status === "failed").length : 0
                readonly property int saved: g ? g.items.filter(i => i.status === "done").length : 0
                readonly property var count: {
                    let done = 0, total = 0
                    if (g) for (const i of g.items) {
                        if (i.status !== "running" && i.status !== "queued") continue
                        const c = page.progress[i.id] || { done: i.done, total: i.total }
                        total += 1
                        done += c.total > 0 ? c.done / c.total : 0
                    }
                    return { done: done, total: total }
                }
                visible: g !== null
                anchors.fill: parent

                TapHandler { onTapped: page.toggle(group.g.seriesId) }

                Cover {
                    id: groupCover
                    x: Theme.gutter
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 56
                    radius: 7
                    source: group.first.cover || ""
                }
                Column {
                    anchors.left: groupCover.right
                    anchors.leftMargin: 16
                    anchors.right: groupActions.left
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Text {
                        width: parent.width
                        text: group.first.seriesTitle || ""
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Row {
                        id: groupStatus
                        width: parent.width
                        spacing: 10
                        Rectangle {
                            id: groupBar
                            visible: group.active > 0
                            width: Math.min(120, parent.width * 0.35)
                            height: 4
                            radius: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.surface3
                            Rectangle {
                                width: parent.width * (group.count.total > 0 ? group.count.done / group.count.total : 0)
                                height: parent.height
                                radius: 2
                                color: Theme.accent
                                Behavior on width { NumberAnimation { duration: Motion.normal } }
                            }
                        }
                        Text {
                            text: [
                                group.saved > 0 ? (group.first.media === "anime" ? (group.saved === 1 ? qsTr("1 episode") : qsTr("%1 episodes").arg(group.saved))
                                                   : (group.saved === 1 ? qsTr("1 chapter") : qsTr("%1 chapters").arg(group.saved))) : "",
                                group.active > 0 ? qsTr("%1 downloading").arg(group.active) : "",
                                group.failed > 0 ? qsTr("%1 failed").arg(group.failed) : ""
                            ].filter(Boolean).join(" · ")
                            color: group.failed > 0 && group.active === 0 && group.saved === 0 ? Theme.danger : Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textSm
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, groupStatus.width - (groupBar.visible ? groupBar.width + groupStatus.spacing : 0))
                        }
                    }
                }
                Row {
                    id: groupActions
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.gutter - 6
                    anchors.verticalCenter: parent.verticalCenter
                    IconButton {
                        visible: group.failed > 0
                        iconName: "refresh-cw"
                        tip: qsTr("Retry failed")
                        onClicked: { for (const i of group.g.items) if (i.status === "failed") Downloads.retry(i.id) }
                    }
                    IconButton {
                        iconName: "book-open"
                        tip: qsTr("Open series")
                        onClicked: Router.openSeries({ media: group.first.media, sourceUrl: group.first.sourceUrl, title: group.first.seriesTitle, cover: group.first.cover, source: group.first.source, extra: group.first.extra || {} }, groupCover)
                    }
                    IconButton {
                        iconName: "trash"
                        tip: qsTr("Delete all")
                        onClicked: { page.removing = group.g; removeAll.open() }
                    }
                    Item {
                        width: 34
                        height: 34
                        Icon {
                            anchors.centerIn: parent
                            name: "chevron-down"
                            color: Theme.textMuted
                            rotation: entry.modelData.open ? 180 : 0
                            Behavior on rotation { NumberAnimation { duration: Motion.normal } }
                        }
                    }
                }
            }

            // One download; under its series (nested), only its chapter or
            // episode.
            Item {
                id: row
                readonly property var modelData: entry.modelData.item || ({})
                readonly property bool nested: !!entry.modelData.nested
                readonly property var count: page.progress[modelData.id] || { done: modelData.done, total: modelData.total }
                visible: entry.modelData.kind === "item"
                anchors.fill: parent

                Cover {
                    id: cover
                    visible: !row.nested
                    x: Theme.gutter
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 56
                    radius: 7
                    source: row.nested ? "" : (row.modelData.cover || "")
                }
                Column {
                    id: details
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gutter + 56
                    anchors.right: actions.left
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Text {
                        width: parent.width
                        text: row.nested ? (row.modelData.unitTitle || "")
                            : (row.modelData.seriesTitle || "") + " · "
                              + (row.modelData.linked ? qsTr("%1 chapters").arg(row.modelData.linked) : (row.modelData.unitTitle || ""))
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: row.nested ? Theme.textSm : Theme.textMd
                        font.weight: row.nested ? Font.Medium : Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Row {
                        id: statusRow
                        width: parent.width
                        spacing: 10
                        Rectangle {
                            id: bar
                            visible: row.modelData.status === "running" || row.modelData.status === "queued"
                            width: Math.min(160, details.width * 0.45)
                            height: 4
                            radius: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.surface3
                            Rectangle {
                                width: parent.width * (row.count.total > 0 ? row.count.done / row.count.total : 0)
                                height: parent.height
                                radius: 2
                                color: Theme.accent
                                Behavior on width { NumberAnimation { duration: Motion.normal } }
                            }
                        }
                        Text {
                            text: row.modelData.status === "running" ? (!row.count.total ? qsTr("Starting…")
                                    : row.modelData.media === "anime" ? Math.floor(row.count.done / row.count.total * 100) + "%"
                                    : row.count.done + qsTr(" of ") + row.count.total + qsTr(" pages"))
                                : row.modelData.status === "queued" ? qsTr("Waiting")
                                : row.modelData.status === "failed" ? (row.modelData.error || qsTr("Failed"))
                                : row.modelData.linked ? qsTr("In Mihon's folder")
                                : qsTr("Saved ") + App.relativeTime(row.modelData.createdAt)
                            color: row.modelData.status === "failed" ? Theme.danger : Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: row.nested ? Theme.textXs : Theme.textSm
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, statusRow.width - (bar.visible ? bar.width + statusRow.spacing : 0))
                        }
                    }
                }
                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.gutter - 6
                    anchors.verticalCenter: parent.verticalCenter
                    Spinner { visible: row.modelData.status === "running"; size: 16; anchors.verticalCenter: parent.verticalCenter }
                    IconButton { visible: row.modelData.status === "failed"; iconName: "refresh-cw"; tip: qsTr("Retry"); onClicked: Downloads.retry(row.modelData.id) }
                    IconButton {
                        visible: row.modelData.status === "done" && !row.nested
                        iconName: "book-open"
                        tip: qsTr("Open series")
                        onClicked: Router.openSeries({ media: row.modelData.media, sourceUrl: row.modelData.sourceUrl, title: row.modelData.seriesTitle, cover: row.modelData.cover, source: row.modelData.source, extra: row.modelData.extra || {} }, cover)
                    }
                    // While it runs: it stops, and what it had goes.
                    IconButton {
                        visible: !row.modelData.linked
                        iconName: "trash"
                        tip: row.modelData.status === "running" ? qsTr("Stop and delete") : qsTr("Delete")
                        onClicked: Downloads.remove(row.modelData.id)
                    }
                    // Mihon's files are never deleted from here.
                    IconButton {
                        visible: !!row.modelData.linked
                        iconName: "x"
                        tip: qsTr("Stop reading these from Mihon's folder (the files stay)")
                        onClicked: Downloads.unlinkSeries(row.modelData.seriesId)
                    }
                }
            }
        }
    }

    BackToTop { flick: list }
}
