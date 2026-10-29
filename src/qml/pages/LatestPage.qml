import QtQuick
import QtQuick.Controls.Basic
import Scancore

// What people on the account's site (scancore.onl) read and watch this week,
// and the newest comments and reviews as they come in: the website's Latest
// page (/api/latest). Trending titles open as they do elsewhere: comics
// through "Read from" (Comick), anime by their AniList entry, novels at
// their address. A sync server without the page says so.
Item {
    id: page
    property var task: null
    property var trending: ({ comics: [], anime: [], novels: [] })
    property var activity: []
    property int refreshSeconds: 20
    // "loading", "ready", "error" or "unavailable".
    property string state: "loading"
    property string problem: ""
    // Ignores answers that a newer load or refresh replaced.
    property int generation: 0

    function load() {
        const mine = ++generation
        state = "loading"
        if (Account.customSite) {
            const check = Account.request("GET", "/api/capabilities")
            check.finished.connect((caps) => { if (mine === page.generation) caps.latest ? page.fetch(mine) : page.state = "unavailable" })
            check.failed.connect(() => { if (mine === page.generation) page.state = "unavailable" })
            return
        }
        fetch(mine)
    }
    function fetch(mine) {
        task = Account.request("GET", "/api/latest")
        task.finished.connect((result) => {
            if (mine !== page.generation) return
            page.trending = result.trending || { comics: [], anime: [], novels: [] }
            page.activity = result.activity || []
            page.refreshSeconds = Math.max(10, Number(result.refreshSeconds) || 20)
            page.state = "ready"
        })
        task.failed.connect((message) => {
            if (mine !== page.generation) return
            page.problem = message
            page.state = "error"
        })
    }

    // Comments and reviews posted since the newest one shown, while the
    // page is in sight and the app in front.
    property bool refreshing: false
    function refreshActivity() {
        if (refreshing || state !== "ready") return
        const newest = activity.length ? activity[0].at : new Date(Date.now() - 7 * 86400000).toISOString()
        const mine = generation
        refreshing = true
        const more = Account.request("GET", "/api/latest?after=" + encodeURIComponent(newest))
        more.finished.connect((result) => {
            page.refreshing = false
            if (mine !== page.generation) return
            const fresh = (result.activity || []).filter(a => !page.activity.some(b => b.kind === a.kind && b.id === a.id))
            if (fresh.length) page.activity = fresh.concat(page.activity).slice(0, 60)
        })
        more.failed.connect(() => page.refreshing = false)
    }
    Timer {
        interval: page.refreshSeconds * 1000
        repeat: true
        running: page.visible && page.state === "ready" && Qt.application.state === Qt.ApplicationActive
        onTriggered: page.refreshActivity()
    }
    onVisibleChanged: if (visible && state === "ready") refreshActivity()
    Connections {
        target: Account
        function onChanged() { if (page.lastSite !== Account.site) { page.lastSite = Account.site; page.load() } }
    }
    property string lastSite: Account.site
    Component.onCompleted: load()

    function siteName() { return String(Account.site || "").replace(/^https?:\/\//, "").replace(/\/.*$/, "") }
    function avatar(url) { return !url ? "" : url.startsWith("/") ? Account.site + url : url }

    ReadFromDialog { id: readFrom }
    function openTitle(it) {
        const media = it.mediaType === "ANIME" ? "anime" : it.mediaType === "NOVEL" ? "novel" : "comic"
        if (media === "anime") {
            if (it.anilistId) Router.openSeries({ media: "anime", anilistId: it.anilistId, title: it.title, cover: it.cover || "" })
            else page.searchFor("anime", it.title)
        } else if (media === "novel" && it.novelUrl) {
            Router.openSeries({ media: "novel", sourceUrl: it.novelUrl, title: it.title, cover: it.cover || "" })
        } else {
            readFrom.start({ media: media, catalog: it.comickHid ? "comick" : "", catalogId: it.comickHid || "", hid: it.comickHid || "",
                             title: it.title, titles: [it.title], cover: it.cover || "" })
        }
    }
    function searchFor(section, title) {
        Router.go(section)
        Qt.callLater(() => {
            const stack = Router.shell ? Router.shell.stackFor(section) : null
            if (stack && stack.depth > 1) stack.pop(null)
            const first = stack ? stack.get(0) : null
            if (first && first.search) first.search(title)
        })
    }

    SmoothFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 48

        Column {
            id: column
            width: parent.width
            spacing: 26

            PageHeader {
                keepBeside: true
                title: qsTr("Latest")
                subtitle: qsTr("What people on %1 read and watch this week").arg(page.siteName())
                IconButton {
                    iconName: "refresh-cw"
                    tip: qsTr("Refresh")
                    enabled: page.state !== "loading"
                    onClicked: page.load()
                }
            }

            StateView {
                width: parent.width
                loading: page.state === "loading"
                error: page.state === "error" ? page.problem : ""
                empty: page.state === "unavailable"
                    || (page.state === "ready" && !page.trending.comics.length && !page.trending.anime.length
                        && !page.trending.novels.length && !page.activity.length)
                emptyIcon: page.state === "unavailable" ? "server" : "flame"
                emptyTitle: page.state === "unavailable" ? qsTr("Not on this server") : qsTr("Quiet this week")
                emptyText: page.state === "unavailable"
                    ? qsTr("Your sync server has no Latest page. It is on scancore.onl.")
                    : qsTr("Nobody has read, watched or reviewed anything here this week yet.")
                onRetry: page.load()
            }

            // Wide windows: the feed in a column of its own beside the
            // trending titles, as on the website; otherwise below them.
            Item {
                id: content
                readonly property bool wide: !Theme.compact && width >= 1100
                readonly property int feedWidth: 400
                width: parent.width
                height: !feed.visible ? trends.height
                    : wide ? Math.max(trends.height, feed.height) : trends.height + (trends.height > 0 ? 26 : 0) + feed.height

                Column {
                    id: trends
                    width: content.wide && feed.visible ? parent.width - content.feedWidth - Theme.gutter : parent.width
                    spacing: 26
                    Shelf {
                        visible: page.state === "ready" && items.length > 0
                        title: qsTr("Trending comics")
                        items: page.trending.comics || []
                        subtitleFor: (it) => it.people === 1 ? qsTr("1 reading") : qsTr("%1 reading").arg(it.people)
                        onOpened: (it) => page.openTitle(it)
                    }
                    Shelf {
                        visible: page.state === "ready" && items.length > 0
                        title: qsTr("Trending anime")
                        items: page.trending.anime || []
                        subtitleFor: (it) => it.people === 1 ? qsTr("1 watching") : qsTr("%1 watching").arg(it.people)
                        onOpened: (it) => page.openTitle(it)
                    }
                    Shelf {
                        visible: page.state === "ready" && items.length > 0
                        title: qsTr("Trending novels")
                        items: page.trending.novels || []
                        subtitleFor: (it) => it.people === 1 ? qsTr("1 reading") : qsTr("%1 reading").arg(it.people)
                        onOpened: (it) => page.openTitle(it)
                    }
                }

                Column {
                    id: feed
                    visible: page.state === "ready" && page.activity.length > 0
                    x: content.wide ? trends.width : Theme.gutter
                    y: content.wide ? 0 : trends.height + (trends.height > 0 ? 26 : 0)
                    width: content.wide ? content.feedWidth : Math.min(parent.width - 2 * Theme.gutter, 820)
                    spacing: 12

                    Text {
                        text: qsTr("Comments and reviews")
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.textXl
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: page.activity
                        delegate: AbstractButton {
                            id: entry
                            required property var modelData
                            width: parent.width
                            height: Math.max(art.height, body.implicitHeight) + 28
                            hoverEnabled: true
                            onClicked: page.openTitle(modelData.series)
                            Accessible.name: modelData.user.username + ": " + modelData.series.title
                            background: Rectangle {
                                radius: Theme.radiusLg
                                color: entry.pressed ? Theme.surface2 : Theme.surface
                                border.width: 1
                                border.color: entry.hovered ? Theme.borderStrong : Theme.border
                            }
                            contentItem: Item {
                                Cover {
                                    id: art
                                    x: 14
                                    y: 14
                                    width: 44
                                    height: 66
                                    radius: Theme.radiusSm
                                    source: entry.modelData.series.cover || ""
                                    title: entry.modelData.series.title
                                }
                                Column {
                                    id: body
                                    x: art.x + art.width + 14
                                    y: 14
                                    width: parent.width - x - 14
                                    spacing: 5
                                    Row {
                                        width: parent.width
                                        spacing: 8
                                        Rectangle {
                                            id: face
                                            width: 22
                                            height: 22
                                            radius: 11
                                            color: Theme.accent
                                            anchors.verticalCenter: parent.verticalCenter
                                            Text {
                                                anchors.centerIn: parent
                                                visible: !photo.visible
                                                text: String(entry.modelData.user.username || "?").slice(0, 1).toUpperCase()
                                                color: Theme.accentInk
                                                font.family: Theme.fontSans
                                                font.pixelSize: 11
                                                font.weight: Font.Bold
                                            }
                                            Cover {
                                                id: photo
                                                anchors.fill: parent
                                                radius: 11
                                                source: page.avatar(entry.modelData.user.avatarUrl)
                                                visible: source.length > 0 && ready
                                            }
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Math.min(implicitWidth, parent.width - face.width - when.width - 16)
                                            elide: Text.ElideRight
                                            text: entry.modelData.user.username
                                            color: Theme.text
                                            font.family: Theme.fontSans
                                            font.pixelSize: Theme.textSm
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            id: when
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: (entry.modelData.kind === "review" ? qsTr("reviewed") : qsTr("commented")) + " · " + App.relativeTime(entry.modelData.at)
                                            color: Theme.textFaint
                                            font.family: Theme.fontSans
                                            font.pixelSize: Theme.textXs + 1
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        elide: Text.ElideRight
                                        text: entry.modelData.series.title + (entry.modelData.unit ? " · " + entry.modelData.unit : "")
                                        color: Theme.textMuted
                                        font.family: Theme.fontSans
                                        font.pixelSize: Theme.textSm
                                        font.weight: Font.Medium
                                    }
                                    Row {
                                        visible: entry.modelData.kind === "review" && entry.modelData.rating > 0
                                        spacing: 2
                                        Repeater {
                                            model: 5
                                            Icon {
                                                required property int index
                                                readonly property bool lit: index < entry.modelData.rating
                                                name: lit ? "star-filled" : "star"
                                                size: 13
                                                color: lit ? Theme.starColor : Theme.textFaint
                                            }
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        text: entry.modelData.body
                                        color: Theme.text
                                        font.family: Theme.fontSans
                                        font.pixelSize: Theme.textSm
                                        lineHeight: 1.2
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 5
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
