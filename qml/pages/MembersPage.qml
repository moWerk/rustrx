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
import "../markup.js" as Markup

// A room's members, a search on top that narrows them by name or Matrix
// ID while typing. A member's menu: a direct chat, or ignoring them.
Page {
    id: page

    property string roomId
    property string roomName
    property var members: []
    // the member a direct chat was asked for from this page
    property string pendingDm

    property string filter
    readonly property var shown: {
        var f = filter.trim().toLowerCase()
        if (!f.length) return members
        var out = []
        for (var i = 0; i < members.length; i++) {
            var m = members[i]
            if ((m.name || "").toLowerCase().indexOf(f) >= 0 || m.user_id.toLowerCase().indexOf(f) >= 0)
                out.push(m)
        }
        return out
    }

    function powerLabel(p) {
        //% "admin"
        if (p === "infinite" || p >= Tunables.powerAdmin) return qsTrId("rustrx-la-power-admin")
        //% "moderator"
        if (p >= Tunables.powerModerator) return qsTrId("rustrx-la-power-moderator")
        return ""
    }

    Connections {
        target: rustrx
        onDmReady: {
            // a direct chat opened from a member row here replaces the room
            if (userId !== page.pendingDm) return
            page.pendingDm = ""
            var main = pageStack.find(function (p) { return p.objectName === "main" })
            pageStack.replaceAbove(main, Qt.resolvedUrl("RoomPage.qml"),
                                   { roomId: roomId, roomName: rustrx.roomInfo(roomId).name || userId })
        }
        onErrorOccurred: if (cmd === "create_dm") page.pendingDm = ""
    }

    // the title and the search above the list, not in it: a new list with
    // every letter took the focus away from the search (a list is a focus
    // scope), and the keyboard closed after each one
    Column {
        id: top
        width: parent.width
        PageHeader {
            //: %n the room's members
            //% "%n members"
            title: qsTrId("rustrx-se-members", page.members.length)
            description: page.roomName
        }
        SearchField {
            width: parent.width
            //% "Search members"
            placeholderText: qsTrId("rustrx-ph-member-search")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextChanged: page.filter = text
            EnterKey.iconSource: "image://theme/icon-m-enter-close"
            EnterKey.onClicked: focus = false
        }
    }

    SilicaListView {
        id: list
        anchors { top: top.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        clip: true
        model: page.shown
        currentIndex: -1

        delegate: ListItem {
            id: row
            contentHeight: Theme.itemSizeMedium
            // nothing to do with oneself here (direct chat, ignore): no menu
            menu: isSelf ? null : memberMenu
            readonly property bool isSelf: modelData.user_id === rustrx.userId

            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    textFormat: Text.StyledText
                    text: rustrx.emojify(Markup.escaped(modelData.name || modelData.user_id),
                                         Math.round(font.pixelSize * Tunables.emojiScaleList))
                          + (page.powerLabel(modelData.power).length ? "  ·  " + page.powerLabel(modelData.power) : "")
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: modelData.user_id
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            Component {
                id: memberMenu
                ContextMenu {
                    MenuItem {
                        //% "Direct chat"
                        text: qsTrId("rustrx-me-direct-chat")
                        onClicked: {
                            page.pendingDm = modelData.user_id
                            rustrx.command({ cmd: "create_dm", user_id: modelData.user_id })
                        }
                    }
                    MenuItem {
                        //% "Ignore this person"
                        text: qsTrId("rustrx-me-ignore")
                        onClicked: {
                            var uid = modelData.user_id, core = rustrx
                            //% "Ignoring"
                            row.remorseAction(qsTrId("rustrx-re-ignoring"), function () {
                                core.command({ cmd: "ignore_user", user_id: uid })
                            })
                        }
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0 && page.filter.trim().length > 0
            //: %1 the search text
            //% "No members match “%1”"
            text: qsTrId("rustrx-la-no-member-match").arg(page.filter.trim())
        }

        VerticalScrollDecorator { }
    }
}
