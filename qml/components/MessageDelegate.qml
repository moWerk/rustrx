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
    signal resend(string txnId, string text)
    // "more" in the reactions: the room's emoji picker, for this message
    signal reactMore(string eventId, string text)
    // this message's thread: open it (and type into it, from the menu)
    signal openThread(string eventId, bool compose)
    // shown in a thread's page: no thread of its own there
    property bool inThread
    // a thread's root: how many replies and the latest
    readonly property var threadSummary: model.thread ? model.thread : null
    // replies in it not seen here yet
    readonly property int threadNew: threadSummary && !inThread
                                     ? (rustrx.threadsSeen, Math.max(0, threadSummary.count - rustrx.threadSeen(eventId, threadSummary.count)))
                                     : 0

    readonly property bool isEvent: model.kind === "event"
    readonly property bool own: model.is_own === true
    readonly property string msgType: model.type || "text"
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
    readonly property var replyTo: model.reply_to ? model.reply_to : null
    // the custom emoji in the message and its reactions ({ url, shortcode });
    // only the menu asks, finding them is a scan of the message
    function emoteList() { return model.emotes ? model.emotes : [] }
    readonly property string eventId: model.event_id || ""
    readonly property bool failed: model.send_state === "failed"
    // a picture or file on its way: the share of it the server has (0..1)
    readonly property bool uploading: model.send_state === "sending" && model.upload_total > 0
    readonly property real uploadFraction: uploading ? Math.min(1, model.upload_done / model.upload_total) : 0
    // captured here for the same reason as reactionList
    readonly property var pollAnswers: model.answers ? model.answers : []
    readonly property bool pollEnded: model.ended === true
    readonly property int pollTotal: {
        var t = 0
        for (var i = 0; i < pollAnswers.length; i++) t += pollAnswers[i].count
        return t
    }
    readonly property bool canAct: isEvent && msgType !== "membership" && msgType !== "state" && msgType !== "redacted"
    // the reactions offered in the menu; the first stays where it always was

    // a reaction's key as shown: text reactions can be long
    function reactionText(key) {
        return key.length > Tunables.reactionKeyMax ? key.substring(0, Tunables.reactionKeyMax - 1) + "\u2026" : key
    }

    // text for a StyledText, as it is
    function escapeText(t) {
        return t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    property bool exporting
    // a tap opens the copy in another app; "Save to Downloads" only saves it
    property bool exportOpens

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

    // audio plays in place once the file is in the cache; only audio rows
    // carry a player
    property bool wantPlay
    readonly property var player: audioLoader.item
    readonly property bool playing: player !== null && player.playbackState === Audio.PlayingState
    Loader {
        id: audioLoader
        active: msgType === "audio"
        sourceComponent: Audio {
            source: model.media_path && model.media_path.length ? "file://" + model.media_path : ""
            onSourceChanged: if (msg.wantPlay && source.toString().length) { msg.wantPlay = false; play() }
        }
    }

    // A double tap replies, without the long press (side swipes belong to
    // the tabs and the room page). A single tap (opening a picture, playing
    // audio) waits the system's double-tap interval first: at the first
    // release Qt cannot know whether a second tap follows.
    onDoubleClicked: {
        singleTap.stop()
        if (!canAct || failed) return
        msg.reply(model.event_id, (model.sender_name || model.sender) + ": " + (model.body || ""))
    }
    onClicked: singleTap.restart()
    Timer {
        id: singleTap
        interval: Qt.styleHints.mouseDoubleClickInterval
        onTriggered: msg.tapped()
    }

    function tapped() {
        // a reply in a thread, shown in the room's flow: on to its thread
        // (pictures, files, sounds and places still open themselves)
        if (model.thread_root && !inThread && !isMedia && !isFile && msgType !== "audio" && msgType !== "location") {
            msg.openThread(model.thread_root, false)
            return
        }
        if (msgType === "audio") {
            if (!player) return
            if (playing) { player.pause(); return }
            if (player.source.toString().length) { player.play(); return }
            wantPlay = true
            rustrx.getMedia(roomId, eventId, false)
        } else if (msgType === "location" && model.geo_uri) {
            Qt.openUrlExternally(model.geo_uri)   // a maps app (Pure Maps) takes geo:
        } else if (msgType === "video") {
            pageStack.push(Qt.resolvedUrl("../pages/VideoPage.qml"), { roomId: roomId, eventId: eventId,
                           source: model.media_path || "" })
        } else if (msgType === "image" || msgType === "sticker") {
            pageStack.push(Qt.resolvedUrl("../pages/ImagePage.qml"), { roomId: roomId, eventId: eventId,
                           source: model.media_path || model.thumb_path || "", fullLoaded: !!(model.media_path && model.media_path.length) })
        } else if (isMedia || isFile) {
            openMedia()
        }
    }

    Connections {
        target: rustrx
        onMediaExported: {
            if (!exporting || eventId !== model.event_id) return
            exporting = false
            if (exportOpens) Qt.openUrlExternally("file://" + path)
            else Notices.show(qsTrId("rustrx-no-saved").arg(path))
        }
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
    Component.onCompleted: requestPicture()
    // the size of Twemoji in the small lines, rendered by the model
    Binding {
        target: msg.ListView.view ? msg.ListView.view.model : null
        property: "smallEmojiSize"
        value: Math.round(Theme.fontSizeExtraSmall * Tunables.emojiScale)
    }
    onEventIdChanged: requestPicture()

    Loader {
        id: loader
        width: parent.width
        sourceComponent: model.kind === "date" ? dateDivider
                       : model.kind === "readmarker" ? readMarker
                       : model.kind === "event" ? eventRow : empty
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

    Component {
        id: eventRow
        Column {
            width: parent.width

            readonly property real sideMargin: Theme.horizontalPageMargin
            readonly property real bubbleWidth: width - sideMargin - Theme.itemSizeSmall
            readonly property real pad: Theme.paddingMedium
            // the widest the content of a bubble gets
            readonly property real innerMax: bubbleWidth - 2 * pad
            readonly property bool plainLine: msgType === "membership" || msgType === "state"

            // state changes and membership as one quiet line, without a bubble
            Label {
                visible: plainLine
                x: sideMargin
                width: parent.width - 2 * sideMargin
                text: model.body || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }

            // the bubble: own messages at the right in the highlight colour,
            // others at the left with the sender's name on top (no avatars)
            Item {
                id: bubble
                visible: !plainLine
                x: own ? parent.width - width - sideMargin : sideMargin
                width: inner.width + 2 * pad
                height: visible ? inner.height + 2 * pad : 0

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.paddingLarge / 2
                    color: own ? Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityFaint)
                               : Theme.rgba(Theme.primaryColor, Theme.opacityFaint / 2)
                }

                Column {
                    id: inner
                    x: pad
                    y: pad
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
                                        else if (r.body !== undefined) quoted = rustrx.emojify(r.body.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/\n/g, "<br>"),
                                                                                               Math.round(Theme.fontSizeExtraSmall * Tunables.emojiScale))
                                        //: the quoted message of a reply while it is fetched
                                        //% "Loading the original message"
                                        else if (r.pending) quoted = "<i>" + qsTrId("rustrx-la-reply-loading") + "</i>"
                                        //: the quoted message of a reply cannot be had (deleted, or no access)
                                        //% "Original message not available"
                                        else quoted = "<i>" + qsTrId("rustrx-la-reply-missing") + "</i>"
                                        var who = r.sender_name || r.sender || ""
                                        return (who.length ? "<font color=\"" + Theme.secondaryHighlightColor + "\">" + who + "</font><br>" : "") + quoted
                                    }
                                }
                            }
                        }
                    }

                    // a thin bar marks a mention; plain bodies get their addresses linked
                    Row {
                        visible: !isMedia
                        width: Math.min(body.implicitWidth + (mentionBar ? spacing + Tunables.barWidth : 0), innerMax)
                        spacing: Theme.paddingSmall
                        Rectangle { visible: mentionBar; width: Tunables.barWidth; height: body.height; color: Theme.highlightColor }
                        Label {
                            id: body
                            width: parent.width - (mentionBar ? parent.spacing + Tunables.barWidth : 0)
                            wrapMode: Text.Wrap
                            textFormat: msgType === "text" || msgType === "notice" || msgType === "emote" || htmlText.length > 0 ? Text.StyledText : Text.PlainText
                            color: msgType === "utd" || msgType === "redacted" ? dimColor : (mention ? Theme.highlightColor : mainColor)
                            font.italic: msgType === "emote" || msgType === "utd" || msgType === "redacted"
                            linkColor: Theme.secondaryHighlightColor
                            onLinkActivated: app.openLink(link)
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
                                if (htmlText.length) return htmlText
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
                                width: Math.min(innerMax, Math.min(msg.width, Screen.width) * Tunables.mediaMaxWidth)
                                height: Math.min(width, Math.max(Theme.itemSizeLarge, width * (model.h && model.w ? model.h / model.w : Tunables.mediaFallbackAspect)))

                                Image {
                                    anchors.fill: parent
                                    asynchronous: true
                                    autoTransform: true
                                    fillMode: Image.PreserveAspectFit
                                    source: model.thumb_path && model.thumb_path.length ? "file://" + model.thumb_path
                                          : model.media_path && model.media_path.length ? "file://" + model.media_path : ""
                                    // what the bubble's width comes to in portrait: decoded once
                                    sourceSize.width: Math.round(Screen.width * Tunables.mediaMaxWidth)
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: !(model.thumb_path && model.thumb_path.length) && !(model.media_path && model.media_path.length)
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
                    Loader {
                        active: uploading
                        visible: active
                        sourceComponent: Component {
                            Rectangle {
                                width: Math.min(innerMax, Math.min(msg.width, Screen.width) * Tunables.mediaMaxWidth)
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
                        }
                    }

                    Loader {
                        active: reactionList.length > 0
                        visible: active
                        sourceComponent: Component {
                            Flow {
                                width: Math.min(innerMax, reactionMeasure.width)
                                spacing: Theme.paddingMedium
                                Repeater {
                                    model: reactionList
                                    // a tap toggles our own reaction with this key
                                    Item {
                                        width: chipRow.width
                                        height: chipRow.height
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
                                                text: (modelData.emote !== undefined ? ""
                                                      : rustrx.emojify(escapeText(reactionText(modelData.key)), Math.round(font.pixelSize * Tunables.emojiScale)) + " ") + modelData.count
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

                                // the reactions' width in one line, so the bubble is no wider
                                // than they need (not shown, so the Flow leaves it out)
                                Row {
                                    id: reactionMeasure
                                    visible: false
                                    spacing: Theme.paddingMedium
                                    Repeater {
                                        model: reactionList
                                        Label {
                                            textFormat: Text.StyledText
                                            text: (modelData.emote !== undefined ? "\u2B1C"
                                                  : rustrx.emojify(escapeText(reactionText(modelData.key)), Math.round(font.pixelSize * Tunables.emojiScale))) + " " + modelData.count
                                            font.pixelSize: Theme.fontSizeExtraSmall
                                        }
                                    }
                                }
                            }
                        }
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
                                    Image {
                                        anchors.verticalCenter: parent.verticalCenter
                                        sourceSize.width: Theme.iconSizeExtraSmall
                                        sourceSize.height: Theme.iconSizeExtraSmall
                                        source: "image://theme/icon-m-menu?" + Theme.secondaryHighlightColor
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
                                    // the thread mark: Silica's menu lines
                                    Image {
                                        anchors.verticalCenter: parent.verticalCenter
                                        sourceSize.width: Theme.iconSizeSmall
                                        sourceSize.height: Theme.iconSizeSmall
                                        source: "image://theme/icon-m-menu?" + Theme.highlightColor
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
                                            //: %n replies not seen yet
                                            //% "%n new"
                                            if (threadNew > 0) t += " · <b><font color=\"" + Theme.highlightColor + "\">" + qsTrId("rustrx-la-thread-new", threadNew) + "</font></b>"
                                            // "sender: text", rendered by the model
                                            if (threadSummary.latest_sender) t += " · " + model.thread_html
                                            return t
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        // at the bubble's right edge
                        x: parent.width - width
                        width: Math.min(implicitWidth, innerMax)
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
                            //% "not sent"
                            if (model.send_state === "failed") t += " · " + qsTrId("rustrx-la-not-sent")
                            //% "edited"
                            if (model.edited) t += " · " + qsTrId("rustrx-la-edited")
                            if (model.shield && model.shield.indexOf("red") === 0)
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

    Component {
        id: contextMenu
        ContextMenu {
            // the reactions first: the five most used at a tap, the menu
            // closes after one; "more" opens the emoji picker for this message
            Row {
                visible: canAct && !failed
                anchors.horizontalCenter: parent.horizontalCenter
                Repeater {
                    model: app.favouriteEmoji
                    BackgroundItem {
                        width: Theme.itemSizeSmall
                        height: Theme.itemSizeSmall
                        onClicked: {
                            app.countEmoji(modelData)
                            rustrx.command({ cmd: "react", room_id: roomId, event_id: msg.eventId, key: modelData })
                            msg.closeMenu()
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
                        var t = (model.sender_name || model.sender) + ": " + (model.body || "")
                        msg.closeMenu()
                        msg.reactMore(id, t)
                    }
                }
            }
            // a failed message: send it again or let it go (a failed upload
            // is discarded and sent anew from the picker)
            MenuItem {
                visible: failed && !isMedia && !isFile
                //% "Send again"
                text: qsTrId("rustrx-me-send-again")
                onClicked: msg.resend(model.txn_id || "", model.body || "")
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
                visible: canAct && !failed
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
            MenuItem {
                //% "Copy text"
                text: qsTrId("rustrx-me-copy")
                visible: model.body && model.body.length > 0
                onClicked: {
                    Clipboard.text = model.body
                    //% "Text copied"
                    Notices.show(qsTrId("rustrx-no-text-copied"), Notice.Short)
                }
            }
            // a custom emoji seen here, into the user's own pack (the first three)
            Repeater {
                model: msg.emoteList().slice(0, Tunables.emoteOfferMax)
                MenuItem {
                    //: %1 a custom emoji's shortcode
                    //% "Add :%1: to my emoji"
                    text: qsTrId("rustrx-me-add-emote").arg(modelData.shortcode || "emoji")
                    onClicked: rustrx.command({ cmd: "add_user_emote", shortcode: modelData.shortcode || "emoji", url: modelData.url })
                }
            }
            MenuItem {
                //% "Copy link"
                text: qsTrId("rustrx-me-copy-link")
                visible: canAct && !failed && msg.eventId.length > 0
                onClicked: {
                    Clipboard.text = "https://matrix.to/#/" + encodeURIComponent(roomId) + "/" + encodeURIComponent(msg.eventId)
                    Notices.show(qsTrId("rustrx-no-link-copied"), Notice.Short)
                }
            }
            MenuItem {
                text: qsTrId("rustrx-me-save-downloads")
                visible: isMedia || isFile
                onClicked: saveMedia()
            }
            MenuItem {
                //% "View source"
                text: qsTrId("rustrx-me-view-source")
                visible: msg.eventId.length > 0
                onClicked: {
                    pageStack.push(Qt.resolvedUrl("../pages/SourcePage.qml"), { eventId: msg.eventId })
                    rustrx.command({ cmd: "event_source", room_id: roomId, event_id: msg.eventId })
                }
            }
            MenuItem {
                //% "Report"
                text: qsTrId("rustrx-me-report")
                visible: canAct && !own
                onClicked: {
                    var id = model.event_id, room = roomId, core = rustrx
                    //% "Reporting to the server admins"
                    msg.remorseAction(qsTrId("rustrx-re-reporting"), function () {
                        core.command({ cmd: "report", room_id: room, event_id: id })
                    })
                }
            }
            MenuItem {
                text: qsTrId("rustrx-me-remove")
                visible: own && msgType !== "redacted" && !failed
                onClicked: {
                    var id = model.event_id, room = roomId, core = rustrx
                    //% "Removing"
                    msg.remorseAction(qsTrId("rustrx-re-removing"), function () {
                        core.command({ cmd: "redact", room_id: room, event_id: id })
                    })
                }
            }
        }
    }
}
