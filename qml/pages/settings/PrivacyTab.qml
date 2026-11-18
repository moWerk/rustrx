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

// Settings, Privacy: what others learn from Rustrx (receipts, typing, the
// text on the lock screen) and whom one has blocked. One subject, no headers.
SilicaFlickable {
    id: tab

    property Item settings

    contentHeight: column.height

    Column {
        id: column
        width: parent.width
        spacing: Theme.paddingMedium

        // the tab bar is drawn over a tab with a pulley menu; content starts below it
        Item { width: 1; height: (tab.pullDownMenu ? settings.tabBarHeight : 0) + Theme.paddingMedium }

        TextSwitch {
            //% "Send read receipts"
            text: qsTrId("rustrx-la-send-receipts")
            //% "Others see how far you have read. When off, only your own devices know."
            description: qsTrId("rustrx-la-send-receipts-hint")
            checked: app.sendReceipts
            automaticCheck: false
            onClicked: app.sendReceipts = !app.sendReceipts
        }

        TextSwitch {
            //% "Mark messages as read only when they have been shown"
            text: qsTrId("rustrx-la-read-when-shown")
            //% "A room counts as read once you scrolled to its newest message. When off, opening the room is enough; the unread line still waits until you saw the messages."
            description: qsTrId("rustrx-la-read-when-shown-hint")
            checked: app.readWhenShown
            automaticCheck: false
            onClicked: app.readWhenShown = !app.readWhenShown
        }

        // sends nothing, but completes the receipts above
        TextSwitch {
            //% "Show who has read a message"
            text: qsTrId("rustrx-la-show-read-by")
            //% "Names appear under the last message each person has read."
            description: qsTrId("rustrx-la-show-read-by-hint")
            checked: app.showReadBy
            automaticCheck: false
            onClicked: app.showReadBy = !app.showReadBy
        }

        TextSwitch {
            //% "Show others that I am typing"
            text: qsTrId("rustrx-la-send-typing")
            checked: app.sendTyping
            automaticCheck: false
            onClicked: app.sendTyping = !app.sendTyping
        }

        // it depends on the switch above: its toggle in line with that
        // one's text, as the emoji field under the notification switches
        TextSwitch {
            leftMargin: Theme.horizontalPageMargin + Theme.itemSizeExtraSmall
                        + (palette.colorScheme === Theme.DarkOnLight ? Theme.paddingMedium : 0)
            //% "Say so under the message field"
            text: qsTrId("rustrx-la-typing-hint")
            //% "A line under the field reminds you that the room sees you typing."
            description: qsTrId("rustrx-la-typing-hint-hint")
            enabled: app.sendTyping
            checked: app.typingHint
            automaticCheck: false
            onClicked: app.typingHint = !app.typingHint
        }

        TextSwitch {
            //% "Show me as online"
            text: qsTrId("rustrx-la-show-online")
            //% "Others see when you are online and when you were last active, on servers that show it (matrix.org does not). Off: you always appear offline."
            description: qsTrId("rustrx-la-show-online-hint")
            checked: app.showOnline
            automaticCheck: false
            onClicked: {
                app.showOnline = !app.showOnline
                service.whenLoggedIn({ cmd: "set_presence", online: app.showOnline })
            }
        }

        // what strangers see on the phone's screen
        TextSwitch {
            //% "Message text in notifications"
            text: qsTrId("rustrx-la-notify-text")
            //% "When off, a notification only says how many messages arrived."
            description: qsTrId("rustrx-la-notify-text-hint")
            checked: service.notifyText
            automaticCheck: false
            onClicked: service.notifyText = !service.notifyText
        }

        TextSwitch {
            //% "Chat names outside Rustrx"
            text: qsTrId("rustrx-la-chat-details")
            //% "Notifications and the cover show which chat and who wrote. Off: they say Rustrx, and the cover shows only the counts."
            description: qsTrId("rustrx-la-chat-details-hint")
            checked: service.chatDetails
            automaticCheck: false
            onClicked: service.chatDetails = !service.chatDetails
        }

        // a photo's EXIF can say where it was taken (src/Exif.h)
        TextSwitch {
            //% "Remove the location from photos"
            text: qsTrId("rustrx-la-strip-location")
            //% "Photos leave without the place they were taken, which cameras write into them. Everything else stays as it was. Off: they go as the camera saved them."
            description: qsTrId("rustrx-la-strip-location-hint")
            checked: rustrx.stripLocation
            automaticCheck: false
            onClicked: rustrx.stripLocation = !rustrx.stripLocation
        }

        BackgroundItem {
            id: ignoredRow
            width: parent.width
            onClicked: pageStack.push(Qt.resolvedUrl("../IgnoredPage.qml"))
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                //% "Ignored users"
                text: qsTrId("rustrx-bt-ignored")
                color: ignoredRow.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            Image {
                anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                source: "image://theme/icon-m-right?" + (ignoredRow.highlighted ? Theme.highlightColor : Theme.primaryColor)
            }
        }

        Item { width: 1; height: Theme.paddingLarge }
    }

    VerticalScrollDecorator { }
}
