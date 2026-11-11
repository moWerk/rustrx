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

// The room behind the conversation, attached to the right of RoomPage:
// name and topic (editable for moderators), alias, encryption, the
// members, and in the pulley an invitation and the way out.
Page {
    id: page

    property string roomId
    property var info: rustrx.roomInfo(roomId)
    property var memberList: []
    property bool membersLoaded
    // own power level, from the member list; 50 is the moderator default
    property int ownPower: 0
    readonly property bool canEdit: ownPower >= 50

    function refresh() {
        info = rustrx.roomInfo(roomId)
    }

    function powerLabel(p) {
        //% "admin"
        if (p === "infinite" || p >= 100) return qsTrId("rustrx-la-power-admin")
        //% "moderator"
        if (p >= 50) return qsTrId("rustrx-la-power-moderator")
        return ""
    }

    Component.onCompleted: rustrx.command({ cmd: "room_members", room_id: roomId })

    // the header lays out after creation (the topic wraps, the fields size),
    // which moves the list's origin and leaves the view scrolled: re-anchor
    Timer { id: settle; interval: 250; running: true; onTriggered: list.positionViewAtBeginning() }

    Connections {
        target: rustrx
        onMembers: {
            if (roomId !== page.roomId) return
            memberList = members
            membersLoaded = true
            settle.restart()
            for (var i = 0; i < members.length; i++) {
                if (members[i].user_id === rustrx.userId) {
                    var p = members[i].power
                    ownPower = p === "infinite" ? 100 : (p || 0)
                }
            }
        }
        onDmReady: {
            // a direct chat opened from a member row replaces this room
            var main = pageStack.find(function (p) { return p.objectName === "main" })
            pageStack.replaceAbove(main, Qt.resolvedUrl("RoomPage.qml"),
                                   { roomId: roomId, roomName: rustrx.roomInfo(roomId).name || userId })
        }
        onErrorOccurred: if (cmd === "set_room_name" || cmd === "set_room_topic") refresh()
    }

    // the room list carries the name and topic; pick changes up
    Connections {
        target: rustrx.rooms
        onDataChanged: refresh()
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: memberList
        currentIndex: -1   // or the view re-anchors to the first member when the list arrives

        PullDownMenu {
            MenuItem {
                //% "Invite someone"
                text: qsTrId("rustrx-me-invite")
                onClicked: pageStack.push(inviteDialog)
            }
            MenuItem {
                //% "Leave room"
                text: qsTrId("rustrx-me-leave-room")
                onClicked: {
                    var id = roomId, core = rustrx
                    //% "Leaving"
                    leaveRemorse.execute(qsTrId("rustrx-re-leaving"), function () {
                        core.command({ cmd: "leave_room", room_id: id })
                        pageStack.pop(pageStack.find(function (p) { return p.objectName === "main" }))
                    })
                }
            }
        }

        RemorsePopup { id: leaveRemorse }

        header: Column {
            width: list.width

            PageHeader {
                title: info.name || ""
                //% "Room"
                description: info.is_dm ? "" : qsTrId("rustrx-he-room")
            }

            // name and topic: fields for moderators, labels for everyone else;
            // a change is sent when the field loses focus
            TextField {
                visible: canEdit && !info.is_dm
                width: parent.width
                //% "Room name"
                label: qsTrId("rustrx-la-room-name")
                placeholderText: label
                text: info.name || ""
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                onActiveFocusChanged: {
                    if (!activeFocus && text.trim().length && text.trim() !== (info.name || ""))
                        rustrx.command({ cmd: "set_room_name", room_id: roomId, name: text.trim() })
                }
            }

            TextArea {
                visible: canEdit && !info.is_dm
                width: parent.width
                //% "Topic"
                label: qsTrId("rustrx-la-topic")
                placeholderText: label
                text: info.topic || ""
                onActiveFocusChanged: {
                    if (!activeFocus && text !== (info.topic || ""))
                        rustrx.command({ cmd: "set_room_topic", room_id: roomId, topic: text })
                }
            }

            Label {
                visible: !canEdit && !!(info.topic && info.topic.length)
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: info.topic || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }

            Item { width: 1; height: Theme.paddingMedium }

            DetailItem {
                visible: !!(info.alias && info.alias.length)
                //% "Address"
                label: qsTrId("rustrx-la-alias")
                value: info.alias || ""
            }
            DetailItem {
                //% "Encryption"
                label: qsTrId("rustrx-la-encryption")
                //% "On"
                value: info.encrypted ? qsTrId("rustrx-la-on")
                //% "Off"
                                      : qsTrId("rustrx-la-off")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: roomId
                wrapMode: Text.WrapAnywhere
                horizontalAlignment: Text.AlignRight
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }

            TextSwitch {
                //% "Favourite"
                text: qsTrId("rustrx-sw-favourite")
                checked: info.is_fav === true
                automaticCheck: false
                onClicked: rustrx.command({ cmd: "set_favourite", room_id: roomId, value: !checked })
            }
            TextSwitch {
                visible: !info.is_dm
                //% "Low priority"
                text: qsTrId("rustrx-sw-low-priority")
                //% "Sorted below the other rooms"
                description: qsTrId("rustrx-la-low-priority-desc")
                checked: info.is_low === true
                automaticCheck: false
                onClicked: rustrx.command({ cmd: "set_low_priority", room_id: roomId, value: !checked })
            }

            SectionHeader {
                //: %n the number of members
                //% "%n members"
                text: qsTrId("rustrx-se-members", memberList.length)
            }

        }

        // outside the header: a header that shrinks would leave the view scrolled
        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: !membersLoaded
        }

        delegate: ListItem {
            id: row
            contentHeight: Theme.itemSizeMedium
            menu: memberMenu
            readonly property bool isSelf: modelData.user_id === rustrx.userId

            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: (modelData.name || modelData.user_id)
                          + (powerLabel(modelData.power).length ? "  ·  " + powerLabel(modelData.power) : "")
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
                        visible: !row.isSelf
                        //% "Direct chat"
                        text: qsTrId("rustrx-me-direct-chat")
                        onClicked: rustrx.command({ cmd: "create_dm", user_id: modelData.user_id })
                    }
                    MenuItem {
                        visible: !row.isSelf
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

        VerticalScrollDecorator { }
    }

    Component {
        id: inviteDialog
        Dialog {
            canAccept: userField.text.trim().indexOf("@") === 0 && userField.text.indexOf(":") > 1
            onAccepted: rustrx.command({ cmd: "invite", room_id: roomId, user_id: userField.text.trim() })
            Column {
                width: parent.width
                DialogHeader {
                    //% "Invite"
                    acceptText: qsTrId("rustrx-dh-invite")
                }
                TextField {
                    id: userField
                    width: parent.width
                    //% "Matrix ID, like @name:server"
                    label: qsTrId("rustrx-la-invite-id")
                    placeholderText: label
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    focus: true
                    EnterKey.enabled: canAccept
                    EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                    EnterKey.onClicked: accept()
                }
            }
        }
    }
}
