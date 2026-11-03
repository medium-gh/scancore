import QtQuick
import Scancore

// What a long press or right click on a series' cover offers (Home, the
// library, history, browsing): open it, carry on, and take it out of the
// library or the history. One for the whole window (Router.seriesMenu).
Item {
    id: root
    property var series: ({})

    function show(item, anchor, px, py) {
        if (!item || !anchor) return
        // A stored series: what the library knows of it, so a browse
        // result that was read shows its history too.
        const row = item.id ? Library.seriesRow(item.id) : ({})
        const stored = !!(row && row.id)
        series = stored ? Object.assign({}, item, row) : item
        const read = stored && !!Library.lastProgress(row.id).unitKey
        const saved = stored && Library.isSaved(row.id)
        const anime = series.media === "anime"
        menu.openAt(anchor, px, py, [
            { text: qsTr("Details"), icon: "info", run: () => Router.openSeries(root.series, null) },
            read ? { text: anime ? qsTr("Continue watching") : qsTr("Continue reading"), icon: "play", run: () => Router.resume(root.series) } : undefined,
            stored && !saved ? { text: qsTr("Add to library"), icon: "plus", run: () => root.save() } : undefined,
            saved || read ? null : undefined,
            saved ? { text: qsTr("Remove from library"), icon: "library", run: () => root.unsave() } : undefined,
            read ? { text: qsTr("Remove from history"), icon: "history", danger: true, run: () => forgetAsk.open() } : undefined
        ].filter(i => i !== undefined))
    }

    function save() {
        Library.save(series)
        Router.toast(qsTr("%1 is in your library").arg(series.title || ""), "success")
    }
    function unsave() {
        const kept = series
        const category = Library.category(kept.id) || "reading"
        Library.remove(kept.id)
        Router.toast(qsTr("%1 left your library").arg(kept.title || ""), "info",
                     { label: qsTr("Undo"), run: () => Library.save(kept, category) })
    }

    ActionMenu { id: menu }
    ConfirmDialog {
        id: forgetAsk
        heading: qsTr("Remove %1 from your history?").arg(root.series.title || "")
        message: qsTr("Your place in it is forgotten here and on your other synced devices. Your library and downloads are kept.")
        confirmText: qsTr("Remove")
        destructive: true
        onConfirmed: Library.forget(root.series.id)
    }
}
