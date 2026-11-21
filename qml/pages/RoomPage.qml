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
    objectName: "room"

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
    // the timeline is one for the app: this page's room is in it. Not so
    // for a room page under another one (a share opened a second room
    // above it), which neither pages back nor sends receipts until it is
    // in front again and has opened its room anew
    readonly property bool inTimeline: rustrx.openRoomId === roomId

    Component.onCompleted: {
        rustrx.openRoom(roomId)
        rustrx.command({ cmd: "image_packs", room_id: roomId })
        // what one may do here (a moderator's Remove, Pin) and the pins
        rustrx.command({ cmd: "room_settings", room_id: roomId })
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
        // the room is left: the unread line goes to where reading stopped
        moveLine()
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
        // a page comes over the chat: the receipt still waiting goes out
        // (the unread line stays until the room is left)
        if (status === PageStatus.Deactivating) sendReceipt()
        if (status === PageStatus.Active) {
            app.lastRoom = roomId
            app.lastRoomAccount = rustrx.accountKey
            // back from a room opened above this one: its own messages again
            // (that page closes its room as it goes only while still the open one)
            if (!inTimeline) {
                atStart = false
                paginating = false
                rustrx.openRoom(roomId)
            }
            // opened by Answer on an incoming call: join it
            if (rustrx.takePendingCall() === roomId)
                rustrx.command({ cmd: "call_start", room_id: roomId })
            readOnArrival()
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

    // Reading. The read receipt follows the newest message whose end was
    // on screen, never back: the unread count is what comes after it. The
    // unread line stays put while the room is open, so nothing shifts
    // under the reader, and moves there when the room is left or the app
    // goes to the cover. Nothing is read before the room has opened at its
    // unread line (positioned): marking at the newest end on the first
    // messages marked a room with 30 unread read on opening.
    // ("Mark messages as read only when they have been shown" off: opening
    // the room is enough, the receipt goes to its newest message at once.)

    // the newest message read in this visit (an event id), "" none yet
    property string readUpTo
    // a jump went past unread messages: no receipts until they are in view
    property bool readHeld

    function readingNow() {
        return positioned && !readHeld && status === PageStatus.Active && inTimeline
                && Qt.application.active && rustrx.timeline.count > 0
    }
    // the last read message's row: this visit's receipt, else the unread
    // line's; -1 when neither is known
    function frontierRow() {
        return readUpTo.length ? rustrx.timeline.rowOfEvent(readUpTo) : markerRow
    }
    function frontierInView() {
        var f = frontierRow()
        if (f < 0) return true
        var b = anchor.rowAt(list.contentY + list.height - 1)
        var top = list.indexAt(list.width / 2, list.contentY + Theme.paddingSmall)
        return !!b && f >= b.index && (top < 0 || f <= top)
    }

    // the room opened, its messages changed, back from the cover
    function readOnArrival() {
        if (!positioned) return
        if (!app.readWhenShown && inTimeline && status === PageStatus.Active)
            queueReceipt("", false)
        readSoon()
    }
    // what is on screen is looked at a moment after the chat moved
    function readSoon() { if (!readScan.running) readScan.start() }
    Timer { id: readScan; interval: Tunables.readScanMs; onTriggered: page.scanRead() }
    function scanRead() {
        if (readHeld && frontierInView()) readHeld = false
        if (!readingNow()) return
        var bottom = list.contentY + list.height
        var r = anchor.rowAt(bottom - 1)
        if (!r) return
        // its end is below the view: the next older message is the last read
        var row = r.index
        if (r.item && r.item.y + r.item.height > bottom + 1) row++
        var newest = rustrx.timeline.seenEventId(0)
        // the newest message shown: the whole room (own and hidden events too)
        if (row === 0) {
            if (!newest.length || newest !== readUpTo) readTo("")
            return
        }
        var id = rustrx.timeline.seenEventId(row)
        if (!id.length) return
        var f = frontierRow()
        if (f >= 0 && rustrx.timeline.rowOfEvent(id) >= f) return
        readTo(id)
    }
    // id "": the whole room. The unread line goes along only while it is
    // not shown: new messages in a room read to its end bring up no line
    function readTo(id) {
        readUpTo = id.length ? id : rustrx.timeline.seenEventId(0)
        queueReceipt(id, !id.length && rustrx.timeline.rowOfKind("readmarker") < 0)
    }

    // at most one receipt per moment: at the newest end the list's
    // signals can fire every frame (seen: one a frame for five seconds)
    // A whole room's receipt waiting wins over one message's.
    function queueReceipt(id, marker) {
        var waiting = markReadTimer.queued
        if (!waiting || !id.length || markReadTimer.eventId.length) markReadTimer.eventId = id
        markReadTimer.marker = (waiting && markReadTimer.marker) || marker
        markReadTimer.queued = true
        if (!markReadTimer.running) markReadTimer.start()
    }
    Timer {
        id: markReadTimer
        property string eventId
        property bool marker
        property bool queued
        interval: Tunables.markReadMs
        onTriggered: page.sendReceipt()
    }
    // the waiting receipt goes out now; it was queued while the room was
    // in front, so it goes out from the cover too
    function sendReceipt() {
        markReadTimer.stop()
        if (!markReadTimer.queued) return
        markReadTimer.queued = false
        app.markRead(roomId, markReadTimer.marker, markReadTimer.eventId)
    }
    // the unread line to where reading stopped (nothing read in this visit:
    // it stays). One request with the receipt still waiting: two at once
    // for the same message, the second failed with the first.
    function moveLine() {
        markReadTimer.stop()
        var waiting = markReadTimer.queued
        markReadTimer.queued = false
        if (waiting && !markReadTimer.eventId.length) {
            app.markRead(roomId, readUpTo.length > 0 || markReadTimer.marker, "")
            return
        }
        if (!readUpTo.length) return
        var whole = inTimeline && readUpTo === rustrx.timeline.seenEventId(0)
        app.markRead(roomId, true, whole ? "" : readUpTo)
    }
    // back from the cover with this room in front: what is shown now is
    // read (its notification goes with the receipt); off to the cover:
    // the line moves
    Connections {
        target: Qt.application
        onActiveChanged: {
            if (page.status !== PageStatus.Active) return
            if (Qt.application.active) page.readOnArrival()
            else page.moveLine()
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

    Timer {
        id: moreLater
        interval: 0
        onTriggered: if (list.atYEnd && !page.paginating && !page.atStart && list.count > 0) {
            page.paginating = true
            rustrx.command({ cmd: "paginate_back", room_id: page.roomId })
        }
    }

    Connections {
        target: rustrx
        onPaginated: if (roomId === page.roomId) {
            paginating = false
            page.atStart = atStart
            // still at the top: the page brought little to show (state
            // events, hidden ones), so the top never left and loading
            // stopped there until a scroll down and up again; the next
            // page comes by itself
            if (!atStart && list.atYEnd && page.inTimeline) moreLater.restart()
            // the older page is in the model once the timeline came
            if (page.jumpTarget.length) jumpRetry.restart()
            if (!page.positioned) seekRetry.restart()
        }
        // a failed page (no network while scrolling up): the next scroll tries again
        onErrorOccurred: if (cmd === "paginate_back") {
            paginating = false
            // no network while looking for the unread line: open where it is
            if (!page.positioned) { page.seekPages = Tunables.readSeekPages; page.position() }
        }
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
            layer.enabled: app.avatarRadius > 0
            layer.effect: RoundMask { radius: app.avatarRadius }
        }
        description: rustrx.syncState === "idle"
                     ? qsTrId("rustrx-la-cover-connecting")
                     // one word with the cover's for the same state (0.8 audit, T5)
                     : rustrx.syncState !== "running"
                     //% "Offline"
                     ? qsTrId("rustrx-la-cover-offline")
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

    // The first time the room fills it opens at the unread line. The first
    // messages are the newest few: when the line is further back, older
    // pages come in until it is (a few at most), and nothing is read meanwhile.
    property bool positioned
    property int seekPages
    // the room's unread count as it opened (-1: not taken yet)
    property int openUnread: -1
    readonly property bool seeking: !positioned && list.count > 0
    function position() {
        if (positioned || rustrx.timeline.count === 0) return
        if (openUnread < 0) openUnread = roomInfo ? (roomInfo.unread || 0) : 0
        if (markerRow < 0 && openUnread > 0 && !atStart && seekPages < Tunables.readSeekPages) {
            if (!paginating && inTimeline) {
                ++seekPages
                paginating = true
                rustrx.command({ cmd: "paginate_back", room_id: roomId })
            }
            return
        }
        positioned = true
        if (markerRow >= 0) {
            viewAt("readmarker", markerRow, ListView.Center)
        } else if (seekPages > 0) {
            //% "Older unread messages are further back"
            Notices.show(qsTrId("rustrx-no-unread-further-back"), Notice.Short)
        }
        readOnArrival()
    }
    // the read marker's row (-1: none), and the first unread message's row
    // when it is above the view (-1: not): then a pill at the top goes back
    // to it
    property int markerRow: -1
    property int unreadAbove: -1
    // the unread line, else the message our read receipt is on: the unread
    // count runs from the receipt, and the line (m.fully_read, moved by
    // other clients only now and then) can lie further back than any
    // search, which then opened the room at the bottom and read it all
    function findMarker() {
        var line = rustrx.timeline.rowOfKind("readmarker")
        return line >= 0 ? line : rustrx.timeline.rowOwnRead()
    }
    function checkMarker() {
        // read as shown: what comes after the last read message; read on
        // opening: the line
        var f = app.readWhenShown ? frontierRow() : markerRow
        if (f <= 0) { unreadAbove = -1; return }
        // newest first, laid out bottom to top: the row at the view's top
        // edge is the oldest in view
        var top = list.indexAt(list.width / 2, list.contentY + Theme.paddingSmall)
        unreadAbove = top >= 0 && f > top ? f : -1
    }
    onMarkerRowChanged: checkMarker()
    onReadUpToChanged: checkMarker()
    Connections {
        target: rustrx.timeline
        onCountChanged: {
            if (trace.enabled) trace.log("count", "n=" + rustrx.timeline.count)
            page.markerRow = page.findMarker()
            page.position()
            page.readOnArrival()
        }
    }
    // the first unread message is above the view: a pill under the header
    // goes back to it
    BackgroundItem {
        id: unreadPill
        z: 2
        visible: page.unreadAbove >= 0
        anchors { top: header.bottom; horizontalCenter: parent.horizontalCenter }
        width: pillLabel.implicitWidth + 2 * Theme.paddingLarge
        height: pillLabel.implicitHeight + Theme.paddingMedium
        onClicked: if (page.unreadAbove >= 0) page.viewAt("unread pill", page.unreadAbove, ListView.Center)
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityHigh)
        }
        Label {
            id: pillLabel
            anchors.centerIn: parent
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            //: the pill that goes back to the first unread message
            //% "↑ Unread"
            text: qsTrId("rustrx-bt-first-unread")
        }
    }

    // pinned messages: a pill at the right under the header; each tap goes
    // to the next one, the newest first (the chat loads back to it)
    readonly property var pinned: app.roomPinned(roomId)
    property int pinStep
    BackgroundItem {
        id: pinPill
        z: 2
        visible: page.pinned.length > 0
        anchors { top: header.bottom; right: parent.right; rightMargin: Theme.horizontalPageMargin }
        width: pinLabel.implicitWidth + 2 * Theme.paddingLarge
        height: pinLabel.implicitHeight + Theme.paddingMedium
        onClicked: {
            var n = page.pinned.length
            page.jumpTo(page.pinned[n - 1 - (page.pinStep % n)])
            page.pinStep++
        }
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityLow)
        }
        Label {
            id: pinLabel
            anchors.centerIn: parent
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            //: the pill that goes to the room's pinned messages; %n how many
            //% "Pinned (%n)"
            text: qsTrId("rustrx-bt-pinned", page.pinned.length)
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
        // no current item: a message arriving at the newest end moved it, and
        // the view gave it the focus, away from the composer in the header,
        // so the keyboard closed on every message sent or received
        currentIndex: -1
        spacing: Theme.paddingSmall
        clip: true

        onContentYChanged: { trace.logY(); page.checkMarker(); page.readSoon() }
        onOriginYChanged: trace.log("oy")
        onContentHeightChanged: trace.logContentHeight()
        onHeightChanged: trace.log("h")
        onMovementEnded: {
            trace.log("moved")
            page.readSoon()
        }

        // the oldest end is at the top in this layout
        onAtYEndChanged: {
            if (atYEnd && count > 0 && !paginating && !atStart && page.inTimeline) {
                paginating = true
                rustrx.command({ cmd: "paginate_back", room_id: roomId })
            }
        }

        // with an empty message an entry sends on its own; with a message
        // drafted it attaches: the text goes along as the caption
        PushUpMenu {
            // pictures and videos through Sailfish's gallery pickers, one
            // step to choose which: videos took the file picker and its
            // folders before (the author's find; four entries are what one
            // thumb's swipe reaches, so not a fifth)
            MenuItem {
                //% "Attach a picture or video"
                text: page.hasDraft ? qsTrId("rustrx-me-attach-media")
                                    //% "Send a picture or video"
                                    : qsTrId("rustrx-me-send-media")
                onClicked: pageStack.push(mediaChoice)
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
                visible: running
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

            // this room's classic call runs while the user reads the chat:
            // back to it (the call page no longer keeps the user away)
            BackgroundItem {
                visible: rustrx.voip.state !== "idle" && rustrx.voip.state !== "incoming" && rustrx.voip.roomId === roomId
                width: parent.width
                height: visible ? Theme.itemSizeSmall : 0
                onClicked: app.showVoip()
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
                        text: qsTrId("rustrx-la-call-return").arg(rustrx.voip.peerName || "")
                        color: Theme.highlightColor
                    }
                }
            }

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
            roomName: page.roomName
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
            // offline the list may be empty for want of a connection, not
            // of messages
            text: rustrx.syncState === "running" || rustrx.syncState === "idle"
                  //% "No messages yet"
                  ? qsTrId("rustrx-la-no-messages")
                  : qsTrId("rustrx-la-cover-offline")
        }

        BusyIndicator {
            visible: running
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            // also while the room looks for its unread line
            running: (list.count === 0 && openGrace.running) || page.seeking
        }

        VerticalScrollDecorator { }
    }

    Component {
        id: multiImagePicker
        MultiImagePickerDialog { }
    }
    Component {
        id: multiVideoPicker
        MultiVideoPickerDialog { }
    }
    // the gallery picker for pictures or the one for videos, in this page's
    // place; what was chosen is sent, the composer's text the caption of a
    // single one
    function pickMedia(videos) {
        var picker = pageStack.replace(videos ? multiVideoPicker : multiImagePicker)
        picker.accepted.connect(function () {
            var caption = picker.selectedContent.count === 1 ? page.takeCaption() : ""
            for (var i = 0; i < picker.selectedContent.count; i++)
                rustrx.sendFile(roomId, picker.selectedContent.get(i).filePath, caption)
        })
    }
    Component {
        id: mediaChoice
        Page {
            Column {
                width: parent.width
                PageHeader {
                    title: page.hasDraft ? qsTrId("rustrx-me-attach-media") : qsTrId("rustrx-me-send-media")
                }
                Repeater {
                    model: [
                        //% "Pictures"
                        { videos: false, text: qsTrId("rustrx-la-media-pictures"), icon: "image://theme/icon-m-image" },
                        //% "Videos"
                        { videos: true, text: qsTrId("rustrx-la-media-videos"), icon: "image://theme/icon-m-video" }
                    ]
                    BackgroundItem {
                        id: choiceRow
                        width: parent.width
                        height: Theme.itemSizeMedium
                        onClicked: page.pickMedia(modelData.videos)
                        Icon {
                            id: choiceIcon
                            x: Theme.horizontalPageMargin
                            anchors.verticalCenter: parent.verticalCenter
                            source: modelData.icon
                            highlighted: choiceRow.highlighted
                        }
                        Label {
                            anchors { left: choiceIcon.right; leftMargin: Theme.paddingLarge; verticalCenter: parent.verticalCenter }
                            text: modelData.text
                            color: choiceRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                    }
                }
            }
        }
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
            // past unread messages (a newer pinned one): they are not read
            // until they were in view
            var f = frontierRow()
            if (f >= 0 && row < f) readHeld = true
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
        if (!paginating && page.inTimeline) {
            ++jumpPages
            paginating = true
            rustrx.command({ cmd: "paginate_back", room_id: roomId })
        }
    }
    Timer { id: jumpRetry; interval: Tunables.menuCloseMs; onTriggered: page.tryJump() }
    Timer { id: seekRetry; interval: Tunables.menuCloseMs; onTriggered: page.position() }
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
