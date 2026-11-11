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

// Something shared from another app: pick the room, it is sent there and
// the room opens. Rooms and direct chats in one list, by activity.
Page {
    id: page

    property var files: []
    property string text

    function sendTo(roomId, name) {
        for (var i = 0; i < files.length; i++)
            rustrx.sendFile(roomId, files[i], files.length === 1 ? text : "")
        if (text.length && files.length !== 1)
            rustrx.sendText(roomId, text, "")
        pageStack.replace(Qt.resolvedUrl("RoomPage.qml"), { roomId: roomId, roomName: name })
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: rustrx.rooms

        header: Column {
            width: list.width
            PageHeader {
                //% "Send to"
                title: qsTrId("rustrx-he-send-to")
                description: files.length > 1
                             //: %n the number of files
                             //% "%n files"
                             ? qsTrId("rustrx-la-n-files", files.length)
                             : files.length === 1 ? files[0].split("/").pop() : text
            }
        }

        delegate: BackgroundItem {
            width: list.width
            height: Theme.itemSizeMedium
            visible: model.state !== "invited"
            onClicked: page.sendTo(model.room_id, model.name)
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                truncationMode: TruncationMode.Fade
                text: model.name
                color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
        }

        VerticalScrollDecorator { }
    }
}
