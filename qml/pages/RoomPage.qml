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

// The conversation. The list runs bottom to top, so its header, the text
// field, sits at the bottom, and the push-up menu with the attachments
// appears when the newest message is in view.
Page {
    id: page

    property string roomId
    property string roomName
    // who else is typing here, for the header
    property var typingNames: []
    property bool paginating
    // the oldest message is in: no more pages to ask for
    property bool atStart
    property bool infoAttached
    // the composer lives in the list header, a scope of its own; it registers here
    property Item composer

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
    Component.onDestruction: {
        if (composer) composer.saveDraft()
        if (rustrx.openRoomId === roomId) rustrx.closeRoom()
    }

    // the chat keeps its place across pages above it (ChatAnchor)
    ChatAnchor {
        id: anchor
        page: page
        list: list
        model: rustrx.timeline
        trace: trace
    }

    onStatusChanged: {
        trace.logStatus()
        anchor.pageStatus(status)
        // a receipt still waiting goes out as the page leaves
        if (status === PageStatus.Deactivating && markReadTimer.running) {
            markReadTimer.stop()
            markReadTimer.triggered()
        }
        if (status === PageStatus.Active) {
            app.lastRoom = roomId
            // opened by Answer on an incoming call: join it
            if (rustrx.takePendingCall() === roomId)
                rustrx.command({ cmd: "call_start", room_id: roomId })
            // read only at the newest end: marking moves the read marker's
            // row, which shifted what someone scrolled up was reading
            if (anchor.atNewest()) markRead()
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
    Timer { id: openGrace; interval: Tunables.roomOpenGraceMs; running: true }

    // at most one receipt per moment: at the newest end the list's
    // signals can fire every frame (seen: one a frame for five seconds)
    function markRead() {
        if (!markReadTimer.running) markReadTimer.start()
    }
    Timer {
        id: markReadTimer
        interval: Tunables.markReadMs
        onTriggered: {
            if (rustrx.timeline.count > 0 && Qt.application.state === Qt.ApplicationActive)
                app.markRead(roomId)
        }
    }

    // a message is drafted: the push-up menu attaches to it
    readonly property bool hasDraft: !!composer && composer.hasDraft

    // whatever is in the composer, taken out of it (VoicePage calls it too)
    function takeCaption() { return composer ? composer.takeCaption() : "" }

    function startEdit(eventId, text) {
        if (composer) composer.edit(eventId, text)
        showComposer()
    }

    // Reply and Edit come from a message's menu: while it was open the list
    // scrolled to keep it in view, and the text field (the list's header, at
    // the bottom) went out of sight. Once the menu has closed, back to the
    // bottom with the field focused.
    function showComposer() { composerLater.restart() }
    Timer {
        id: composerLater
        interval: Tunables.menuCloseMs
        onTriggered: {
            page.viewAtBeginning("composer")
            if (page.composer) page.composer.field.forceActiveFocus()
            trace.log("composer-focused")
        }
    }

    // the composer takes a text reaction; the draft waits meanwhile
    function startTextReaction(eventId, text) {
        if (composer) composer.react(eventId, text)
        showComposer()
    }

    Connections {
        target: rustrx
        onPaginated: if (roomId === page.roomId) {
            paginating = false
            page.atStart = atStart
            // the older page is in the model once the timeline came
            if (page.jumpTarget.length) jumpRetry.restart()
        }
        // a failed page (no network while scrolling up): the next scroll tries again
        onErrorOccurred: if (cmd === "paginate_back") paginating = false
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
    readonly property bool hasPicture: !!(roomInfo && roomInfo.avatar_path)

    // the header stays put; the list runs underneath it
    PageHeader {
        id: header
        title: roomName
        // room for the picture right of the name, as tall as the name's letters
        readonly property real pictureSize: Math.round(_titleItem.font.pixelSize * Tunables.avatarScale)
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
            source: page.hasPicture ? "file://" + page.roomInfo.avatar_path + "?v=" + (page.roomInfo.avatar_v || 0) : ""
            layer.enabled: app.roundAvatars
            layer.effect: RoundMask {}
        }
        description: rustrx.syncState === "idle"
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
            if (trace.enabled) trace.log("count", "n=" + rustrx.timeline.count)
            if (!page.positioned && rustrx.timeline.count > 0) {
                page.positioned = true
                for (var i = 0; i < rustrx.timeline.count; i++) {
                    if (rustrx.timeline.get(i).kind === "readmarker") {
                        page.viewAt("readmarker", i, ListView.Center)
                        break
                    }
                }
            }
            if (anchor.atNewest()) markRead()
        }
    }
    // the chat position trace, off by default (ChatTrace)
    ChatTrace {
        id: trace
        page: page
        list: list
        model: rustrx.timeline
        dock: emojiDock
    }

    SilicaListView {
        id: list
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        // the emoji picker takes the keyboard's place below the composer (EmojiDock)
        anchors.bottomMargin: emojiDock.aboveKeyboard
        verticalLayoutDirection: ListView.BottomToTop
        model: rustrx.timeline
        // read once the newest end is reached (scrolled to, or arrived at):
        // the room opens at the read marker, and marking on every return
        // moved the marker under someone scrolled up
        onAtYBeginningChanged: if (atYBeginning && page.status === PageStatus.Active) page.markRead()
        // no current item: a message arriving at the newest end moved it, and
        // the view gave it the focus, away from the composer in the header,
        // so the keyboard closed on every message sent or received
        currentIndex: -1
        spacing: Theme.paddingSmall
        clip: true

        onContentYChanged: trace.logY()
        onOriginYChanged: trace.log("oy")
        onContentHeightChanged: trace.logContentHeight()
        onHeightChanged: trace.log("h")
        onMovementEnded: {
            trace.log("moved")
            // scrolled to the newest message: the room is read
            if (page.status === PageStatus.Active && anchor.atNewest()) page.markRead()
        }

        // the oldest end is at the top in this layout
        onAtYEndChanged: {
            if (atYEnd && count > 0 && !paginating && !atStart) {
                paginating = true
                rustrx.command({ cmd: "paginate_back", room_id: roomId })
            }
        }

        // with an empty message an entry sends on its own; with a message
        // drafted it attaches: the text goes along as the caption
        PushUpMenu {
            MenuItem {
                //% "Attach an image"
                text: page.hasDraft ? qsTrId("rustrx-me-attach-image")
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
            // a long message wraps to a new line: the field grows downwards,
            // under the keyboard, unless the list follows it (dodoradio)
            onHeightChanged: {
                trace.log("hdr")
                if (page.composer && page.composer.field.activeFocus) page.viewAtBeginning("grow")
            }

            Item { width: 1; height: Theme.paddingMedium }

            // a call is running here: join it
            BackgroundItem {
                visible: !!(page.roomInfo && page.roomInfo.in_call)
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

            Composer {
                id: composer
                roomId: page.roomId
                drafts: true
                typing: true
                packs: page.packs
                emojiOpen: emojiDock.open
                //% "Message"
                placeholderText: qsTrId("rustrx-ph-message")
                //: %1 the room's name
                //% "Typing to %1"
                label: app.sendTyping && app.typingHint ? qsTrId("rustrx-la-typing-to").arg(roomName) : ""
                labelVisible: label !== ""
                // no scrolling the chat to the field when it gets its focus
                // back (every return from a page above, and with every step
                // of the keyboard's animation): the chat keeps its place
                autoScroll: false
                Component.onCompleted: page.composer = composer
                // the emoji picker (and the stickers with an empty message);
                // a second tap brings the keyboard back
                onEmojiClicked: {
                    if (emojiDock.open && emojiDock.mode === "insert") {
                        emojiDock.toKeyboard()
                    } else {
                        emojiDock.stickers = page.hasStickers && composer.text.trim().length === 0
                        emojiDock.show("insert", "")
                    }
                }
            }
        }

        delegate: MessageDelegate {
            roomId: page.roomId
            // a row that grows or shrinks (a picture, a menu), for the trace
            property real cpH
            onHeightChanged: {
                trace.logRow(index, cpH, height)
                cpH = height
            }
            onReply: {
                if (page.composer) page.composer.reply(eventId, text)
                page.showComposer()
            }
            onEdit: page.startEdit(eventId, text)
            // the failed message goes out again as it was
            onResend: rustrx.command({ cmd: "retry_send", room_id: page.roomId, txn_id: txnId })
            onReactMore: { page.reactMoreText = text; emojiDock.show("react", eventId) }
            onJumpTo: page.jumpTo(eventId)
            flash: model.event_id !== undefined && model.event_id === page.flashEventId
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

    function insertEmoji(emoji) {
        if (composer) composer.insert(emoji)
    }
    // the message the picker reacts to, for "React with text"
    property string reactMoreText
    // the emoji picker in the keyboard's place (EmojiDock)
    EmojiDock {
        id: emojiDock
        page: page
        trace: trace
        packs: page.packs
        // a pack's picture: a reaction (its address the key), a sticker sent
        // (an empty message, and the image is one), else :shortcode: in the text
        onPickedImage: {
            if (mode === "react")
                rustrx.command({ cmd: "react", room_id: page.roomId, event_id: eventId, key: image.mxc })
            else if (stickers && image.sticker && (!page.composer || page.composer.text.trim().length === 0)) {
                rustrx.command({ cmd: "send_sticker", room_id: page.roomId, mxc: image.mxc,
                                 body: image.body || image.shortcode, info: image.info })
                emojiDock.open = false
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

    // A reply's quote tapped: the chat goes to the message it answers. Not
    // loaded yet, older pages come in until it is (a few at most), then it
    // shows in the middle with a moment of highlight.
    property string jumpTarget
    property int jumpPages
    property string flashEventId
    function jumpTo(eventId) {
        jumpTarget = eventId
        jumpPages = 0
        tryJump()
    }
    function tryJump() {
        if (!jumpTarget.length) return
        var row = rustrx.timeline.rowOfEvent(jumpTarget)
        if (row >= 0) {
            viewAt("jump", row, ListView.Center)
            flashEventId = jumpTarget
            flashOff.restart()
            jumpTarget = ""
            return
        }
        if (atStart || jumpPages >= Tunables.jumpMaxPages) {
            jumpTarget = ""
            //% "The original message is too far back"
            Notices.show(qsTrId("rustrx-no-jump-too-far"), Notice.Short)
            return
        }
        if (!paginating) {
            ++jumpPages
            paginating = true
            rustrx.command({ cmd: "paginate_back", room_id: roomId })
        }
    }
    Timer { id: jumpRetry; interval: Tunables.menuCloseMs; onTriggered: page.tryJump() }
    Timer { id: flashOff; interval: Tunables.jumpFlashMs * 3; onTriggered: page.flashEventId = "" }

    // positioning, with a trace line before and after
    function viewAt(tag, index, mode) {
        trace.log("pos<" + tag, "i=" + index)
        list.positionViewAtIndex(index, mode)
        trace.log("pos>" + tag)
    }
    function viewAtBeginning(tag) {
        trace.log("pos<" + tag, "beginning")
        list.positionViewAtBeginning()
        trace.log("pos>" + tag)
    }
}
