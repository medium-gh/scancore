import QtQuick
import QtQuick.Controls.Basic
import Scancore

// Watch together (see Party): start a party for the anime being watched or
// join one with its code; in a party, its code to share, who is in it and
// the chat. From the right (from the bottom on phones), like the comments.
Drawer {
    id: panel
    // What a party started here is for ({anilistId, title, cover, episode});
    // empty where there is nothing to start one for (joining only).
    property var anime: ({})
    readonly property bool canStart: !!anime && (anime.anilistId || 0) > 0
    readonly property var hostMember: Party.members.find(m => m.host) || null
    readonly property bool elsewhere: Party.active && !Party.hosting && !!Party.state.anime
                                      && Party.state.anime.anilistId !== (anime ? anime.anilistId || 0 : 0)

    function avatarOf(url) { return url && url.startsWith("/") ? Account.site + url : url || "" }

    edge: Theme.compact ? Qt.BottomEdge : Qt.RightEdge
    width: Theme.compact ? parent.width : Math.min(420, parent.width)
    height: Theme.compact ? parent.height * 0.8 : parent.height
    parent: Overlay.overlay
    modal: true
    dim: true
    Overlay.modal: Rectangle { color: Theme.overlay }
    onOpened: Party.markRead()
    Connections {
        target: Party
        function onChatChanged() { if (panel.opened) Party.markRead() }
    }
    background: SurfacePanel {
        fill: Theme.surface
        radius: Theme.compact ? Theme.radiusXl : 0
        Rectangle {
            visible: !Theme.compact
            width: 1
            height: parent.height
            color: Theme.border
        }
    }

    contentItem: Item {
        Row {
            id: head
            x: 20
            y: 16
            width: parent.width - 40
            spacing: 8
            Column {
                width: parent.width - close.width - 8
                spacing: 2
                Text {
                    text: qsTr("Watch together")
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textXl
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: !Party.active ? qsTr("Everyone watches from their own source; the host's player steers the others.")
                        : Party.hosting ? qsTr("You are the host: your player steers everyone's.")
                        : panel.hostMember ? qsTr("Playing along with %1").arg(panel.hostMember.name)
                        : Party.connected ? qsTr("Waiting for the host…") : qsTr("Joining…")
                    wrapMode: Text.Wrap
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                }
            }
            IconButton {
                id: close
                iconName: "x"
                tip: qsTr("Close")
                onClicked: panel.close()
            }
        }

        // ------------------------------------------------ not in a party
        Column {
            visible: !Party.active
            x: 20
            y: head.y + head.height + 20
            width: parent.width - 40
            spacing: 14
            Text {
                visible: !Account.signedIn
                width: parent.width
                text: qsTr("Sign in to scancore.onl (Settings → Account) to watch together.")
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
            }
            ScButton {
                visible: panel.canStart && Account.signedIn
                width: parent.width
                variant: "primary"
                iconName: "users"
                text: Party.busy ? qsTr("Starting…") : qsTr("Start a watch party")
                enabled: !Party.busy && Party.available
                onClicked: Party.start(panel.anime)
            }
            Text {
                visible: Account.signedIn
                text: panel.canStart ? qsTr("Or join one") : qsTr("Join a watch party")
                color: Theme.textMuted
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
            }
            Row {
                visible: Account.signedIn
                width: parent.width
                spacing: 8
                InlineField {
                    id: codeField
                    width: parent.width - joinButton.width - 8
                    placeholderText: qsTr("Party code")
                    font.letterSpacing: 2
                    font.capitalization: Font.AllUppercase
                    inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    maximumLength: 8
                    onAccepted: if (joinButton.enabled) Party.join(text)
                }
                ScButton {
                    id: joinButton
                    text: qsTr("Join")
                    enabled: codeField.text.replace(/[\s-]/g, "").length === 6 && Party.available
                    onClicked: Party.join(codeField.text)
                }
            }
            Text {
                visible: Party.error.length > 0
                width: parent.width
                text: Party.error
                wrapMode: Text.Wrap
                color: Theme.danger
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
        }

        // ------------------------------------------------ in a party
        Column {
            id: top
            visible: Party.active
            x: 20
            y: head.y + head.height + 16
            width: parent.width - 40
            spacing: 12
            Rectangle {
                width: parent.width
                height: 64
                radius: Theme.radiusLg
                color: Theme.surface2
                border.width: 1
                border.color: Theme.border
                Text {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: Party.code.slice(0, 3) + " " + Party.code.slice(3)
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                    font.letterSpacing: 3
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    IconButton {
                        iconName: "copy"
                        tip: qsTr("Copy the code")
                        onClicked: { App.copy(Party.code); Router.toast(qsTr("Code copied"), "success") }
                    }
                    IconButton {
                        visible: App.platform === "android"
                        iconName: "share-2"
                        tip: qsTr("Share")
                        onClicked: App.shareText(qsTr("Watch %1 with me in SCANCORE: join the party with the code %2")
                                                 .arg(Party.state.anime ? Party.state.anime.title : panel.anime.title || "").arg(Party.code), qsTr("Watch party"))
                    }
                }
            }
            // The host is on another anime: open it.
            Rectangle {
                visible: panel.elsewhere
                width: parent.width
                height: elsewhereRow.height + 20
                radius: Theme.radiusLg
                color: Theme.alpha(Theme.accent, 0.12)
                Row {
                    id: elsewhereRow
                    x: 12
                    y: 10
                    width: parent.width - 24
                    spacing: 10
                    Text {
                        width: parent.width - openButton.width - 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: Party.state.anime ? qsTr("The host is watching %1, episode %2").arg(Party.state.anime.title).arg(Party.state.episode) : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textSm
                    }
                    ScButton {
                        id: openButton
                        compact: true
                        variant: "primary"
                        text: qsTr("Open")
                        onClicked: { panel.close(); Router.openPartyAnime(Party.state) }
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: Party.members
                    Rectangle {
                        required property var modelData
                        height: 30
                        width: memberRow.width + 16
                        radius: Theme.pill(height)
                        color: Theme.surface2
                        border.width: modelData.host ? 1 : 0
                        border.color: Theme.alpha(Theme.accent, 0.6)
                        Row {
                            id: memberRow
                            x: 4
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            Rectangle {
                                width: 22
                                height: 22
                                radius: 11
                                color: Theme.alpha(Theme.accent, 0.25)
                                Text {
                                    anchors.centerIn: parent
                                    text: (modelData.name || "?").charAt(0).toUpperCase()
                                    color: Theme.text
                                    font.family: Theme.fontSans
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                                Cover {
                                    anchors.fill: parent
                                    visible: !!modelData.avatar && ready
                                    source: panel.avatarOf(modelData.avatar)
                                    radius: 11
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name + (modelData.host ? " · " + qsTr("host") : "")
                                color: Theme.text
                                font.family: Theme.fontSans
                                font.pixelSize: Theme.textSm
                            }
                        }
                    }
                }
            }
        }

        ListView {
            id: chatList
            visible: Party.active
            x: 20
            y: top.y + top.height + 12
            width: parent.width - 40
            height: parent.height - y - bottom.height - 16
            clip: true
            spacing: 8
            model: Party.chat
            onCountChanged: Qt.callLater(positionViewAtEnd)
            delegate: Column {
                required property var modelData
                width: chatList.width
                spacing: 2
                Text {
                    visible: !modelData.mine
                    text: modelData.name
                    textFormat: Text.PlainText
                    color: Theme.textFaint
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs
                }
                Rectangle {
                    anchors.right: modelData.mine ? parent.right : undefined
                    width: Math.min(chatList.width * 0.85, line.implicitWidth + 24)
                    height: line.height + 14
                    radius: Theme.radiusLg
                    color: modelData.mine ? Theme.alpha(Theme.accent, 0.22) : Theme.surface2
                    Text {
                        id: line
                        x: 12
                        y: 7
                        width: parent.width - 24
                        text: modelData.text
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.text
                        font.family: Theme.fontSans
                        font.pixelSize: Theme.textMd
                    }
                }
            }
            Text {
                visible: chatList.count === 0
                anchors.centerIn: parent
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Say hello: everyone in the party sees it.")
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
            }
        }

        Column {
            id: bottom
            visible: Party.active
            x: 20
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            width: parent.width - 40
            spacing: 10
            Row {
                width: parent.width
                spacing: 8
                InlineField {
                    id: say
                    width: parent.width - sendButton.width - 8
                    placeholderText: qsTr("Message")
                    maximumLength: 300
                    onAccepted: sendButton.clicked()
                }
                IconButton {
                    id: sendButton
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "send"
                    tip: qsTr("Send")
                    enabled: say.text.trim().length > 0
                    onClicked: {
                        Party.say(say.text)
                        say.text = ""
                    }
                }
            }
            ScButton {
                width: parent.width
                variant: "ghost"
                iconName: "log-out"
                text: Party.hosting ? qsTr("End the party") : qsTr("Leave the party")
                onClicked: Party.leave()
            }
        }
    }
}
