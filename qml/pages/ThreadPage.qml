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

// A thread: its root message on top, the replies under it, newest at the
// bottom by the text field; what is sent here goes into the thread.
// Reacting, replying, editing work as in the room.
Page {
    id: page

    property string roomId
    property string roomName
    property string rootId
    // opened from "Reply in thread": the keyboard comes up
    property bool compose

    property string replyToId
    property string replyToText
    property string editId
    property string editText
    // the text field lives in the list's header, a scope of its own; it registers here
    property Item inputArea

    Component.onCompleted: rustrx.openThread(roomId, rootId)
    Component.onDestruction: rustrx.closeThread()

    // what is shown here is seen: the replies counted, and a threaded read
    // receipt for the other clients
    function markSeen() {
        if (status !== PageStatus.Active) return
        var events = 0
        for (var i = 0; i < rustrx.thread.count; i++)
            if (rustrx.thread.get(i).kind === "event") events++
        if (events === 0) return
        rustrx.markThreadSeen(rootId, Math.max(events - 1, rustrx.threadSeen(rootId, 0)))
        rustrx.command({ cmd: "mark_thread_read" })
    }
    onStatusChanged: if (status === PageStatus.Active) markSeen()
    Connections { target: rustrx.thread; onCountChanged: page.markSeen() }

    function send() {
        var input = inputArea
        if (!input) return
        var text = input.text.trim()
        if (!text.length) return
        if (editId.length) {
            if (text !== editText) rustrx.command({ cmd: "edit", room_id: roomId, event_id: editId, body: text })
            editId = ""; editText = ""
        } else {
            rustrx.sendText(roomId, text, replyToId, {}, rootId)
            replyToId = ""; replyToText = ""
        }
        input.text = ""
        input.forceActiveFocus()
    }

    // the emoji picker in the keyboard's place (as in the room, simpler: it
    // slides in when the keyboard has gone)
    function insertEmoji(emoji) {
        if (!inputArea) return
        var at = inputArea.cursorPosition
        var t = inputArea.text
        inputArea.text = t.slice(0, at) + emoji + t.slice(at)
        inputArea.cursorPosition = at + emoji.length
    }
    EmojiPanel {
        id: emojiPanel
        anchors.bottom: parent.bottom
        height: Math.round(Screen.height * (page.isPortrait ? 0.36 : 0.5))
        visible: open
        onPicked: page.insertEmoji(emoji)
        onPickedImage: page.insertEmoji(":" + image.shortcode + ":")
    }
    Connections {
        target: Qt.inputMethod
        onVisibleChanged: if (Qt.inputMethod.visible) emojiPanel.open = false
    }

    SilicaListView {
        id: list
        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.bottomMargin: emojiPanel.open ? emojiPanel.height : 0
        verticalLayoutDirection: ListView.BottomToTop
        model: rustrx.thread
        currentIndex: -1

        PushUpMenu {
            MenuItem {
                //% "Send an image"
                text: qsTrId("rustrx-me-send-image")
                onClicked: {
                    var picker = pageStack.push(imagePicker)
                    picker.accepted.connect(function () {
                        for (var i = 0; i < picker.selectedContent.count; i++)
                            rustrx.sendFile(roomId, picker.selectedContent.get(i).filePath, "", rootId)
                    })
                }
            }
        }

        // the composer, at the bottom of a bottom-to-top list
        header: Column {
            width: list.width

            Item { width: 1; height: Theme.paddingMedium }

            Item {
                visible: page.replyToId.length > 0 || page.editId.length > 0
                width: parent.width
                height: visible ? Theme.itemSizeExtraSmall : 0
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - x - clearLine.width
                    anchors.verticalCenter: parent.verticalCenter
                    truncationMode: TruncationMode.Fade
                    maximumLineCount: 1
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryHighlightColor
                    text: page.editId.length ? qsTrId("rustrx-la-editing").arg(page.editText.replace(/\s+/g, " "))
                                             : qsTrId("rustrx-la-replying-to").arg(page.replyToText.replace(/\s+/g, " "))
                }
                IconButton {
                    id: clearLine
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    icon.source: "image://theme/icon-m-clear"
                    onClicked: { page.replyToId = ""; page.replyToText = ""; page.editId = ""; page.editText = ""; input.text = "" }
                }
            }

            Row {
                width: parent.width
                BackgroundItem {
                    id: emojiButton
                    width: Theme.itemSizeSmall
                    height: Math.max(input.height, Theme.itemSizeSmall)
                    onClicked: {
                        if (emojiPanel.open) {
                            emojiPanel.open = false
                            input.forceActiveFocus()
                            Qt.inputMethod.show()
                        } else {
                            Qt.inputMethod.hide()
                            emojiPanel.show("insert", "")
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        text: "🙂"
                        font.pixelSize: Theme.fontSizeLarge
                        opacity: emojiButton.highlighted || emojiPanel.open ? 1.0 : Theme.opacityHigh
                    }
                }
                TextArea {
                    id: input
                    width: parent.width - emojiButton.width - sendButton.width
                    textLeftMargin: Theme.paddingSmall
                    //% "Reply in the thread"
                    placeholderText: qsTrId("rustrx-ph-thread")
                    focusOutBehavior: FocusBehavior.KeepFocus
                    Component.onCompleted: {
                        page.inputArea = input
                        if (page.compose) forceActiveFocus()
                    }
                }
                BackgroundItem {
                    id: sendButton
                    width: Math.max(Theme.itemSizeLarge, sendLabel.implicitWidth + 2 * Theme.paddingLarge)
                    height: Math.max(input.height, Theme.itemSizeSmall)
                    onClicked: page.send()
                    Label {
                        id: sendLabel
                        anchors.centerIn: parent
                        text: page.editId.length ? qsTrId("rustrx-bt-save") : qsTrId("rustrx-bt-send")
                        color: input.text.trim().length === 0 ? Theme.secondaryColor
                             : sendButton.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }
        }

        delegate: MessageDelegate {
            roomId: page.roomId
            inThread: true
            onReply: { page.editId = ""; page.replyToId = eventId; page.replyToText = text; if (page.inputArea) page.inputArea.forceActiveFocus() }
            onEdit: {
                page.replyToId = ""; page.editId = eventId; page.editText = text
                if (page.inputArea) { page.inputArea.text = text; page.inputArea.forceActiveFocus() }
            }
            onResend: {
                rustrx.command({ cmd: "redact", room_id: page.roomId, txn_id: txnId })
                rustrx.sendText(page.roomId, text, "", {}, page.rootId)
            }
        }

        // the thread's title over the oldest end
        footer: PageHeader {
            //% "Thread"
            title: qsTrId("rustrx-he-thread")
            description: page.roomName
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: list.count === 0
        }

        VerticalScrollDecorator { }
    }

    Component {
        id: imagePicker
        MultiImagePickerDialog { }
    }
}
