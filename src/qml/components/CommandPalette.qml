import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Ctrl+K: jump anywhere, search any media type, reopen something recent, or
// run a quick command (theme, motion, update check, sync, shortcuts).
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, parent.width - 80)
    height: Math.min(520, parent.height - 120)
    modal: true
    focus: true
    padding: 0
    property var recent: []
    property int selected: 0

    readonly property var actions: {
        const q = input.text.trim()
        const out = []
        if (q.length > 0) {
            out.push({ group: qsTr("Search"), icon: "book-open", label: qsTr("Search comics for “") + q + "”", run: () => searchIn("comics", q) })
            out.push({ group: qsTr("Search"), icon: "book-text", label: qsTr("Search novels for “") + q + "”", run: () => searchIn("novels", q) })
            out.push({ group: qsTr("Search"), icon: "tv", label: qsTr("Search anime for “") + q + "”", run: () => searchIn("anime", q) })
        }
        const lower = q.toLowerCase()
        for (const entry of recent) {
            if (out.length > 14) break
            if (lower && !(entry.title || "").toLowerCase().includes(lower)) continue
            out.push({ group: qsTr("Recent"), icon: entry.media === "anime" ? "tv" : entry.media === "novel" ? "book-text" : "book-open",
                       label: entry.title, detail: entry.unitTitle || "", cover: entry.cover, run: () => Router.resume(entry) })
        }
        const pages = [["home", "Home", "house"], ["comics", qsTr("Comics"), "book-open"], ["novels", qsTr("Novels"), "book-text"],
                       ["anime", qsTr("Anime"), "tv"], ["library", qsTr("Library"), "library"], ["history", qsTr("History"), "history"],
                       ["activity", qsTr("Activity"), "activity"], ["latest", qsTr("Latest"), "flame"],
                       ["downloads", qsTr("Downloads"), "download"], ["settings", qsTr("Settings"), "settings"]]
        for (const [key, label, icon] of pages)
            if (!lower || label.toLowerCase().includes(lower))
                out.push({ group: qsTr("Pages"), icon: icon, label: qsTr("Go to ") + label, run: () => Router.go(key) })
        const commands = [
            { icon: Theme.dark ? "sun" : "moon", label: Theme.dark ? qsTr("Switch to the light theme") : qsTr("Switch to the dark theme"),
              run: () => App.setSetting("theme", Theme.dark ? "light" : "dark") },
            { icon: "sparkles", label: Motion.reduced ? qsTr("Turn animations back on") : qsTr("Reduce motion"),
              run: () => App.setSetting("reducedMotion", !Motion.reduced) },
            { icon: "refresh-cw", label: qsTr("Check the library for new chapters"), run: () => checkLibrary() },
            { icon: "venetian-mask", label: App.settings.incognito === true ? qsTr("Turn incognito off") : qsTr("Turn incognito on"),
              run: () => Router.setIncognito(App.settings.incognito !== true) }
        ]
        if (Lock.enabled)
            commands.push({ icon: "lock", label: qsTr("Lock SCANCORE"), run: () => Lock.lockNow() })
        if (Account.signedIn)
            commands.push({ icon: "cloud", label: qsTr("Sync with scancore.onl"), run: () => Account.sync() })
        if (App.platform !== "android")
            commands.push({ icon: "keyboard", label: qsTr("Keyboard shortcuts"), detail: "?", run: () => Router.shell.openShortcuts() })
        for (const c of commands)
            if (!lower || c.label.toLowerCase().includes(lower))
                out.push(Object.assign({ group: qsTr("Commands") }, c))
        return out
    }

    // Held here so the check is not cancelled when the palette closes.
    property var checkTask: null
    function checkLibrary() {
        Router.toast(qsTr("Checking your library for new chapters…"))
        checkTask = Library.checkUpdates()
    }
    Connections {
        target: root.checkTask
        function onFinished(result) {
            if (result.newUnits === 0) Router.toast(qsTr("Everything is up to date"))
        }
        function onFailed(error) { Router.toast(error, "error") }
    }

    function searchIn(section, q) {
        Router.go(section)
        Qt.callLater(() => {
            const stack = Router.shell.stackFor(section)
            if (stack && stack.depth > 1) stack.pop(null)
            const page = stack ? stack.get(0) : null
            if (page && page.search) page.search(q)
        })
    }

    onOpened: {
        input.text = ""
        selected = 0
        input.forceActiveFocus()
        historyTask = Library.history("", 30)
    }
    property var historyTask: null
    Connections {
        target: root.historyTask
        function onFinished(result) { root.recent = result }
    }

    function runSelected() {
        const action = actions[selected]
        if (!action) return
        close()
        action.run()
    }

    Overlay.modal: Rectangle {
        color: Theme.overlay
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.95; to: 1; duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.spring }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Motion.fast }
            NumberAnimation { property: "scale"; to: 0.97; duration: Motion.fast }
        }
    }

    background: GlassPanel {
        radius: Theme.radiusXl
        fill: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    contentItem: Column {
        TextField {
            id: input
            width: parent.width
            height: 60
            leftPadding: 54
            rightPadding: 20
            placeholderText: qsTr("Search comics, novels, anime or jump to a page")
            placeholderTextColor: Theme.textFaint
            color: Theme.text
            font.family: Theme.fontSans
            font.pixelSize: Theme.textLg
            background: Item {
                Icon {
                    x: 20
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 19
                    color: Theme.textMuted
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.border
                }
            }
            onTextChanged: root.selected = 0
            Keys.onDownPressed: root.selected = Math.min(root.actions.length - 1, root.selected + 1)
            Keys.onUpPressed: root.selected = Math.max(0, root.selected - 1)
            EnterKey.type: Qt.EnterKeyGo
            Keys.onReturnPressed: root.runSelected()
            Keys.onEnterPressed: root.runSelected()
            Keys.onEscapePressed: root.close()
        }

        ListView {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            WheelScroll {}
            id: list
            width: parent.width
            height: root.height - input.height
            clip: true
            topMargin: 8
            bottomMargin: 8
            model: root.actions
            currentIndex: root.selected
            highlightMoveDuration: Motion.fast
            highlightFollowsCurrentItem: true
            // Sized to the whole row, header included; only the item part lights up.
            highlight: Item {
                Rectangle {
                    x: 8
                    anchors.bottom: parent.bottom
                    width: list.width - 16
                    height: 46
                    radius: Theme.radiusMd
                    color: Theme.surface3
                }
            }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                // Each group (Search, Recent, Pages, Commands) starts with its name.
                readonly property bool firstOfGroup: index === 0 || root.actions[index - 1].group !== modelData.group
                width: list.width
                height: 46 + (firstOfGroup ? 30 : 0)
                Text {
                    visible: row.firstOfGroup
                    x: 22
                    y: row.index === 0 ? 6 : 10
                    text: row.modelData.group || ""
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.1
                }
                Item {
                    id: line
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 46
                    Row {
                        x: 22
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14
                        Item {
                            width: 28
                            height: 28
                            anchors.verticalCenter: parent.verticalCenter
                            Cover {
                                visible: !!modelData.cover
                                anchors.fill: parent
                                radius: 6
                                source: modelData.cover || ""
                            }
                            Icon {
                                visible: !modelData.cover
                                anchors.centerIn: parent
                                name: modelData.icon
                                size: 17
                                color: Theme.textMuted
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: list.width - 160
                            text: modelData.label
                            color: Theme.text
                            font.family: Theme.fontSans
                            font.pixelSize: Theme.textMd
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 22
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110
                        horizontalAlignment: Text.AlignRight
                        text: modelData.detail || ""
                        color: Theme.textFaint
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textXs
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    anchors.fill: line
                    hoverEnabled: true
                    onEntered: root.selected = index
                    onClicked: {
                        root.selected = index
                        root.runSelected()
                    }
                }
            }
        }
    }
}
