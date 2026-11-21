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
import Rustrx 1.0
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

    // the composer lives in the list's header, a scope of its own; it registers here
    property Item composer

    Component.onCompleted: rustrx.openThread(roomId, rootId)
    // what is typed stays as the thread's draft (a back swipe lost it)
    Component.onDestruction: {
        if (composer) composer.saveDraft()
        rustrx.closeThread()
    }

    // what is shown here is read: a threaded read receipt, which the thread
    // lines here and in the other clients go by
    function markSeen() {
        if (status !== PageStatus.Active) return
        var events = 0
        for (var i = 0; i < rustrx.thread.count; i++)
            if (rustrx.thread.get(i).kind === "event") events++
        if (events === 0) return
        rustrx.command({ cmd: "mark_thread_read", "private": !app.sendReceipts })
    }
    onStatusChanged: if (status === PageStatus.Active) markSeen()
    Connections { target: rustrx.thread; onCountChanged: page.markSeen() }

    // the emoji picker in the keyboard's place, as in the room (EmojiDock)
    function insertEmoji(emoji) {
        if (composer) composer.insert(emoji)
    }
    // the message the picker reacts to, for "React with text"
    property string reactMoreText
    EmojiDock {
        id: emojiDock
        page: page
        // above the list, which is declared after it
        z: 1
        // a pack's picture: a reaction (its address the key), else :shortcode: in the text
        onPickedImage: {
            if (mode === "react")
                rustrx.command({ cmd: "react", room_id: page.roomId, event_id: eventId, key: image.mxc })
            else
                page.insertEmoji(":" + image.shortcode + ":")
        }
        onPicked: {
            if (mode === "react")
                rustrx.command({ cmd: "react", room_id: page.roomId, event_id: eventId, key: emoji })
            else
                page.insertEmoji(emoji)
        }
        onTextReaction: {
            if (!page.composer) return
            page.composer.react(eventId, page.reactMoreText)
            page.composer.field.forceActiveFocus()
        }
    }
    SilicaListView {
        id: list
        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.bottomMargin: emojiDock.aboveKeyboard
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
            // a long message wraps to a new line: the field grows downwards,
            // under the keyboard, unless the list follows it
            onHeightChanged: if (page.composer && page.composer.field.activeFocus) list.positionViewAtBeginning()

            Item { width: 1; height: Theme.paddingMedium }

            Composer {
                id: composer
                roomId: page.roomId
                rootId: page.rootId
                drafts: true
                emojiOpen: emojiDock.open
                //% "Reply in the thread"
                placeholderText: qsTrId("rustrx-ph-thread")
                Component.onCompleted: {
                    page.composer = composer
                    if (page.compose) field.forceActiveFocus()
                }
                onEmojiClicked: {
                    if (emojiDock.open && emojiDock.mode === "insert")
                        emojiDock.toKeyboard()
                    else
                        emojiDock.show("insert", "")
                }
            }
        }

        delegate: MessageDelegate {
            roomId: page.roomId
            inThread: true
            // "+" in the message menu: the picker reacts to this message
            onReactMore: { page.reactMoreText = text; emojiDock.show("react", eventId) }
            onReply: {
                if (!page.composer) return
                page.composer.reply(eventId, text)
                page.composer.field.forceActiveFocus()
            }
            onEdit: {
                if (!page.composer) return
                page.composer.edit(eventId, text)
                page.composer.field.forceActiveFocus()
            }
            onResend: rustrx.command({ cmd: "retry_send", room_id: page.roomId, txn_id: txnId })
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
