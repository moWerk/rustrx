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

// One timeline row: a date divider, a read marker, or an event. Own
// messages sit at the right in the highlight colour, others at the left.
ListItem {
    id: item
    width: ListView.view.width
    contentHeight: loader.height + Theme.paddingSmall
    menu: isEvent ? contextMenu : null
    enabled: isEvent

    property string roomId
    signal reply(string eventId, string text)

    readonly property bool isEvent: model.kind === "event"
    readonly property bool own: model.is_own === true
    readonly property string msgType: model.type || "text"
    readonly property bool isMedia: msgType === "image" || msgType === "video" || msgType === "sticker"
    readonly property bool isFile: msgType === "file" || msgType === "audio"
    readonly property color mainColor: own ? (highlighted ? Theme.highlightColor : Theme.highlightColor)
                                           : (highlighted ? Theme.highlightColor : Theme.primaryColor)
    readonly property color dimColor: own ? Theme.secondaryHighlightColor : Theme.secondaryColor
    // captured here: inside the Repeater, `model` would name the Repeater's own property
    readonly property var reactionList: model.reactions ? model.reactions : []

    property bool exporting

    // other apps cannot read this app's data folder: a copy goes to Downloads
    function openMedia() {
        exporting = true
        rustrx.exportMedia(roomId, model.event_id)
    }

    onClicked: if (isMedia || isFile) openMedia()

    Connections {
        target: rustrx
        onMediaExported: {
            if (!exporting || eventId !== model.event_id) return
            exporting = false
            Qt.openUrlExternally("file://" + path)
        }
    }

    // a media row asks for its picture once it is on screen: the thumbnail
    // where there is one, else the image itself
    Component.onCompleted: {
        if (!isMedia) return
        if (model.thumb_path && model.thumb_path.length || model.media_path && model.media_path.length) return
        if (model.has_thumb) rustrx.getMedia(roomId, model.event_id, true)
        else if (msgType !== "video") rustrx.getMedia(roomId, model.event_id, false)
    }

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
                text: rustrx.formatDate(model.ts)
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
            spacing: Theme.paddingSmall / 2

            readonly property real sideMargin: Theme.horizontalPageMargin
            readonly property real bubbleWidth: width - sideMargin - Theme.itemSizeSmall

            Label {
                visible: !own && msgType !== "membership" && msgType !== "state"
                x: sideMargin
                width: bubbleWidth
                text: model.sender_name || model.sender || ""
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
            }

            // state changes and membership as one quiet line
            Label {
                visible: msgType === "membership" || msgType === "state"
                x: sideMargin
                width: parent.width - 2 * sideMargin
                text: model.body || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }

            Label {
                visible: msgType !== "membership" && msgType !== "state" && !isMedia
                x: own ? parent.width - width - sideMargin : sideMargin
                width: Math.min(implicitWidth, bubbleWidth)
                wrapMode: Text.Wrap
                textFormat: model.html && model.html.length ? Text.StyledText : Text.PlainText
                color: msgType === "utd" || msgType === "redacted" ? dimColor : mainColor
                font.italic: msgType === "emote" || msgType === "utd" || msgType === "redacted"
                linkColor: Theme.secondaryHighlightColor
                onLinkActivated: Qt.openUrlExternally(link)
                text: {
                    //% "Waiting for the key to decrypt this message"
                    if (msgType === "utd") return qsTrId("rustrx-la-utd")
                    //% "Message removed"
                    if (msgType === "redacted") return qsTrId("rustrx-la-redacted")
                    if (msgType === "file" || msgType === "audio") {
                        var size = model.size ? " (" + Format.formatFileSize(model.size) + ")" : ""
                        return (model.filename || model.body || "") + size
                    }
                    if (msgType === "location") return (model.body || "") + "\n" + (model.geo_uri || "")
                    //% "Verification request"
                    if (msgType === "verification") return qsTrId("rustrx-la-verification-request")
                    if (msgType === "emote") return (model.sender_name || model.sender) + " " + (model.body || "")
                    if (model.html && model.html.length) return model.html
                    return model.body || ""
                }
            }

            // image, video thumbnail or sticker
            Item {
                visible: isMedia
                x: own ? parent.width - width - sideMargin : sideMargin
                width: Math.min(bubbleWidth, Screen.width * 0.6)
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

            Flow {
                visible: reactionList.length > 0
                x: own ? parent.width - width - sideMargin : sideMargin
                width: bubbleWidth
                spacing: Theme.paddingMedium
                Repeater {
                    model: reactionList
                    Label {
                        text: modelData.key + " " + modelData.count
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: modelData.me ? Theme.highlightColor : Theme.secondaryColor
                    }
                }
            }

            Label {
                visible: msgType !== "membership" && msgType !== "state"
                x: own ? parent.width - width - sideMargin : sideMargin
                font.pixelSize: Theme.fontSizeTiny
                color: dimColor
                text: {
                    var t = rustrx.formatTime(model.ts)
                    //% "sending"
                    if (model.send_state === "sending") t += " · " + qsTrId("rustrx-la-sending")
                    //% "not sent"
                    if (model.send_state === "failed") t += " · " + qsTrId("rustrx-la-not-sent")
                    //% "edited"
                    if (model.edited) t += " · " + qsTrId("rustrx-la-edited")
                    if (model.reply_to && model.reply_to.body)
                        //: %1 the quoted text
                        //% "in reply to: %1"
                        t += " · " + qsTrId("rustrx-la-in-reply-to").arg(model.reply_to.body.substr(0, 60))
                    if (app.showReadBy && model.read_by && model.read_by.length)
                        //: %1 the names of the people whose read receipt sits on this message
                        //% "read by %1"
                        t += " · " + qsTrId("rustrx-la-read-by").arg(model.read_by.join(", "))
                    return t
                }
            }
        }
    }

    Component {
        id: contextMenu
        ContextMenu {
            MenuItem {
                //% "Reply"
                text: qsTrId("rustrx-me-reply")
                visible: msgType !== "membership" && msgType !== "state" && msgType !== "redacted"
                onClicked: item.reply(model.event_id, (model.sender_name || model.sender) + ": " + (model.body || ""))
            }
            MenuItem {
                //% "Thumbs up"
                text: qsTrId("rustrx-me-react-thumbs-up")
                visible: msgType !== "membership" && msgType !== "state" && msgType !== "redacted"
                onClicked: rustrx.command({ cmd: "react", room_id: roomId, event_id: model.event_id, key: "👍" })
            }
            MenuItem {
                //% "Copy text"
                text: qsTrId("rustrx-me-copy")
                visible: model.body && model.body.length > 0
                onClicked: Clipboard.text = model.body
            }
            MenuItem {
                //% "Save"
                text: qsTrId("rustrx-me-save")
                visible: isMedia || isFile
                onClicked: openMedia()
            }
            MenuItem {
                //% "Remove"
                text: qsTrId("rustrx-me-remove")
                visible: own && msgType !== "redacted"
                onClicked: {
                    var id = model.event_id
                    //% "Removing"
                    item.remorseAction(qsTrId("rustrx-re-removing"), function () {
                        rustrx.command({ cmd: "redact", room_id: roomId, event_id: id })
                    })
                }
            }
        }
    }
}
