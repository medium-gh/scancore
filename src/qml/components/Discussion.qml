import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Comments of a series, or of one of its chapters or episodes, from the
// account's site (scancore.onl): the same threads as the website. Anyone can
// read them; signed-in accounts can post, reply and vote.
Column {
    id: discussion
    // "comic" | "novel" | "anime"
    property string media: "comic"
    property string title
    // Catalogue ids that tie the thread to the series on the website.
    property string comickHid
    property int anilistId: 0
    property string url
    // A chapter or episode number; empty for the series discussion.
    property string unit
    property bool active: true

    property var thread: null
    property var comments: []
    property string state: "loading"     // loading | ready | error | unavailable
    property string problem
    property string sort: "TOP"
    property string replyTo
    // The reader's own comment being edited (its id).
    property string editing
    property var postTask: null

    spacing: 14

    function query(params) {
        return Object.keys(params).filter(k => params[k] !== undefined && params[k] !== null && params[k] !== "")
            .map(k => encodeURIComponent(k) + "=" + encodeURIComponent(params[k])).join("&")
    }

    // Only the website keeps comments; a SCANCORE sync server does not.
    function load() {
        if (!active || !title) return
        state = "loading"
        if (Account.customSite) {
            const check = Account.request("GET", "/api/capabilities")
            check.finished.connect((caps) => caps.discussions ? discussion.resolve() : discussion.state = "unavailable")
            check.failed.connect(() => discussion.state = "unavailable")
            return
        }
        resolve()
    }

    function resolve() {
        const task = Account.request("GET", "/api/discussions/resolve?" + query({
            media: media, title: title.slice(0, 200), comickHid: comickHid,
            anilistId: anilistId > 0 ? anilistId : "", url: /^https?:/.test(url) ? url : "", unit: unit
        }))
        task.finished.connect((found) => {
            discussion.thread = found
            discussion.refresh()
        })
        task.failed.connect((message) => {
            discussion.state = "error"
            discussion.problem = message
        })
    }

    function refresh() {
        if (!thread) return load()
        const task = Account.request("GET", "/api/comments?" + query({
            seriesName: thread.seriesName, chapterName: thread.chapterName, seriesId: thread.seriesId,
            unitId: thread.unitId, mediaType: thread.mediaType, thread: thread.unitId || (!thread.seriesId && unit) ? "chapter" : "series",
            sort: sort
        }))
        task.finished.connect((result) => {
            discussion.comments = result.comments || []
            discussion.state = "ready"
        })
        task.failed.connect((message) => {
            discussion.state = "error"
            discussion.problem = message
        })
    }

    function post(body, parentId) {
        if (!thread || !body.trim()) return null
        const input = {
            seriesName: thread.seriesName, chapterName: thread.chapterName, mediaType: thread.mediaType,
            body: body.trim(), parentId: parentId || null, submissionId: uuid(),
            thread: thread.unitId || (!thread.seriesId && unit) ? "chapter" : "series"
        }
        if (thread.seriesId) input.seriesId = thread.seriesId
        if (thread.unitId) input.unitId = thread.unitId
        postTask = Account.request("POST", "/api/comments", input)
        postTask.finished.connect(() => { discussion.replyTo = ""; discussion.refresh() })
        postTask.failed.connect((message) => Router.toast(message, "error"))
        return postTask
    }

    // Lets the site recognise a post sent twice (a retry) as one.
    function uuid() {
        return "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".replace(/[xy]/g, (ch) => {
            const r = Math.random() * 16 | 0
            return (ch === "x" ? r : (r & 3 | 8)).toString(16)
        })
    }

    function vote(comment, direction) {
        if (!Account.signedIn) return Router.toast(qsTr("Sign in to vote"))
        const next = comment.viewerVote === direction ? null : direction
        const task = Account.request("POST", "/api/comments/vote", { commentId: comment.id, direction: next })
        task.finished.connect(() => discussion.refresh())
        task.failed.connect((message) => Router.toast(message, "error"))
    }

    // The reader's own comments can be edited and deleted; others' reported.
    function mine(c) { return Account.signedIn && !!c.user && c.user.username === (Account.user.username || "") }
    function deleted(c) { return !!c.deletedAt || c.body === "This comment has been deleted" }
    function save(c, body) {
        if (!body.trim()) return null
        postTask = Account.request("PATCH", "/api/comments", { commentId: c.id, body: body.trim() })
        postTask.finished.connect(() => { discussion.editing = ""; discussion.refresh() })
        postTask.failed.connect((message) => Router.toast(message, "error"))
        return postTask
    }
    function remove(c) {
        const task = Account.request("DELETE", "/api/comments?commentId=" + encodeURIComponent(c.id))
        task.finished.connect(() => { Router.toast(qsTr("Comment deleted"), "success"); discussion.refresh() })
        task.failed.connect((message) => Router.toast(message, "error"))
    }
    function report(c, reason) {
        const task = Account.request("PATCH", "/api/comments", { commentId: c.id, reason: reason })
        task.finished.connect(() => { Router.toast(qsTr("Reported. Thank you."), "success"); discussion.refresh() })
        task.failed.connect((message) => Router.toast(message, "error"))
    }
    property var pendingDelete: null
    ActionMenu { id: commentMenu }
    ConfirmDialog {
        id: deleteConfirm
        heading: qsTr("Delete this comment?")
        message: qsTr("It shows as deleted for everyone; replies to it stay.")
        confirmText: qsTr("Delete")
        destructive: true
        onConfirmed: if (discussion.pendingDelete) discussion.remove(discussion.pendingDelete)
    }
    function openMenu(c, anchor) {
        if (mine(c)) {
            commentMenu.openAt(anchor, 0, anchor.height, [
                { text: qsTr("Edit"), icon: "pencil", run: () => { discussion.replyTo = ""; discussion.editing = c.id } },
                { text: qsTr("Delete"), icon: "trash", run: () => { discussion.pendingDelete = c; deleteConfirm.open() } }
            ])
        } else {
            // Shown in the reader's language; the site keeps the English.
            const reasons = [[qsTr("Spam"), "Spam"], [qsTr("Abuse or harassment"), "Abuse or harassment"],
                             [qsTr("Unmarked spoilers"), "Spoilers without a warning"], [qsTr("Piracy links"), "Links to pirated copies"]]
            commentMenu.openAt(anchor, 0, anchor.height, [{ text: qsTr("Report this comment for"), icon: "triangle-alert", enabled: false }]
                .concat(reasons.map(r => ({ text: r[0], run: () => discussion.report(c, r[1]) }))))
        }
    }

    function ago(iso) {
        const minutes = Math.max(1, Math.round((Date.now() - Date.parse(iso)) / 60000))
        if (minutes < 60) return minutes + qsTr(" min ago")
        if (minutes < 1440) return Math.round(minutes / 60) + qsTr(" h ago")
        if (minutes < 43200) return Math.round(minutes / 1440) + qsTr(" days ago")
        return new Date(iso).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    // Top-level comments with their replies under them.
    readonly property var rows: {
        const byParent = {}
        for (const c of comments) {
            const key = c.parentId || ""
            ;(byParent[key] = byParent[key] || []).push(c)
        }
        const out = []
        const add = (parent, depth) => {
            for (const c of byParent[parent] || []) {
                out.push({ comment: c, depth: Math.min(depth, 2) })
                add(c.id, depth + 1)
            }
        }
        add("", 0)
        return out
    }

    onActiveChanged: if (active && state === "loading") load()
    onUnitChanged: { thread = null; comments = []; shownRows = 30; load() }
    Component.onCompleted: load()

    // Composer
    Column {
        width: parent.width
        spacing: 8
        visible: discussion.state !== "unavailable"
        Row {
            width: parent.width
            spacing: 10
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: discussion.comments.length
                      ? discussion.comments.length + (discussion.comments.length === 1 ? qsTr(" comment") : qsTr(" comments"))
                      : qsTr("Comments")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
            }
            Item { width: 1; height: 1 }
            Segmented {
                anchors.verticalCenter: parent.verticalCenter
                options: [{ value: "TOP", label: qsTr("Top") }, { value: "NEWEST", label: qsTr("Newest") }]
                value: discussion.sort
                onPicked: (v) => { discussion.sort = v; discussion.refresh() }
            }
        }
        Loader {
            width: parent.width
            active: Account.signedIn && discussion.replyTo === ""
            visible: active
            sourceComponent: composerComponent
        }
        Text {
            visible: !Account.signedIn
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Sign in to your account in Settings → Sync to comment.")
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: Router.openSettings("account")
            }
        }
    }

    Component {
        id: composerComponent
        Column {
            property string parentId: ""
            // Editing one of the reader's comments: it, with its text.
            property var editOf: null
            onEditOfChanged: if (editOf) box.text = editOf.body
            width: parent ? parent.width : 0
            spacing: 8
            TextArea {
                id: box
                width: parent.width
                implicitHeight: Math.max(80, contentHeight + 24)
                wrapMode: TextEdit.Wrap
                placeholderText: parentId ? qsTr("Write a reply") : qsTr("Share your thoughts (no spoilers without a warning)")
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                padding: 12
                selectByMouse: true
                background: Rectangle {
                    radius: Theme.radiusMd
                    color: Theme.surface2
                    border.width: 1
                    border.color: box.activeFocus ? Theme.alpha(Theme.accent, 0.7) : Theme.border
                }
            }
            Row {
                spacing: 8
                ScButton {
                    compact: true
                    variant: "primary"
                    text: editOf ? qsTr("Save") : parentId ? qsTr("Reply") : qsTr("Post")
                    enabled: box.text.trim().length > 0 && !(discussion.postTask && discussion.postTask.loading)
                    loading: !!discussion.postTask && discussion.postTask.loading
                    onClicked: {
                        const task = editOf ? discussion.save(editOf, box.text) : discussion.post(box.text, parentId)
                        if (task && !editOf) task.finished.connect(() => box.text = "")
                    }
                }
                ScButton {
                    visible: !!parentId || !!editOf
                    compact: true
                    text: qsTr("Cancel")
                    onClicked: { discussion.replyTo = ""; discussion.editing = "" }
                }
            }
        }
    }

    Text {
        visible: discussion.state === "loading"
        text: qsTr("Loading comments…")
        color: Theme.textFaint
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
    }
    Text {
        visible: discussion.state === "error" || discussion.state === "unavailable"
        width: parent.width
        wrapMode: Text.Wrap
        text: discussion.state === "unavailable" ? qsTr("This server has no comments. They are on scancore.onl.")
                                                 : qsTr("Comments could not be loaded. ") + discussion.problem
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
    }
    Text {
        visible: discussion.state === "ready" && discussion.comments.length === 0
        text: qsTr("No comments yet. Be the first.")
        color: Theme.textFaint
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
    }

    // The first comments until asked for more (a chapter can have 200).
    property int shownRows: 30
    Repeater {
        model: discussion.state === "ready" ? discussion.rows.slice(0, discussion.shownRows) : []
        Column {
            required property var modelData
            readonly property var c: modelData.comment
            x: modelData.depth * 28
            width: discussion.width - x
            spacing: 6
            Row {
                spacing: 8
                Rectangle {
                    width: 26
                    height: 26
                    radius: 13
                    color: Theme.surface3
                    clip: true
                    Image {
                        anchors.fill: parent
                        visible: !!c.user.avatarUrl
                        source: c.user.avatarUrl ? (/^https?:/.test(c.user.avatarUrl) ? c.user.avatarUrl : Account.site + c.user.avatarUrl) : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !c.user.avatarUrl
                        text: String(c.user.username || "?").charAt(0).toUpperCase()
                        color: Theme.text
                        font.family: Theme.fontDisplay
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: c.user.username
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: discussion.ago(c.createdAt) + (c.editedAt ? qsTr(" · edited") : "")
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                }
            }
            Text {
                visible: discussion.editing !== c.id
                width: parent.width
                leftPadding: 34
                text: c.hidden ? qsTr("This comment is hidden.") : c.body
                color: c.hidden ? Theme.textFaint : Theme.text
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                lineHeight: 1.35
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
            Row {
                leftPadding: 26
                spacing: 2
                IconButton {
                    diameter: 30
                    iconSize: 15
                    iconName: "arrow-up"
                    active: c.viewerVote === "UP"
                    tip: qsTr("Upvote")
                    onClicked: discussion.vote(c, "UP")
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: c.upvoteCount - c.downvoteCount
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    font.features: { "tnum": 1 }
                }
                IconButton {
                    diameter: 30
                    iconSize: 15
                    iconName: "arrow-up"
                    rotation: 180
                    active: c.viewerVote === "DOWN"
                    tip: qsTr("Downvote")
                    onClicked: discussion.vote(c, "DOWN")
                }
                ScButton {
                    visible: Account.signedIn && modelData.depth < 2
                    compact: true
                    text: qsTr("Reply")
                    onClicked: { discussion.editing = ""; discussion.replyTo = discussion.replyTo === c.id ? "" : c.id }
                }
                IconButton {
                    id: moreButton
                    visible: Account.signedIn && !discussion.deleted(c) && !c.hidden
                    diameter: 30
                    iconSize: 15
                    iconName: "ellipsis"
                    tip: qsTr("More")
                    onClicked: discussion.openMenu(c, moreButton)
                }
            }
            Loader {
                width: parent.width
                active: discussion.replyTo === c.id
                visible: active
                sourceComponent: composerComponent
                onLoaded: item.parentId = c.id
            }
            Loader {
                width: parent.width
                active: discussion.editing === c.id
                visible: active
                sourceComponent: composerComponent
                onLoaded: item.editOf = c
            }
            Item { width: 1; height: 4 }
        }
    }

    ScButton {
        visible: discussion.state === "ready" && discussion.rows.length > discussion.shownRows
        variant: "ghost"
        text: qsTr("Show more comments (%1)").arg(discussion.rows.length - discussion.shownRows)
        onClicked: discussion.shownRows += 50
    }
}
