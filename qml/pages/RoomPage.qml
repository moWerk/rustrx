/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import "../components"

// The conversation. The list runs bottom to top, so its header, the text
// field, sits at the bottom, and the push-up menu with the attachments
// appears when the newest message is in view.
Page {
    id: page

    property string roomId
    property string roomName
    property string replyToId
    property string replyToText
    // editing one of our own messages: the composer holds its text
    property string editId
    property string editText
    // who else is typing here, for the header
    property var typingNames: []
    property bool paginating
    // the oldest message is in: no more pages to ask for
    property bool atStart
    property bool infoAttached
    // the text field lives in the list header, a scope of its own; it registers here
    property Item inputArea

    Component.onCompleted: {
        rustrx.openRoom(roomId)
    }
    Component.onDestruction: {
        if (inputArea && !editId.length) rustrx.setDraft(roomId, inputArea.text)
        if (rustrx.openRoomId === roomId) rustrx.closeRoom()
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            markRead()
            // the room's page, one swipe to the right
            if (!infoAttached) {
                infoAttached = true
                pageStack.pushAttached(Qt.resolvedUrl("RoomInfoPage.qml"), { roomId: roomId })
            }
        }
    }

    // a few seconds of patience before an empty room says so
    Timer { id: openGrace; interval: 6000; running: true }

    function markRead() {
        if (rustrx.timeline.count > 0 && Qt.application.state === Qt.ApplicationActive)
            app.markRead(roomId)
    }

    function startEdit(eventId, text) {
        replyToId = ""; replyToText = ""
        editId = eventId; editText = text
        if (inputArea) { inputArea.text = text; inputArea.forceActiveFocus(); inputArea.cursorPosition = text.length }
    }

    function cancelEdit() {
        editId = ""; editText = ""
        if (inputArea) inputArea.text = rustrx.draft(roomId)
    }

    function send() {
        if (!inputArea) return
        var text = inputArea.text.trim()
        if (text.length === 0) return
        if (editId.length) {
            if (text !== editText) rustrx.command({ cmd: "edit", room_id: roomId, event_id: editId, body: text })
            editId = ""; editText = ""
            inputArea.text = rustrx.draft(roomId)
            return
        }
        rustrx.sendText(roomId, text, replyToId)
        inputArea.text = ""
        rustrx.setDraft(roomId, "")
        replyToId = ""
        replyToText = ""
        typingTimer.stop()
        if (app.sendTyping) rustrx.command({ cmd: "typing", room_id: roomId, typing: false })
    }

    Timer {
        id: typingTimer
        interval: 4000
        onTriggered: if (app.sendTyping) rustrx.command({ cmd: "typing", room_id: roomId, typing: false })
    }

    Connections {
        target: rustrx
        onPaginated: if (roomId === page.roomId) { paginating = false; page.atStart = atStart }
        onTyping: if (roomId === page.roomId) page.typingNames = names
    }

    // the header stays put; the list runs underneath it
    PageHeader {
        id: header
        title: roomName
        description: rustrx.syncState !== "running"
                     //% "Not connected"
                     ? qsTrId("rustrx-la-not-connected")
                     : typingNames.length === 1
                     //: %1 a name
                     //% "%1 is typing…"
                     ? qsTrId("rustrx-la-typing-one").arg(typingNames[0])
                     : typingNames.length === 2
                     //: %1 and %2 names
                     //% "%1 and %2 are typing…"
                     ? qsTrId("rustrx-la-typing-two").arg(typingNames[0]).arg(typingNames[1])
                     : typingNames.length > 2
                     //% "Several people are typing…"
                     ? qsTrId("rustrx-la-typing-many")
                     : ""
        MouseArea { anchors.fill: parent; onClicked: pageStack.navigateForward() }
    }

    Connections {
        target: rustrx.timeline
        onCountChanged: if (list.atYBeginning) markRead()
    }

    SilicaListView {
        id: list
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        verticalLayoutDirection: ListView.BottomToTop
        model: rustrx.timeline
        spacing: Theme.paddingSmall
        clip: true

        // the oldest end is at the top in this layout
        onAtYEndChanged: {
            if (atYEnd && count > 0 && !paginating && !atStart) {
                paginating = true
                rustrx.command({ cmd: "paginate_back", room_id: roomId, count: 40 })
            }
        }

        PushUpMenu {
            MenuItem {
                //% "Take a photo"
                text: qsTrId("rustrx-me-take-photo")
                onClicked: {
                    var cam = pageStack.push(Qt.resolvedUrl("CameraPage.qml"))
                    cam.captured.connect(function (path) { rustrx.sendFile(roomId, path) })
                }
            }
            MenuItem {
                //% "Send an image"
                text: qsTrId("rustrx-me-send-image")
                onClicked: {
                    var picker = pageStack.push(multiImagePicker)
                    picker.accepted.connect(function () {
                        for (var i = 0; i < picker.selectedContent.count; i++)
                            rustrx.sendFile(roomId, picker.selectedContent.get(i).filePath)
                    })
                }
            }
            MenuItem {
                //% "Send a file"
                text: qsTrId("rustrx-me-send-file")
                onClicked: {
                    var picker = pageStack.push(filePicker)
                    picker.selectedContentPropertiesChanged.connect(function () {
                        if (picker.selectedContentProperties.filePath)
                            rustrx.sendFile(roomId, picker.selectedContentProperties.filePath)
                    })
                }
            }
        }

        // in a bottom-to-top list the footer is drawn at the top: a line while
        // older messages load, nothing once the beginning is reached
        footer: Item {
            width: list.width
            height: paginating ? Theme.itemSizeSmall : Theme.paddingLarge
            BusyIndicator {
                anchors.centerIn: parent
                size: BusyIndicatorSize.Small
                running: paginating
            }
        }

        header: Column {
            width: list.width

            Item { width: 1; height: Theme.paddingMedium }

            // editing: the message being changed, tap to cancel
            Label {
                visible: editId.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                //: %1 the original text
                //% "Editing: %1"
                text: qsTrId("rustrx-la-editing").arg(editText)
                MouseArea { anchors.fill: parent; onClicked: page.cancelEdit() }
            }

            Label {
                visible: replyToId.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                //: %1 the quoted text
                //% "Replying to: %1"
                text: qsTrId("rustrx-la-replying-to").arg(replyToText)
                MouseArea { anchors.fill: parent; onClicked: { replyToId = ""; replyToText = "" } }
            }

            Row {
                width: parent.width

                TextArea {
                    id: input
                    width: parent.width - sendButton.width
                    //% "Message"
                    placeholderText: qsTrId("rustrx-ph-message")
                    label: roomName
                    Component.onCompleted: {
                        page.inputArea = input
                        text = rustrx.draft(roomId)
                    }
                    onTextChanged: {
                        if (editId.length) return   // an edit is not a draft
                        draftSave.restart()
                        if (text.length === 0 || !app.sendTyping) return
                        if (!typingTimer.running)
                            rustrx.command({ cmd: "typing", room_id: roomId, typing: true })
                        typingTimer.restart()
                    }
                    // the draft is written once typing pauses, not per keystroke
                    Timer { id: draftSave; interval: 800; onTriggered: if (!editId.length) rustrx.setDraft(roomId, input.text) }
                }

                // a text button, as in Jolla's Messages
                BackgroundItem {
                    id: sendButton
                    width: Math.max(Theme.itemSizeLarge, sendLabel.implicitWidth + 2 * Theme.paddingLarge)
                    height: Math.max(input.height, Theme.itemSizeSmall)
                    onClicked: send()
                    Label {
                        id: sendLabel
                        anchors.centerIn: parent
                        //% "Save"
                        text: editId.length ? qsTrId("rustrx-bt-save")
                        //% "Send"
                                            : qsTrId("rustrx-bt-send")
                        color: input.text.trim().length === 0 ? Theme.secondaryColor
                             : sendButton.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }
        }

        delegate: MessageDelegate {
            roomId: page.roomId
            onReply: {
                if (editId.length) page.cancelEdit()
                replyToId = eventId
                replyToText = text
                if (inputArea) inputArea.focus = true
            }
            onEdit: page.startEdit(eventId, text)
            onResend: {
                // the failed echo goes, the same text goes out again
                rustrx.command({ cmd: "redact", room_id: page.roomId, txn_id: txnId })
                rustrx.sendText(page.roomId, text, "")
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0 && !openGrace.running
            //% "No messages yet"
            text: qsTrId("rustrx-la-no-messages")
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: list.count === 0 && openGrace.running
        }

        VerticalScrollDecorator { }
    }

    Component {
        id: multiImagePicker
        MultiImagePickerDialog { }
    }

    Component {
        id: filePicker
        FilePickerPage { }
    }
}
