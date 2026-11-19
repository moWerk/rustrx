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

// Settings, Privacy: what others learn from Rustrx and who may reach one,
// in short groups under plain headers as Jolla's own settings pages have
// them (reports/settings-review-2026-11-18/privacy-layout-B.md): what goes
// out to the people in one's rooms, then what one's own screen shows, then
// who gets through.
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

        //% "Read receipts"
        SectionHeader { text: qsTrId("rustrx-se-receipts") }

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
            //% "Messages count as read as they come into view, and the unread count goes down with them. Off: opening a room is enough."
            description: qsTrId("rustrx-la-read-as-shown-hint")
            checked: app.readWhenShown
            automaticCheck: false
            onClicked: app.readWhenShown = !app.readWhenShown
        }

        // sends nothing, but belongs with the receipts
        TextSwitch {
            //% "Show who has read a message"
            text: qsTrId("rustrx-la-show-read-by")
            //% "Names appear under the last message each person has read."
            description: qsTrId("rustrx-la-show-read-by-hint")
            checked: app.showReadBy
            automaticCheck: false
            onClicked: app.showReadBy = !app.showReadBy
        }

        //% "Activity"
        SectionHeader { text: qsTrId("rustrx-se-activity") }

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
            //% "Others see when you are online and last active, where the server shows it (matrix.org does not)."
            description: qsTrId("rustrx-la-show-online-hint")
            checked: app.showOnline
            automaticCheck: false
            onClicked: {
                app.showOnline = !app.showOnline
                service.whenLoggedIn({ cmd: "set_presence", online: app.showOnline })
            }
        }

        //% "Pictures and videos"
        SectionHeader { text: qsTrId("rustrx-se-pictures-videos") }

        // what a picture or video says about itself: place, time, camera
        // (src/Exif.h, Png.h, Mp4.h); a downscaled picture is drawn anew
        // and keeps nothing at any level
        ComboBox {
            id: metadataBox
            width: parent.width
            //% "Leave out when sending"
            label: qsTrId("rustrx-la-metadata-level")
            description: [
                //% "Pictures and videos go as the camera saved them, with the place and time they were taken."
                qsTrId("rustrx-la-metadata-keep-hint"),
                //% "Where a picture or video was taken goes; the rest stays (time, camera, which way up)."
                qsTrId("rustrx-la-metadata-location-hint"),
                //% "Pictures and videos leave without the place and time they were taken, the camera and anything else it noted, only which way up they are."
                qsTrId("rustrx-la-metadata-all-hint")
            ][Math.max(0, currentIndex)]
            // the box follows the setting (the ComboBox sets currentIndex itself)
            readonly property int shownIndex: Math.max(0, Math.min(2, rustrx.metadataLevel))
            currentIndex: shownIndex
            onShownIndexChanged: currentIndex = shownIndex
            menu: ContextMenu {
                //% "Nothing"
                MenuItem { text: qsTrId("rustrx-me-metadata-keep") }
                //% "The location"
                MenuItem { text: qsTrId("rustrx-me-metadata-location") }
                //% "All metadata"
                MenuItem { text: qsTrId("rustrx-me-metadata-all") }
            }
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex !== rustrx.metadataLevel)
                    rustrx.metadataLevel = currentIndex
            }
        }

        //% "Calls"
        SectionHeader { text: qsTrId("rustrx-se-calls") }

        TextSwitch {
            //% "Calls only through a relay"
            text: qsTrId("rustrx-la-relay-only")
            //% "The other side sees your server's relay instead of your phone's internet address. Needs a relay on your server (most have one); without it calls do not connect. The sound may lag a little more."
            description: qsTrId("rustrx-la-relay-only-hint")
            checked: rustrx.callsRelayOnly
            automaticCheck: false
            onClicked: rustrx.callsRelayOnly = !rustrx.callsRelayOnly
        }

        // what anyone holding or seeing the phone learns
        //% "Screen"
        SectionHeader { text: qsTrId("rustrx-se-screen") }

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
            //% "Notifications and the cover name the chat, who wrote and who is calling. Off: only Rustrx, the counts and \"Call\"."
            description: qsTrId("rustrx-la-chat-details-hint2")
            checked: service.chatDetails
            automaticCheck: false
            onClicked: service.chatDetails = !service.chatDetails
        }

        //% "People"
        SectionHeader { text: qsTrId("rustrx-se-people") }

        TextSwitch {
            //% "Invitations from strangers"
            text: qsTrId("rustrx-la-stranger-invites")
            //% "A banner and the picture for invitations from people you share no room with. Off: they wait quietly in the list."
            description: qsTrId("rustrx-la-stranger-invites-hint")
            checked: app.strangerInvites
            automaticCheck: false
            onClicked: app.strangerInvites = !app.strangerInvites
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
