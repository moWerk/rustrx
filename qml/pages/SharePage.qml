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

// Something shared from another app: pick the room, check it, send; the
// room opens. It sent at the first tap, so a mis-tap in a long list sent
// a photo to the wrong room (0.8 audit, the author's decision 4). With
// several accounts every account's rooms are offered, each with its name.
RoomPickerPage {
    id: page

    property var files: []
    property string text

    allAccounts: true
    //% "Send to"
    title: qsTrId("rustrx-he-send-to")
    description: files.length > 1
                 //: %n the number of files
                 //% "%n files"
                 ? qsTrId("rustrx-la-n-files", files.length)
                 : files.length === 1 ? files[0].split("/").pop() : text

    onPicked: pageStack.push(Qt.resolvedUrl("ConfirmSendPage.qml"), {
        account: account, roomId: roomId, roomName: name, avatarPath: avatarPath, avatarVersion: avatarVersion,
        files: page.files, text: page.text
    })
}
