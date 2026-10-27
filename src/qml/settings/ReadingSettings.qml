import QtQuick
import QtQuick.Controls.Basic
import Scancore

SettingsPane {
    id: pane
    title: qsTr("Reading")
    subtitle: qsTr("Comic and novel readers")
    iconName: "book-open"

    readonly property int tileWidth: Theme.compact ? Math.floor((width - 36) / 2) : 168
    readonly property var look: Theme.readingLook(App.settings.novelTheme)
    readonly property int sliderWidth: Theme.compact ? width - 80 : 220
    readonly property var railColours: [
        { key: "readerRailUnread", label: qsTr("Not read yet"), fallback: "#e4e4e7" },
        { key: "readerRailRead", label: qsTr("Read"), fallback: "#8b8b94" },
        { key: "readerRailCurrent", label: qsTr("Current page"), fallback: "#000000" }]

    // A value next to its slider.
    component Readout: Text {
        color: Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.textSm
        font.features: { "tnum": 1 }
        anchors.verticalCenter: parent.verticalCenter
    }

    SettingsGroup {
        title: qsTr("Chapters and screen")
        SettingRow {
            iconName: "list"
            title: qsTr("Show chapters")
            description: qsTr("On a comic's or novel's page: every chapter in one list, or ten at a time with page numbers.")
            Segmented {
                options: [{ value: "list", label: qsTr("One list") }, { value: "pages", label: qsTr("Pages of 10") }]
                value: App.settings.chapterList || "list"
                onPicked: (v) => App.setSetting("chapterList", v)
            }
        }
        SettingRow {
            iconName: "layout-grid"
            title: qsTr("Chapter layout")
            description: qsTr("Rows, or a grid of two or more for short titles such as volumes. Each series can have its own from the grid button above its chapters.")
            Segmented {
                options: [{ value: "list", label: qsTr("Rows") }, { value: "grid", label: qsTr("Grid") }]
                value: App.settings.chapterLayout || "list"
                onPicked: (v) => App.setSetting("chapterLayout", v)
            }
        }
        SettingRow {
            iconName: "sun"
            title: qsTr("Keep the screen on while reading")
            description: qsTr("The screen does not dim or lock while a reader is open. Videos always keep it on while they play.")
            Toggle { checked: App.settings.readerKeepAwake !== false; onToggled: App.setSetting("readerKeepAwake", checked) }
        }
        SettingRow {
            visible: App.platform === "android"
            iconName: "maximize"
            title: qsTr("Full screen while reading")
            description: qsTr("The status and navigation bars hide with the reader's controls, so pages fill the screen, beside a punch hole too. Swipe from the edge to see them.")
            Toggle { checked: App.settings.readerHideBars !== false; onToggled: App.setSetting("readerHideBars", checked) }
        }
    }

    SettingsGroup {
        title: qsTr("Comic reader")
        note: qsTr("While reading: M switches layout, Ctrl + scroll changes the strip width (or zooms a page), and F goes full screen. Each series can keep its own settings from the reader's settings button.")

        Flow {
            x: 12
            width: parent.width - 24
            topPadding: 12
            bottomPadding: 12
            spacing: 12
            ChoiceTile {
                id: stripTile
                width: pane.tileWidth
                height: Math.max(implicitHeight, pagesTile.implicitHeight)
                label: qsTr("Vertical strip")
                detail: qsTr("Webtoons, manhwa")
                selected: App.settings.readerMode !== "paged"
                onClicked: App.setSetting("readerMode", "strip")
                Column {
                    anchors.centerIn: parent
                    spacing: 3
                    Repeater {
                        model: 3
                        Rectangle { width: 34; height: 22; radius: 3; color: index === 1 ? Theme.borderStrong : Theme.surface3 }
                    }
                }
            }
            ChoiceTile {
                id: pagesTile
                width: pane.tileWidth
                height: Math.max(implicitHeight, stripTile.implicitHeight)
                label: qsTr("Pages")
                detail: qsTr("Manga")
                selected: App.settings.readerMode === "paged"
                onClicked: App.setSetting("readerMode", "paged")
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    Repeater {
                        model: 2
                        Rectangle { width: 30; height: 44; radius: 3; color: index === 0 ? Theme.borderStrong : Theme.surface3 }
                    }
                }
            }
        }
        SettingRow {
            title: qsTr("Page direction")
            description: qsTr("For the Pages layout.")
            Segmented {
                options: [{ value: "ltr", label: qsTr("Left to right") }, { value: "rtl", label: qsTr("Right to left") }]
                value: App.settings.readerDirection || "ltr"
                onPicked: (v) => App.setSetting("readerDirection", v)
            }
        }
        SettingRow {
            visible: App.platform !== "android"
            title: qsTr("Two pages side by side")
            description: qsTr("In the Pages layout, when the window is wider than it is tall. Covers and double-page spreads stay alone.")
            Toggle { checked: App.settings.readerSpread === "double"; onToggled: App.setSetting("readerSpread", checked ? "double" : "single") }
        }
        SettingRow {
            title: qsTr("Trim page borders")
            description: qsTr("Cuts the plain white or black margin around scanned pages in the Pages layout.")
            Toggle { checked: App.settings.readerCrop === true; onToggled: App.setSetting("readerCrop", checked) }
        }
        SettingRow {
            title: qsTr("Split wide pages")
            description: qsTr("Shows a two-page spread as its two halves, one after the other, in the Pages layout.")
            Toggle { checked: App.settings.readerSplitWide === true; onToggled: App.setSetting("readerSplitWide", checked) }
        }
        SettingRow {
            title: qsTr("Tap zones")
            description: qsTr("Where a tap turns the page back or forward, or shows the controls. The reader shows them when you change them.")
            Select {
                model: [{ value: "sides", label: qsTr("Left and right") }, { value: "lshape", label: qsTr("L shape") },
                        { value: "kindle", label: qsTr("Kindle") }, { value: "edge", label: qsTr("Edges") }, { value: "off", label: qsTr("Off") }]
                value: App.settings.readerTapZones || "sides"
                onPicked: (v) => App.setSetting("readerTapZones", v)
            }
        }
        SettingRow {
            visible: App.settings.readerTapZones !== "off"
            title: qsTr("Flip the tap zones")
            Select {
                model: [{ value: "none", label: qsTr("Not flipped") }, { value: "horizontal", label: qsTr("Left and right") },
                        { value: "vertical", label: qsTr("Top and bottom") }, { value: "both", label: qsTr("Both ways") }]
                value: App.settings.readerTapInvert || "none"
                onPicked: (v) => App.setSetting("readerTapInvert", v)
            }
        }
        SettingRow {
            visible: App.settings.readerTapZones !== "off"
            title: qsTr("Tap zones in the vertical strip")
            description: qsTr("A tap scrolls a screen up or down instead of showing the controls.")
            Toggle { checked: App.settings.readerStripTaps === true; onToggled: App.setSetting("readerStripTaps", checked) }
        }
        SettingRow {
            iconName: "zoom-in"
            title: qsTr("Long press to zoom")
            description: qsTr("Hold a finger or the mouse on the page to see it twice as big until you let go, for small dialogue and notes in the margins. When off, holding a page opens its menu: copy, save or share it.")
            Toggle { checked: App.settings.readerLongPressZoom !== false; onToggled: App.setSetting("readerLongPressZoom", checked) }
        }
        SettingRow {
            visible: App.settings.readerLongPressZoom !== false
            iconName: "hand"
            title: qsTr("Long press zoom position")
            Segmented {
                options: [{ value: "press", label: qsTr("Where you press") }, { value: "centre", label: qsTr("Centre") }]
                value: App.settings.readerLongPressZoomAt || "press"
                onPicked: (v) => App.setSetting("readerLongPressZoomAt", v)
            }
        }
        SettingRow {
            iconName: "maximize"
            title: qsTr("Double tap to zoom")
            description: qsTr("On pages: a double tap in the middle zooms in, another zooms out.")
            Toggle { checked: App.settings.readerDoubleTapZoom !== false; onToggled: App.setSetting("readerDoubleTapZoom", checked) }
        }
        SettingRow {
            visible: App.platform === "android"
            title: qsTr("Volume keys turn pages")
            description: qsTr("Volume down goes forward, volume up back, while a comic is open.")
            Toggle { checked: App.settings.readerVolumeKeys === true; onToggled: App.setSetting("readerVolumeKeys", checked) }
        }
        SettingRow {
            title: qsTr("Strip width")
            description: qsTr("How wide the vertical strip is on large screens.")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 360; to: 2000; stepSize: 20; value: App.settings.readerWidth; onMoved: App.setSetting("readerWidth", value) }
                Readout { text: App.settings.readerWidth + qsTr(" px") }
            }
        }
        SettingRow {
            title: qsTr("Run on into the next chapter")
            description: qsTr("The next chapter joins the strip as you near the end, so reading never stops for a button.")
            Toggle { checked: App.settings.readerContinuous !== false; onToggled: App.setSetting("readerContinuous", checked) }
        }
        SettingRow {
            title: qsTr("Gap between panels")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 0; to: 40; stepSize: 2; value: App.settings.readerGap || 0; onMoved: App.setSetting("readerGap", value) }
                Readout { text: (App.settings.readerGap || 0) + qsTr(" px") }
            }
        }
    }

    // The comic reader's progress bar: picked by looking at it, then shaped.
    readonly property bool barOn: App.settings.readerProgressBar !== false
    readonly property var railPresets: [
        { label: qsTr("Grey"), unread: "#e4e4e7", read: "#8b8b94", current: "#000000" },
        { label: qsTr("Accent"), unread: "#3f3f46", read: String(Theme.accent), current: "#ffffff" },
        { label: qsTr("Ocean"), unread: "#1e293b", read: "#38bdf8", current: "#f0f9ff" },
        { label: qsTr("Rose"), unread: "#3b1d25", read: "#fb7185", current: "#fff1f2" },
        { label: qsTr("Mint"), unread: "#1c2b25", read: "#34d399", current: "#ecfdf5" }]
    SettingsGroup {
        title: qsTr("Comic progress bar")
        note: qsTr("How far through the chapter you are. Tap or drag along it in the reader to go to a page.")
        Item {
            width: parent.width
            height: barPicker.height + 28
            ProgressBarPicker {
                id: barPicker
                x: 16
                y: 14
                width: parent.width - 32
                // Phones: two by two, big enough to tell the bars apart.
                tileWidth: Theme.compact ? Math.min(170, Math.floor((width - spacing) / 2)) : Math.min(140, Math.floor((width - 3 * spacing) / 4))
            }
        }
        SettingRow {
            visible: pane.barOn
            title: qsTr("Show it")
            Segmented {
                options: [{ value: true, label: qsTr("Always") }, { value: "controls", label: qsTr("With controls") }]
                value: App.settings.readerProgressBar === "controls" ? "controls" : true
                onPicked: (v) => App.setSetting("readerProgressBar", v)
            }
        }
        SettingRow {
            visible: pane.barOn
            title: qsTr("Edge")
            Segmented {
                options: [{ value: "left", label: qsTr("Left") }, { value: "right", label: qsTr("Right") }, { value: "bottom", label: qsTr("Bottom") }]
                value: App.settings.readerProgressEdge || "left"
                onPicked: (v) => App.setSetting("readerProgressEdge", v)
            }
        }
        SettingRow {
            visible: pane.barOn
            title: qsTr("Thickness")
            Segmented {
                options: [{ value: 3, label: qsTr("Thin") }, { value: 6, label: qsTr("Medium") }, { value: 10, label: qsTr("Thick") }]
                value: Number(App.settings.readerProgressWidth) || 3
                onPicked: (v) => App.setSetting("readerProgressWidth", v)
            }
        }
        SettingRow {
            visible: pane.barOn
            title: qsTr("Strength")
            description: qsTr("Lower lets the page show through.")
            Row {
                spacing: 12
                ScSlider {
                    width: pane.sliderWidth
                    from: 0.3
                    to: 1
                    stepSize: 0.05
                    value: App.settings.readerProgressOpacity !== undefined ? Number(App.settings.readerProgressOpacity) : 0.85
                    onMoved: App.setSetting("readerProgressOpacity", Math.round(value * 100) / 100)
                }
                Readout { text: Math.round((App.settings.readerProgressOpacity !== undefined ? Number(App.settings.readerProgressOpacity) : 0.85) * 100) + "%" }
            }
        }
        SettingRow {
            visible: pane.barOn
            title: qsTr("Colours")
            description: qsTr("Pages not read yet, pages read, and the page you are on.")
            Row {
                spacing: 10
                Repeater {
                    model: pane.railColours
                    AbstractButton {
                        id: swatch
                        required property var modelData
                        width: 36
                        height: 36
                        hoverEnabled: true
                        Accessible.name: modelData.label
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.label
                        onClicked: { railPicker.key = modelData.key; railPicker.title = modelData.label; railPicker.edit(App.settings[modelData.key]) }
                        background: Rectangle {
                            radius: Theme.radiusMd
                            color: App.settings[swatch.modelData.key] || "#000000"
                            border.width: 1
                            border.color: swatch.hovered ? Theme.text : Theme.borderStrong
                        }
                    }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: pane.railColours.some(c => App.settings[c.key] !== c.fallback)
                    iconName: "rotate-ccw"
                    tip: qsTr("Reset colours")
                    Accessible.name: tip
                    onClicked: { for (const c of pane.railColours) App.setSetting(c.key, c.fallback) }
                }
            }
        }
        // Sets of colours that go together.
        Item {
            visible: pane.barOn
            width: parent.width
            height: visible ? presetRow.height + 20 : 0
            ChipRow {
                id: presetRow
                y: 4
                padding: 16
                Repeater {
                    model: pane.railPresets
                    Chip {
                        required property var modelData
                        text: modelData.label
                        dot: modelData.read
                        selected: App.settings.readerRailUnread === modelData.unread && App.settings.readerRailRead === modelData.read
                                  && App.settings.readerRailCurrent === modelData.current
                        onClicked: {
                            App.setSetting("readerRailUnread", modelData.unread)
                            App.setSetting("readerRailRead", modelData.read)
                            App.setSetting("readerRailCurrent", modelData.current)
                        }
                    }
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Novel reader")

        // Live preview in the chosen type and reading theme.
        Item {
            width: parent.width
            height: preview.height + 24
            Rectangle {
                id: preview
                x: 12
                y: 12
                width: parent.width - 24
                height: sample.implicitHeight + 40
                radius: Theme.radiusMd
                color: pane.look.bg
                border.width: 1
                border.color: Theme.border
                Behavior on color { ColorAnimation { duration: Motion.normal } }
                Text {
                    id: sample
                    x: 20
                    y: 20
                    width: parent.width - 40
                    text: qsTr("The lamps of the old city came on one by one, and for a moment the whole street seemed to hold its breath. ")
                        + qsTr("She turned the page and kept reading.")
                    color: pane.look.text
                    font.family: App.settings.novelFont || Theme.fontSerif
                    font.weight: App.settings.novelWeight || Font.Normal
                    font.pixelSize: App.settings.novelSize || 19
                    lineHeight: App.settings.novelLineHeight || 1.5
                    horizontalAlignment: App.settings.novelJustify === true ? Text.AlignJustify : Text.AlignLeft
                    wrapMode: Text.Wrap
                }
            }
        }
        SettingRow {
            title: qsTr("Reading theme")
            Row {
                spacing: 8
                Repeater {
                    model: ["app", "paper", "sepia", "dark", "black", "custom"]
                    AbstractButton {
                        id: chip
                        required property string modelData
                        readonly property var colors: Theme.readingLook(modelData)
                        readonly property bool selected: (App.settings.novelTheme || "app") === modelData
                        width: Theme.compact ? 44 : 50
                        height: 50
                        hoverEnabled: true
                        onClicked: App.setSetting("novelTheme", modelData)
                        background: Rectangle {
                            radius: Theme.radiusMd
                            color: chip.colors.bg
                            border.width: chip.selected ? 2 : 1
                            border.color: chip.selected ? Theme.accent : chip.hovered ? Theme.borderStrong : Theme.border
                        }
                        contentItem: Column {
                            topPadding: 6
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Aa"
                                color: chip.colors.text
                                font.family: Theme.fontSerif
                                font.pixelSize: 15
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: chip.colors.label
                                color: chip.colors.muted
                                font.family: Theme.fontSans
                                font.pixelSize: 9
                            }
                        }
                    }
                }
            }
        }
        SettingRow {
            title: qsTr("Font")
            Segmented {
                options: [{ value: "Literata", label: qsTr("Serif") }, { value: "DM Sans", label: qsTr("Sans") }, { value: "Space Grotesk", label: qsTr("Grotesk") }]
                value: App.settings.novelFont
                onPicked: (v) => App.setSetting("novelFont", v)
            }
        }
        SettingRow {
            title: qsTr("Text size")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 13; to: 34; stepSize: 1; value: App.settings.novelSize; onMoved: App.setSetting("novelSize", value) }
                Readout { text: App.settings.novelSize + qsTr(" px") }
            }
        }
        SettingRow {
            title: qsTr("Line spacing")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 1.2; to: 2.2; stepSize: 0.05; value: App.settings.novelLineHeight || 1.5; onMoved: App.setSetting("novelLineHeight", Math.round(value * 100) / 100) }
                Readout { text: Number(App.settings.novelLineHeight || 1.5).toFixed(2) }
            }
        }
        SettingRow {
            visible: !Theme.compact
            title: qsTr("Column width")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 480; to: 1100; stepSize: 20; value: App.settings.novelWidth || 720; onMoved: App.setSetting("novelWidth", value) }
                Readout { text: (App.settings.novelWidth || 720) + qsTr(" px") }
            }
        }
        SettingRow {
            title: qsTr("Justify text")
            description: qsTr("Even right edges, like a printed book.")
            Toggle { checked: App.settings.novelJustify === true; onToggled: App.setSetting("novelJustify", checked) }
        }
        SettingRow {
            title: qsTr("Font weight")
            Segmented {
                options: [{ value: 300, label: qsTr("Light") }, { value: 400, label: qsTr("Regular") }, { value: 500, label: qsTr("Medium") }, { value: 600, label: qsTr("Bold") }]
                value: App.settings.novelWeight || 400
                onPicked: (v) => App.setSetting("novelWeight", v)
            }
        }
        SettingRow {
            title: qsTr("Paragraph spacing")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 0; to: 2.5; stepSize: 0.1; value: App.settings.novelParagraphSpacing === undefined ? 0.9 : App.settings.novelParagraphSpacing; onMoved: App.setSetting("novelParagraphSpacing", Math.round(value * 10) / 10) }
                Readout { text: Number(App.settings.novelParagraphSpacing === undefined ? 0.9 : App.settings.novelParagraphSpacing).toFixed(1) }
            }
        }
        SettingRow {
            title: qsTr("Side margins")
            description: qsTr("Space beside the text on narrow screens.")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 8; to: 120; stepSize: 2; value: App.settings.novelMargin === undefined ? 32 : App.settings.novelMargin; onMoved: App.setSetting("novelMargin", value) }
                Readout { text: (App.settings.novelMargin === undefined ? 32 : App.settings.novelMargin) + qsTr(" px") }
            }
        }
        SettingRow {
            title: qsTr("Indent paragraphs")
            Toggle { checked: App.settings.novelIndent === true; onToggled: App.setSetting("novelIndent", checked) }
        }
        SettingRow {
            title: qsTr("Dim the page")
            description: qsTr("Darker than the screen goes, for reading in the dark.")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 0; to: 0.8; stepSize: 0.05; value: Number(App.settings.novelDim) || 0; onMoved: App.setSetting("novelDim", Math.round(value * 100) / 100) }
                Readout { text: Math.round((Number(App.settings.novelDim) || 0) * 100) + "%" }
            }
        }
    }

    // Translating comic pages and novel chapters (Translator).
    SettingsGroup {
        title: qsTr("Translate")
        note: Translator.pagesReady ? qsTr("Comic readers have Translate the pages in their top bar. Keys stay on this device and are only sent to their own service.")
                                    : Translator.hint
        SettingRow {
            iconName: "languages"
            title: qsTr("Translate into")
            Select {
                model: Customize.languages
                value: Translator.target
                onPicked: (v) => App.setSetting("translateTarget", v)
            }
        }
        SettingRow {
            visible: OnDevice.available
            iconName: "download"
            title: App.platform === "android" ? qsTr("Translate on this device") : qsTr("Translate on this computer")
            description: {
                App.dependOn(OnDevice.revision)
                return OnDevice.any() ? qsTr("Pages and chapters translated with nothing sent anywhere. Add or remove languages.")
                                      : qsTr("Download a language once; pages and chapters are then translated with nothing sent anywhere.")
            }
            ScButton {
                text: qsTr("Languages…")
                onClicked: onDeviceLanguages.start()
            }
            OnDeviceDialog { id: onDeviceLanguages }
        }
        SettingRow {
            iconName: "book-open"
            title: qsTr("Translate novels in other languages")
            description: qsTr("Chapters from a source in another language (WTR-LAB's, in English) open translated. The reader's Translate button decides for each series.")
            Toggle { checked: App.settings.translateNovelsAuto === true; onToggled: App.setSetting("translateNovelsAuto", checked) }
        }
        SettingRow {
            iconName: "scroll-text"
            title: qsTr("Read the page's text with")
            description: qsTr("Finds the text in a comic's pages.")
            Select {
                model: Translator.readers()
                value: App.settings.translateReader || "auto"
                onPicked: (v) => App.setSetting("translateReader", v)
            }
        }
        SettingRow {
            iconName: "globe"
            title: qsTr("Translate with")
            Select {
                model: Translator.translators()
                value: App.settings.translateText || "auto"
                onPicked: (v) => App.setSetting("translateText", v)
            }
        }
        SettingRow {
            iconName: "lock"
            title: qsTr("DeepL key")
            description: qsTr("From your DeepL account (free keys end in :fx).")
            InlineField {
                width: 220
                echoMode: TextInput.PasswordEchoOnEdit
                text: App.settings.translateDeeplKey || ""
                placeholderText: qsTr("Not set")
                onEditingFinished: App.setSetting("translateDeeplKey", text.trim())
            }
        }
        SettingRow {
            iconName: "lock"
            title: qsTr("Google Cloud key")
            description: qsTr("An API key with Cloud Translation and Cloud Vision turned on.")
            InlineField {
                width: 220
                echoMode: TextInput.PasswordEchoOnEdit
                text: App.settings.translateGoogleKey || ""
                placeholderText: qsTr("Not set")
                onEditingFinished: App.setSetting("translateGoogleKey", text.trim())
            }
        }
        SettingRow {
            iconName: "server"
            title: qsTr("Language model")
            description: qsTr("Any OpenAI-compatible service: its address up to /v1 (a local Ollama is http://localhost:11434/v1), a model and a key if it needs one.")
            wide: true
            Column {
                spacing: 8
                InlineField {
                    width: 260
                    text: App.settings.translateChatUrl || ""
                    placeholderText: "https://api.openai.com/v1"
                    onEditingFinished: App.setSetting("translateChatUrl", text.trim())
                }
                InlineField {
                    width: 260
                    text: App.settings.translateChatModel || ""
                    placeholderText: qsTr("Model, e.g. gpt-4o-mini")
                    onEditingFinished: App.setSetting("translateChatModel", text.trim())
                }
                InlineField {
                    width: 260
                    echoMode: TextInput.PasswordEchoOnEdit
                    text: App.settings.translateChatKey || ""
                    placeholderText: qsTr("Key (if it needs one)")
                    onEditingFinished: App.setSetting("translateChatKey", text.trim())
                }
            }
        }
        SettingRow {
            iconName: "image"
            title: qsTr("Show the model the page")
            description: qsTr("Sends the comic page's picture with its text, so the model can see who speaks. Needs a model that reads images; costs more.")
            Toggle { checked: App.settings.translateChatImage === true; onToggled: App.setSetting("translateChatImage", checked) }
        }
        SettingRow {
            iconName: "trash"
            title: qsTr("Forget translations")
            description: qsTr("Pages and chapters are translated again the next time.")
            ScButton {
                text: qsTr("Forget")
                variant: "ghost"
                onClicked: {
                    Translator.clearCache()
                    Router.toast(qsTr("Translations forgotten"))
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Read aloud")
        SettingRow {
            visible: !Speech.available
            title: qsTr("No voices are installed on this device")
            description: Piper.available ? qsTr("Download a neural voice below, or add one in the system's text-to-speech settings.")
                                         : qsTr("Add one in the system's text-to-speech settings.")
        }
        SettingRow {
            visible: Piper.available
            iconName: "volume-2"
            title: qsTr("Neural voices")
            description: Piper.voices.length > 0 ? qsTr("%1 downloaded; natural voices that need no internet").arg(Piper.voices.length)
                                                 : qsTr("Natural voices that read without the internet, downloaded once")
            ScButton {
                text: qsTr("Choose…")
                onClicked: neuralVoices.start()
            }
            NeuralVoicesDialog { id: neuralVoices }
        }
        SettingRow {
            visible: Speech.available
            title: qsTr("Voice")
            Select {
                model: [{ value: "", label: qsTr("System voice") }].concat(Speech.voices.map(v => ({ value: v.id, label: v.name + " · " + v.language })))
                value: App.settings.ttsVoice || ""
                onPicked: (v) => App.setSetting("ttsVoice", v)
            }
        }
        SettingRow {
            visible: Speech.available
            title: qsTr("Voice speed")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 0.5; to: 3; stepSize: 0.05; value: Number(App.settings.ttsRate) || 1; onMoved: App.setSetting("ttsRate", Math.round(value * 100) / 100) }
                Readout { text: Number(App.settings.ttsRate || 1).toFixed(2) + "×" }
            }
        }
        SettingRow {
            // Neural voices keep their own.
            visible: Speech.available && !String(App.settings.ttsVoice || "").startsWith("piper:")
            title: qsTr("Voice pitch")
            Row {
                spacing: 12
                ScSlider { width: pane.sliderWidth; from: 0.5; to: 2; stepSize: 0.05; value: Number(App.settings.ttsPitch) || 1; onMoved: App.setSetting("ttsPitch", Math.round(value * 100) / 100) }
                Readout { text: Number(App.settings.ttsPitch || 1).toFixed(2) + "×" }
            }
        }
        SettingRow {
            visible: Speech.available
            title: qsTr("Go on to the next chapter")
            Toggle { checked: App.settings.ttsContinue !== false; onToggled: App.setSetting("ttsContinue", checked) }
        }
        SettingRow {
            visible: Speech.available
            title: qsTr("Keep the spoken paragraph on screen")
            Toggle { checked: App.settings.ttsFollow !== false; onToggled: App.setSetting("ttsFollow", checked) }
        }
        SettingRow {
            visible: Speech.available
            title: qsTr("Highlight the spoken sentence")
            Toggle { checked: App.settings.ttsHighlight !== false; onToggled: App.setSetting("ttsHighlight", checked) }
        }
        SettingRow {
            visible: Speech.available
            title: qsTr("Pause between paragraphs")
            Row {
                spacing: 12
                ScSlider {
                    width: pane.sliderWidth; from: 0; to: 2000; stepSize: 50
                    value: App.settings.ttsParagraphPause === undefined ? 350 : Number(App.settings.ttsParagraphPause)
                    onMoved: App.setSetting("ttsParagraphPause", Math.round(value))
                }
                Readout { text: ((App.settings.ttsParagraphPause === undefined ? 350 : Number(App.settings.ttsParagraphPause)) / 1000).toFixed(1) + " s" }
            }
        }
        SettingRow {
            visible: Speech.available
            title: qsTr("Pause between sentences")
            Row {
                spacing: 12
                ScSlider {
                    width: pane.sliderWidth; from: 0; to: 1500; stepSize: 50
                    value: Number(App.settings.ttsSentencePause) || 0
                    onMoved: App.setSetting("ttsSentencePause", Math.round(value))
                }
                Readout { text: ((Number(App.settings.ttsSentencePause) || 0) / 1000).toFixed(1) + " s" }
            }
        }
        // A voice for each language a book can be in (a Korean web novel in
        // a Korean voice, the rest in yours).
        SettingRow {
            id: perLanguage
            visible: Speech.available && languages.length > 1
            title: qsTr("Voice for a language")
            description: qsTr("Books in this language are read with this voice.")
            readonly property var languages: {
                const seen = {}
                const out = []
                for (const s of Novels.sources()) {
                    const code = String(s.language || "")
                    if (!code || seen[code]) continue
                    seen[code] = true
                    out.push({ value: code, label: s.languageName || code })
                }
                return out.sort((a, b) => a.label.localeCompare(b.label))
            }
            property string language: languages.length ? languages[0].value : ""
            readonly property string base: language.split(/[-_]/)[0]
            Column {
                spacing: 8
                Select {
                    model: perLanguage.languages
                    value: perLanguage.language
                    onPicked: (v) => perLanguage.language = v
                }
                Select {
                    model: [{ value: "", label: qsTr("The voice above") }].concat(Speech.voices
                        .filter(v => String(v.language).split(/[-_]/)[0] === perLanguage.base)
                        .map(v => ({ value: v.id, label: v.name + " · " + v.language })))
                    value: {
                        App.dependOn(App.settings.ttsVoices)
                        const voice = ReadAloud.voiceFor(perLanguage.language)
                        return voice === (App.settings.ttsVoice || "") ? "" : voice
                    }
                    onPicked: (v) => ReadAloud.setVoiceFor(perLanguage.language, v)
                }
            }
        }
    }

    ColorPicker {
        id: railPicker
        property string key
        swatches: ["#000000", "#18181b", "#3f3f46", "#71717a", "#8b8b94", "#a1a1aa", "#d4d4d8", "#e4e4e7",
                   "#ffffff", "#7c9cff", "#a78bfa", "#34d399", "#fb923c", "#fb7185"]
        onChanged: (c) => App.setSetting(key, String(c).slice(0, 7))
    }
}
