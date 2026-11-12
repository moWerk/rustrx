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

    readonly property bool isEvent: model.kind === "event"
    readonly property bool own: model.is_own === true
    readonly property string msgType: model.type || "text"
    readonly property bool isMedia: msgType === "image" || msgType === "video" || msgType === "sticker"
    readonly property bool isFile: msgType === "file" || msgType === "audio"
    readonly property color mainColor: own || highlighted ? Theme.highlightColor : Theme.primaryColor
    // a message that mentions us (or a keyword rule) gets the highlight colour
    readonly property bool mention: model.highlight === true && !own
    readonly property color dimColor: own ? Theme.secondaryHighlightColor : Theme.secondaryColor
    // captured here: inside the Repeater, `model` would name the Repeater's own property
    readonly property var reactionList: model.reactions ? model.reactions : []
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
    readonly property var quickReactions: ["👍", "❤️", "😂", "😮", "😢", "🎉"]

    // plain bodies: escape, link the addresses, keep the line breaks
    function linkify(t) {
        return t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                .replace(/(https?:\/\/[^\s<]+|matrix:[^\s<]+)/g, "<a href=\"$1\">$1</a>")
                .replace(/\n/g, "<br>")
    }

    property bool exporting

    // other apps cannot read this app's data folder: a copy goes to Downloads
    function openMedia() {
        exporting = true
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

    onClicked: {
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
            Qt.openUrlExternally("file://" + path)
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
                    radius: Theme.paddingLarge
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
                    Row {
                        visible: !!(model.reply_to && model.reply_to.body)
                        width: Math.min(quote.implicitWidth + spacing + 2, innerMax)
                        spacing: Theme.paddingSmall

                        Rectangle { width: 2; height: quote.height; color: Theme.secondaryHighlightColor }

                        Label {
                            id: quote
                            width: parent.width - 2 - parent.spacing
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                            textFormat: Text.StyledText
                            text: model.reply_to
                                  ? "<font color=\"" + Theme.secondaryHighlightColor + "\">"
                                    + (model.reply_to.sender_name || model.reply_to.sender || "") + "</font><br>"
                                    + (model.reply_to.body || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/\n/g, "<br>")
                                  : ""
                        }
                    }

                    // a thin bar marks a mention; plain bodies get their addresses linked
                    Row {
                        visible: !isMedia
                        width: Math.min(body.implicitWidth + (mention ? spacing + 2 : 0), innerMax)
                        spacing: Theme.paddingSmall
                        Rectangle { visible: mention; width: 2; height: body.height; color: Theme.highlightColor }
                        Label {
                            id: body
                            width: parent.width - (mention ? parent.spacing + 2 : 0)
                            wrapMode: Text.Wrap
                            textFormat: msgType === "text" || msgType === "notice" || (model.html && model.html.length) ? Text.StyledText : Text.PlainText
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
                                if (msgType === "emote") return (model.sender_name || model.sender) + " " + (model.body || "")
                                if (model.html && model.html.length) return model.html
                                if (msgType === "text" || msgType === "notice") return linkify(model.body || "")
                                return model.body || ""
                            }
                        }
                    }

                    // image, video thumbnail or sticker
                    Item {
                        visible: isMedia
                        width: Math.min(innerMax, Screen.width * 0.6)
                        height: visible ? Math.min(width, Math.max(Theme.itemSizeLarge, width * (model.h && model.w ? model.h / model.w : 0.75))) : 0

                        Image {
                            anchors.fill: parent
                            asynchronous: true
                            autoTransform: true
                            fillMode: Image.PreserveAspectFit
                            source: model.thumb_path && model.thumb_path.length ? "file://" + model.thumb_path
                                  : model.media_path && model.media_path.length ? "file://" + model.media_path : ""
                            sourceSize.width: parent.width
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: !(model.thumb_path && model.thumb_path.length) && !(model.media_path && model.media_path.length)
                            text: model.body || ""
                            color: dimColor
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }

                    // a poll: one line per answer with its votes; a tap votes
                    // (or changes the vote) while it is open
                    Column {
                        visible: msgType === "poll"
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

                    // the text sent with a picture, a video or a file
                    Label {
                        visible: !!(model.caption && model.caption.length)
                        width: Math.min(implicitWidth, innerMax)
                        wrapMode: Text.Wrap
                        textFormat: Text.StyledText
                        text: visible ? linkify(model.caption) : ""
                        color: mainColor
                        linkColor: Theme.secondaryHighlightColor
                        onLinkActivated: app.openLink(link)
                    }

                    // an upload on its way: how much of the file is at the server
                    Rectangle {
                        visible: uploading
                        width: Math.min(innerMax, Screen.width * 0.6)
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

                    Flow {
                        visible: reactionList.length > 0
                        width: Math.min(innerMax, reactionMeasure.width)
                        spacing: Theme.paddingMedium
                        Repeater {
                            model: reactionList
                            // a tap toggles our own reaction with this key
                            Label {
                                text: modelData.key + " " + modelData.count
                                font.pixelSize: Theme.fontSizeExtraSmall
                                color: modelData.me ? Theme.highlightColor : Theme.secondaryColor
                                MouseArea {
                                    anchors { fill: parent; margins: -Theme.paddingSmall }
                                    onClicked: rustrx.command({ cmd: "react", room_id: roomId, event_id: msg.eventId, key: modelData.key })
                                }
                            }
                        }
                    }
                    // the reactions' width in one line, so the bubble is no wider than they need
                    Row {
                        id: reactionMeasure
                        visible: false
                        spacing: Theme.paddingMedium
                        Repeater {
                            model: reactionList
                            Label { text: modelData.key + " " + modelData.count; font.pixelSize: Theme.fontSizeExtraSmall }
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
            // the reactions first: six at a tap, the menu closes after one
            Row {
                visible: canAct && !failed
                anchors.horizontalCenter: parent.horizontalCenter
                Repeater {
                    model: msg.quickReactions
                    BackgroundItem {
                        width: Theme.itemSizeSmall
                        height: Theme.itemSizeSmall
                        onClicked: {
                            rustrx.command({ cmd: "react", room_id: roomId, event_id: msg.eventId, key: modelData })
                            msg.closeMenu()
                        }
                        Label { anchors.centerIn: parent; text: modelData; font.pixelSize: Theme.fontSizeLarge }
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
                //% "Edit"
                text: qsTrId("rustrx-me-edit")
                visible: own && model.editable === true && (msgType === "text" || msgType === "emote" || msgType === "notice")
                onClicked: msg.edit(model.event_id, model.body || "")
            }
            MenuItem {
                //% "Copy text"
                text: qsTrId("rustrx-me-copy")
                visible: model.body && model.body.length > 0
                onClicked: Clipboard.text = model.body
            }
            MenuItem {
                //% "Copy link"
                text: qsTrId("rustrx-me-copy-link")
                visible: canAct && !failed && msg.eventId.length > 0
                onClicked: Clipboard.text = "https://matrix.to/#/" + encodeURIComponent(roomId) + "/" + encodeURIComponent(msg.eventId)
            }
            MenuItem {
                //% "Save"
                text: qsTrId("rustrx-me-save")
                visible: isMedia || isFile
                onClicked: openMedia()
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
                //% "Remove"
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
