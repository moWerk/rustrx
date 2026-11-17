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

// The users this account ignores (from a member's menu): their messages
// and invitations do not reach this account. Long press to stop ignoring.
Page {
    id: page

    property var users: []
    property bool loaded

    Component.onCompleted: rustrx.command({ cmd: "ignored_users" })

    Connections {
        target: rustrx
        onIgnoredUsers: { page.users = users; page.loaded = true }
        onErrorOccurred: if (cmd === "ignored_users" || cmd === "unignore_user") page.loaded = true
    }

    SilicaListView {
        anchors.fill: parent
        model: page.users

        header: PageHeader {
            //% "Ignored users"
            title: qsTrId("rustrx-he-ignored")
        }

        delegate: ListItem {
            id: row
            contentHeight: Theme.itemSizeMedium
            menu: ContextMenu {
                MenuItem {
                    //% "Stop ignoring"
                    text: qsTrId("rustrx-me-unignore")
                    onClicked: {
                        var uid = modelData.user_id, core = rustrx
                        //% "No longer ignoring"
                        row.remorseAction(qsTrId("rustrx-re-unignoring"), function () {
                            core.command({ cmd: "unignore_user", user_id: uid })
                        })
                    }
                }
            }
            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                    text: modelData.name || modelData.user_id
                }
                Label {
                    visible: !!modelData.name
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: modelData.user_id
                }
            }
        }

        ViewPlaceholder {
            enabled: page.loaded && page.users.length === 0
            //% "Nobody is ignored"
            text: qsTrId("rustrx-la-ignored-none")
            //% "Ignore someone from a room's member list."
            hintText: qsTrId("rustrx-la-ignored-hint")
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: !page.loaded
        }

        VerticalScrollDecorator { }
    }
}
