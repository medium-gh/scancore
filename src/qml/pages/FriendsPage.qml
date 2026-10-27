import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Friends on the account's site (scancore.onl): following each other by
// username makes friends; what friends read and watch shows here once they
// share it; a library list can be shared by link, and a shared list opened
// and added to the library whole. All through the site's /api/app/friends,
// /api/app/activity and /api/app/lists.
Item {
    id: page

    property var friendsTask: Account.signedIn ? Account.request("GET", "/api/app/friends") : null
    property var feedTask: Account.signedIn ? Account.request("GET", "/api/app/activity") : null
    property var listsTask: Account.signedIn ? Account.request("GET", "/api/app/lists") : null
    readonly property var people: friendsTask && friendsTask.ok ? friendsTask.result : ({ shareActivity: false, friends: [], sent: [], received: [] })
    readonly property var events: feedTask && feedTask.ok ? feedTask.result.events : []
    readonly property var myLists: listsTask && listsTask.ok ? listsTask.result.lists : []
    // Requests in flight, held so they are not collected before they answer.
    property var pending: []
    // A shared list opened by its code: {code, title, owner, items}.
    property var opened: null

    function reload() {
        friendsTask = Account.request("GET", "/api/app/friends")
        feedTask = Account.request("GET", "/api/app/activity")
        listsTask = Account.request("GET", "/api/app/lists")
    }
    function send(method, path, body, done) {
        const task = Account.request(method, path, body || {})
        pending = pending.concat([task])
        task.finished.connect((result) => {
            page.pending = page.pending.filter(t => t !== task)
            if (done) done(result)
            page.reload()
        })
        task.failed.connect((error) => {
            page.pending = page.pending.filter(t => t !== task)
            Router.toast(error, "error")
        })
    }
    function linkFor(code) { return String(Account.site).replace(/\/$/, "") + "/lists/" + code }

    function when(iso) {
        const minutes = Math.round((Date.now() - new Date(iso).getTime()) / 60000)
        if (minutes < 60) return minutes <= 1 ? qsTr("just now") : qsTr("%1 min ago").arg(minutes)
        if (minutes < 1440) return qsTr("%1 h ago").arg(Math.round(minutes / 60))
        return qsTr("%1 days ago").arg(Math.round(minutes / 1440))
    }
    function line(e) {
        const p = e.payload || {}
        const who = e.user.displayName || "@" + e.user.username
        if (e.type === "shared") return qsTr("%1 shared a list: %2").arg(who).arg(p.title)
        const what = p.title + (p.unit ? " · " + p.unit : "")
        return e.type === "watched" ? qsTr("%1 watched %2").arg(who).arg(what)
                                    : qsTr("%1 read %2").arg(who).arg(what)
    }

    // A library list, as the site keeps it.
    function shareList(category) {
        const task = Library.entries("", true)
        pending = pending.concat([task])
        task.finished.connect((entries) => {
            page.pending = page.pending.filter(t => t !== task)
            const chosen = entries.filter(e => (e.category || "reading") === category.value)
            if (chosen.length === 0) { Router.toast(qsTr("That list is empty."), "info"); return }
            const items = chosen.slice(0, 500).map(e => ({ title: e.title, media: e.media, source: e.source || "",
                                                           sourceUrl: e.sourceUrl || "", cover: e.cover || "",
                                                           anilistId: Router.anilistIdOf(e) }))
            page.send("POST", "/api/app/lists", { title: category.label, items: items }, (result) => {
                App.copy(page.linkFor(result.code))
                Router.toast(qsTr("Shared. The link is copied: %1").arg(page.linkFor(result.code)), "success")
            })
        })
    }
    function openList(text) {
        const code = String(text).trim().replace(/^.*\/lists\//, "").replace(/[^a-zA-Z0-9]/g, "").toLowerCase()
        if (!code) return
        send("GET", "/api/app/lists/" + code, null, (result) => page.opened = result)
    }
    // Every series of the opened list that has an address goes into the
    // library (Reading); the rest are named.
    function addAll() {
        let added = 0, skipped = 0
        for (const item of opened.items) {
            let id = ""
            if (item.media === "anime" && item.anilistId) id = Anime.seriesId(item.anilistId)
            else if (item.media === "novel" && item.sourceUrl) id = Novels.seriesId(item.sourceUrl)
            else if (item.sourceUrl) id = Comics.seriesId(item.sourceUrl)
            // Ids are "<media>:<source>:<address>": a series on none of
            // this app's sources has no source and cannot be added.
            const source = String(id).split(":")[1] || ""
            if (!id || !source) { ++skipped; continue }
            if (!Library.isSaved(id)) {
                Library.save({ id: id, media: item.media, title: item.title, cover: item.cover || "", sourceUrl: item.sourceUrl || "",
                               source: source, extra: item.anilistId ? { anilistId: item.anilistId } : {} })
                if (Library.isSaved(id)) ++added
                else ++skipped
            }
        }
        Router.toast(skipped > 0 ? qsTr("%1 added to your library; %2 could not be found on your sources.").arg(added).arg(skipped)
                     : added === 0 ? qsTr("All of it is in your library already.")
                     : qsTr("%1 added to your library.").arg(added), "success")
    }

    SmoothFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 48

        Column {
            id: column
            width: flick.width
            spacing: 18

            PageHeader {
                title: qsTr("Friends")
                subtitle: qsTr("Follow each other on %1 to be friends").arg(String(Account.site).replace(/^https?:\/\//, ""))
                showBack: true
            }

            Text {
                visible: !Account.signedIn
                x: Theme.gutter
                width: parent.width - 2 * Theme.gutter
                text: qsTr("Sign in (Settings → Sync) to add friends, see what they read and share lists.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                wrapMode: Text.Wrap
            }

            SettingsGroup {
                visible: Account.signedIn
                title: qsTr("You")
                SettingRow {
                    iconName: "users"
                    title: qsTr("Share what I read and watch")
                    description: qsTr("Your friends see the series you read and watch, and the lists you share. Off: they see nothing.")
                    Toggle {
                        checked: page.people.shareActivity === true
                        onToggled: page.send("PATCH", "/api/app/friends", { shareActivity: checked })
                    }
                }
                SettingRow {
                    iconName: "users"
                    title: qsTr("Add a friend")
                    description: qsTr("Their username; when they add you too, you are friends. You are @%1.").arg(Account.user.username || "")
                    Row {
                        spacing: 8
                        InlineField {
                            id: username
                            width: 180
                            placeholderText: qsTr("username")
                            onAccepted: addButton.clicked()
                        }
                        ScButton {
                            id: addButton
                            text: qsTr("Add")
                            enabled: username.text.trim().length > 0
                            onClicked: page.send("POST", "/api/app/friends", { username: username.text }, (p) => {
                                Router.toast(qsTr("You follow @%1.").arg(p.username), "success")
                                username.text = ""
                            })
                        }
                    }
                }
            }

            SettingsGroup {
                visible: page.people.received.length > 0
                title: qsTr("Asking to be friends")
                Repeater {
                    model: page.people.received.slice(0, 50)
                    delegate: SettingRow {
                        required property var modelData
                        title: modelData.displayName || "@" + modelData.username
                        description: modelData.displayName ? "@" + modelData.username : qsTr("Added you")
                        ScButton { text: qsTr("Add back"); onClicked: page.send("POST", "/api/app/friends", { username: modelData.username }) }
                    }
                }
            }

            SettingsGroup {
                visible: Account.signedIn
                title: qsTr("Friends")
                note: page.people.friends.length === 0 ? qsTr("No friends yet.") : ""
                Repeater {
                    model: page.people.friends.slice(0, 100)
                    delegate: SettingRow {
                        required property var modelData
                        title: modelData.displayName || "@" + modelData.username
                        description: modelData.displayName ? "@" + modelData.username : ""
                        ScButton { text: qsTr("Remove"); variant: "ghost"; onClicked: page.send("DELETE", "/api/app/friends?user=" + modelData.id) }
                    }
                }
                Repeater {
                    model: page.people.sent.slice(0, 50)
                    delegate: SettingRow {
                        required property var modelData
                        title: modelData.displayName || "@" + modelData.username
                        description: qsTr("Waiting for them to add you")
                        ScButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: page.send("DELETE", "/api/app/friends?user=" + modelData.id) }
                    }
                }
            }

            SettingsGroup {
                visible: Account.signedIn
                title: qsTr("What friends read")
                note: page.events.length === 0 ? qsTr("Nothing yet: friends show here once they share what they read.") : ""
                Repeater {
                    model: page.events.slice(0, 100)
                    delegate: SettingRow {
                        required property var modelData
                        iconName: modelData.type === "watched" ? "tv" : modelData.type === "shared" ? "share-2" : "book-open"
                        title: page.line(modelData)
                        description: page.when(modelData.at) + (modelData.payload && modelData.payload.source ? " · " + modelData.payload.source : "")
                        ScButton {
                            visible: !!(modelData.payload && modelData.payload.sourceUrl)
                            text: qsTr("Open")
                            variant: "ghost"
                            onClicked: Router.openSeries({ media: modelData.payload.media, sourceUrl: modelData.payload.sourceUrl,
                                                           title: modelData.payload.title, cover: modelData.payload.cover || "" })
                        }
                    }
                }
            }

            SettingsGroup {
                visible: Account.signedIn
                title: qsTr("Shared lists")
                SettingRow {
                    iconName: "share-2"
                    title: qsTr("Share a list from your library")
                    description: qsTr("Anyone with the link sees it, and can add it all to their library in SCANCORE.")
                    Row {
                        spacing: 8
                        Select {
                            id: listPick
                            model: Router.libraryCategories(App.settings.libraryMedia || "comic")
                            value: "reading"
                        }
                        ScButton {
                            text: qsTr("Share")
                            onClicked: page.shareList(Router.libraryCategories(App.settings.libraryMedia || "comic").find(c => c.value === listPick.value)
                                                      || { value: listPick.value, label: listPick.value })
                        }
                    }
                }
                SettingRow {
                    iconName: "link"
                    title: qsTr("Open a shared list")
                    description: qsTr("Its link or code.")
                    Row {
                        spacing: 8
                        InlineField { id: listCode; width: 200; placeholderText: qsTr("link or code"); onAccepted: page.openList(text) }
                        ScButton { text: qsTr("Open"); enabled: listCode.text.trim().length > 0; onClicked: page.openList(listCode.text) }
                    }
                }
                SettingRow {
                    visible: page.opened !== null
                    iconName: "library"
                    title: page.opened ? page.opened.title : ""
                    description: page.opened ? qsTr("%1 series from @%2: %3").arg(page.opened.items.length).arg(page.opened.owner.username)
                                                   .arg(page.opened.items.slice(0, 4).map(i => i.title).join(", ") + (page.opened.items.length > 4 ? "…" : "")) : ""
                    ScButton { text: qsTr("Add all to my library"); variant: "primary"; onClicked: page.addAll() }
                }
                Repeater {
                    model: page.myLists
                    delegate: SettingRow {
                        required property var modelData
                        title: modelData.title
                        description: qsTr("%1 series · shared %2").arg(modelData.count).arg(page.when(modelData.at))
                        Row {
                            spacing: 8
                            ScButton {
                                text: qsTr("Copy link")
                                variant: "ghost"
                                onClicked: { App.copy(page.linkFor(modelData.code)); Router.toast(qsTr("Link copied"), "success") }
                            }
                            ScButton { text: qsTr("Stop sharing"); variant: "ghost"; onClicked: page.send("DELETE", "/api/app/lists?code=" + modelData.code) }
                        }
                    }
                }
            }
        }
    }
}
