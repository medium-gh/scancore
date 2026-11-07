import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Comick's search filters for the Comics page: sorting, status, type,
// demographic, content, genres and tags to include or leave out, years,
// chapters, rating and how recently a series was added. Edits a copy;
// Apply hands it back (chosen). The shape is Catalogue.h's comickQuery.
Dialog {
    id: sheet
    property var filters: ({})
    signal chosen(var filters)

    // Genres ([{slug, name, group}]) and tags ([{slug, title}]), loaded on
    // first open: the 300 most used tags, every tag once a search needs them.
    property var genresTask: null
    property var popularTask: null
    property var allTagsTask: null
    readonly property var genres: genresTask && genresTask.ok ? genresTask.result : []
    readonly property var popularTags: popularTask && popularTask.ok ? popularTask.result : []
    readonly property var allTags: allTagsTask && allTagsTask.ok ? allTagsTask.result : []
    property string find: ""
    property int tagsShown: 24

    // Comick's limits.
    readonly property int maxGenres: 10
    readonly property int maxTags: 10
    readonly property int maxExcludedTags: 30

    function start(current) {
        filters = normal(current)
        find = ""
        tagsShown = 24
        if (!genresTask || genresTask.failed) genresTask = Comics.comickGenres()
        if (!popularTask || popularTask.failed) popularTask = Comics.comickTags(false)
        open()
    }

    // Settings hand lists back as list objects; plain JS from here on.
    function normal(f) {
        const out = JSON.parse(JSON.stringify(f || {}))
        for (const key of ["genres", "tags", "content", "violence"]) out[key] = out[key] || {}
        for (const key of ["country", "demographic"]) out[key] = Array.from(out[key] || [])
        return out
    }

    // How many filters are set (the Comics page's button shows it).
    function count(f) {
        if (!f) return 0
        let n = 0
        for (const key of ["genres", "tags", "content", "violence"])
            n += Object.keys(f[key] || {}).length
        for (const key of ["country", "demographic"]) n += Array.from(f[key] || []).length
        for (const key of ["sort", "status", "time", "from", "to", "minimum", "maximum", "rating"])
            if (f[key] && String(f[key]) !== "0") ++n
        if (f.completed) ++n
        if (f.showall) ++n
        return n
    }

    function set(key, value) {
        const next = normal(filters)
        if (value === "" || value === 0 || value === false || value === undefined) delete next[key]
        else next[key] = value
        filters = next
    }
    // Any → include → leave out → any, for a map of states (genres, content).
    function cycle(key, slug, extra) {
        const next = normal(filters)
        const map = next[key]
        const current = stateOf(map[slug])
        const state = (current + 1) % 3
        if (key === "genres" && state === 1 && included("genres") >= maxGenres) {
            map[slug] = 2
        } else if (key === "tags" && state === 1 && included("tags") >= maxTags) {
            map[slug] = { state: 2, title: extra }
        } else if (state === 0 || (key === "tags" && state === 2 && excluded("tags") >= maxExcludedTags)) {
            delete map[slug]
        } else {
            map[slug] = key === "tags" ? { state: state, title: extra } : state
        }
        filters = next
    }
    function stateOf(v) { return !v ? 0 : typeof v === "object" ? (v.state || 0) : v }
    function included(key) { return Object.values(filters[key] || {}).filter(v => stateOf(v) === 1).length }
    function excluded(key) { return Object.values(filters[key] || {}).filter(v => stateOf(v) === 2).length }
    function toggleIn(key, value) {
        const next = normal(filters)
        const list = next[key]
        const at = list.indexOf(value)
        if (at >= 0) list.splice(at, 1)
        else list.push(value)
        filters = next
    }

    // Typing in the box searches every tag (loaded once).
    onFindChanged: if (find.trim().length >= 2 && !allTagsTask) allTagsTask = Comics.comickTags(true)
    readonly property string needle: find.trim().toLowerCase()
    readonly property var genreGroups: {
        const order = ["genre", "theme", "format", "content"]
        const names = { genre: qsTr("Genres"), theme: qsTr("Themes"), format: qsTr("Format"), content: qsTr("Content") }
        const groups = order.map(key => ({ key: key, label: names[key], items: [] }))
        for (const g of genres) {
            const group = groups.find(x => x.key === String(g.group).toLowerCase())
            if (group && (!needle || g.name.toLowerCase().indexOf(needle) >= 0)) group.items.push(g)
        }
        return groups.filter(x => x.items.length > 0)
    }
    // The chosen tags first, then suggestions: the most used, or matches.
    readonly property var chosenTags: Object.keys(filters.tags || {}).map(slug => ({ slug: slug, title: filters.tags[slug].title || slug }))
    readonly property var tagSuggestions: {
        const chosen = filters.tags || {}
        if (needle.length >= 2) {
            const list = allTags.length ? allTags : popularTags
            const out = []
            for (let i = 0; i < list.length && out.length < 40; ++i)
                if (!chosen[list[i].slug] && list[i].title.toLowerCase().indexOf(needle) >= 0) out.push(list[i])
            return out
        }
        return popularTags.filter(t => !chosen[t.slug] && (mature || !explicit.test(t.title))).slice(0, tagsShown)
    }
    // Without adult sources turned on, the most used tags leave out the
    // explicit ones (typing still finds them).
    readonly property bool mature: App.settings.showNsfwSources === true
    readonly property var explicit: /rape|sex|breast|blowjob|fellatio|nudity|incest|loli|shota|porn|smut|hentai|bdsm|masturbat|prostitut|tentacle|cuckold|netorare|\bntr\b|oral|anal\b|bondage|nsfw/i

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(620, (parent ? parent.width : 620) - 24)
    height: Math.min(implicitHeight, (parent ? parent.height : 700) - 48)
    modal: true
    focus: true
    padding: 20
    topPadding: 4
    title: ""

    Overlay.modal: Rectangle { color: Theme.overlay }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast }
            NumberAnimation { property: "scale"; to: 0.97; duration: Motion.fast }
        }
    }
    background: SurfacePanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    header: Item {
        implicitHeight: headerText.height + 26
        Column {
            id: headerText
            x: 20
            y: 14
            width: parent.width - 80
            Text {
                text: qsTr("Search filters")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: qsTr("Comick's catalogue. With filters set, an empty search browses.")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs
                wrapMode: Text.Wrap
            }
        }
        IconButton { anchors.right: parent.right; anchors.rightMargin: 12; y: 14; iconName: "x"; tip: qsTr("Close"); onClicked: sheet.close() }
    }

    footer: Item {
        implicitHeight: 64
        Text {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - buttons.width - 52
            // Only where it fits whole (the page shows the count too).
            visible: implicitWidth <= width
            text: sheet.count(sheet.filters) === 0 ? qsTr("No filters") : sheet.count(sheet.filters) === 1 ? qsTr("1 filter") : qsTr("%1 filters").arg(sheet.count(sheet.filters))
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            elide: Text.ElideRight
        }
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            ScButton {
                text: qsTr("Clear")
                enabled: sheet.count(sheet.filters) > 0
                onClicked: sheet.filters = sheet.normal({})
            }
            ScButton {
                text: qsTr("Apply")
                variant: "primary"
                onClicked: {
                    sheet.close()
                    sheet.chosen(sheet.normal(sheet.filters))
                }
            }
        }
    }

    contentItem: Flickable {
        maximumFlickVelocity: Motion.flickVelocity
        flickDeceleration: Motion.flickDeceleration
        implicitHeight: body.implicitHeight
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll {}
        ScrollBar.vertical: ScScrollBar {}

        Column {
            id: body
            width: parent.width
            spacing: 18

            // ------------------------------------------------ order and state
            Grid {
                width: parent.width
                columns: width >= 440 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12
                readonly property real cell: columns === 2 ? (width - columnSpacing) / 2 : width
                Field {
                    width: parent.cell
                    label: qsTr("Sort by")
                    Select {
                        width: parent.width
                        Accessible.name: qsTr("Sort by")
                        model: [{ value: "", label: qsTr("Best match") }, { value: "user_follow_count", label: qsTr("Most followed") },
                                { value: "view", label: qsTr("Most viewed") }, { value: "rating", label: qsTr("Top rated") },
                                { value: "average_rating", label: qsTr("Average rating") }, { value: "uploaded", label: qsTr("New chapters") },
                                { value: "created_at", label: qsTr("Recently added") }]
                        value: sheet.filters.sort || ""
                        onPicked: (v) => sheet.set("sort", v)
                    }
                }
                Field {
                    width: parent.cell
                    label: qsTr("Status")
                    Select {
                        width: parent.width
                        Accessible.name: qsTr("Status")
                        model: [{ value: 0, label: qsTr("Any") }, { value: 1, label: qsTr("Ongoing") }, { value: 2, label: qsTr("Completed") },
                                { value: 3, label: qsTr("Cancelled") }, { value: 4, label: qsTr("On hiatus") }]
                        value: Number(sheet.filters.status || 0)
                        onPicked: (v) => sheet.set("status", v)
                    }
                }
                Field {
                    width: parent.cell
                    label: qsTr("Added to Comick")
                    Select {
                        width: parent.width
                        Accessible.name: qsTr("Added to Comick")
                        model: [{ value: 0, label: qsTr("Any time") }, { value: 7, label: qsTr("In the last week") },
                                { value: 30, label: qsTr("In the last month") }, { value: 90, label: qsTr("In the last 3 months") },
                                { value: 365, label: qsTr("In the last year") }]
                        value: Number(sheet.filters.time || 0)
                        onPicked: (v) => sheet.set("time", v)
                    }
                }
                Field {
                    width: parent.cell
                    label: qsTr("Rating at least")
                    Select {
                        width: parent.width
                        Accessible.name: qsTr("Rating at least")
                        model: [{ value: 0, label: qsTr("Any") }].concat([6, 7, 7.5, 8, 8.5, 9].map(r => ({ value: r, label: String(r) })))
                        value: Number(sheet.filters.rating || 0)
                        onPicked: (v) => sheet.set("rating", v)
                    }
                }
                Field {
                    width: parent.cell
                    label: qsTr("Published")
                    Range { width: parent.width; low: "from"; high: "to"; lowText: qsTr("From year"); highText: qsTr("To year"); most: 2100 }
                }
                Field {
                    width: parent.cell
                    label: qsTr("Chapters")
                    Range { width: parent.width; low: "minimum"; high: "maximum"; lowText: qsTr("At least"); highText: qsTr("At most"); most: 100000 }
                }
            }
            SwitchRow {
                label: qsTr("Fully translated")
                detail: qsTr("Every chapter is out in English, not only that the series has ended.")
                checked: sheet.filters.completed === true
                onToggled: (on) => sheet.set("completed", on)
            }
            SwitchRow {
                label: qsTr("Include series without a chapter count")
                checked: sheet.filters.showall === true
                onToggled: (on) => sheet.set("showall", on)
            }

            // ------------------------------------------------ kind of series
            Field {
                width: parent.width
                label: qsTr("Type")
                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: [{ value: "kr", label: qsTr("Manhwa") }, { value: "jp", label: qsTr("Manga") },
                                { value: "cn", label: qsTr("Manhua") }, { value: "hk", label: qsTr("Hong Kong") }]
                        Chip {
                            required property var modelData
                            text: modelData.label
                            selected: (sheet.filters.country || []).indexOf(modelData.value) >= 0
                            iconName: selected ? "check" : ""
                            onClicked: sheet.toggleIn("country", modelData.value)
                        }
                    }
                }
            }
            Field {
                width: parent.width
                label: qsTr("Demographic")
                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: [{ value: "1", label: qsTr("Shounen") }, { value: "2", label: qsTr("Shoujo") }, { value: "3", label: qsTr("Seinen") },
                                { value: "4", label: qsTr("Josei") }, { value: "5", label: qsTr("None given") }]
                        Chip {
                            required property var modelData
                            text: modelData.label
                            selected: (sheet.filters.demographic || []).indexOf(modelData.value) >= 0
                            iconName: selected ? "check" : ""
                            onClicked: sheet.toggleIn("demographic", modelData.value)
                        }
                    }
                }
            }
            Field {
                width: parent.width
                label: qsTr("Content rating")
                hint: qsTr("Tap once to include, twice to leave out")
                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: [{ key: "content", value: "safe", label: qsTr("Safe") }, { key: "content", value: "suggestive", label: qsTr("Suggestive") },
                                { key: "content", value: "erotica", label: qsTr("Erotica") }, { key: "content", value: "pornographic", label: qsTr("Pornographic") },
                                { key: "violence", value: "graphic", label: qsTr("Graphic violence") }]
                        TriChip {
                            required property var modelData
                            text: modelData.label
                            tri: sheet.stateOf((sheet.filters[modelData.key] || {})[modelData.value])
                            onClicked: sheet.cycle(modelData.key, modelData.value)
                        }
                    }
                }
            }

            // ------------------------------------------------ genres and tags
            InlineField {
                width: parent.width
                placeholderText: qsTr("Find a genre or tag")
                Accessible.name: qsTr("Find a genre or tag")
                text: sheet.find
                onTextChanged: sheet.find = text
            }
            StateView {
                width: parent.width
                visible: !!sheet.genresTask && !sheet.genresTask.ok
                task: sheet.genresTask
                onRetry: sheet.genresTask = Comics.comickGenres()
            }
            Repeater {
                model: sheet.genreGroups
                Field {
                    id: group
                    required property var modelData
                    property bool all: false
                    readonly property int cap: sheet.needle || all ? modelData.items.length : 18
                    // Chosen genres always show, the rest up to the cap.
                    readonly property var shown: {
                        const chosen = sheet.filters.genres || {}
                        const items = modelData.items
                        if (cap >= items.length) return items
                        return items.filter(g => chosen[g.slug]).concat(items.filter(g => !chosen[g.slug]).slice(0, cap))
                    }
                    width: body.width
                    label: modelData.label
                    hint: modelData.key === "genre" ? qsTr("Up to %1 included").arg(sheet.maxGenres) : ""
                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: group.shown
                            TriChip {
                                required property var modelData
                                text: modelData.name
                                tri: sheet.stateOf(sheet.filters.genres[modelData.slug])
                                onClicked: sheet.cycle("genres", modelData.slug)
                            }
                        }
                        Chip {
                            visible: group.shown.length < group.modelData.items.length
                            text: qsTr("Show all %1").arg(group.modelData.items.length)
                            iconName: "chevron-down"
                            onClicked: group.all = true
                        }
                    }
                }
            }
            Field {
                width: parent.width
                label: qsTr("Tags")
                hint: qsTr("Up to %1 included and %2 left out").arg(sheet.maxTags).arg(sheet.maxExcludedTags)
                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: sheet.chosenTags
                        TriChip {
                            required property var modelData
                            text: modelData.title
                            tri: sheet.stateOf(sheet.filters.tags[modelData.slug])
                            onClicked: sheet.cycle("tags", modelData.slug, modelData.title)
                        }
                    }
                    Repeater {
                        model: sheet.tagSuggestions
                        TriChip {
                            required property var modelData
                            text: modelData.title
                            tri: 0
                            onClicked: sheet.cycle("tags", modelData.slug, modelData.title)
                        }
                    }
                    Chip {
                        visible: !sheet.needle && sheet.tagsShown < sheet.popularTags.length
                        text: qsTr("More tags")
                        iconName: "chevron-down"
                        onClicked: sheet.tagsShown += 48
                    }
                }
                Text {
                    visible: sheet.needle.length >= 2 && sheet.tagSuggestions.length === 0
                        && !(sheet.allTagsTask && sheet.allTagsTask.loading)
                    width: parent.width
                    text: qsTr("No tag matches “%1”.").arg(sheet.find.trim())
                    color: Theme.textMuted
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                }
                Spinner { visible: !!sheet.allTagsTask && sheet.allTagsTask.loading && sheet.needle.length >= 2 }
            }
        }
    }

    // A labelled block.
    component Field: Column {
        property string label
        property string hint
        default property alias content: holder.data
        spacing: 8
        Row {
            spacing: 8
            Text {
                id: fieldLabel
                text: parent.parent.label
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
            }
            Text {
                visible: text.length > 0
                y: fieldLabel.baselineOffset - baselineOffset
                text: parent.parent.hint
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textXs
            }
        }
        Column {
            id: holder
            width: parent.width
            spacing: 8
        }
    }

    component SwitchRow: Row {
        id: switchRow
        property string label
        property string detail
        property bool checked
        signal toggled(bool on)
        width: body.width
        spacing: 12
        Column {
            width: parent.width - toggle.width - 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text { width: parent.width; text: switchRow.label; color: Theme.text; font.family: Theme.fontSans; font.pixelSize: Theme.textMd; wrapMode: Text.Wrap }
            Text { visible: text.length > 0; width: parent.width; text: switchRow.detail; color: Theme.textMuted; font.family: Theme.fontSans; font.pixelSize: Theme.textSm; wrapMode: Text.Wrap }
        }
        Toggle { id: toggle; anchors.verticalCenter: parent.verticalCenter; checked: switchRow.checked; onToggled: switchRow.toggled(checked) }
    }

    // Two whole numbers (years, chapter counts); blank for none.
    component Range: Row {
        id: range
        property string low
        property string high
        property string lowText
        property string highText
        property int most: 100000
        spacing: 8
        Repeater {
            model: [{ key: range.low, hint: range.lowText }, { key: range.high, hint: range.highText }]
            InlineField {
                required property var modelData
                width: (range.width - range.spacing) / 2
                placeholderText: modelData.hint
                Accessible.name: modelData.hint
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0; top: range.most }
                text: sheet.filters[modelData.key] ? String(sheet.filters[modelData.key]) : ""
                onEditingFinished: sheet.set(modelData.key, text.length ? Number(text) : 0)
            }
        }
    }

    // Any → include → leave out, as the extensions' filters.
    component TriChip: Chip {
        property int tri: 0
        selected: tri !== 0
        iconName: tri === 1 ? "check" : tri === 2 ? "x" : ""
        dot: tri === 2 ? Theme.danger : "transparent"
        Accessible.description: tri === 1 ? qsTr("Included") : tri === 2 ? qsTr("Left out") : ""
    }
}
