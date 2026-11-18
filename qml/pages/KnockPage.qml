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

// Requests to join the rooms and spaces we may invite to (their join rule
// is "knock"). A tap or the menu answers: accept invites them, decline
// turns the request down, decline and ban keeps them out. Looking at the
// list marks the requests as seen (no notification for them again).
Page {
    id: page

    // declining, with or without a ban, and an optional reason they see
    function decline(request, ban) {
        var room = request.room_id, user = request.user_id, core = rustrx
        var d = pageStack.push(Qt.resolvedUrl("../components/ReasonDialog.qml"), {
            title: ban ? qsTrId("rustrx-me-knock-ban") : qsTrId("rustrx-me-knock-decline"),
            acceptText: ban ? qsTrId("rustrx-bt-ban-short") : qsTrId("rustrx-me-knock-decline")
        })
        d.accepted.connect(function () {
            var c = { cmd: "knock_answer", room_id: room, user_id: user, accept: false }
            if (ban) c.ban = true
            if (d.reason.trim().length) c.reason = d.reason.trim()
            core.command(c)
        })
    }
    objectName: "knocks"

    function markSeen() {
        var byRoom = {}
        var all = rustrx.knockRequests
        for (var i = 0; i < all.length; i++) {
            if (all[i].seen) continue
            if (!byRoom[all[i].room_id]) byRoom[all[i].room_id] = []
            byRoom[all[i].room_id].push(all[i].user_id)
        }
        for (var room in byRoom)
            rustrx.command({ cmd: "knock_seen", room_id: room, user_ids: byRoom[room] })
    }
    onStatusChanged: if (status === PageStatus.Active) markSeen()

    SilicaListView {
        id: list
        anchors.fill: parent
        model: rustrx.knockRequests

        header: PageHeader {
            //% "Requests to join"
            title: qsTrId("rustrx-he-knocks")
        }

        delegate: ListItem {
            id: item
            readonly property var request: modelData
            contentHeight: column.height + 2 * Theme.paddingMedium
            menu: ContextMenu {
                MenuItem {
                    //% "Accept"
                    text: qsTrId("rustrx-me-knock-accept")
                    onClicked: rustrx.command({ cmd: "knock_answer", room_id: item.request.room_id,
                                                user_id: item.request.user_id, accept: true })
                }
                MenuItem {
                    //% "Decline"
                    text: qsTrId("rustrx-me-knock-decline")
                    onClicked: page.decline(item.request, false)
                }
                MenuItem {
                    //% "Decline and ban"
                    text: qsTrId("rustrx-me-knock-ban")
                    onClicked: page.decline(item.request, true)
                }
            }
            onClicked: openMenu()

            Column {
                id: column
                x: Theme.horizontalPageMargin
                y: Theme.paddingMedium
                width: parent.width - 2 * x

                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                    text: item.request.name
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: item.request.user_id
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: item.request.is_space
                          //: %1 a space (a group of rooms)
                          //% "asks to join the space %1"
                          ? qsTrId("rustrx-la-knock-space").arg(item.request.room_name)
                          //: %1 a room
                          //% "asks to join %1"
                          : qsTrId("rustrx-la-knock-room").arg(item.request.room_name)
                }
                Label {
                    visible: !!item.request.reason
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    font.italic: true
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                    text: item.request.reason ? "“" + item.request.reason + "”" : ""
                }
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            //% "No open requests"
            text: qsTrId("rustrx-la-knock-none")
        }

        VerticalScrollDecorator { }
    }
}
