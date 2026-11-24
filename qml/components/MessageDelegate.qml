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
import QtMultimedia 5.6
import Sailfish.Silica 1.0
import "../markup.js" as Markup

// One timeline row: a date divider, a read marker, or an event. An event is
// a bubble, own messages at the right in the highlight colour, others at the
// left under the sender's name; membership and state changes are a line.
ListItem {
    // not "item": inside a Loader's component that name is the Loader's own property
    id: msg
    width: ListView.view.width
    contentHeight: loader.height + Theme.paddingSmall
    menu: isEvent ? contextMenu : null
    enabled: isEvent

    property string roomId
    signal reply(string eventId, string text)
    signal edit(string eventId, string text)
    signal resend(string txnId)
    // "more" in the reactions: the room's emoji picker, for this message
    signal reactMore(string eventId, string text)
    // this message's thread: open it (and type into it, from the menu)
    signal openThread(string eventId, bool compose)
    // a tap on the reply's quote: to the message it answers
    signal jumpTo(string eventId)
    // shown for a moment when the chat jumped here
    property bool flash
    // its highlight still fading out
    property bool flashFading
    // shown in a thread's page: no thread of its own there
    property bool inThread
    // for the text selection's header (the More page)
    property string roomName
    // a thread's root: how many replies and the latest
    readonly property var threadSummary: model.thread ? model.thread : null
    // replies none of our read receipts reaches yet (the core's check:
    // read in another client counts too)
    readonly property bool threadUnread: !!threadSummary && !inThread && threadSummary.unread === true

    readonly property bool isEvent: model.kind === "event"
    readonly property bool own: model.is_own === true
    readonly property string msgType: model.type || "text"
    // state changes and membership: one quiet line, no bubble
    readonly property bool plainLine: msgType === "membership" || msgType === "state"
    readonly property bool isMedia: msgType === "image" || msgType === "video" || msgType === "sticker"
    readonly property bool isFile: msgType === "file" || msgType === "audio"
    readonly property color mainColor: own || highlighted ? Theme.highlightColor : Theme.primaryColor
    // a message that mentions us (or a keyword rule) gets the highlight colour
    readonly property bool mention: model.highlight === true && !own
    // its bar in front of the text, though not in a reply: there the line
    // belongs to the quote above (a reply to us always counts as a mention)
    readonly property bool mentionBar: mention && !replyTo
    readonly property color dimColor: own ? Theme.secondaryHighlightColor : Theme.secondaryColor
    // captured here: inside the Repeater, `model` would name the Repeater's own property
    readonly property var reactionList: model.reactions ? model.reactions : []
    // read once here: each read of a role asks the model again
    readonly property string htmlText: model.html || ""
    // the message with its spoilers shown, once the reader tapped one
    property string spoilersShown
    // opened once, they stay open when the row scrolls back into view (audit D2)
    function syncSpoilers() {
        var m = msg.ListView.view ? msg.ListView.view.model : null
        spoilersShown = m && htmlText.length && m.spoilersOpen(index) ? m.spoilersShown(index) : ""
    }
    onHtmlTextChanged: syncSpoilers()
    // a tap on a link in the text: a covered spoiler shows, the rest opens
    function linkTapped(link) {
        if (link === "rustrx:spoiler") spoilersShown = msg.ListView.view.model.spoilersShown(index)
        else app.openLink(link)
    }
    readonly property var replyTo: model.reply_to ? model.reply_to : null
    // the custom emoji in the message and its reactions ({ url, shortcode });
    // only the menu asks, finding them is a scan of the message
    function emoteList() { return model.emotes ? model.emotes : [] }
    // what the "More…" page needs, copied: the row can go while it is open
    function moreInfo() {
        var body = model.body || ""
        return {
            roomId: roomId, roomName: roomName, eventId: msg.eventId, rowId: model.id || "",
            sender: model.sender_name || model.sender || "", body: body,
            // what Forward's confirm view shows: a file by its name, a
            // picture by its thumbnail
            msgType: msgType, filename: model.filename || "",
            thumb: model.thumb_path || (msgType === "image" ? (model.media_path || "") : ""),
            // a picture's or file's caption or name: its menu has Save instead
            copyText: (isMedia || isFile) && body.length > 0,
            selectText: !inThread,
            copyLink: canAct && msg.eventId.length > 0,
            emotes: emoteList(),
            report: canAct && !own,
            // one's own, or as a moderator anyone's (the room's power levels)
            remove: msgType !== "redacted" && isEvent && (own || app.roomMay(roomId).redact_other === true),
            removeOther: !own,
            pin: canAct && msg.eventId.length > 0 && app.roomMay(roomId).pin === true,
            pinned: app.roomPinned(roomId).indexOf(msg.eventId) >= 0
        }
    }
    readonly property string eventId: model.event_id || ""
    readonly property bool failed: model.send_state === "failed"
    // the server takes no file this large: sending again cannot help
    readonly property bool tooLarge: failed && model.send_error === "too_large"
    // a picture or file on its way: the share of it the server has (0..1)
    readonly property bool uploading: model.send_state === "sending" && model.upload_total > 0
    readonly property real uploadFraction: uploading ? Math.min(1, model.upload_done / model.upload_total) : 0
    // a picture or file still going out (queued or uploading) can be
    // stopped: the SDK aborts a running upload and drops the message
    readonly property bool cancellable: (isMedia || isFile) && model.send_state === "sending" && !!model.txn_id
    function cancelUpload() {
        rustrx.command({ cmd: "redact", room_id: roomId, txn_id: model.txn_id })
    }
    // captured here for the same reason as reactionList
    readonly property var pollAnswers: model.answers ? model.answers : []
    readonly property bool pollEnded: model.ended === true
    readonly property int pollTotal: {
        var t = 0
        for (var i = 0; i < pollAnswers.length; i++) t += pollAnswers[i].count
        return t
    }
    readonly property bool canAct: isEvent && msgType !== "membership" && msgType !== "state" && msgType !== "redacted"
    // a reaction goes on state rows too: a wave on a join (Matrix allows one
    // on any event; the core counts them, the SDK does not)
    readonly property bool canReact: canAct || ((msgType === "membership" || msgType === "state") && eventId.length > 0)
    // the reactions offered in the menu; the first stays where it always was

    property bool exporting
    // a tap opens the copy in another app; "Save to Downloads" only saves it
    property bool exportOpens
    // a file opening or saving, a sound being fetched: a spinner in the
    // time line, and taps wait for it (0.8 audit, item 7)
    readonly property bool busy: exporting || wantPlay

    // a message from a device its sender has not verified (the core's red shield)
    readonly property bool unverified: !!model.shield && model.shield.indexOf("red") === 0

    // GIFs move (AnimatedImage; Image shows the first frame only). In the
    // chat only those in view play, by themselves if "Play GIFs" is on,
    // else after a tap; once, unless "Loop GIFs" is on. The thumbnail may be
    // a still JPEG, so the moving one is the full file (the author, 0.8)
    readonly property bool isGif: msgType === "image" && model.mime === "image/gif"
    readonly property bool inView: {
        // first: other rows take no dependency on the scroll (audit T12)
        if (!isGif) return false
        var v = msg.ListView.view
        if (!v || !msg.visible || !Qt.application.active) return false
        return msg.y + msg.height > v.contentY && msg.y < v.contentY + v.height
    }
    // played by a tap, until it leaves the view
    property bool gifTapped
    // played to its end once (no looping): it stays on its last frame
    property bool gifDone
    // it moves right now (set from inside the picture's component)
    property bool gifAnimating
    readonly property bool gifWanted: isGif && inView && !gifDone && (gifTapped || app.settings.gifAutoplay)
    onInViewChanged: if (!inView) { gifTapped = false; gifDone = false }
    // while the chat moves GIFs hold their frame (paused: playing false
    // would start them over) and one that comes into view meanwhile starts
    // once the chat stops (Qt 5.6 drops paused when a GIF starts)
    readonly property bool listMoving: isGif && !!msg.ListView.view && msg.ListView.view.moving
    property bool gifRun
    function updateGifRun() { gifRun = gifWanted && (gifRun || !listMoving) }
    onListMovingChanged: updateGifRun()
    // the moving GIF needs the full file: fetched when it is to play
    onGifWantedChanged: { updateGifRun(); fetchGif() }
    property bool gifAsked
    function fetchGif() {
        if (gifAsked || !gifWanted || !eventId.length || model.media_path && model.media_path.length) return
        gifAsked = true
        rustrx.getMedia(roomId, eventId, false)
    }

    // other apps cannot read this app's data folder: a copy goes to Downloads
    function openMedia() {
        exporting = true
        exportOpens = true
        rustrx.exportMedia(roomId, model.event_id)
    }
    function saveMedia() {
        exporting = true
        exportOpens = false
        rustrx.exportMedia(roomId, model.event_id)
    }

    // audio plays in place once the file is in the cache; an audio row gets
    // its player at the first tap, not as it scrolls in (audit MI5)
    property bool wantPlay
    property bool audioUsed
    readonly property var player: audioLoader.item
    readonly property bool playing: player !== null && player.playbackState === Audio.PlayingState
    Loader {
        id: audioLoader
        active: msgType === "audio" && audioUsed
        sourceComponent: Audio {
            source: model.media_path && model.media_path.length ? "file://" + model.media_path : ""
            onSourceChanged: if (msg.wantPlay && source.toString().length) { msg.wantPlay = false; play() }
        }
    }

    // A double tap replies, without the long press (side swipes belong to
    // the tabs and the room page), on every message, pictures and files
    // included: a single tap waits the double-tap interval before it opens
    // anything, as Qt cannot know at the first release whether a second tap
    // follows (the author: a double tap on a picture must reply). Only a
    // failed message acts at once: a tap sends it again.
    readonly property bool tapsAtOnce: failed
    onDoubleClicked: {
        singleTap.stop()
        if (tapsAtOnce || !canAct) return
        msg.reply(model.event_id, (model.sender_name || model.sender) + ": " + (model.body || ""))
    }
    onClicked: {
        if (!tapsAtOnce) { singleTap.restart(); return }
        // the second tap of a double tap, or a tap while it still loads
        if (tapCalm.running || busy) return
        tapCalm.restart()
        msg.tapped()
    }
    Timer {
        id: singleTap
        interval: Qt.styleHints.mouseDoubleClickInterval
        onTriggered: msg.tapped()
    }
    Timer { id: tapCalm; interval: Qt.styleHints.mouseDoubleClickInterval }

    function tapped() {
        // not sent: a tap sends it again as it was (0.8 audit, item 6)
        if (failed) {
            if (model.txn_id && !tooLarge) msg.resend(model.txn_id)
            return
        }
        // a reply in a thread, shown in the room's flow: on to its thread
        // (pictures, files, sounds and places still open themselves)
        if (model.thread_root && !inThread && !isMedia && !isFile && msgType !== "audio" && msgType !== "location") {
            msg.openThread(model.thread_root, false)
            return
        }
        // a picture, file or sound still on its way has nothing to open yet
        if (!eventId.length && (isMedia || isFile || msgType === "audio")) return
        if (msgType === "audio") {
            audioUsed = true
            if (!player) return
            if (playing) { player.pause(); return }
            if (player.source.toString().length) { player.play(); return }
            wantPlay = true
            rustrx.getMedia(roomId, eventId, false)
        } else if (msgType === "location" && model.geo_uri) {
            if (/^geo:/i.test(model.geo_uri)) app.openExternal(model.geo_uri)   // a maps app (Pure Maps) takes geo:
        } else if (msgType === "video") {
            pageStack.push(Qt.resolvedUrl("../pages/VideoPage.qml"), { roomId: roomId, eventId: eventId,
                           source: model.media_path || "" })
        } else if (isGif && !gifAnimating) {
            // a GIF standing still plays at a tap; a tap on a moving one opens it
            gifDone = false
            gifTapped = true
            fetchGif()
        } else if (msgType === "image" || msgType === "sticker") {
            pageStack.push(Qt.resolvedUrl("../pages/ImagePage.qml"), { roomId: roomId, eventId: eventId, mime: model.mime || "",
                           source: model.media_path || model.thumb_path || "", fullLoaded: !!(model.media_path && model.media_path.length) })
        } else if (isMedia || isFile) {
            openMedia()
        }
    }

    // A received file opens in the app its type belongs to; its name came
    // from the sender, so the path is encoded piece by piece (a "#" or "?"
    // in it opened another file), and installers and scripts are not
    // started from a message: those stay in Downloads
    function openFile(path) {
        var ext = (/\.([a-z0-9]+)$/i.exec(path) || ["", ""])[1].toLowerCase()
        if (["rpm", "apk", "deb", "sh", "desktop", "run", "bin", "appimage"].indexOf(ext) >= 0) {
            //: %1 where the file was saved
            //% "Saved to %1. Install it from there yourself if you trust it."
            Notices.show(qsTrId("rustrx-no-saved-not-opened").arg(path))
            return
        }
        Qt.openUrlExternally("file://" + path.split("/").map(encodeURIComponent).join("/"))
    }

    // only while this row waits for something (audit T5)
    Connections {
        target: exporting || wantPlay || gifAsked ? rustrx : null
        onMediaExported: {
            if (!exporting || eventId !== model.event_id) return
            exporting = false
            if (exportOpens) msg.openFile(path)
            else Notices.show(qsTrId("rustrx-no-saved").arg(path))
        }
        // the error names no event: whatever waited for a file stops waiting
        onErrorOccurred: if (cmd === "get_media") { exporting = false; wantPlay = false; gifAsked = false }
    }

    // a media row asks for its picture once it is on screen: the thumbnail
    // where there is one, else the image itself. An upload still on its way
    // has no event yet; it asks once the server has given it one.
    function requestPicture() {
        if (!isMedia || !eventId.length) return
        if (model.thumb_path && model.thumb_path.length || model.media_path && model.media_path.length) return
        if (model.has_thumb) rustrx.getMedia(roomId, eventId, true)
        else if (msgType !== "video") rustrx.getMedia(roomId, eventId, false)
    }
    Component.onCompleted: {
        requestPicture()
        updateGifRun()
        fetchGif()
        syncSpoilers()
    }
    onEventIdChanged: { requestPicture(); fetchGif() }

    // the size of Twemoji in the small lines is set by the page (RoomPage,
    // ThreadPage) on its model

    Loader {
        id: loader
        width: parent.width
        sourceComponent: model.kind === "date" ? dateDivider
                       : model.kind === "readmarker" ? readMarker
                       : model.kind === "event" ? (plainLine ? plainRow : eventRow) : empty
    }

    Component { id: empty; Item { height: 0 } }

    Component {
        id: dateDivider
        Item {
            height: Theme.itemSizeExtraSmall
            Label {
                anchors.centerIn: parent
                text: app.dayLabel(model.ts)
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }
        }
    }

    Component {
        id: readMarker
        Item {
            height: Theme.paddingLarge
            Separator {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                color: Theme.highlightColor
                horizontalAlignment: Qt.AlignHCenter
            }
        }
    }

    // state changes and membership as one quiet line, without a bubble:
    // a component of its own, so these rows build no bubble (audit T5)
    Component {
        id: plainRow
        Column {
            width: parent.width
            readonly property real sideMargin: Theme.horizontalPageMargin

            Label {
                x: sideMargin
                width: parent.width - 2 * sideMargin
                text: model.body || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }
            // its reactions, close under the line (a wave on a join, as nheko has it)
            Loader {
                readonly property real maxWidth: parent.width - 2 * sideMargin
                readonly property real gap: Theme.paddingSmall
                x: sideMargin
                active: reactionList.length > 0
                visible: active
                sourceComponent: reactionPills
            }
        }
    }

    Component {
        id: eventRow
        Column {
            width: parent.width

            readonly property real sideMargin: Theme.horizontalPageMargin
            readonly property real bubbleWidth: width - sideMargin - Theme.itemSizeSmall
            readonly property real pad: Theme.paddingMedium
            // the widest the content of a bubble gets
            readonly property real innerMax: bubbleWidth - 2 * pad

            // the bubble: own messages at the right in the highlight colour,
            // others at the left with the sender's name on top (no avatars).
            // A picture or video sent on its own (no caption, no reply) has
            // no bubble: the picture itself, its corners rounded as one,
            // the name above and the time below it.
            Item {
                id: bubble
                readonly property bool bare: isMedia && msgType !== "sticker" && replyTo === null
                                             && !(model.caption && model.caption.length)
                readonly property real edge: bare ? 0 : pad
                x: own ? parent.width - width - sideMargin : sideMargin
                width: inner.width + 2 * edge
                height: inner.height + 2 * edge

                // the chat jumped here from a reply's quote: a moment of
                // highlight, made only then and kept until it has faded
                Loader {
                    anchors.fill: parent
                    active: msg.flash || msg.flashFading
                    sourceComponent: Component {
                        Rectangle {
                            radius: Theme.paddingLarge / 2
                            color: Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityHigh)
                            opacity: 0
                            Behavior on opacity { FadeAnimation { duration: Tunables.jumpFlashMs } }
                            Component.onCompleted: {
                                msg.flashFading = true
                                opacity = Qt.binding(function () { return msg.flash ? 1 : 0 })
                            }
                            onOpacityChanged: if (opacity === 0 && !msg.flash) msg.flashFading = false
                        }
                    }
                }
                Rectangle {
                    visible: !bubble.bare
                    anchors.fill: parent
                    radius: Theme.paddingLarge / 2
                    color: own ? Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityFaint)
                               : Theme.rgba(Theme.primaryColor, Theme.opacityFaint / 2)
                }

                Column {
                    id: inner
                    x: bubble.edge
                    y: bubble.edge
                    spacing: Theme.paddingSmall / 2

                    Label {
                        visible: !own
                        width: Math.min(implicitWidth, innerMax)
                        text: model.sender_name || model.sender || ""
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                    }

                    // the message this one replies to: who said it and its first lines
                    // in a bubble of its own, a shade darker than the message's,
                    // the line its left edge
                    Loader {
                        active: replyTo !== null
                        visible: active
                        sourceComponent: Component {
                            Item {
                                width: Math.min(quote.implicitWidth + Tunables.barWidth + 2 * Theme.paddingSmall, innerMax)
                                height: quote.height + 2 * Theme.paddingSmall
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !!(replyTo && replyTo.event_id)
                                    onClicked: msg.jumpTo(replyTo.event_id)
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.paddingSmall
                                    color: Qt.rgba(0, 0, 0, Tunables.quoteShade)
                                }
                                Rectangle {
                                    width: Tunables.barWidth
                                    height: parent.height
                                    color: Theme.secondaryHighlightColor
                                }

                                Label {
                                    id: quote
                                    x: Tunables.barWidth + Theme.paddingSmall
                                    y: Theme.paddingSmall
                                    width: parent.width - Tunables.barWidth - 2 * Theme.paddingSmall
                                    wrapMode: Text.Wrap
                                    maximumLineCount: Tunables.quoteLines
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    color: Theme.secondaryColor
                                    textFormat: Text.StyledText
                                    text: {
                                        var r = replyTo
                                        if (!r) return ""
                                        var quoted
                                        // a leading paragraph or break would leave an empty line under the name
                                        if (r.html && r.html.length) quoted = r.html.replace(/^(\s|<p[^>]*>|<br\s*\/?>)+/i, "")
                                        else if (r.body !== undefined) quoted = rustrx.emojify(Markup.escaped(r.body).replace(/\n/g, "<br>"),
                                                                                               Math.round(Theme.fontSizeExtraSmall * Tunables.emojiScale))
                                        //: the quoted message of a reply while it is fetched
                                        //% "Loading the original message"
                                        else if (r.pending) quoted = "<i>" + qsTrId("rustrx-la-reply-loading") + "</i>"
                                        //: the quoted message of a reply cannot be had (deleted, or no access)
                                        //% "Original message not available"
                                        else quoted = "<i>" + qsTrId("rustrx-la-reply-missing") + "</i>"
                                        var who = Markup.escaped(r.sender_name || r.sender)
                                        return (who.length ? "<font color=\"" + Theme.secondaryHighlightColor + "\">" + who + "</font><br>" : "") + quoted
                                    }
                                }
                            }
                        }
                    }

                    // a thin bar marks a mention; plain bodies get their addresses linked
                    Row {
                        id: bodyRow
                        // what goes before the text: the mention bar, a file's or a place's icon
                        readonly property real lead: (mentionBar ? spacing + Tunables.barWidth : 0)
                                                     + (kindIcon.active ? spacing + kindIcon.width : 0)
                        visible: !isMedia
                        width: Math.min(body.implicitWidth + lead, innerMax)
                        spacing: Theme.paddingSmall
                        Rectangle { visible: mentionBar; width: Tunables.barWidth; height: body.height; color: Theme.highlightColor }
                        // the kind of file at a glance (the system's icon for
                        // its type), a pin on a place (0.8 audit, icons)
                        Loader {
                            id: kindIcon
                            active: msgType === "file" || msgType === "location"
                            visible: active
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.iconSizeMedium
                            height: width
                            // the file icons keep their own colours, as in the file manager
                            sourceComponent: msgType === "file" ? fileIcon : placeIcon
                            Component {
                                id: fileIcon
                                HighlightImage {
                                    sourceSize.width: width
                                    sourceSize.height: height
                                    source: Theme.iconForMimeType(model.mime || "")
                                    highlighted: msg.highlighted
                                }
                            }
                            Component {
                                id: placeIcon
                                Icon {
                                    sourceSize.width: width
                                    sourceSize.height: height
                                    source: "image://theme/icon-m-location"
                                    color: mainColor
                                }
                            }
                        }
                        Label {
                            id: body
                            width: parent.width - bodyRow.lead
                            wrapMode: Text.Wrap
                            textFormat: msgType === "text" || msgType === "notice" || msgType === "emote" || htmlText.length > 0 ? Text.StyledText : Text.PlainText
                            color: msgType === "utd" || msgType === "redacted" ? dimColor : (mention ? Theme.highlightColor : mainColor)
                            font.italic: msgType === "emote" || msgType === "utd" || msgType === "redacted"
                            linkColor: Theme.secondaryHighlightColor
                            onLinkActivated: linkTapped(link)
                            text: {
                                //% "Waiting for the key to decrypt this message"
                                if (msgType === "utd") return qsTrId("rustrx-la-utd")
                                //% "Message removed"
                                if (msgType === "redacted") return qsTrId("rustrx-la-redacted")
                                if (msgType === "audio") {
                                    var state = playing ? "⏸ " : "▶ "
                                    var len = player && player.duration > 0 ? player.duration : (model.duration || 0)
                                    var pos = player && player.playbackState !== Audio.StoppedState
                                              ? Format.formatDuration(player.position / 1000, Formatter.DurationShort) + " / " : ""
                                    return state + (model.filename || model.body || "") + (len > 0 ? "  " + pos + Format.formatDuration(len / 1000, Formatter.DurationShort) : "")
                                }
                                if (msgType === "file") {
                                    var size = model.size ? " (" + Format.formatFileSize(model.size) + ")" : ""
                                    return (model.filename || model.body || "") + size
                                }
                                //: a shared location, tap opens it in a maps app
                                //% "Location (tap to open)"
                                if (msgType === "location") return (model.body || "") + "\n" + qsTrId("rustrx-la-location-open")
                                //% "Verification request"
                                if (msgType === "verification") return qsTrId("rustrx-la-verification-request")
                                // the model renders plain bodies (body_html) once
                                if (msgType === "emote") return model.body_html
                                if (htmlText.length) return spoilersShown.length ? spoilersShown : htmlText
                                if (msgType === "text" || msgType === "notice") return model.body_html
                                return model.body || ""
                            }
                        }
                    }

                    // image, video thumbnail or sticker
                    Loader {
                        active: isMedia
                        visible: active
                        sourceComponent: Component {
                            Item {
                                readonly property real maxWidth: Math.min(innerMax, Math.min(msg.width, Screen.width) * Tunables.mediaMaxWidth)
                                // the picture as it shows, once loaded (upright: its turn
                                // applied): the sender's size may be the stored one of a
                                // turned video, and the box came out landscape around a
                                // portrait thumbnail (the author's find); before that the
                                // sender's size, else a guess
                                readonly property bool loaded: picture.status === Image.Ready
                                                               && picture.implicitWidth > 0 && picture.implicitHeight > 0
                                readonly property bool sized: loaded || !!(model.h && model.w)
                                readonly property real aspect: loaded ? picture.implicitHeight / picture.implicitWidth
                                                             : sized ? model.h / model.w : Tunables.mediaFallbackAspect
                                // a portrait one as narrow as it is: no empty sides
                                // beside it, and the rounded corners on its own
                                height: Math.min(maxWidth, Math.max(Theme.itemSizeLarge, maxWidth * aspect))
                                width: sized ? Math.min(maxWidth, height / aspect) : maxWidth
                                layer.enabled: bubble.bare
                                layer.effect: RoundMask { radiusPx: Theme.paddingLarge / 2 }

                                // without a bubble: a shade where it is still loading
                                Rectangle {
                                    visible: bubble.bare && picture.status !== Image.Ready
                                    anchors.fill: parent
                                    color: Theme.rgba(Theme.primaryColor, Theme.opacityFaint / 2)
                                }

                                Image {
                                    id: picture
                                    // a thumbnail Qt cannot read (a format without its
                                    // image plugin): the picture itself instead
                                    property bool thumbFailed
                                    anchors.fill: parent
                                    asynchronous: true
                                    autoTransform: true
                                    fillMode: Image.PreserveAspectFit
                                    // a video's own file is no picture: only its thumbnail
                                    source: !thumbFailed && model.thumb_path && model.thumb_path.length ? rustrx.picture(model.thumb_path)
                                          : msgType !== "video" && model.media_path && model.media_path.length
                                            ? rustrx.picture(model.media_path) : ""
                                    onStatusChanged: {
                                        if (status !== Image.Error || thumbFailed || msgType === "video") return
                                        if (!(model.thumb_path && model.thumb_path.length)) return
                                        thumbFailed = true
                                        if (!(model.media_path && model.media_path.length)) rustrx.getMedia(roomId, eventId, false)
                                    }
                                    // what the bubble's width comes to in portrait: decoded once
                                    sourceSize.width: Math.round(Screen.width * Tunables.mediaMaxWidth)
                                    // under a moving GIF (a transparent one showed the still through)
                                    opacity: gif.item && gif.item.status === Image.Ready ? 0 : 1
                                }
                                // a GIF that moves: the full file, over the still; made
                                // only for a GIF whose file is in (audit T5). Qt 5.6's
                                // AnimatedImage ignores sourceSize: its frames decode at
                                // the GIF's own size (audit T15)
                                Loader {
                                    id: gif
                                    anchors.fill: parent
                                    active: isGif && !!(model.media_path && model.media_path.length)
                                    sourceComponent: Component {
                                        AnimatedImage {
                                            visible: status === Image.Ready
                                            asynchronous: true
                                            cache: false
                                            fillMode: Image.PreserveAspectFit
                                            source: rustrx.picture(model.media_path)
                                            playing: msg.gifRun
                                            paused: msg.gifRun && msg.listMoving
                                            // no looping: it stops on its last frame
                                            onCurrentFrameChanged: if (!app.settings.gifLoop && frameCount > 1 && currentFrame === frameCount - 1) msg.gifDone = true
                                            // one that ends by itself (its own loop count) counts as done; looping, it starts again
                                            onPlayingChanged: if (!playing && msg.gifWanted) {
                                                msg.gifDone = true
                                                if (app.settings.gifLoop) gifAgain.restart()
                                            }
                                            Timer { id: gifAgain; interval: 0; onTriggered: msg.gifDone = false }
                                        }
                                    }
                                }
                                Binding { target: msg; property: "gifAnimating"; value: !!gif.item && gif.item.visible && gif.item.playing && gif.item.frameCount > 1 }
                                // a GIF standing still says what it is, so a tap is expected to play it
                                Rectangle {
                                    visible: isGif && !msg.gifAnimating
                                    anchors { left: parent.left; bottom: parent.bottom; margins: Theme.paddingSmall }
                                    width: gifMark.implicitWidth + 2 * Theme.paddingSmall
                                    height: gifMark.implicitHeight
                                    radius: Theme.paddingSmall
                                    color: Theme.rgba(Theme.overlayBackgroundColor, Theme.opacityOverlay)
                                    Label {
                                        id: gifMark
                                        anchors.centerIn: parent
                                        //: the mark on a GIF that is not moving; a tap plays it
                                        //% "GIF"
                                        text: qsTrId("rustrx-la-gif")
                                        font.pixelSize: Theme.fontSizeExtraSmall
                                        font.bold: true
                                        color: Theme.primaryColor
                                    }
                                }
                                // a video is told from a photo before the tap (0.8 audit, icons)
                                Image {
                                    visible: msgType === "video" && picture.status === Image.Ready
                                    anchors.centerIn: parent
                                    readonly property real side: Math.min(parent.width, parent.height) > 2 * Theme.iconSizeLarge
                                                                 ? Theme.iconSizeLarge : Theme.iconSizeMedium
                                    width: side
                                    height: side
                                    sourceSize.width: side
                                    sourceSize.height: side
                                    source: visible ? "image://theme/icon-l-opaque-play" : ""
                                    opacity: msg.highlighted ? Theme.opacityHigh : 1
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: picture.status === Image.Error
                                             || !(model.thumb_path && model.thumb_path.length) && !(model.media_path && model.media_path.length)
                                    text: model.body || ""
                                    color: dimColor
                                    font.pixelSize: Theme.fontSizeSmall
                                }
                            }
                        }
                    }

                    // a poll: one line per answer with its votes; a tap votes
                    // (or changes the vote) while it is open
                    Loader {
                        active: msgType === "poll"
                        visible: active
                        sourceComponent: Component {
                            Column {
                                width: innerMax
                                Repeater {
                                    model: msgType === "poll" && msg.pollAnswers ? msg.pollAnswers : []
                                    BackgroundItem {
                                        width: parent.width
                                        height: Theme.itemSizeExtraSmall
                                        enabled: !msg.pollEnded
                                        onClicked: rustrx.command({ cmd: "poll_vote", room_id: roomId, event_id: msg.eventId, answers: [modelData.id] })
                                        Rectangle {
                                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                                            height: parent.height - Theme.paddingSmall
                                            width: parent.width * (msg.pollTotal > 0 ? modelData.count / msg.pollTotal : 0)
                                            radius: Theme.paddingSmall
                                            color: Theme.rgba(modelData.mine ? Theme.highlightColor : Theme.primaryColor, Theme.opacityFaint)
                                        }
                                        Label {
                                            anchors { left: parent.left; leftMargin: Theme.paddingSmall; verticalCenter: parent.verticalCenter }
                                            width: parent.width - countLabel.width - 2 * Theme.paddingSmall
                                            truncationMode: TruncationMode.Fade
                                            text: (modelData.mine ? "✓ " : "") + modelData.text
                                            color: modelData.mine ? Theme.highlightColor : Theme.primaryColor
                                            font.pixelSize: Theme.fontSizeSmall
                                        }
                                        Label {
                                            id: countLabel
                                            anchors { right: parent.right; rightMargin: Theme.paddingSmall; verticalCenter: parent.verticalCenter }
                                            text: modelData.count
                                            color: Theme.secondaryColor
                                            font.pixelSize: Theme.fontSizeSmall
                                        }
                                    }
                                }
                                Label {
                                    visible: msg.pollEnded
                                    //% "The poll has ended"
                                    text: qsTrId("rustrx-la-poll-ended")
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    color: dimColor
                                }
                            }
                        }
                    }

                    // the text sent with a picture, a video or a file
                    Loader {
                        active: !!(model.caption && model.caption.length)
                        visible: active
                        sourceComponent: Component {
                            Label {
                                width: Math.min(implicitWidth, innerMax)
                                wrapMode: Text.Wrap
                                textFormat: Text.StyledText
                                text: model.caption_html
                                color: mainColor
                                linkColor: Theme.secondaryHighlightColor
                                onLinkActivated: app.openLink(link)
                            }
                        }
                    }

                    // an upload on its way: how much of the file is at the server
                    // with a cross beside it that stops the upload
                    Loader {
                        active: uploading
                        visible: active
                        sourceComponent: Component {
                            Item {
                                width: Math.min(innerMax, Math.min(msg.width, Screen.width) * Tunables.mediaMaxWidth)
                                height: cancelButton.height
                                Rectangle {
                                    anchors { left: parent.left; right: cancelButton.left; rightMargin: Theme.paddingSmall; verticalCenter: parent.verticalCenter }
                                    height: Theme.paddingSmall
                                    radius: height / 2
                                    color: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
                                    Rectangle {
                                        width: parent.width * uploadFraction
                                        height: parent.height
                                        radius: parent.radius
                                        color: Theme.highlightColor
                                    }
                                }
                                IconButton {
                                    id: cancelButton
                                    anchors.right: parent.right
                                    width: Theme.iconSizeMedium
                                    height: width
                                    icon.source: "image://theme/icon-m-clear"
                                    onClicked: msg.cancelUpload()
                                }
                            }
                        }
                    }

                    Loader {
                        readonly property real maxWidth: innerMax
                        readonly property real gap: Theme.paddingMedium
                        active: reactionList.length > 0
                        visible: active
                        sourceComponent: reactionPills
                    }
                    // a reply in a thread, here in the room's flow: the thread mark,
                    // a tap opens the thread
                    Loader {
                        active: !inThread && !!model.thread_root
                        visible: active
                        sourceComponent: Component {
                            BackgroundItem {
                                id: inThreadLine
                                width: Math.min(innerMax, inThreadRow.width + 2 * Theme.paddingSmall)
                                height: inThreadRow.height + Theme.paddingSmall
                                onClicked: msg.openThread(model.thread_root, false)
                                Row {
                                    id: inThreadRow
                                    x: Theme.paddingSmall
                                    spacing: Theme.paddingSmall
                                    // the thread mark: one icon in one size everywhere (0.8 audit, icons)
                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.iconSizeSmall
                                        height: width
                                        sourceSize.width: width
                                        sourceSize.height: height
                                        source: "image://theme/icon-s-chat"
                                        color: inThreadLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        font.pixelSize: Theme.fontSizeExtraSmall
                                        color: inThreadLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                                        //% "in a thread"
                                        text: qsTrId("rustrx-la-in-thread")
                                    }
                                }
                            }
                        }
                    }

                    // the replies in this message's thread: a tap opens it
                    Loader {
                        active: !inThread && threadSummary !== null && threadSummary.count > 0
                        visible: active
                        sourceComponent: Component {
                            BackgroundItem {
                                id: threadLine
                                width: Math.min(innerMax, threadRow.width + 2 * Theme.paddingSmall)
                                height: threadRow.height + 2 * Theme.paddingSmall
                                onClicked: msg.openThread(msg.eventId, false)
                                Row {
                                    id: threadRow
                                    x: Theme.paddingSmall
                                    y: Theme.paddingSmall
                                    spacing: Theme.paddingSmall
                                    // the thread mark, as above
                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.iconSizeSmall
                                        height: width
                                        sourceSize.width: width
                                        sourceSize.height: height
                                        source: "image://theme/icon-s-chat"
                                        color: Theme.highlightColor
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.min(implicitWidth, innerMax - Theme.iconSizeSmall - 3 * Theme.paddingSmall)
                                        truncationMode: TruncationMode.Fade
                                        font.pixelSize: Theme.fontSizeExtraSmall
                                        color: threadLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                                        textFormat: Text.StyledText
                                        text: {
                                            if (!threadSummary) return ""
                                            //: %n replies in a thread
                                            //% "%n replies"
                                            var t = qsTrId("rustrx-la-thread-replies", threadSummary.count)
                                            //: a thread has replies not read yet
                                            //% "new replies"
                                            if (threadUnread) t += " · <b><font color=\"" + Theme.highlightColor + "\">" + qsTrId("rustrx-la-thread-unread") + "</font></b>"
                                            // "sender: text", rendered by the model
                                            if (threadSummary.latest_sender_name) t += " · " + model.thread_html
                                            return t
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // a message the server did not take, said plainly in the
                    // error colour; a tap sends it again (0.8 audit, item 6)
                    Loader {
                        active: failed
                        visible: active
                        x: parent.width - width
                        sourceComponent: Component {
                            Row {
                                spacing: Theme.paddingSmall
                                Icon {
                                    id: failedMark
                                    anchors.verticalCenter: parent.verticalCenter
                                    source: "image://theme/icon-s-warning"
                                    color: Theme.errorColor
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, innerMax - failedMark.width - parent.spacing)
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    color: Theme.errorColor
                                    text: msg.tooLarge
                                          //% "Not sent: larger than your server takes."
                                          ? qsTrId("rustrx-la-not-sent-too-large")
                                          //% "Not sent. Tap to send again."
                                          : qsTrId("rustrx-la-not-sent-tap")
                                }
                            }
                        }
                    }

                    // the time and the message's state, at the bubble's right
                    // edge: a spinner while its file opens or saves, a warning
                    // mark on a message from an unverified device
                    Row {
                        id: stateRow
                        x: parent.width - width
                        spacing: Theme.paddingSmall
                        readonly property real markSize: Math.round(Theme.fontSizeExtraSmall * Tunables.emojiScale)
                        // both made only when shown (audit T5)
                        Loader {
                            id: busyMark
                            active: msg.busy
                            visible: active
                            anchors.verticalCenter: parent.verticalCenter
                            sourceComponent: Component {
                                BusyIndicator {
                                    size: BusyIndicatorSize.ExtraSmall
                                    running: true
                                }
                            }
                        }
                        Loader {
                            id: shieldMark
                            active: msg.unverified
                            visible: active
                            anchors.verticalCenter: parent.verticalCenter
                            width: stateRow.markSize
                            height: width
                            sourceComponent: Component {
                                Icon {
                                    sourceSize.width: width
                                    sourceSize.height: height
                                    source: "image://theme/icon-s-high-importance"
                                    color: Theme.errorColor
                                }
                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, innerMax - (busyMark.active ? busyMark.width + parent.spacing : 0)
                                                                    - (shieldMark.active ? shieldMark.width + parent.spacing : 0))
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeTiny
                            color: dimColor
                            text: {
                                var t = rustrx.formatTime(model.ts)
                                if (uploading)
                                    //: a file on its way to the server, %1 how much of it, in percent
                                    //% "uploading %1 %"
                                    t += " · " + qsTrId("rustrx-la-uploading").arg(Math.round(uploadFraction * 100))
                                //% "sending"
                                else if (model.send_state === "sending") t += " · " + qsTrId("rustrx-la-sending")
                                //% "edited"
                                if (model.edited) t += " · " + qsTrId("rustrx-la-edited")
                                if (msg.unverified)
                                    //: a message from a device the sender has not verified
                                    //% "unverified device"
                                    t += " · " + qsTrId("rustrx-la-shield-red")
                                else if (model.shield && model.shield.indexOf("grey") === 0)
                                    //: authenticity cannot be checked (e.g. a key from backup)
                                    //% "not verifiable"
                                    t += " · " + qsTrId("rustrx-la-shield-grey")
                                if (app.showReadBy && model.read_by && model.read_by.length)
                                    //: %1 the names of the people whose read receipt sits on this message
                                    //% "read by %1"
                                    t += " · " + qsTrId("rustrx-la-read-by").arg(model.read_by.join(", "))
                                return t
                            }
                        }
                    }
                }
            }
        }
    }

    // the reaction pills, under a bubble or under a state line; the Loader
    // that shows them gives the widest they get and the space between them
    Component {
        id: reactionPills
        Flow {
            id: pills
            // the reactions' width in one line, summed from the chips
            // themselves, so the bubble is no wider than they need (a hidden
            // second set of labels measured it before, audit T5)
            property real oneLine
            function measure() {
                var w = 0
                for (var i = 0; i < pillRepeater.count; i++) {
                    var it = pillRepeater.itemAt(i)
                    if (it) w += (w > 0 ? spacing : 0) + it.width
                }
                oneLine = w
            }
            width: Math.min(parent ? parent.maxWidth : 0, oneLine)
            spacing: parent ? parent.gap : Theme.paddingMedium
            Repeater {
                id: pillRepeater
                model: reactionList
                onItemAdded: pills.measure()
                onItemRemoved: pills.measure()
                // a tap toggles our own reaction with this key
                Item {
                    width: chipRow.width
                    height: chipRow.height
                    onWidthChanged: pills.measure()
                    Row {
                        id: chipRow
                        spacing: Theme.paddingSmall / 2
                        // a custom emoji (its mxc address is the key)
                        Image {
                            visible: modelData.emote !== undefined
                            anchors.verticalCenter: parent.verticalCenter
                            height: visible ? chip.height : 0
                            width: height
                            fillMode: Image.PreserveAspectFit
                            sourceSize.height: chip.height
                            source: modelData.emote || ""
                        }
                        Label {
                            id: chip
                            textFormat: Text.StyledText
                            // label: the key as the model rendered it
                            text: (modelData.emote !== undefined ? "" : modelData.label + " ") + modelData.count
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: modelData.me ? Theme.highlightColor : Theme.secondaryColor
                        }
                    }
                    MouseArea {
                        anchors { fill: parent; margins: -Theme.paddingSmall }
                        onClicked: rustrx.command({ cmd: "react", room_id: roomId, event_id: msg.eventId, key: modelData.key })
                    }
                }
            }

        }
    }

    Component {
        id: contextMenu
        ContextMenu {
            // the reactions first: the five most used at a tap, the menu
            // closes after one; "more" opens the emoji picker for this message
            Row {
                visible: canReact && !failed && !cancellable
                anchors.horizontalCenter: parent.horizontalCenter
                Repeater {
                    model: app.favouriteEmoji
                    BackgroundItem {
                        width: Theme.itemSizeSmall
                        height: Theme.itemSizeSmall
                        onClicked: {
                            var key = modelData
                            rustrx.command({ cmd: "react", room_id: roomId, event_id: msg.eventId, key: key })
                            msg.closeMenu()
                            // last: counting can reorder the five, and the
                            // Repeater then destroys this button mid-handler
                            app.countEmoji(key)
                        }
                        // a Twemoji picture where there is one
                        Image {
                            anchors.centerIn: parent
                            width: Theme.iconSizeMedium * Tunables.emojiGlyphScale
                            height: width
                            sourceSize.width: width
                            source: rustrx.emojiUrl(modelData)
                        }
                        Label { anchors.centerIn: parent; visible: !rustrx.emojiUrl(modelData); text: modelData; font.pixelSize: Theme.fontSizeLarge }
                    }
                }
                IconButton {
                    width: Theme.itemSizeSmall
                    height: Theme.itemSizeSmall
                    icon.source: "image://theme/icon-m-add"
                    onClicked: {
                        var id = msg.eventId
                        // a state line already names who it is about
                        var t = canAct ? (model.sender_name || model.sender) + ": " + (model.body || "") : (model.body || "")
                        msg.closeMenu()
                        msg.reactMore(id, t)
                    }
                }
            }
            // a picture or file still going out: stop it
            MenuItem {
                visible: cancellable
                //% "Cancel upload"
                text: qsTrId("rustrx-me-cancel-upload")
                onClicked: msg.cancelUpload()
            }
            // a failed message, picture or file: send it again as it was, or
            // let it go
            MenuItem {
                visible: failed && !tooLarge
                //% "Send again"
                text: qsTrId("rustrx-me-send-again")
                onClicked: msg.resend(model.txn_id || "")
            }
            MenuItem {
                visible: failed
                //% "Discard"
                text: qsTrId("rustrx-me-discard")
                onClicked: rustrx.command({ cmd: "redact", room_id: roomId, txn_id: model.txn_id || "" })
            }
            MenuItem {
                //% "Reply"
                text: qsTrId("rustrx-me-reply")
                visible: canAct && !failed && !cancellable
                onClicked: msg.reply(model.event_id, (model.sender_name || model.sender) + ": " + (model.body || ""))
            }
            MenuItem {
                //% "Reply in thread"
                text: qsTrId("rustrx-me-reply-thread")
                visible: canAct && !failed && !inThread && !model.thread_root
                onClicked: {
                    var id = msg.eventId
                    msg.closeMenu()
                    msg.openThread(id, true)
                }
            }
            MenuItem {
                //% "Edit"
                text: qsTrId("rustrx-me-edit")
                visible: own && model.editable === true && (msgType === "text" || msgType === "emote" || msgType === "notice")
                onClicked: msg.edit(model.event_id, model.body || "")
            }
            // four at most, as Sailfish apps keep a menu; the rest on the
            // "More…" page. The fourth: Copy text, or Save for a picture or file
            MenuItem {
                //% "Copy text"
                text: qsTrId("rustrx-me-copy")
                visible: !isMedia && !isFile && model.body && model.body.length > 0
                onClicked: {
                    Clipboard.text = model.body
                    //% "Text copied"
                    Notices.show(qsTrId("rustrx-no-text-copied"), Notice.Short)
                }
            }
            MenuItem {
                text: qsTrId("rustrx-me-save-downloads")
                visible: (isMedia || isFile) && !failed
                onClicked: saveMedia()
            }
            MenuItem {
                //% "More…"
                text: qsTrId("rustrx-me-more")
                visible: !failed && msg.eventId.length > 0
                onClicked: pageStack.push(Qt.resolvedUrl("../pages/MessageActionsPage.qml"), { info: msg.moreInfo() })
            }
        }
    }
}
