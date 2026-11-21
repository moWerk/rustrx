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
import "../components"

// A message's less used actions, behind "More…" in its menu (the menu
// keeps four, as Sailfish apps do). It works from a copy of the message
// (MessageDelegate.moreInfo()), not the row: new messages can scroll the
// row away while this page is open. Copying comes back to the chat;
// the source and the text selection take this page's place; reporting and
// removing count down here first.
Page {
    id: page

    property var info: ({})

    function act(fn) { fn(); pageStack.pop() }

    RemorsePopup { id: remorse }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: page.info.sender || ""
                // the message's first line
                description: String(page.info.body || "").split("\n")[0]
            }

            ActionLine {
                visible: page.info.copyText === true
                text: qsTrId("rustrx-me-copy")
                onClicked: page.act(function () {
                    Clipboard.text = page.info.body
                    Notices.show(qsTrId("rustrx-no-text-copied"), Notice.Short)
                })
            }
            ActionLine {
                visible: page.info.selectText === true
                //% "Select text"
                text: qsTrId("rustrx-me-select-text")
                onClicked: pageStack.replace(Qt.resolvedUrl("TranscriptPage.qml"),
                                             { roomName: page.info.roomName || "", startId: page.info.rowId })
            }
            ActionLine {
                visible: page.info.copyLink === true
                //% "Copy link"
                text: qsTrId("rustrx-me-copy-link")
                onClicked: page.act(function () {
                    Clipboard.text = "https://matrix.to/#/" + encodeURIComponent(page.info.roomId) + "/" + encodeURIComponent(page.info.eventId)
                    Notices.show(qsTrId("rustrx-no-link-copied"), Notice.Short)
                })
            }
            // into another room or chat of this account: the picker, then a
            // last look, then the notice (the author's ask since 0.4.7)
            ActionLine {
                visible: page.info.copyLink === true && page.info.forward !== false
                //% "Forward"
                text: qsTrId("rustrx-me-forward")
                onClicked: {
                    // taken before this page goes: the handler runs after it
                    var info = page.info, back = pageStack.previousPage(page)
                    var stack = pageStack, confirmUrl = Qt.resolvedUrl("ConfirmSendPage.qml")
                    var picker = stack.replace(Qt.resolvedUrl("RoomPickerPage.qml"), {
                        //% "Forward to"
                        title: qsTrId("rustrx-he-forward-to"),
                        description: String(info.body || "").split("\n")[0]
                    })
                    picker.picked.connect(function (account, roomId, name, avatarPath, avatarVersion) {
                        var media = info.msgType === "image" || info.msgType === "video" || info.msgType === "sticker"
                                    || info.msgType === "file" || info.msgType === "audio"
                        stack.push(confirmUrl, {
                            account: account, roomId: roomId, roomName: name,
                            avatarPath: avatarPath, avatarVersion: avatarVersion,
                            // a file's or picture's body is its name or caption
                            text: media ? "" : String(info.body || ""),
                            fileName: media && !(info.thumb && (info.msgType === "image" || info.msgType === "sticker"))
                                      ? String(info.filename || info.body || "") : "",
                            picture: String(info.thumb || ""),
                            eventId: info.eventId, fromRoom: info.roomId, backTo: back
                        })
                    })
                }
            }
            // a custom emoji seen in the message, into the user's own pack
            Repeater {
                model: page.info.emotes || []
                ActionLine {
                    //: %1 a custom emoji's shortcode
                    //% "Add :%1: to my emoji"
                    text: qsTrId("rustrx-me-add-emote").arg(modelData.shortcode || "emoji")
                    onClicked: {
                        var e = modelData
                        page.act(function () {
                            rustrx.command({ cmd: "add_user_emote", shortcode: e.shortcode || "emoji", mxc: e.mxc })
                        })
                    }
                }
            }
            ActionLine {
                visible: (page.info.eventId || "").length > 0
                //% "View source"
                text: qsTrId("rustrx-me-view-source")
                onClicked: {
                    rustrx.command({ cmd: "event_source", room_id: page.info.roomId, event_id: page.info.eventId })
                    pageStack.replace(Qt.resolvedUrl("SourcePage.qml"), { eventId: page.info.eventId })
                }
            }
            // pinned above the chat (for those the room lets)
            ActionLine {
                visible: page.info.pin === true
                text: page.info.pinned
                      //% "Unpin"
                      ? qsTrId("rustrx-me-unpin")
                      //% "Pin"
                      : qsTrId("rustrx-me-pin")
                onClicked: {
                    var c = { cmd: "pin", room_id: page.info.roomId, event_id: page.info.eventId, pinned: !page.info.pinned }
                    page.act(function () { rustrx.command(c) })
                }
            }
            // with an optional reason: the dialog takes this page's place and
            // comes back to the chat
            ActionLine {
                visible: page.info.report === true
                //% "Report"
                text: qsTrId("rustrx-me-report")
                onClicked: {
                    var room = page.info.roomId, id = page.info.eventId, core = rustrx
                    var d = pageStack.replace(Qt.resolvedUrl("../components/ReasonDialog.qml"), {
                        //% "Report this message"
                        title: qsTrId("rustrx-he-report-message"),
                        acceptText: qsTrId("rustrx-bt-report"),
                        text: qsTrId("rustrx-la-report-goes-to-server")
                    })
                    d.accepted.connect(function () {
                        var c = { cmd: "report", room_id: room, event_id: id }
                        if (d.reason.trim().length) c.reason = d.reason.trim()
                        core.command(c)
                    })
                }
            }
            ActionLine {
                visible: page.info.remove === true && !page.info.removeOther
                text: qsTrId("rustrx-me-remove")
                onClicked: {
                    var room = page.info.roomId, id = page.info.eventId, core = rustrx
                    //% "Removing"
                    remorse.execute(qsTrId("rustrx-re-removing"), function () {
                        core.command({ cmd: "redact", room_id: room, event_id: id })
                        pageStack.pop()
                    })
                }
            }
            // a moderator removing someone else's message: they see the reason
            ActionLine {
                visible: page.info.remove === true && page.info.removeOther === true
                text: qsTrId("rustrx-me-remove")
                onClicked: {
                    var room = page.info.roomId, id = page.info.eventId, core = rustrx
                    var d = pageStack.replace(Qt.resolvedUrl("../components/ReasonDialog.qml"), {
                        //% "Remove this message"
                        title: qsTrId("rustrx-he-remove-message"),
                        acceptText: qsTrId("rustrx-bt-remove"),
                        //% "It is removed for everyone; the room sees that a moderator removed it."
                        text: qsTrId("rustrx-la-remove-other-note")
                    })
                    d.accepted.connect(function () {
                        var c = { cmd: "redact", room_id: room, event_id: id }
                        if (d.reason.trim().length) c.reason = d.reason.trim()
                        core.command(c)
                    })
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
