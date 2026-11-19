import QtQuick
import QtQuick.Controls.Basic
import Scancore

// A catalogue shelf's whole list ("See all" on Comics and Novels), a page at
// a time as it scrolls, as Comick's own "See more" goes on. A list by period
// (trending, most followed new comics) has the week, month or three months.
Item {
    id: page
    property string media: "comic"
    property string listKey
    property bool periods: false
    property string period: "7"
    readonly property var service: media === "novel" ? Novels : Comics
    readonly property bool novel: media === "novel"
    // Lists in an order of their own show each title's place in it.
    readonly property bool ranked: ["newest", "completed", "season"].indexOf(listKey) < 0

    property var items: []
    property int pageNumber: 0
    property bool hasNext: true
    property var task: null
    property string label: ""
    // Ignores pages of a period the user has since left.
    property int generation: 0
    readonly property var places: {
        const out = {}
        items.forEach((it, i) => out[it.catalogId || it.hid || it.title] = i + 1)
        return out
    }

    function reload() {
        generation += 1
        items = []
        pageNumber = 0
        hasNext = true
        task = null
        more()
        flick.scrollTo(0)
    }
    // Scrolling asks for the next page; after a failure only "Try again" does.
    function more(retry) {
        if ((task && task.loading) || !hasNext) return
        if (!retry && task && !task.ok) return
        const g = generation
        const next = pageNumber + 1
        task = page.service.catalogList(listKey, next, periods ? period : "")
        task.finished.connect((r) => {
            if (g !== page.generation) return
            page.pageNumber = next
            if (r.label) page.label = r.label
            // A title that moved up a place between two pages comes once.
            const seen = {}
            for (const it of page.items) seen[it.catalogId || it.hid || it.title] = true
            const fresh = r.items.filter(it => !seen[it.catalogId || it.hid || it.title])
            page.hasNext = r.hasNext && fresh.length > 0
            if (fresh.length) page.items = page.items.concat(fresh)
        })
    }

    Component.onCompleted: reload()
    ReadFromDialog { id: readFrom }

    SmoothFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 60
        // The next page loads before the end comes into view.
        onContentYChanged: if (contentY + height > contentHeight - 1200 && page.items.length) page.more()

        Column {
            id: column
            width: parent.width
            spacing: 20

            PageHeader {
                title: Router.listTitle(page.listKey)
                subtitle: page.label ? qsTr("From %1's catalogue").arg(page.label) : ""
                showBack: true
            }

            Item {
                visible: page.periods
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                height: periodSwitch.height
                Segmented {
                    id: periodSwitch
                    options: [{ value: "7", label: qsTr("This week") },
                              { value: "30", label: qsTr("This month") },
                              { value: "90", label: qsTr("Last 3 months") }]
                    value: page.period
                    onPicked: (v) => {
                        if (v === page.period) return
                        page.period = v
                        page.reload()
                    }
                }
            }

            CoverGrid {
                items: page.items
                subtitleFor: (it) => page.novel ? [it.type, it.status].filter(s => s).join(" · ")
                    : [it.type, it.status, it.lastChapter ? qsTr("Ch. ") + it.lastChapter : ""].filter(s => s).join(" · ")
                badgeFor: (it) => page.ranked ? "#" + (page.places[it.catalogId || it.hid || it.title] || "") : ""
                onOpened: (it, cover) => readFrom.start(it)
            }

            StateView {
                width: parent.width
                visible: page.items.length === 0
                task: page.task
                empty: page.task !== null && page.task.ok && page.items.length === 0
                emptyTitle: qsTr("Nothing here")
                emptyText: qsTr("The catalogue listed nothing.")
                emptyIcon: "search"
                onRetry: page.reload()
            }

            // The next page.
            Item {
                width: parent.width
                height: 60
                visible: page.items.length > 0 && (page.hasNext || (page.task && !page.task.ok && !page.task.loading))
                Spinner { anchors.centerIn: parent; size: 20; visible: page.task !== null && page.task.loading }
                ScButton {
                    anchors.centerIn: parent
                    visible: page.task !== null && !page.task.loading && !page.task.ok
                    text: qsTr("Try again")
                    onClicked: page.more(true)
                }
            }
        }
    }

    BackToTop { flick: flick }
}
