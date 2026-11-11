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

// The app icon faint in the back; the connection state and the unread
// count in front.
CoverBackground {
    id: cover

    Image {
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: -height / 4
        }
        width: parent.width * 0.9
        height: width
        source: "/usr/share/icons/hicolor/172x172/apps/harbour-rustrx.png"
        opacity: 0.15
        asynchronous: true
    }

    Column {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Theme.paddingLarge
        }
        spacing: Theme.paddingMedium

        // count with its label beside it, as Jolla's own covers do
        Row {
            width: parent.width
            spacing: Theme.paddingMedium
            visible: rustrx.loggedIn
            Label {
                id: countLabel
                text: rustrx.unreadTotal >= 1000 ? Math.floor(rustrx.unreadTotal / 1000) + "K" : rustrx.unreadTotal
                font.pixelSize: Theme.fontSizeHuge
                color: Theme.primaryColor
            }
            Label {
                anchors.verticalCenter: countLabel.verticalCenter
                width: parent.width - countLabel.width - parent.spacing
                wrapMode: Text.Wrap
                //: beside the big count on the cover
                //% "unread messages"
                text: qsTrId("rustrx-la-cover-unread-messages")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
            }
        }

        Row {
            width: parent.width
            spacing: Theme.paddingMedium
            visible: rustrx.loggedIn && rustrx.unreadChats > 0
            Label {
                id: inLabel
                anchors.verticalCenter: chatsCount.verticalCenter
                //% "in"
                text: qsTrId("rustrx-la-cover-in")
                font.pixelSize: Theme.fontSizeSmall
            }
            Label {
                id: chatsCount
                text: rustrx.unreadChats
                font.pixelSize: Theme.fontSizeHuge
            }
            Label {
                anchors.verticalCenter: chatsCount.verticalCenter
                //% "chats"
                text: qsTrId("rustrx-la-cover-chats-word")
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeLarge
            text: {
                //% "Not logged in"
                if (!rustrx.loggedIn) return qsTrId("rustrx-la-cover-not-logged-in")
                switch (rustrx.syncState) {
                //% "Connected"
                case "running": return qsTrId("rustrx-la-cover-connected")
                //% "Connecting"
                case "idle": return qsTrId("rustrx-la-cover-connecting")
                //% "Offline"
                default: return qsTrId("rustrx-la-cover-offline")
                }
            }
        }
    }

    // left: find a room or a person; right: the most recent room with
    // unread messages (only while there is one)
    CoverActionList {
        enabled: rustrx.loggedIn
        CoverAction {
            iconSource: "image://theme/icon-cover-search"
            onTriggered: {
                app.activate()
                pageStack.push(Qt.resolvedUrl("../pages/SearchPage.qml"), {}, PageStackAction.Immediate)
            }
        }
        CoverAction {
            iconSource: "image://theme/icon-cover-message"
            onTriggered: {
                app.activate()
                var id = rustrx.firstUnreadRoom()
                if (id.length) app.showRoom(id)
            }
        }
    }
}
