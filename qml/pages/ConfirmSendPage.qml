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

// The last look before something goes out: where to (the room's picture
// and name, and with several accounts whose), what (the text, the files'
// names or the picture), then Send. One view for sharing from other apps
// and for Forward (the author's decision 4, 0.8 audit).
// A forward (eventId set) is sent here and waits for the core; the
// notice follows and the chat comes back (backTo). A share is sent here
// too, and its room opens.
ShortPage {
    id: page

    // where
    property int account
    property string roomId
    property string roomName
    property string avatarPath
    property real avatarVersion
    // what: a share's files and text, or the forwarded message's text, its
    // file's name and its picture
    property var files: []
    property string text
    property string fileName
    property string picture
    // a forward: the message and its room, and the page to come back to
    property string eventId
    property string fromRoom
    property Item backTo
    // a forward on its way, or why it failed
    property bool sending
    property string error

    readonly property string accountName: service.accountLabel(account)
    // a single shared picture is shown, more files by their names
    readonly property string sharedPicture: files.length === 1 && /\.(jpe?g|png|gif|webp|bmp)$/i.test(files[0]) ? files[0] : ""
    readonly property string shownPicture: picture.length ? picture : sharedPicture

    function names() {
        var out = []
        for (var i = 0; i < files.length; i++) out.push(String(files[i]).split("/").pop())
        if (fileName.length) out.push(fileName)
        return out.join("\n")
    }

    function send() {
        if (eventId.length) {
            page.error = ""
            page.sending = true
            rustrx.command({ cmd: "forward", room_id: page.fromRoom, event_id: page.eventId, to_room: page.roomId })
            return
        }
        rustrx.shareTo(page.account, page.roomId, page.files, page.text)
        // shared into the room it was opened from: back to that page, not a
        // second one of the same room above it
        var room = page.roomId, key = page.account
        var below = pageStack.find(function (p) { return p.objectName === "room" })
        if (below && below.roomId === room && key === rustrx.accountKey) pageStack.pop(below)
        else app.openIn(key, room)
    }

    Connections {
        target: rustrx
        onForwarded: {
            if (!page.sending || eventId !== page.eventId || toRoom !== page.roomId) return
            page.sending = false
            //: %1 the room or chat it went to
            //% "Forwarded to %1"
            Notices.show(qsTrId("rustrx-no-forwarded").arg(page.roomName), Notice.Short)
            if (page.backTo) pageStack.pop(page.backTo)
            else pageStack.pop()
        }
        onErrorOccurred: if (cmd === "forward" && page.sending) { page.sending = false; page.error = message }
    }

    title: roomName
    description: accountName

    body: [
        OwnAvatar {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.iconSizeExtraLarge
            height: width
            letterSize: Theme.fontSizeHuge
            path: page.avatarPath
            version: page.avatarVersion
            name: page.roomName
            userId: page.roomId
        },
        Image {
            visible: page.shownPicture.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - 2 * Theme.horizontalPageMargin, Theme.itemSizeHuge * 2)
            height: visible ? Math.min(width, implicitHeight > 0 ? width * implicitHeight / Math.max(1, implicitWidth) : width) : 0
            fillMode: Image.PreserveAspectFit
            sourceSize.width: width
            asynchronous: true
            source: visible ? "file://" + page.shownPicture : ""
        },
        Note {
            kind: "status"
            horizontalAlignment: Text.AlignHCenter
            // the picture shows itself; other files by their names
            text: page.shownPicture.length && !page.fileName.length ? "" : page.names()
        },
        Note {
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 8
            elide: Text.ElideRight
            text: page.text
        },
        Note {
            kind: "error"
            horizontalAlignment: Text.AlignHCenter
            text: page.error
        }
    ]

    buttons: [
        PageButton {
            text: qsTrId("rustrx-bt-send")
            button.enabled: !page.sending
            onClicked: page.send()
        },
        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            size: BusyIndicatorSize.Medium
            running: page.sending
            visible: running
        }
    ]
}
