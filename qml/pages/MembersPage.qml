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
// ID while typing. A member's menu: a direct chat, or ignoring them. Below
// the members, in sections of their own: those asking to join, the
// invited and the banned (a tap on one opens their page: let in, revoke,
// lift the ban).
Page {
    id: page

    property string roomId
    property string roomName
    property var members: []
    // the member a direct chat was asked for from this page
    property string pendingDm

    property string filter
    // the sections in their order; a member without a membership (an older
    // core) counts as joined
    readonly property var groups: ["join", "knock", "invite", "ban"]
    function groupOf(m) { return m.membership && groups.indexOf(m.membership) >= 0 ? m.membership : "join" }
    readonly property int joinedCount: members.filter(function (m) { return groupOf(m) === "join" }).length
    // the rows, by section; the first of a section after the members
    // carries its header
    readonly property var shown: {
        var f = filter.trim().toLowerCase()
        var out = []
        for (var g = 0; g < groups.length; g++) {
            var first = true
            for (var i = 0; i < members.length; i++) {
                var m = members[i]
                if (groupOf(m) !== groups[g]) continue
                if (f.length && (m.name || "").toLowerCase().indexOf(f) < 0 && m.user_id.toLowerCase().indexOf(f) < 0) continue
                out.push({ user_id: m.user_id, name: m.name, role: m.role, power: m.power, group: groups[g],
                           header: first && g > 0 })
                first = false
            }
        }
        return out
    }
    function sectionName(group) {
        //% "Asking to join"
        if (group === "knock") return qsTrId("rustrx-se-members-knock")
        //% "Invited"
        if (group === "invite") return qsTrId("rustrx-se-members-invited")
        //% "Banned"
        return qsTrId("rustrx-se-members-banned")
    }

    function powerLabel(m) {
        //% "owner"
        if (m.role === "creator") return qsTrId("rustrx-la-power-owner")
        //% "admin"
        if (m.role === "admin" || m.power === "infinite" || m.power >= Tunables.powerAdmin) return qsTrId("rustrx-la-power-admin")
        //% "moderator"
        if (m.role === "moderator" || m.power >= Tunables.powerModerator) return qsTrId("rustrx-la-power-moderator")
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
            title: qsTrId("rustrx-se-members", page.joinedCount)
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

        delegate: Column {
            width: list.width
            SectionHeader {
                visible: modelData.header
                text: visible ? page.sectionName(modelData.group) : ""
            }
            ListItem {
                id: row
                contentHeight: Theme.itemSizeMedium
                // nothing to do with oneself here (direct chat, ignore): no menu
                menu: isSelf ? null : memberMenu
                readonly property bool isSelf: modelData.user_id === rustrx.userId
                onClicked: pageStack.push(Qt.resolvedUrl("UserPage.qml"),
                                          { userId: modelData.user_id, roomId: page.roomId, name: modelData.name || "" })

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
                              + (page.powerLabel(modelData).length ? "  ·  " + page.powerLabel(modelData) : "")
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
