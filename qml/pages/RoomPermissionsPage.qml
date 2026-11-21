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

// A room's roles and permissions, for those who may change them: who has
// a role (owners, admins, moderators; a tap opens their page, where it is
// changed), and for each thing that can be done, from which role on.
Page {
    id: page

    property string roomId
    property var settings: ({})
    property var members: []

    // back from a person's page (a kick, a ban, a new role): the list again
    onStatusChanged: if (status === PageStatus.Active && members.length) rustrx.command({ cmd: "room_members", room_id: roomId })
    Connections {
        target: rustrx
        onMembers: if (roomId === page.roomId) page.members = members
    }

    readonly property var levels: settings.levels || ({})
    readonly property var events: levels.events || ({})
    readonly property bool canChange: !!settings.may && settings.may.power_levels === true
    readonly property int ownLevel: settings.own_level === "infinite" ? 100 : (settings.own_level || 0)
    readonly property bool isSpace: rustrx.roomInfo(roomId).is_space === true

    // those with a role, the highest first
    readonly property var staff: members.filter(function (m) {
        return (!m.membership || m.membership === "join") && (m.role === "creator" || m.role === "admin" || m.role === "moderator")
    }).sort(function (a, b) { return page.rank(b) - page.rank(a) })
    function rank(m) { return m.role === "creator" ? 1000 : (m.power === "infinite" ? 1000 : (m.power || 0)) }

    // an event type's level, or the settings' level it falls back to
    function eventLevel(type) {
        var v = events[type]
        return v === null || v === undefined ? (levels.state_default || 0) : v
    }
    // the box whose change was sent last: a refusal takes it back
    property Item pendingBox
    function set(box, key, level) {
        pendingBox = box
        rustrx.command({ cmd: "set_level", room_id: roomId, permission: key, level: level })
    }

    Connections {
        target: rustrx
        onRoomSettings: if (settings.room_id === page.roomId) page.settings = settings
        onErrorOccurred: {
            if (cmd !== "set_level") return
            // it goes back and says why (decision 3, 0.8 audit)
            if (page.pendingBox) page.pendingBox.revert(app.friendlyError(cmd, message))
            page.pendingBox = null
            rustrx.command({ cmd: "room_settings", room_id: page.roomId })
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                //% "Roles and permissions"
                title: qsTrId("rustrx-he-permissions")
                description: rustrx.roomInfo(page.roomId).name || ""
            }

            //% "Owners, admins and moderators"
            SectionHeader { text: qsTrId("rustrx-se-staff") }
            Repeater {
                model: page.staff
                BackgroundItem {
                    id: staffRow
                    width: column.width
                    height: Theme.itemSizeSmall
                    onClicked: pageStack.push(Qt.resolvedUrl("UserPage.qml"), { userId: modelData.user_id, roomId: page.roomId, name: modelData.name || "" })
                    Label {
                        anchors { left: parent.left; leftMargin: Theme.horizontalPageMargin; right: role.left; rightMargin: Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                        truncationMode: TruncationMode.Fade
                        text: modelData.name || modelData.user_id
                        color: staffRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        id: role
                        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        text: modelData.role === "creator" ? qsTrId("rustrx-la-role-creator")
                            : modelData.role === "admin" ? qsTrId("rustrx-la-role-admin") : qsTrId("rustrx-la-role-moderator")
                    }
                }
            }
            Note {
                kind: "footnote"
                //% "A role is given on the person's page: tap a member in the member list."
                text: qsTrId("rustrx-la-staff-hint")
            }

            //% "Who may"
            SectionHeader { text: qsTrId("rustrx-se-who-may") }
            Repeater {
                // the fields and event types of the room's power levels, in
                // the order one looks for them
                model: [
                    //% "Members start as"
                    { key: "users_default", text: qsTrId("rustrx-la-perm-default"), field: true },
                    //% "Send messages"
                    { key: "events_default", text: qsTrId("rustrx-la-perm-send"), field: true },
                    //% "Invite people"
                    { key: "invite", text: qsTrId("rustrx-la-perm-invite"), field: true },
                    //% "Remove people"
                    { key: "kick", text: qsTrId("rustrx-la-perm-kick"), field: true },
                    //% "Ban people"
                    { key: "ban", text: qsTrId("rustrx-la-perm-ban"), field: true },
                    //% "Remove others' messages"
                    { key: "redact", text: qsTrId("rustrx-la-perm-redact"), field: true },
                    //% "Notify everyone (@room)"
                    { key: "notify_room", text: qsTrId("rustrx-la-perm-notify"), field: true },
                    //% "Pin messages"
                    { key: "m.room.pinned_events", text: qsTrId("rustrx-la-perm-pin") },
                    //% "Change the name"
                    { key: "m.room.name", text: qsTrId("rustrx-la-perm-name") },
                    //% "Change the topic"
                    { key: "m.room.topic", text: qsTrId("rustrx-la-perm-topic") },
                    //% "Change the picture"
                    { key: "m.room.avatar", text: qsTrId("rustrx-la-perm-avatar") },
                    //% "Manage the space's rooms"
                    { key: "m.space.child", text: qsTrId("rustrx-la-perm-children"), space: true },
                    //% "Change other settings"
                    { key: "state_default", text: qsTrId("rustrx-la-perm-settings"), field: true },
                    //% "Change roles and permissions"
                    { key: "m.room.power_levels", text: qsTrId("rustrx-la-perm-power") },
                    //% "Change who can read the history"
                    { key: "m.room.history_visibility", text: qsTrId("rustrx-la-perm-history") },
                    //% "Change the addresses"
                    { key: "m.room.canonical_alias", text: qsTrId("rustrx-la-perm-aliases") },
                    //% "Change server access"
                    { key: "m.room.server_acl", text: qsTrId("rustrx-la-perm-acl") },
                    //% "Turn on encryption"
                    { key: "m.room.encryption", text: qsTrId("rustrx-la-perm-encryption") },
                    //% "Upgrade the room"
                    { key: "m.room.tombstone", text: qsTrId("rustrx-la-perm-upgrade") }
                ].filter(function (p) { return !p.space || page.isSpace })
                LevelComboBox {
                    id: levelBox
                    label: modelData.text
                    enabled: page.canChange
                    ownLevel: page.ownLevel
                    level: modelData.field ? (page.levels[modelData.key] || 0) : page.eventLevel(modelData.key)
                    onChosen: page.set(levelBox, modelData.key, value)
                }
            }
            Note {
                kind: "footnote"
                //% "Each is open to that role and every higher one. Only levels up to your own can be chosen."
                text: qsTrId("rustrx-la-perm-hint")
            }
        }
        VerticalScrollDecorator { }
    }
}
