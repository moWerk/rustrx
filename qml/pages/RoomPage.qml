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
import Nemo.Configuration 1.0
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
    // a text reaction to this message: Send reacts with what is typed
    property string reactToId
    property string reactToText
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
        rustrx.command({ cmd: "image_packs", room_id: roomId })
    }

    // custom emoji and stickers offered here (the user's pack, the enabled
    // ones, the room's), from the core's "image_packs"
    property var packs: []
    readonly property bool hasStickers: {
        for (var i = 0; i < packs.length; i++)
            for (var j = 0; j < packs[i].images.length; j++)
                if (packs[i].images[j].sticker) return true
        return false
    }
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
    Component.onDestruction: {
        if (inputArea && !editId.length && !reactToId.length) rustrx.setDraft(roomId, inputArea.text)
        if (rustrx.openRoomId === roomId) rustrx.closeRoom()
    }

    // The chat keeps its place. It moves on an action (send, reply, edit,
    // a reaction) or to follow new messages at the newest end; otherwise the
    // message at the bottom edge keeps its distance from that edge. When a
    // page comes over the chat (a picture, a thread, the room's page) that
    // message (by its row id, not a raw contentY: the keyboard changes the
    // height in between and older history may page in) and its distance
    // are noted; back, they are put back once, after the keyboard has
    // settled. (Silica's auto scroll to the focused text field, the main
    // mover before, is off: see the TextArea.)
    property string anchorId
    property real anchorOffset
    property bool anchorNewest
    property bool anchorPending

    // the row at a point of the view, as an item and its index
    function rowAt(y) {
        for (var k = 0; k < 30; k++) {
            var i = list.indexAt(list.width / 2, y - k * 8)
            if (i >= 0) return { index: i, item: list.itemAt(list.width / 2, y - k * 8) }
        }
        return null
    }
    function takeAnchor() {
        anchorNewest = list.atYBeginning
        anchorId = ""
        var bottom = list.contentY + list.height
        var r = rowAt(bottom - 1)
        if (!r || !r.item) return
        anchorId = rustrx.timeline.get(r.index).id || ""
        anchorOffset = bottom - (r.item.y + r.item.height)
        cp("anchor-take", "id=" + anchorId.slice(0, 10) + " off=" + Math.round(anchorOffset) + " newest=" + anchorNewest)
    }
    function applyAnchor() {
        anchorPending = false
        if (anchorNewest) { cp("anchor-newest"); list.positionViewAtBeginning(); return }
        if (!anchorId.length) return
        var index = -1
        for (var i = 0; i < rustrx.timeline.count; i++)
            if (rustrx.timeline.get(i).id === anchorId) { index = i; break }
        if (index < 0) { cp("anchor-gone"); return }
        // into view first, then exactly to its old distance from the bottom
        list.positionViewAtIndex(index, ListView.Contain)
        for (var y = list.contentY; y < list.contentY + list.height; y += 8) {
            if (list.indexAt(list.width / 2, y) !== index) continue
            var item = list.itemAt(list.width / 2, y)
            if (item) list.contentY = item.y + item.height + anchorOffset - list.height
            break
        }
        cp("anchor-apply", "index=" + index)
    }
    // once the keyboard has stopped moving (its animation changes the height)
    Timer { id: anchorSettle; interval: 150; onTriggered: if (page.anchorPending) page.applyAnchor() }
    // the first wait, for a keyboard that does not move at all
    Timer { id: anchorFirst; interval: 300; onTriggered: if (page.anchorPending) page.applyAnchor() }
    Connections {
        target: pageStack
        onPanelSizeChanged: if (page.anchorPending && page.status === PageStatus.Active) { anchorFirst.stop(); anchorSettle.restart() }
    }

    onStatusChanged: {
        cpFresh = true
        cp("status")
        if (status === PageStatus.Deactivating) {
            takeAnchor()
            anchorPending = true
        }
        if (status === PageStatus.Active && anchorPending)
            anchorFirst.restart()
        if (status === PageStatus.Active) {
            app.lastRoom = roomId
            // opened by Answer on an incoming call: join it
            if (rustrx.takePendingCall() === roomId)
                rustrx.command({ cmd: "call_start", room_id: roomId })
            // read only at the newest end: marking moves the read marker's
            // row, which shifted what someone scrolled up was reading
            if (list.atYBeginning) markRead()
            // the room's page, one swipe to the right; attached once the
            // transition is over (inside it the stack reports a binding loop)
            if (!infoAttached) attachInfo.start()
        }
    }

    Timer {
        id: attachInfo
        interval: 0
        onTriggered: {
            if (page.infoAttached) return
            page.infoAttached = true
            pageStack.pushAttached(Qt.resolvedUrl("RoomInfoPage.qml"), { roomId: roomId })
        }
    }

    // a few seconds of patience before an empty room says so
    Timer { id: openGrace; interval: 6000; running: true }

    function markRead() {
        if (rustrx.timeline.count > 0 && Qt.application.state === Qt.ApplicationActive)
            app.markRead(roomId)
    }

    // a message is drafted: the push-up menu attaches to it
    readonly property bool hasDraft: !!inputArea && !editId.length && !reactToId.length
                                     && inputArea.text.trim().length > 0

    // whatever is in the composer, taken out of it
    function takeCaption() {
        if (!inputArea || editId.length || reactToId.length) return ""
        var t = inputArea.text.trim()
        inputArea.text = ""
        rustrx.setDraft(roomId, "")
        return t
    }

    function startEdit(eventId, text) {
        replyToId = ""; replyToText = ""
        reactToId = ""; reactToText = ""
        editId = eventId; editText = text
        if (inputArea) { inputArea.text = text; inputArea.cursorPosition = text.length }
        showComposer()
    }

    // Reply and Edit come from a message's menu: while it was open the list
    // scrolled to keep it in view, and the text field (the list's header, at
    // the bottom) went out of sight. Once the menu has closed, back to the
    // bottom with the field focused.
    function showComposer() { composerLater.restart() }
    Timer {
        id: composerLater
        interval: 350
        onTriggered: {
            page.viewAtBeginning("composer")
            if (page.inputArea) page.inputArea.forceActiveFocus()
            page.cp("composer-focused")
        }
    }

    function cancelEdit() {
        editId = ""; editText = ""
        if (inputArea) inputArea.text = rustrx.draft(roomId)
    }

    // the composer takes a text reaction; the draft waits meanwhile
    function startTextReaction(eventId, text) {
        if (editId.length) cancelEdit()
        replyToId = ""; replyToText = ""
        if (inputArea) rustrx.setDraft(roomId, inputArea.text)
        reactToId = eventId; reactToText = text
        if (inputArea) inputArea.text = ""
        showComposer()
    }
    function cancelTextReaction() {
        reactToId = ""; reactToText = ""
        if (inputArea) inputArea.text = rustrx.draft(roomId)
    }

    function send() {
        if (!inputArea) return
        var text = inputArea.text.trim()
        if (text.length === 0) return
        // "/react <text>" while replying reacts to that message (as in nheko)
        var command = text.match(/^\/react\s+([\s\S]+)$/)
        if (command && replyToId.length) {
            rustrx.command({ cmd: "react", room_id: roomId, event_id: replyToId, key: command[1].trim() })
            inputArea.text = ""
            rustrx.setDraft(roomId, "")
            replyToId = ""; replyToText = ""
            inputArea.forceActiveFocus()
            return
        }
        if (reactToId.length) {
            rustrx.command({ cmd: "react", room_id: roomId, event_id: reactToId, key: text })
            cancelTextReaction()
            inputArea.forceActiveFocus()
            return
        }
        if (editId.length) {
            if (text !== editText) rustrx.command({ cmd: "edit", room_id: roomId, event_id: editId, body: text })
            editId = ""; editText = ""
            inputArea.text = rustrx.draft(roomId)
            inputArea.forceActiveFocus()
            return
        }
        rustrx.sendText(roomId, text, replyToId, emotesIn(text))
        inputArea.text = ""
        rustrx.setDraft(roomId, "")
        replyToId = ""
        replyToText = ""
        typingTimer.stop()
        if (app.sendTyping) rustrx.command({ cmd: "typing", room_id: roomId, typing: false })
        inputArea.forceActiveFocus()
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
        onImagePacks: if (roomId === page.roomId) page.packs = packs
        onEmoteAdded: {
            //: %1 the shortcode a custom emoji got in the user's own pack
            //% "Added :%1: to your emoji"
            Notices.show(qsTrId("rustrx-no-emote-added").arg(shortcode), Notice.Short)
            rustrx.command({ cmd: "image_packs", room_id: page.roomId })
        }
    }

    // the room's picture beside its name in the header, kept current with
    // the room list (a new picture comes with the sync)
    property var roomInfo: rustrx.roomInfo(roomId)
    Connections { target: rustrx.rooms; onDataChanged: page.roomInfo = rustrx.roomInfo(page.roomId) }
    Connections { target: rustrx.people; onDataChanged: page.roomInfo = rustrx.roomInfo(page.roomId) }
    readonly property bool hasPicture: !!(roomInfo && roomInfo.avatar)

    // the header stays put; the list runs underneath it
    PageHeader {
        id: header
        title: roomName
        // room for the picture right of the name, as tall as the name's letters
        readonly property real pictureSize: Math.round(_titleItem.font.pixelSize * 0.9)
        rightMargin: Theme.horizontalPageMargin + (page.hasPicture ? pictureSize + Theme.paddingMedium : 0)

        Image {
            visible: page.hasPicture && status === Image.Ready
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
                verticalCenter: header._titleItem.verticalCenter
            }
            width: header.pictureSize
            height: width
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: width * 2
            sourceSize.height: height * 2
            source: page.hasPicture ? "file://" + page.roomInfo.avatar + "?v=" + (page.roomInfo.avatar_v || 0) : ""
            layer.enabled: app.roundAvatars
            layer.effect: ShaderEffect {
                fragmentShader: "
                    varying highp vec2 qt_TexCoord0;
                    uniform sampler2D source;
                    uniform lowp float qt_Opacity;
                    void main() {
                        highp float d = length(qt_TexCoord0 - vec2(0.5));
                        lowp float a = 1.0 - smoothstep(0.49, 0.5, d);
                        gl_FragColor = texture2D(source, qt_TexCoord0) * a * qt_Opacity;
                    }"
            }
        }
        description: rustrx.syncState === "idle"
                     //% "Connecting"
                     ? qsTrId("rustrx-la-cover-connecting")
                     : rustrx.syncState !== "running"
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

    // the first time the room fills, start at the read marker if it is there
    property bool positioned
    Connections {
        target: rustrx.timeline
        onCountChanged: {
            if (page.cpOn) page.cp("count", "n=" + rustrx.timeline.count)
            if (!page.positioned && rustrx.timeline.count > 0) {
                page.positioned = true
                for (var i = 0; i < rustrx.timeline.count; i++) {
                    if (rustrx.timeline.get(i).kind === "readmarker") {
                        page.viewAt("readmarker", i, ListView.Center)
                        break
                    }
                }
            }
            if (list.atYBeginning) markRead()
        }
        onRowsInserted: if (page.cpOn) page.cp("ins", "rows=" + first + ".." + last)
        onRowsRemoved: if (page.cpOn) page.cp("rem", "rows=" + first + ".." + last)
        onRowsMoved: if (page.cpOn) page.cp("mov", "rows=" + start + ".." + end + ">" + row)
        onDataChanged: if (page.cpOn) page.cp("data", "rows=" + topLeft.row + ".." + bottomRight.row)
        onModelReset: page.cp("reset")
        onLayoutChanged: page.cp("layout")
    }

    SilicaListView {
        id: list
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        // the emoji panel takes the keyboard's place below the composer
        // the picker's part above the keyboard (none while the keyboard covers it)
        anchors.bottomMargin: Math.max(0, page.emojiLift - page.keyboardSize)
        verticalLayoutDirection: ListView.BottomToTop
        model: rustrx.timeline
        // no current item: a message arriving at the newest end moved it, and
        // the view gave it the focus, away from the composer in the header,
        // so the keyboard closed on every message sent or received
        currentIndex: -1
        spacing: Theme.paddingSmall
        clip: true

        onContentYChanged: if (page.cpOn && (page.cpFresh || isNaN(page.cpLastY) || Math.abs(contentY - page.cpLastY) > 20)) {
            page.cpFresh = false
            page.cp("y")
        }
        onOriginYChanged: page.cp("oy")
        onContentHeightChanged: if (page.cpOn && (isNaN(page.cpLastCh) || Math.abs(contentHeight - page.cpLastCh) > 20)) page.cp("ch")
        onHeightChanged: page.cp("h")
        onMovementEnded: page.cp("moved")

        // the oldest end is at the top in this layout
        onAtYEndChanged: {
            if (atYEnd && count > 0 && !paginating && !atStart) {
                paginating = true
                rustrx.command({ cmd: "paginate_back", room_id: roomId, count: 40 })
            }
        }

        // with an empty message an entry sends on its own; with a message
        // drafted it attaches: the text goes along as the caption
        PushUpMenu {
            MenuItem {
                //% "Attach an image"
                text: page.hasDraft ? qsTrId("rustrx-me-attach-image")
                //% "Send an image"
                                    : qsTrId("rustrx-me-send-image")
                onClicked: {
                    var picker = pageStack.push(multiImagePicker)
                    picker.accepted.connect(function () {
                        // the text in the composer becomes the caption of a single picture
                        var caption = picker.selectedContent.count === 1 ? page.takeCaption() : ""
                        for (var i = 0; i < picker.selectedContent.count; i++)
                            rustrx.sendFile(roomId, picker.selectedContent.get(i).filePath, caption)
                    })
                }
            }
            MenuItem {
                //% "Attach a file"
                text: page.hasDraft ? qsTrId("rustrx-me-attach-file")
                //% "Send a file"
                                    : qsTrId("rustrx-me-send-file")
                onClicked: {
                    var picker = pageStack.push(filePicker)
                    picker.selectedContentPropertiesChanged.connect(function () {
                        if (picker.selectedContentProperties.filePath)
                            rustrx.sendFile(roomId, picker.selectedContentProperties.filePath, page.takeCaption())
                    })
                }
            }
            MenuItem {
                //% "Attach a photo"
                text: page.hasDraft ? qsTrId("rustrx-me-attach-photo")
                //% "Take a photo"
                                    : qsTrId("rustrx-me-take-photo")
                onClicked: {
                    var cam = pageStack.push(Qt.resolvedUrl("CameraPage.qml"))
                    cam.captured.connect(function (path) { rustrx.sendFile(roomId, path, page.takeCaption()) })
                }
            }
            // closest to the edge
            MenuItem {
                //% "Attach a voice message"
                text: page.hasDraft ? qsTrId("rustrx-me-attach-voice")
                //% "Record a voice message"
                                    : qsTrId("rustrx-me-record-voice")
                onClicked: pageStack.push(Qt.resolvedUrl("VoicePage.qml"), { roomId: roomId, takeCaption: page.takeCaption })
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
            onHeightChanged: page.cp("hdr")

            Item { width: 1; height: Theme.paddingMedium }

            // a call is running here: join it
            BackgroundItem {
                visible: !!page.roomInfo.in_call
                width: parent.width
                height: visible ? Theme.itemSizeSmall : 0
                onClicked: rustrx.command({ cmd: "call_start", room_id: roomId })
                Row {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.paddingMedium
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "image://theme/icon-m-call"
                        highlighted: parent.parent.highlighted
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        //% "Call in progress, tap to join"
                        text: qsTrId("rustrx-la-call-join")
                        color: Theme.highlightColor
                    }
                }
            }

            // editing: the message being changed, tap to cancel
            // an edit or a reply in progress: the line and a clear button to
            // drop it (a tap on the line drops it too)
            Item {
                visible: editId.length > 0
                width: parent.width
                height: visible ? Theme.itemSizeExtraSmall : 0
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - x - clearEdit.width
                    anchors.verticalCenter: parent.verticalCenter
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryHighlightColor
                    //: %1 the original text
                    //% "Editing: %1"
                    text: qsTrId("rustrx-la-editing").arg(editText.replace(/\s+/g, " "))
                    maximumLineCount: 1
                    MouseArea { anchors.fill: parent; onClicked: page.cancelEdit() }
                }
                IconButton {
                    id: clearEdit
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    icon.source: "image://theme/icon-m-clear"
                    onClicked: page.cancelEdit()
                }
            }

            Item {
                visible: reactToId.length > 0
                width: parent.width
                height: visible ? Theme.itemSizeExtraSmall : 0
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - x - clearReact.width
                    anchors.verticalCenter: parent.verticalCenter
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryHighlightColor
                    //: %1 the message reacted to
                    //% "Reacting to: %1"
                    text: qsTrId("rustrx-la-reacting-to").arg(reactToText.replace(/\s+/g, " "))
                    maximumLineCount: 1
                    MouseArea { anchors.fill: parent; onClicked: page.cancelTextReaction() }
                }
                IconButton {
                    id: clearReact
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    icon.source: "image://theme/icon-m-clear"
                    onClicked: page.cancelTextReaction()
                }
            }

            Item {
                visible: replyToId.length > 0
                width: parent.width
                height: visible ? Theme.itemSizeExtraSmall : 0
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - x - clearReply.width
                    anchors.verticalCenter: parent.verticalCenter
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryHighlightColor
                    //: %1 the quoted text
                    //% "Replying to: %1"
                    text: qsTrId("rustrx-la-replying-to").arg(replyToText.replace(/\s+/g, " "))
                    maximumLineCount: 1
                    MouseArea { anchors.fill: parent; onClicked: { replyToId = ""; replyToText = "" } }
                }
                IconButton {
                    id: clearReply
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    icon.source: "image://theme/icon-m-clear"
                    onClicked: { replyToId = ""; replyToText = "" }
                }
            }

            Row {
                width: parent.width

                // the emoji picker (and the stickers with an empty message);
                // a second tap brings the keyboard back
                BackgroundItem {
                    id: emojiButton
                    width: Theme.itemSizeSmall
                    height: Math.max(input.height, Theme.itemSizeSmall)
                    onClicked: {
                        if (emojiPanel.open && emojiPanel.mode === "insert") {
                            page.emojiToKeyboard()
                        } else {
                            emojiPanel.stickers = page.hasStickers && input.text.trim().length === 0
                            page.showEmoji("insert", "")
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
                    // the emoji button holds the page margin
                    textLeftMargin: Theme.paddingSmall
                    //% "Message"
                    placeholderText: qsTrId("rustrx-ph-message")
                    //: %1 the room's name
                    //% "Typing to %1"
                    label: app.sendTyping && app.typingHint ? qsTrId("rustrx-la-typing-to").arg(roomName) : ""
                    labelVisible: label !== ""
                    // a tap on Send (or anywhere else) must not take the focus and
                    // close the keyboard, as in Jolla's Messages
                    focusOutBehavior: FocusBehavior.KeepFocus
                    // no scrolling the chat to the field when it gets its focus
                    // back (every return from a page above, and with every step
                    // of the keyboard's animation): the chat keeps its place
                    autoScrollEnabled: false
                    onActiveFocusChanged: page.cp("inputfocus", "af=" + activeFocus)
                    Component.onCompleted: {
                        page.inputArea = input
                        text = rustrx.draft(roomId)
                    }
                    onTextChanged: {
                        if (editId.length || reactToId.length) return   // an edit or a reaction is not a draft
                        draftSave.restart()
                        if (text.length === 0 || !app.sendTyping) return
                        if (!typingTimer.running)
                            rustrx.command({ cmd: "typing", room_id: roomId, typing: true })
                        typingTimer.restart()
                    }
                    // the draft is written once typing pauses, not per keystroke
                    Timer { id: draftSave; interval: 800; onTriggered: if (!editId.length && !reactToId.length) rustrx.setDraft(roomId, input.text) }
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
                        //% "React"
                        text: reactToId.length ? qsTrId("rustrx-bt-react")
                        //% "Save"
                            : editId.length ? qsTrId("rustrx-bt-save")
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
            // a row that grows or shrinks (a picture, a menu)
            property real cpH
            onHeightChanged: {
                if (page.cpOn && cpH > 0 && Math.abs(height - cpH) > 20)
                    page.cp("row", "i=" + index + " " + Math.round(cpH) + ">" + Math.round(height))
                cpH = height
            }
            onReply: {
                if (editId.length) page.cancelEdit()
                if (reactToId.length) page.cancelTextReaction()
                replyToId = eventId
                replyToText = text
                page.showComposer()
            }
            onEdit: page.startEdit(eventId, text)
            onResend: {
                // the failed echo goes, the same text goes out again
                rustrx.command({ cmd: "redact", room_id: page.roomId, txn_id: txnId })
                rustrx.sendText(page.roomId, text, "")
            }
            onReactMore: { page.reactMoreText = text; page.showEmoji("react", eventId) }
            onOpenThread: pageStack.push(Qt.resolvedUrl("ThreadPage.qml"),
                                         { roomId: page.roomId, roomName: page.roomName, rootId: eventId, compose: compose })
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

    // The emoji picker in the keyboard's place: inserts at the cursor, or
    // reacts to a message (EmojiPanel). The picker and the keyboard share
    // one place at the bottom of the screen. Silica shrinks the page by the
    // keyboard as it slides (keyboardSize); the picker sits at the screen's
    // bottom, lifted by emojiLift, and the conversation ends above whichever
    // is higher. Switching from one to the other keeps the height: the
    // picker takes the keyboard's, lies under it and is uncovered as the
    // keyboard slides away, or is covered by the keyboard sliding up and
    // only then goes. With nothing open it slides in and out on its own.
    readonly property real keyboardSize: pageStack.panelSize
    property real emojiHeight: defaultEmojiHeight()
    property real emojiLift: 0
    property bool emojiLiftAnimated
    // the keyboard is coming back in the picker's place
    property bool emojiToKeys
    Behavior on emojiLift {
        enabled: page.emojiLiftAnimated
        NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
    }
    function defaultEmojiHeight() { return Math.round(Screen.height * (isPortrait ? 0.36 : 0.5)) }
    onIsPortraitChanged: { emojiHeight = defaultEmojiHeight(); if (emojiPanel.open) emojiLift = emojiHeight }

    function showEmoji(mode, eventId) {
        emojiToKeys = false
        if (keyboardSize > 0) {
            // under the keyboard, at its height, uncovered as it goes
            emojiHeight = keyboardSize
            emojiLiftAnimated = false
            emojiLift = emojiHeight
            emojiPanel.show(mode, eventId)
            Qt.inputMethod.hide()
        } else {
            emojiPanel.show(mode, eventId)
            emojiLiftAnimated = true
            emojiLift = emojiHeight
        }
    }
    // the emoji button again, or a tap into the field: the keyboard covers
    // the picker, which goes once it is covered
    function emojiToKeyboard() {
        emojiToKeys = true
        if (inputArea) inputArea.forceActiveFocus()
        Qt.inputMethod.show()
    }
    onKeyboardSizeChanged: {
        if (cpOn && (keyboardSize === 0 || Math.abs(keyboardSize - cpLastKb) > 40)) { cpLastKb = keyboardSize; cp("kb") }
        // the keyboard's own height, for the picker next time
        if (Qt.inputMethod.visible && !emojiPanel.open && keyboardSize > 0)
            emojiHeight = keyboardSize
        if (emojiToKeys && emojiPanel.open && keyboardSize >= emojiLift - 1) {
            emojiToKeys = false
            emojiLiftAnimated = false
            emojiLift = 0
            emojiPanel.open = false
        }
    }
    function insertEmoji(emoji) {
        if (!inputArea) return
        var at = inputArea.cursorPosition
        var t = inputArea.text
        inputArea.text = t.slice(0, at) + emoji + t.slice(at)
        inputArea.cursorPosition = at + emoji.length
    }
    // the message the picker reacts to, for "React with text"
    property string reactMoreText
    EmojiPanel {
        id: emojiPanel
        // at the screen's bottom, below the page while the keyboard is up
        y: page.height + page.keyboardSize - page.emojiLift
        height: page.emojiHeight
        visible: page.emojiLift > 0
        // closed by itself (a reaction, a sticker): it slides away
        onOpenChanged: if (!open && page.emojiLift > 0) { page.emojiLiftAnimated = true; page.emojiLift = 0 }
        packs: page.packs
        // a pack's picture: a reaction (its address the key), a sticker sent
        // (an empty message, and the image is one), else :shortcode: in the text
        onPickedImage: {
            if (mode === "react")
                rustrx.command({ cmd: "react", room_id: page.roomId, event_id: eventId, key: image.url })
            else if (stickers && image.sticker && (!page.inputArea || page.inputArea.text.trim().length === 0)) {
                rustrx.command({ cmd: "send_sticker", room_id: page.roomId, url: image.url,
                                 body: image.body || image.shortcode, info: image.info })
                emojiPanel.open = false
            } else
                page.insertEmoji(":" + image.shortcode + ":")
        }
        onTextReaction: page.startTextReaction(eventId, page.reactMoreText)
        onPicked: {
            if (mode === "react")
                rustrx.command({ cmd: "react", room_id: page.roomId, event_id: eventId, key: emoji })
            else
                page.insertEmoji(emoji)
        }
    }
    // the keyboard back (a tap into the field): it takes the picker's place
    Connections {
        target: Qt.inputMethod
        onVisibleChanged: {
            page.cp("im", "vis=" + Qt.inputMethod.visible)
            if (Qt.inputMethod.visible && emojiPanel.open) page.emojiToKeys = true
        }
    }

    // Chat position trace: every move of the list with what caused it, in
    // the app log. dconf write /apps/harbour-rustrx/debugChatPos true
    ConfigurationValue { id: debugChatPos; key: "/apps/harbour-rustrx/debugChatPos"; defaultValue: false }
    readonly property bool cpOn: debugChatPos.value === true
    property real cpLastY: NaN
    property real cpLastCh: NaN
    property real cpLastKb
    // the first move after a status change is always logged
    property bool cpFresh
    onEmojiLiftChanged: if (cpOn && (!emojiLiftAnimated || emojiLift === 0 || emojiLift >= emojiHeight)) cp("lift")

    function viewAt(tag, index, mode) {
        cp("pos<" + tag, "i=" + index)
        list.positionViewAtIndex(index, mode)
        cp("pos>" + tag)
    }
    function viewAtBeginning(tag) {
        cp("pos<" + tag, "beginning")
        list.positionViewAtBeginning()
        cp("pos>" + tag)
    }
    function cpStatus(s) {
        return s === PageStatus.Inactive ? "inactive" : s === PageStatus.Activating ? "activating"
             : s === PageStatus.Active ? "active" : s === PageStatus.Deactivating ? "deactivating" : String(s)
    }
    // the row at a content y, stepping past the spacing and footer gaps
    function cpRow(y, step) {
        for (var d = 0; d <= 40; d += 8) {
            var i = list.indexAt(list.contentX + list.width / 2, y + step * d)
            if (i >= 0) return i
        }
        return -1
    }
    function cp(ev, extra) {
        if (!cpOn) return
        var top = cpRow(list.contentY + 1, 1)
        var bot = cpRow(list.contentY + list.height - 1, -1)
        // the composer covers the bottom edge: the row right above it
        if (bot < 0 && list.headerItem && list.headerItem.y > list.contentY)
            bot = cpRow(list.headerItem.y - 1, -1)
        var tb = ""
        if (top >= 0) {
            var r = rustrx.timeline.get(top)
            tb = r.kind === "event" ? (r.body || "").replace(/\s+/g, " ").slice(0, 16) : r.kind
        }
        var f = app.activeFocusItem
        var fn = f === undefined ? "n/a" : f ? (f.objectName || String(f).split("(")[0]) : "none"
        console.log("chatpos: " + ev + " y=" + Math.round(list.contentY) + " oy=" + Math.round(list.originY)
                    + " ch=" + Math.round(list.contentHeight) + " h=" + Math.round(list.height)
                    + " hdr=" + (list.headerItem ? Math.round(list.headerItem.height) : -1)
                    + " kb=" + Math.round(keyboardSize) + " lift=" + Math.round(emojiLift)
                    + " top=" + top + " \"" + tb + "\" bot=" + bot + " status=" + cpStatus(status)
                    + " focus=" + fn + " inp=" + (inputArea && inputArea.activeFocus ? 1 : 0)
                    + " src=" + (list.dragging ? "drag" : list.flicking ? "flick" : "-")
                    + (extra ? " " + extra : ""))
        cpLastY = list.contentY
        cpLastCh = list.contentHeight
    }
}
