import QtQuick
import QtQuick.Controls.Basic
import Scancore

// One story: a series' other forms (the comic an anime adapts, its novel,
// every season) with your progress in each, and where each anime season
// leaves off in the comic or novel, from readers' votes on scancore.onl.
Column {
    id: panel
    property string seriesId
    property string media: "comic"
    property string title
    property int anilistId: 0
    // A comic or novel page: jumps to a chapter of this series.
    signal openChapter(real number)

    property var task: null
    property var pointsTask: null
    property var points: ({})
    property var submitTask: null
    property bool suggesting: false
    readonly property var info: task && task.ok ? task.result : null
    readonly property var forms: info ? info.forms : []
    readonly property int selfId: info ? info.anilistId : 0
    // The seasons and films whose ends are worth a point.
    readonly property var animeForms: media === "anime" ? []
        : forms.filter(f => f.media === "anime" && ["TV", "TV SHORT", "MOVIE", "ONA"].indexOf(f.format) >= 0)
    readonly property var pointIds: media === "anime" ? (selfId > 0 ? [selfId] : []) : animeForms.map(f => f.anilistId)
    // Comic and novel forms a point can lead to (anime page).
    readonly property var sourceForms: forms.filter(f => f.media !== "anime")
    readonly property var ownPoints: media === "anime" ? (points[String(selfId)] || []) : []
    // Comic or novel page: per season, its best point in this media.
    readonly property var seasonPoints: {
        const out = []
        for (const f of animeForms) {
            const best = (points[String(f.anilistId)] || []).find(p => p.media === media && p.score >= 0)
            if (best) out.push({ anime: f, point: best })
        }
        return out
    }

    function load() {
        if (!seriesId) return
        task = Story.forms(seriesId, media, title, anilistId)
    }
    onPointIdsChanged: if (pointIds.length > 0) pointsTask = Story.points(pointIds)
    Connections {
        target: panel.pointsTask
        function onFinished(result) { panel.points = result }
    }
    Component.onCompleted: Qt.callLater(load)
    onSeriesIdChanged: Qt.callLater(load)
    onAnilistIdChanged: Qt.callLater(load)

    function open(form, cover) {
        if (form.media === "anime") Router.openSeries({ media: "anime", anilistId: form.anilistId, title: form.title, cover: form.cover }, cover)
        else if (form.seriesId) Router.openSeries(Library.seriesRow(form.seriesId), cover)
        else readFrom.start(form.entry)
    }
    function formById(id) { return forms.find(f => f.anilistId === id) || null }
    function kindOf(form, fallbackMedia) {
        const type = form && form.entry ? String(form.entry.type || "") : ""
        if (type === "Manhwa") return qsTr("Manhwa")
        if (type === "Manhua") return qsTr("Manhua")
        if (type === "Manga") return qsTr("Manga")
        return (form ? form.media : fallbackMedia) === "novel" ? qsTr("Novel") : qsTr("Comic")
    }
    function where(point) {
        return point.volume > 0 ? qsTr("volume %1").arg(point.volume) : qsTr("chapter %1").arg(Number(point.chapter))
    }
    function vote(point, value) {
        if (!Account.signedIn) return Router.toast(qsTr("Sign in to vote"))
        const t = Story.vote(selfId, point.id, point.mine === value ? 0 : value)
        t.finished.connect((list) => { const next = Object.assign({}, panel.points); next[String(panel.selfId)] = list; panel.points = next })
        t.failed.connect((error) => Router.toast(error, "error"))
    }
    function suggest() {
        const number = Number(String(numberField.text).replace(",", "."))
        if (!(number > 0)) return Router.toast(qsTr("Enter the chapter or volume to continue from"), "error")
        const form = sourceForms.find(f => f.anilistId === sourcePick.value) || sourceForms[0]
        const byVolume = form.media === "novel" && unitPick.value === "volume"
        submitTask = Story.submit(selfId, form.media, form.anilistId, byVolume ? 0 : number, byVolume ? Math.round(number) : 0)
        submitTask.finished.connect((list) => {
            const next = Object.assign({}, panel.points); next[String(panel.selfId)] = list; panel.points = next
            panel.suggesting = false
            numberField.text = ""
            Router.toast(qsTr("Thanks! Readers can vote on it now."), "success")
        })
        submitTask.failed.connect((error) => Router.toast(error, "error"))
    }

    width: parent ? parent.width : 600
    spacing: 14
    visible: forms.length > 0

    ReadFromDialog { id: readFrom }

    Shelf {
        width: panel.width
        title: qsTr("The story elsewhere")
        items: panel.forms.map(f => Object.assign({ id: f.seriesId || ("anilist:" + f.anilistId) }, f))
        cardWidth: Math.round((Theme.compact ? 100 : 124) * Theme.coverScale)
        subtitleFor: (f) => {
            const kind = f.media === "anime" ? (f.format || qsTr("Anime")) : panel.kindOf(f, f.media)
            if (f.done > 0 && f.total > 0)
                return kind + " · " + (f.media === "anime" ? qsTr("%1 of %2 watched") : qsTr("%1 of %2 read")).arg(f.done).arg(f.total)
            if (f.done > 0)
                return kind + " · " + (f.media === "anime" ? qsTr("%1 watched") : qsTr("%1 read")).arg(f.done)
            return kind + (f.year > 0 ? " · " + f.year : "")
        }
        progressFor: (f) => f.done > 0 && f.total > 0 ? Math.min(1, f.done / f.total) : -1
        onOpened: (f, cover) => panel.open(f, cover)
    }

    // Comic or novel page: where each season ends here.
    Repeater {
        model: panel.seasonPoints
        Rectangle {
            id: seasonRow
            required property var modelData
            x: Theme.gutter
            width: panel.width - 2 * Theme.gutter
            height: seasonContent.height + 24
            radius: Theme.radiusLg
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            Item {
                id: seasonContent
                x: 14
                y: 12
                width: parent.width - 28
                height: Math.max(seasonTexts.height, goButton.height)
                Icon { id: seasonIcon; anchors.verticalCenter: parent.verticalCenter; name: "tv"; size: 18; color: Theme.accent }
                Column {
                    id: seasonTexts
                    anchors.left: seasonIcon.right
                    anchors.leftMargin: 12
                    anchors.right: goButton.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: qsTr("After %1, continue from %2").arg(seasonRow.modelData.anime.title).arg(panel.where(seasonRow.modelData.point))
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        text: (seasonRow.modelData.point.seeded ? qsTr("From SCANCORE's list") : qsTr("Suggested by readers"))
                            + " · " + (seasonRow.modelData.point.score === 1 ? qsTr("1 vote") : qsTr("%1 votes").arg(seasonRow.modelData.point.score))
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                        elide: Text.ElideRight
                    }
                }
                ScButton {
                    id: goButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: Number(seasonRow.modelData.point.chapter) > 0
                    compact: true
                    text: qsTr("Go there")
                    onClicked: panel.openChapter(Number(seasonRow.modelData.point.chapter))
                }
            }
        }
    }

    // Anime page: where this one leaves off, voted on.
    Column {
        visible: panel.media === "anime" && panel.selfId > 0 && panel.sourceForms.length > 0
        x: Theme.gutter
        width: panel.width - 2 * Theme.gutter
        spacing: 8

        Item {
            width: parent.width
            height: 30
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - suggestButton.width - 12
                elide: Text.ElideRight
                text: qsTr("Where it leaves off")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textXl
                font.weight: Font.DemiBold
            }
            ScButton {
                id: suggestButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                compact: true
                variant: "ghost"
                iconName: panel.suggesting ? "x" : "plus"
                text: panel.suggesting ? qsTr("Cancel") : qsTr("Suggest")
                onClicked: {
                    if (!panel.suggesting && !Account.signedIn) return Router.toast(qsTr("Sign in to suggest where it leaves off"))
                    panel.suggesting = !panel.suggesting
                }
            }
        }

        Text {
            visible: panel.ownPoints.length === 0 && !panel.suggesting
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Nobody has said yet where the story goes on after this. Know it? Suggest the chapter.")
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
        }

        Repeater {
            model: panel.ownPoints
            Rectangle {
                id: pointRow
                required property var modelData
                readonly property var form: panel.formById(modelData.sourceId)
                width: parent.width
                height: pointContent.height + 20
                radius: Theme.radiusLg
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                Item {
                    id: pointContent
                    x: 14
                    y: 10
                    width: parent.width - 28
                    height: Math.max(pointTexts.height, votes.height)
                    Column {
                        id: pointTexts
                        anchors.left: parent.left
                        anchors.right: votes.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            width: parent.width
                            text: qsTr("%1 continues from %2").arg(panel.kindOf(pointRow.form, pointRow.modelData.media)).arg(panel.where(pointRow.modelData))
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textMd
                            font.weight: Font.Medium
                            wrapMode: Text.Wrap
                        }
                        Text {
                            width: parent.width
                            text: [pointRow.form ? pointRow.form.title : "",
                                   pointRow.modelData.seeded ? qsTr("From SCANCORE's list") : pointRow.modelData.submitted ? qsTr("Your suggestion") : qsTr("Suggested by readers")]
                                .filter(s => s).join(" · ")
                            textFormat: Text.PlainText
                            color: Theme.textMuted
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textSm
                            elide: Text.ElideRight
                        }
                    }
                    Row {
                        id: votes
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        IconButton {
                            diameter: 30
                            iconSize: 15
                            iconName: "arrow-up"
                            active: pointRow.modelData.mine === 1
                            tip: qsTr("Upvote")
                            onClicked: panel.vote(pointRow.modelData, 1)
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: pointRow.modelData.score
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
                            active: pointRow.modelData.mine === -1
                            tip: qsTr("Downvote")
                            onClicked: panel.vote(pointRow.modelData, -1)
                        }
                        IconButton {
                            visible: !!pointRow.form
                            diameter: 30
                            iconSize: 15
                            iconName: "chevron-right"
                            tip: qsTr("Open")
                            onClicked: panel.open(pointRow.form, null)
                        }
                    }
                }
            }
        }

        // Suggesting a point: which comic or novel, and where it goes on.
        Flow {
            visible: panel.suggesting
            width: parent.width
            spacing: 8
            Select {
                id: sourcePick
                visible: panel.sourceForms.length > 1
                width: Theme.compact ? parent.width : 240
                model: panel.sourceForms.map(f => ({ value: f.anilistId, label: f.title + " · " + panel.kindOf(f, f.media) }))
                value: panel.sourceForms.length > 0 ? panel.sourceForms[0].anilistId : 0
                onPicked: (v) => value = v
            }
            Segmented {
                id: unitPick
                visible: (panel.sourceForms.find(f => f.anilistId === sourcePick.value) || panel.sourceForms[0] || {}).media === "novel"
                options: [{ value: "chapter", label: qsTr("Chapter") }, { value: "volume", label: qsTr("Volume") }]
                value: "chapter"
                onPicked: (v) => value = v
            }
            InlineField {
                id: numberField
                width: 120
                placeholderText: unitPick.visible && unitPick.value === "volume" ? qsTr("Volume") : qsTr("Chapter")
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator { bottom: 0; top: 99999; decimals: 1; notation: DoubleValidator.StandardNotation }
                onAccepted: panel.suggest()
            }
            ScButton {
                variant: "primary"
                text: qsTr("Add")
                loading: panel.submitTask !== null && panel.submitTask.loading
                onClicked: panel.suggest()
            }
        }
    }
}
