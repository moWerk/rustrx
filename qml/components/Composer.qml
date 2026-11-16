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

// The text field with its emoji and Send buttons, and the line above it
// for a reply, an edit or a text reaction in progress. Used by the room
// and by a thread. The page places the emoji picker and moves the focus.
Column {
    id: composer

    property string roomId
    // in a thread: its root message; "" in the room
    property string rootId
    // the draft is kept per room (and restored around an edit or a reaction)
    property bool drafts
    // typing notices go out while typing
    property bool typing
    // the image packs (the room's): the custom emoji a message uses go along
    property var packs: []
    // the emoji picker is open: the button shows it
    property bool emojiOpen

    property alias placeholderText: input.placeholderText
    property alias label: input.label
    property alias labelVisible: input.labelVisible
    property alias autoScroll: input.autoScrollEnabled
    property alias field: input
    readonly property string text: input.text

    // "", "reply", "edit" or "react"; targetId and targetText are the message
    property string mode: ""
    property string targetId
    property string targetText
    // the field holds the user's own text (an edit or a reaction is not a draft)
    readonly property bool holdsDraft: mode === "" || mode === "reply"
    readonly property bool hasDraft: holdsDraft && input.text.trim().length > 0
    // text put in by code: no typing notice, no draft save
    property bool restoring

    signal emojiClicked()

    // the custom emoji a text uses, shortcode to address, for its HTML
    function emotesIn(text) {
        var out = {}
        if (text.indexOf(":") < 0) return out
        for (var i = 0; i < packs.length; i++) {
            for (var j = 0; j < packs[i].images.length; j++) {
                var img = packs[i].images[j]
                if (img.emoticon && out[img.shortcode] === undefined && text.indexOf(":" + img.shortcode + ":") >= 0)
                    out[img.shortcode] = img.url
            }
        }
        return out
    }

    width: parent.width

    function put(t) {
        restoring = true
        input.text = t
        restoring = false
    }
    // the draft is noted before the field takes something else
    function stash() {
        if (drafts && holdsDraft) rustrx.setDraft(roomId, input.text)
    }

    function reply(id, text) {
        if (!holdsDraft) cancel()
        mode = "reply"; targetId = id; targetText = text
    }
    function edit(id, text) {
        stash()
        mode = "edit"; targetId = id; targetText = text
        put(text)
        input.cursorPosition = text.length
    }
    // a text reaction; the draft waits meanwhile
    function react(id, text) {
        stash()
        mode = "react"; targetId = id; targetText = text
        put("")
    }
    // drop the reply, edit or reaction; the draft comes back after an edit
    // or a reaction, a reply keeps what is typed
    function cancel() {
        var back = !holdsDraft
        restoring = true
        mode = ""; targetId = ""; targetText = ""
        if (back) input.text = drafts ? rustrx.draft(roomId) : ""
        restoring = false
    }

    // at the cursor (an emoji is typing, so it counts as such)
    function insert(str) {
        var at = input.cursorPosition
        var t = input.text
        input.text = t.slice(0, at) + str + t.slice(at)
        input.cursorPosition = at + str.length
    }

    // whatever is in the composer, taken out of it
    function takeCaption() {
        if (!holdsDraft) return ""
        var t = input.text.trim()
        put("")
        if (drafts) rustrx.setDraft(roomId, "")
        return t
    }

    function saveDraft() {
        if (drafts && holdsDraft) rustrx.setDraft(roomId, input.text)
    }

    function send() {
        var text = input.text.trim()
        if (text.length === 0) return
        // "/react <text>" while replying reacts to that message (as in nheko);
        // in the room only, as before
        var command = rootId.length ? null : text.match(/^\/react\s+([\s\S]+)$/)
        if (command && mode === "reply") {
            rustrx.command({ cmd: "react", room_id: roomId, event_id: targetId, key: command[1].trim() })
            put("")
            if (drafts) rustrx.setDraft(roomId, "")
            mode = ""; targetId = ""; targetText = ""
            input.forceActiveFocus()
            return
        }
        if (mode === "react") {
            rustrx.command({ cmd: "react", room_id: roomId, event_id: targetId, key: text })
            cancel()
            input.forceActiveFocus()
            return
        }
        if (mode === "edit") {
            if (text !== targetText) rustrx.command({ cmd: "edit", room_id: roomId, event_id: targetId, body: text })
            cancel()
            input.forceActiveFocus()
            return
        }
        rustrx.sendText(roomId, text, mode === "reply" ? targetId : "", emotesIn(text), rootId)
        put("")
        if (drafts) rustrx.setDraft(roomId, "")
        mode = ""; targetId = ""; targetText = ""
        if (typing) {
            typingTimer.stop()
            if (app.sendTyping) rustrx.command({ cmd: "typing", room_id: roomId, typing: false })
        }
        input.forceActiveFocus()
    }

    Timer {
        id: typingTimer
        interval: 4000
        onTriggered: if (composer.typing && app.sendTyping) rustrx.command({ cmd: "typing", room_id: composer.roomId, typing: false })
    }
    // the draft is written once typing pauses, not per keystroke
    Timer {
        id: draftSave
        interval: 800
        onTriggered: if (composer.drafts && composer.holdsDraft) rustrx.setDraft(composer.roomId, input.text)
    }

    // the message replied to, edited or reacted to: tap it or the clear
    // button to drop it
    Item {
        visible: composer.mode !== ""
        width: parent.width
        height: visible ? Theme.itemSizeExtraSmall : 0
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - x - clearLine.width
            anchors.verticalCenter: parent.verticalCenter
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
            maximumLineCount: 1
            readonly property string quoted: composer.targetText.replace(/\s+/g, " ")
            text: composer.mode === "edit"
                  //: %1 the original text
                  //% "Editing: %1"
                  ? qsTrId("rustrx-la-editing").arg(quoted)
                  : composer.mode === "react"
                  //: %1 the message reacted to
                  //% "Reacting to: %1"
                  ? qsTrId("rustrx-la-reacting-to").arg(quoted)
                  //: %1 the quoted text
                  //% "Replying to: %1"
                  : qsTrId("rustrx-la-replying-to").arg(quoted)
            MouseArea { anchors.fill: parent; onClicked: composer.cancel() }
        }
        IconButton {
            id: clearLine
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            icon.source: "image://theme/icon-m-clear"
            onClicked: composer.cancel()
        }
    }

    Row {
        width: parent.width

        // the emoji picker; the page decides what a tap does
        BackgroundItem {
            id: emojiButton
            width: Theme.itemSizeSmall
            height: Math.max(input.height, Theme.itemSizeSmall)
            onClicked: composer.emojiClicked()
            Label {
                anchors.centerIn: parent
                text: "🙂"
                font.pixelSize: Theme.fontSizeLarge
                opacity: emojiButton.highlighted || composer.emojiOpen ? 1.0 : Theme.opacityHigh
            }
        }

        TextArea {
            id: input
            width: parent.width - emojiButton.width - sendButton.width
            // the emoji button holds the page margin
            textLeftMargin: Theme.paddingSmall
            // a tap on Send (or anywhere else) must not take the focus and
            // close the keyboard, as in Jolla's Messages
            focusOutBehavior: FocusBehavior.KeepFocus
            Component.onCompleted: if (composer.drafts) composer.put(rustrx.draft(composer.roomId))
            onTextChanged: {
                if (composer.restoring || !composer.holdsDraft) return
                if (composer.drafts) draftSave.restart()
                if (!composer.typing || text.length === 0 || !app.sendTyping) return
                if (!typingTimer.running)
                    rustrx.command({ cmd: "typing", room_id: composer.roomId, typing: true })
                typingTimer.restart()
            }
        }

        // a text button, as in Jolla's Messages
        BackgroundItem {
            id: sendButton
            width: Math.max(Theme.itemSizeLarge, sendLabel.implicitWidth + 2 * Theme.paddingLarge)
            height: Math.max(input.height, Theme.itemSizeSmall)
            onClicked: composer.send()
            Label {
                id: sendLabel
                anchors.centerIn: parent
                //% "React"
                text: composer.mode === "react" ? qsTrId("rustrx-bt-react")
                //% "Save"
                    : composer.mode === "edit" ? qsTrId("rustrx-bt-save")
                //% "Send"
                    : qsTrId("rustrx-bt-send")
                color: input.text.trim().length === 0 ? Theme.secondaryColor
                     : sendButton.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
        }
    }
}
