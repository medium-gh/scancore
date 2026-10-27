import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Reviews of a series from the account's site (scancore.onl), the same as
// on the website: the average, everyone's reviews and your own.
Column {
    id: reviews
    property string media: "comic"
    property string title
    property string comickHid
    property int anilistId: 0
    property string url
    property bool active: true

    property var thread: null
    property var items: []
    property real average: 0
    property int total: 0
    property string state: "loading"
    property string problem
    property int rating: 5
    // The star under the pointer while choosing (0: none), shown as the
    // rating it would give.
    property int hoveredStar: 0
    property var saveTask: null
    // One review per person per title: yours, when you wrote one, is
    // edited rather than added to.
    readonly property var mine: {
        const name = Account.user ? Account.user.username : ""
        return name ? items.find(r => r.user && r.user.username === name) || null : null
    }
    onMineChanged: if (mine) {
        rating = mine.rating
        reviewBox.text = mine.body
    }

    spacing: 14

    function query(params) {
        return Object.keys(params).filter(k => params[k] !== undefined && params[k] !== null && params[k] !== "")
            .map(k => encodeURIComponent(k) + "=" + encodeURIComponent(params[k])).join("&")
    }

    function load() {
        if (!active || !title) return
        state = "loading"
        if (Account.customSite) {
            const check = Account.request("GET", "/api/capabilities")
            check.finished.connect((caps) => caps.discussions ? reviews.resolve() : reviews.state = "unavailable")
            check.failed.connect(() => reviews.state = "unavailable")
            return
        }
        resolve()
    }

    function resolve() {
        const task = Account.request("GET", "/api/discussions/resolve?" + query({
            media: media, title: title.slice(0, 200), comickHid: comickHid,
            anilistId: anilistId > 0 ? anilistId : "", url: /^https?:/.test(url) ? url : ""
        }))
        task.finished.connect((found) => { reviews.thread = found; reviews.refresh() })
        task.failed.connect((message) => { reviews.state = "error"; reviews.problem = message })
    }

    function refresh() {
        const task = Account.request("GET", "/api/reviews?" + query({
            seriesName: thread.seriesName, seriesId: thread.seriesId, mediaType: thread.mediaType
        }))
        task.finished.connect((result) => {
            reviews.items = result.reviews || []
            reviews.average = Number(result.averageRating || 0)
            reviews.total = Number(result.totalRatings || 0)
            reviews.state = "ready"
        })
        task.failed.connect((message) => { reviews.state = "error"; reviews.problem = message })
    }

    function save(body) {
        const input = { seriesName: thread.seriesName, mediaType: thread.mediaType, rating: rating, body: body.trim() }
        if (thread.seriesId) input.seriesId = thread.seriesId
        saveTask = Account.request("POST", "/api/reviews", input)
        saveTask.finished.connect(() => { Router.toast(reviews.mine ? qsTr("Review updated") : qsTr("Review saved"), "success"); reviews.refresh() })
        saveTask.failed.connect((message) => Router.toast(message, "error"))
    }

    onActiveChanged: if (active && state === "loading") load()
    Component.onCompleted: load()

    Row {
        visible: reviews.state === "ready"
        spacing: 14
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: reviews.total ? reviews.average.toFixed(1) : "–"
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.text2xl
            font.weight: Font.DemiBold
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            Row {
                spacing: 2
                Repeater {
                    model: 5
                    Icon {
                        required property int index
                        readonly property bool lit: index < Math.round(reviews.average)
                        name: lit ? "star-filled" : "star"
                        size: 15
                        color: lit ? Theme.starColor : Theme.textFaint
                    }
                }
            }
            Text {
                text: reviews.total + (reviews.total === 1 ? qsTr(" rating") : qsTr(" ratings"))
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs + 1
            }
        }
    }

    Column {
        visible: reviews.state === "ready" && Account.signedIn
        width: parent.width
        spacing: 8
        Row {
            spacing: 4
            Text {
                anchors.verticalCenter: parent.verticalCenter
                rightPadding: 6
                text: reviews.mine ? qsTr("Your review") : qsTr("Your rating")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
            Repeater {
                model: 5
                IconButton {
                    id: starButton
                    required property int index
                    readonly property bool lit: index < (reviews.hoveredStar || reviews.rating)
                    diameter: 30
                    iconSize: 18
                    iconName: lit ? "star-filled" : "star"
                    iconColor: lit ? Theme.starColor : Theme.textFaint
                    tip: (index + 1) + (index === 0 ? qsTr(" star") : qsTr(" stars"))
                    scale: pressed ? 0.88 : lit && reviews.hoveredStar === index + 1 ? 1.12 : 1
                    Behavior on scale { NumberAnimation { duration: Motion.fast; easing.type: Easing.OutBack } }
                    onHoveredChanged: reviews.hoveredStar = hovered ? index + 1 : (reviews.hoveredStar === index + 1 ? 0 : reviews.hoveredStar)
                    onClicked: reviews.rating = index + 1
                    Accessible.checked: index < reviews.rating
                }
            }
        }
        TextArea {
            id: reviewBox
            width: parent.width
            implicitHeight: Math.max(90, contentHeight + 24)
            wrapMode: TextEdit.Wrap
            placeholderText: qsTr("What did you think? At least 10 characters.")
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
                border.color: reviewBox.activeFocus ? Theme.alpha(Theme.accent, 0.7) : Theme.border
            }
        }
        ScButton {
            compact: true
            variant: "primary"
            text: reviews.mine ? qsTr("Update review") : qsTr("Save review")
            enabled: reviewBox.text.trim().length >= 10 && !(reviews.saveTask && reviews.saveTask.loading)
                && !(reviews.mine && reviews.mine.rating === reviews.rating && reviews.mine.body === reviewBox.text.trim())
            loading: !!reviews.saveTask && reviews.saveTask.loading
            onClicked: reviews.save(reviewBox.text)
        }
    }
    Text {
        visible: reviews.state === "ready" && !Account.signedIn
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("Sign in to your account in Settings → Sync to write a review.")
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
    }
    Text {
        visible: reviews.state !== "ready"
        width: parent.width
        wrapMode: Text.Wrap
        text: reviews.state === "loading" ? qsTr("Loading reviews…")
            : reviews.state === "unavailable" ? qsTr("This server has no reviews. They are on scancore.onl.")
            : qsTr("Reviews could not be loaded. ") + reviews.problem
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
    }
    Text {
        visible: reviews.state === "ready" && reviews.items.length === 0
        text: qsTr("No reviews yet.")
        color: Theme.textFaint
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
    }

    // The first reviews until asked for more (up to 100 load).
    property int shownItems: 20
    Repeater {
        model: reviews.state === "ready" ? reviews.items.slice(0, reviews.shownItems) : []
        Rectangle {
            id: card
            required property var modelData
            width: reviews.width
            height: reviewColumn.implicitHeight + 28
            radius: Theme.radiusLg
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            Column {
                id: reviewColumn
                x: 14
                y: 14
                width: parent.width - 28
                spacing: 6
                Row {
                    spacing: 8
                    Text {
                        text: modelData.user.username
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        font.weight: Font.DemiBold
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        Repeater {
                            model: 5
                            Icon {
                                required property int index
                                readonly property bool lit: index < card.modelData.rating
                                name: lit ? "star-filled" : "star"
                                size: 12
                                color: lit ? Theme.starColor : Theme.textFaint
                            }
                        }
                    }
                }
                Text {
                    width: parent.width
                    text: modelData.body
                    color: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    lineHeight: 1.35
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
            }
        }
    }
    ScButton {
        visible: reviews.state === "ready" && reviews.items.length > reviews.shownItems
        variant: "ghost"
        text: qsTr("Show more reviews (%1)").arg(reviews.items.length - reviews.shownItems)
        onClicked: reviews.shownItems += 40
    }
}
