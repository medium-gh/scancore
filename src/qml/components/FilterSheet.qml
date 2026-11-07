import QtQuick
import QtQuick.Controls.Basic
import Scancore

// A source's search filters as its extension describes them: headings,
// lists, text, check boxes, include/exclude boxes, sorting and groups of
// them. Edits a copy; Apply hands the states back (filtersApplied).
Dialog {
    id: sheet
    property var defaults: []
    property var states: []
    signal filtersApplied(var states)

    function start(filters, current) {
        defaults = JSON.parse(JSON.stringify(filters))
        states = JSON.parse(JSON.stringify(current && current.length ? current : filters))
        open()
    }
    // How many filters differ from the source's defaults.
    function changes(list, base) {
        let n = 0
        for (let i = 0; i < list.length; ++i) {
            const f = list[i], d = base[i] || {}
            if (!f) continue
            if (f.type === "group") n += changes(f.filters || [], d.filters || [])
            else if (JSON.stringify(f.state) !== JSON.stringify(d.state)) ++n
        }
        return n
    }
    // Sets a state by its path ([index] or [group, index]); a new array so
    // bindings see it.
    function set(path, value) {
        const next = JSON.parse(JSON.stringify(states))
        let f = next[path[0]]
        if (path.length > 1) f = f.filters[path[1]]
        f.state = value
        states = next
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, (parent ? parent.width : 560) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 700) - 64)
    modal: true
    focus: true
    padding: 24
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
        implicitHeight: 64
        Text {
            x: 24
            y: 22
            text: qsTr("Filters")
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.textLg
            font.weight: Font.DemiBold
        }
        IconButton { anchors.right: parent.right; anchors.rightMargin: 14; y: 14; iconName: "x"; tip: qsTr("Close"); onClicked: sheet.close() }
    }

    footer: Item {
        implicitHeight: 68
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            ScButton {
                text: qsTr("Reset")
                enabled: sheet.changes(sheet.states, sheet.defaults) > 0
                onClicked: sheet.states = JSON.parse(JSON.stringify(sheet.defaults))
            }
            ScButton {
                text: qsTr("Apply")
                variant: "primary"
                onClicked: {
                    sheet.close()
                    sheet.filtersApplied(sheet.states)
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
            spacing: 14
            Repeater {
                model: sheet.states
                delegate: FilterItem {
                    required property var modelData
                    required property int index
                    width: body.width
                    filter: modelData
                    path: [index]
                }
            }
        }
    }

    component Label: Text {
        color: Theme.text
        font.family: Theme.fontSans
        font.pixelSize: Theme.textMd
        font.weight: Font.Medium
        wrapMode: Text.Wrap
    }

    // One filter; groups hold more of them.
    component FilterItem: Column {
        id: item
        property var filter: ({})
        property var path: []
        spacing: 8

        // Heading.
        Text {
            visible: item.filter.type === "header"
            width: parent.width
            text: item.filter.name || ""
            color: Theme.textMuted
            font.family: Theme.fontSans
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        Rectangle { visible: item.filter.type === "separator"; width: parent.width; height: 1; color: Theme.border }

        // One of a list.
        Row {
            visible: item.filter.type === "select"
            width: parent.width
            spacing: 12
            Label { width: parent.width - pick.width - 12; text: item.filter.name || ""; anchors.verticalCenter: parent.verticalCenter }
            Select {
                id: pick
                width: Math.min(240, item.width * 0.5)
                model: item.filter.type === "select" ? (item.filter.values || []).map((v, i) => ({ value: i, label: v })) : []
                value: item.filter.state || 0
                onPicked: (v) => sheet.set(item.path, v)
            }
        }

        // Text.
        Column {
            visible: item.filter.type === "text"
            width: parent.width
            spacing: 6
            Label { width: parent.width; text: item.filter.name || "" }
            InlineField {
                width: parent.width
                text: item.filter.type === "text" ? (item.filter.state || "") : ""
                onEditingFinished: if (text !== (item.filter.state || "")) sheet.set(item.path, text)
            }
        }

        // A check box (on its own; inside a group it is a chip).
        Row {
            visible: item.filter.type === "checkbox" && item.path.length === 1
            width: parent.width
            spacing: 12
            Label { width: parent.width - box.width - 12; text: item.filter.name || ""; anchors.verticalCenter: parent.verticalCenter }
            Toggle { id: box; checked: item.filter.state === true; onToggled: sheet.set(item.path, checked) }
        }

        // Include / exclude (on its own).
        Row {
            visible: item.filter.type === "tristate" && item.path.length === 1
            width: parent.width
            spacing: 12
            Label { width: parent.width - tri.width - 12; text: item.filter.name || ""; anchors.verticalCenter: parent.verticalCenter }
            TriChip { id: tri; filter: item.filter; path: item.path; text: [qsTr("Any"), qsTr("Include"), qsTr("Exclude")][item.filter.state || 0] }
        }

        // Sorting: by what, and which way.
        Row {
            visible: item.filter.type === "sort"
            width: parent.width
            spacing: 10
            Label { width: parent.width - sortPick.width - direction.width - 20; text: item.filter.name || ""; anchors.verticalCenter: parent.verticalCenter }
            Select {
                id: sortPick
                width: Math.min(220, item.width * 0.45)
                model: item.filter.type === "sort" ? (item.filter.values || []).map((v, i) => ({ value: i, label: v })) : []
                value: item.filter.state ? item.filter.state.index : -1
                onPicked: (v) => sheet.set(item.path, { index: v, ascending: item.filter.state ? item.filter.state.ascending : false })
            }
            IconButton {
                id: direction
                anchors.verticalCenter: parent.verticalCenter
                iconName: item.filter.state && item.filter.state.ascending ? "arrow-up" : "arrow-down-wide-narrow"
                tip: item.filter.state && item.filter.state.ascending ? qsTr("Ascending") : qsTr("Descending")
                onClicked: sheet.set(item.path, { index: item.filter.state ? item.filter.state.index : 0,
                                                  ascending: !(item.filter.state && item.filter.state.ascending) })
            }
        }

        // A group: check boxes and include/exclude boxes as chips.
        Column {
            visible: item.filter.type === "group"
            width: parent.width
            spacing: 8
            Label { width: parent.width; text: item.filter.name || "" }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: item.filter.type === "group" ? (item.filter.filters || []) : []
                    delegate: Loader {
                        id: child
                        required property var modelData
                        required property int index
                        readonly property var childPath: item.path.concat([index])
                        sourceComponent: !modelData ? null
                            : modelData.type === "checkbox" ? checkChip
                            : modelData.type === "tristate" ? triChip : otherChild
                        Component {
                            id: checkChip
                            Chip {
                                text: child.modelData.name
                                checkable: true
                                checked: child.modelData.state === true
                                onClicked: sheet.set(child.childPath, checked)
                            }
                        }
                        Component {
                            id: triChip
                            TriChip { filter: child.modelData; path: child.childPath; text: child.modelData.name }
                        }
                        // Lists and text inside a group (rare) take a line each.
                        Component {
                            id: otherChild
                            Row {
                                width: item.width
                                spacing: 12
                                Label { width: parent.width - control.width - 12; text: child.modelData.name || ""; anchors.verticalCenter: parent.verticalCenter }
                                Loader {
                                    id: control
                                    sourceComponent: child.modelData.type === "select" ? groupSelect : child.modelData.type === "text" ? groupText : null
                                }
                                Component {
                                    id: groupSelect
                                    Select {
                                        width: Math.min(220, item.width * 0.45)
                                        model: (child.modelData.values || []).map((v, i) => ({ value: i, label: v }))
                                        value: child.modelData.state || 0
                                        onPicked: (v) => sheet.set(child.childPath, v)
                                    }
                                }
                                Component {
                                    id: groupText
                                    InlineField {
                                        width: Math.min(220, item.width * 0.45)
                                        text: child.modelData.state || ""
                                        onEditingFinished: if (text !== (child.modelData.state || "")) sheet.set(child.childPath, text)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Any → include → exclude, as Mihon's include/exclude boxes.
    component TriChip: Chip {
        property var filter: ({})
        property var path: []
        readonly property int tri: filter.state || 0
        selected: tri !== 0
        iconName: tri === 1 ? "check" : tri === 2 ? "x" : ""
        dot: tri === 2 ? Theme.danger : "transparent"
        onClicked: sheet.set(path, (tri + 1) % 3)
    }
}
