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

// Where something goes: every joined room and chat, with its picture, a
// search on top. Sharing from other apps (SharePage) and Forward
// (MessageActionsPage) pick here, then confirm on ConfirmSendPage (the
// author's decision 4, 0.8 audit: sharing sent at the first tap).
// allAccounts: the rooms of every logged-in account, each row naming its
// account (sharing); without it the shown account's (forwarding stays in
// the message's account).
Page {
    id: page

    property string title
    property string description
    property bool allAccounts
    // the search, lower case
    property string filter
    // several accounts in the list: each row says whose it is
    readonly property bool several: allAccounts && rustrx.accounts.length > 1

    signal picked(int account, string roomId, string name, string avatarPath, real avatarVersion)

    Component.onCompleted: if (allAccounts) rustrx.followAllTargets(true)
    Component.onDestruction: if (allAccounts) rustrx.followAllTargets(false)

    SilicaListView {
        id: list
        anchors.fill: parent
        // every joined room and chat, whatever the main page's filter
        model: page.allAccounts ? rustrx.allTargets : rustrx.targets

        header: Column {
            width: list.width
            PageHeader {
                title: page.title
                description: page.description
            }
            SearchField {
                width: parent.width
                //% "Search rooms and chats"
                placeholderText: qsTrId("rustrx-ph-picker-search")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextChanged: page.filter = text.trim().toLowerCase()
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }
        }

        delegate: BackgroundItem {
            id: row
            // the shown account's rows carry no account of their own
            readonly property int account: typeof model.account === "number" ? model.account : rustrx.accountKey
            readonly property bool match: page.filter.length === 0
                                          || String(model.name || "").toLowerCase().indexOf(page.filter) >= 0
                                          || String(model.alias || "").toLowerCase().indexOf(page.filter) >= 0
            width: list.width
            height: match ? Theme.itemSizeMedium : 0
            visible: match
            onClicked: page.picked(row.account, model.room_id, model.name || "", model.avatar_path || "", model.avatar_v || 0)

            OwnAvatar {
                id: avatar
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconSizeMedium
                height: width
                letterSize: Theme.fontSizeMedium
                path: model.avatar_path || ""
                version: model.avatar_v || 0
                name: model.name || ""
                userId: model.room_id || ""
            }
            RowTexts {
                anchors {
                    left: avatar.right
                    leftMargin: Theme.paddingLarge
                    right: chip.visible ? chip.left : parent.right
                    rightMargin: chip.visible ? Theme.paddingMedium : Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: model.name || ""
                // the account by name, so it also reads with the colours off
                description: page.several ? service.accountLabel(row.account) : ""
                highlighted: row.highlighted
            }
            AccountChip {
                id: chip
                visible: page.several && rustrx.colourAccounts
                anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                colour: app.accountColour(row.account)
            }
        }

        // shared before the first sync: the list is still coming
        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: !rustrx.roomsLoaded && list.count === 0
            visible: running
        }
        ViewPlaceholder {
            enabled: rustrx.roomsLoaded && list.count === 0
            //% "No rooms or chats yet"
            text: qsTrId("rustrx-la-picker-empty")
        }

        VerticalScrollDecorator { }
    }
}
