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
import Sailfish.Silica 1.0

// The overview: the app icon faint in the back; the connection state and
// the unread count in front. In a chat (dodoradio's design): the room's
// picture in the back, its name on top, its latest unread message or the
// unread count in the middle, and at the bottom the app's name, or the
// connection state when it is not connected.
CoverBackground {
    id: cover

    readonly property bool inChat: rustrx.loggedIn && rustrx.openRoomId.length > 0
    property var chat: ({})
    function refreshChat() { chat = inChat ? (rustrx.roomInfo(rustrx.openRoomId) || {}) : {} }
    onInChatChanged: refreshChat()
    Connections { target: rustrx; onOpenRoomChanged: cover.refreshChat() }
    Connections { target: rustrx.rooms; onDataChanged: if (cover.inChat) cover.refreshChat() }
    Connections { target: rustrx.people; onDataChanged: if (cover.inChat) cover.refreshChat() }
    // the same number as the room's badge in the list
    readonly property int chatUnread: chat.notifications > 0 ? chat.notifications : (chat.unread || 0)

    Image {
        anchors.fill: parent
        visible: cover.inChat && !!cover.chat.avatar_path
        source: visible ? "file://" + cover.chat.avatar_path + "?v=" + (cover.chat.avatar_v || 0) : ""
        sourceSize.width: width
        fillMode: Image.PreserveAspectCrop
        opacity: 0.3
        asynchronous: true
    }

    Image {
        visible: !cover.inChat || !cover.chat.avatar_path
        anchors {
            horizontalCenter: parent.horizontalCenter
            // the icon's bottom as far from the tile's bottom as the cover
            // action icons are: lipstick centres them (iconSizeSmall) in a
            // strip itemSizeSmall tall at the bottom. The size alone moves
            // the top; the bottom stays here.
            bottom: parent.bottom
            bottomMargin: (Theme.itemSizeSmall - Theme.iconSizeSmall) / 2
        }
        // 1.368 times the tile's width, cropped at its sides: measured on the
        // Jolla Phone 2026 so the first (red) fibre lies behind "in N chats"
        width: parent.width * 1.368
        height: width
        // the 512 px icon: the installed sizes end at 172 px and would blur
        source: "rustrx-512.png"
        sourceSize.width: width
        opacity: 0.15
        asynchronous: true
    }

    Column {
        visible: !cover.inChat
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
                text: rustrx.unreadTotal >= Tunables.coverKiloFrom ? Math.floor(rustrx.unreadTotal / 1000) + "K" : rustrx.unreadTotal
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
                font.pixelSize: Theme.fontSizeSmall * Tunables.metaTextScale
                color: Theme.primaryColor
            }
        }

        Row {
            width: parent.width
            spacing: Theme.paddingMedium
            visible: rustrx.loggedIn && rustrx.unreadChats > 0
            Label {
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
    }

    Item {
        visible: cover.inChat
        anchors {
            fill: parent
            margins: Theme.paddingLarge
            // above the cover actions' strip
            bottomMargin: Theme.itemSizeSmall
        }

        Label {
            id: chatName
            width: parent.width
            text: cover.chat.name || ""
            font.pixelSize: Theme.fontSizeMedium
            color: Theme.highlightColor
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        // the latest unread message, or only the count when notifications
        // leave message texts out (Settings > Privacy)
        Column {
            anchors { top: chatName.bottom; topMargin: Theme.paddingMedium; bottom: parent.bottom; bottomMargin: chatState.height + Theme.paddingSmall }
            width: parent.width
            spacing: Theme.paddingSmall
            visible: cover.chatUnread > 0
            readonly property bool withText: service.notifyText && !!cover.chat.preview

            Label {
                id: chatSender
                width: parent.width
                // in a direct chat the room's name already says who wrote
                visible: parent.withText && !cover.chat.is_dm && !!cover.chat.preview_sender
                text: cover.chat.preview_sender || ""
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                truncationMode: TruncationMode.Fade
            }
            Label {
                visible: parent.withText
                width: parent.width
                height: parent.height - (chatSender.visible ? chatSender.height + parent.spacing : 0)
                text: (cover.chat.preview || "").replace(/\s+/g, " ")
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
                elide: Text.ElideRight
            }
            Row {
                visible: !parent.withText
                width: parent.width
                spacing: Theme.paddingMedium
                Label {
                    id: chatCount
                    text: cover.chatUnread >= Tunables.coverKiloFrom ? Math.floor(cover.chatUnread / 1000) + "K" : cover.chatUnread
                    font.pixelSize: Theme.fontSizeHuge
                }
                Label {
                    anchors.verticalCenter: chatCount.verticalCenter
                    width: parent.width - chatCount.width - parent.spacing
                    wrapMode: Text.Wrap
                    text: qsTrId("rustrx-la-cover-unread-messages")
                    font.pixelSize: Theme.fontSizeSmall * Tunables.metaTextScale
                }
            }
        }

    }

    // the same line at the bottom in both views: "Rustrx" while connected,
    // which is the usual case; otherwise what is wrong
    Label {
        id: chatState
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: Theme.paddingLarge
            // above the cover actions' strip
            bottomMargin: Theme.itemSizeSmall
        }
        truncationMode: TruncationMode.Fade
        font.pixelSize: Theme.fontSizeExtraSmall
        readonly property bool fine: rustrx.loggedIn && rustrx.syncState === "running"
        color: fine ? Theme.secondaryColor : Theme.highlightColor
        //% "Not logged in"
        text: !rustrx.loggedIn ? qsTrId("rustrx-la-cover-not-logged-in")
            : fine ? "Rustrx"
            //% "Connecting"
            : rustrx.syncState === "idle" ? qsTrId("rustrx-la-cover-connecting")
            //% "Offline"
            : qsTrId("rustrx-la-cover-offline")
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
