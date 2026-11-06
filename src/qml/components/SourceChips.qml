import QtQuick
import Scancore

// The source chips under a search field: "All sources", then one per source
// with its result count while a search runs. Installed extensions can add
// hundreds of sources, so it is a ListView (only the chips on screen exist)
// on one line that scrolls sideways; wide windows with few sources wrap
// them instead. `providers` maps a source key to the search's provider
// entry ({pending, ok, count}), looked up once per update, not per chip.
Item {
    id: root
    property var sources: []
    property string current: "all"
    property var providers: ({})
    property bool showLanguage: false
    // Several can be ticked: the keys ticked, none meaning "All" (picked()
    // still says which chip was pressed; the caller updates the list).
    property var ticked: null
    property string allLabel: qsTr("All sources")
    // Space before the first chip and after the last (0 inside a dialog).
    property real inset: Theme.gutter
    // Wrapping is fine for a handful; past this the chips scroll.
    property int wrapLimit: 30
    // A last chip that opens a menu (the caller's), when set: the Comics
    // page folds its sites into one.
    property string moreLabel: ""
    signal picked(string key)
    signal more(Item chip)

    readonly property var entries: [{ key: "all", label: allLabel }].concat(sources)
        .concat(moreLabel ? [{ key: "\u0001more", label: moreLabel }] : [])
    readonly property bool scrolls: Theme.compact || entries.length > wrapLimit

    width: parent ? parent.width : 400
    height: scrolls ? 32 : wrapped.height

    function label(s) {
        return s.label + (showLanguage && s.language && s.language !== "en" ? " · " + s.language.toUpperCase() : "")
    }
    function detail(key) {
        const p = providers[key]
        return !p ? "" : p.pending ? "…" : p.ok ? String(p.count) : "!"
    }

    component SourceChip: Chip {
        required property var modelData
        readonly property bool isMore: modelData.key === "\u0001more"
        text: isMore ? modelData.label : root.label(modelData)
        trailingIcon: isMore ? "chevron-down" : ""
        selected: !isMore && (root.ticked ? (modelData.key === "all" ? root.ticked.length === 0 : root.ticked.indexOf(modelData.key) >= 0)
                                          : root.current === modelData.key)
        detail: modelData.key === "all" || isMore ? "" : root.detail(modelData.key)
        onClicked: isMore ? root.more(this) : root.picked(modelData.key)
    }

    ListView {
        id: chipList
        maximumFlickVelocity: Motion.flickVelocity
        Glide { flick: chipList }
        flickDeceleration: Motion.flickDeceleration
        visible: root.scrolls
        anchors.fill: parent
        orientation: ListView.Horizontal
        spacing: 8
        leftMargin: root.inset
        rightMargin: root.inset
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // Built ahead a little so a fling does not show gaps.
        cacheBuffer: 400
        model: root.scrolls ? root.entries : []
        delegate: SourceChip {}
    }

    Flow {
        id: wrapped
        visible: !root.scrolls
        x: root.inset
        width: parent.width - 2 * root.inset
        spacing: 8
        Repeater {
            model: root.scrolls ? [] : root.entries
            SourceChip {}
        }
    }
}
