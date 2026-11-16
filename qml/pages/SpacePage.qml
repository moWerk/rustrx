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
import "../components"

// A space: its rooms and subspaces as the server lists them (joined or
// not), the requests to join it, and what can be done with it. A joined
// room opens, one not joined shows its preview (join or ask to join), a
// subspace opens as a space. Who may manage the space adds and removes
// rooms; its settings (name, topic, picture, who can join, members,
// invite, leave) are the room settings page.
Page {
    id: page

    property string spaceId
    property string spaceName
    property var rooms: []
    property bool loading: true

    property var info: rustrx.roomInfo(spaceId)
    Connections { target: rustrx.spaces; onDataChanged: page.info = rustrx.roomInfo(page.spaceId) }

    readonly property bool canManage: info.can_manage_children === true
    readonly property int requests: app.knockCount(spaceId)

    Component.onCompleted: rustrx.command({ cmd: "space_open", space_id: spaceId })
    Component.onDestruction: rustrx.command({ cmd: "space_close", space_id: spaceId })

    Connections {
        target: rustrx
        onSpaceRooms: {
            if (spaceId !== page.spaceId) return
            page.rooms = rooms
            page.loading = loading
        }
        onRoomLink: {
            if (roomId !== page.spaceId) return
            Clipboard.text = link
            Notices.show(qsTrId("rustrx-no-link-copied"), Notice.Short)
        }
    }

    function stateText(r) {
        //% "joined"
        if (r.state === "joined") return qsTrId("rustrx-la-state-joined")
        //% "invited"
        if (r.state === "invited") return qsTrId("rustrx-la-state-invited")
        //% "asked to join"
        if (r.state === "knocked") return qsTrId("rustrx-la-state-knocked")
        return ""
    }

    function open(r) {
        if (r.is_space && r.state === "joined")
            pageStack.push(Qt.resolvedUrl("SpacePage.qml"), { spaceId: r.room_id, spaceName: r.name })
        else if (r.state === "joined")
            pageStack.push(Qt.resolvedUrl("RoomPage.qml"), { roomId: r.room_id, roomName: r.name })
        else
            pageStack.push(Qt.resolvedUrl("JoinPage.qml"), { address: r.alias || r.room_id, via: r.via })
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: page.rooms

        PullDownMenu {
            MenuItem {
                //% "Space settings"
                text: qsTrId("rustrx-me-space-settings")
                onClicked: pageStack.push(Qt.resolvedUrl("RoomInfoPage.qml"), { roomId: page.spaceId })
            }
            MenuItem {
                //% "Copy link"
                text: qsTrId("rustrx-me-copy-space-link")
                onClicked: rustrx.command({ cmd: "room_link", room_id: page.spaceId })
            }
            MenuItem {
                visible: page.canManage
                //% "Add an existing room"
                text: qsTrId("rustrx-me-space-add")
                onClicked: pageStack.push(Qt.resolvedUrl("AddToSpacePage.qml"), { spaceId: page.spaceId, spaceName: page.spaceName, existing: page.rooms })
            }
            MenuItem {
                visible: page.canManage
                //% "New subspace"
                text: qsTrId("rustrx-me-space-new-space")
                onClicked: pageStack.push(Qt.resolvedUrl("CreateRoomPage.qml"), { space: true, parentId: page.spaceId, parentName: page.spaceName })
            }
            MenuItem {
                visible: page.canManage
                //% "New room in this space"
                text: qsTrId("rustrx-me-space-new-room")
                onClicked: pageStack.push(Qt.resolvedUrl("CreateRoomPage.qml"), { parentId: page.spaceId, parentName: page.spaceName })
            }
        }

        header: Column {
            width: list.width

            PageHeader {
                title: page.info.name || page.spaceName
                description: app.joinRuleName(page.info.join_rule || "", "")
            }

            Label {
                visible: !!page.info.topic
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                bottomPadding: Theme.paddingMedium
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: page.info.topic || ""
            }

            // people asking to join this space
            BannerItem {
                visible: page.requests > 0
                onClicked: pageStack.push(Qt.resolvedUrl("KnockPage.qml"))
                text: qsTrId("rustrx-la-banner-knocks", page.requests)
            }

            SectionHeader {
                visible: page.rooms.length > 0
                //% "Rooms"
                text: qsTrId("rustrx-se-space-rooms")
            }
        }

        delegate: ListItem {
            id: row
            readonly property var r: modelData
            contentHeight: Theme.itemSizeMedium
            onClicked: page.open(r)
            menu: page.canManage ? removeMenu : null

            Rectangle {
                id: avatar
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconSizeMedium
                height: width
                // a space a rounded square, a room a circle
                radius: row.r.is_space ? Theme.paddingSmall : width / 2
                color: Theme.rgba(Theme.highlightBackgroundColor, Tunables.cardOpacity)
                clip: true
                Label {
                    anchors.centerIn: parent
                    visible: !picture.source.toString().length
                    text: (row.r.name || "?").charAt(0).toUpperCase()
                    color: Theme.highlightColor
                }
                Image {
                    id: picture
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: width
                    source: row.r.avatar_mxc ? rustrx.emotePath(row.r.avatar_mxc) : ""
                }
            }

            Column {
                anchors { left: avatar.right; leftMargin: Theme.paddingLarge; right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                    text: row.r.name
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: {
                        var parts = []
                        if (row.r.is_space) parts.push(qsTrId("rustrx-la-space-rooms", row.r.children || 0))
                        else parts.push(qsTrId("rustrx-la-n-members", row.r.members || 0))
                        if (row.r.state !== "joined") parts.push(app.joinRuleName(row.r.join_rule, page.info.name || page.spaceName))
                        var st = page.stateText(row.r)
                        if (st.length) parts.push(st)
                        //% "suggested"
                        if (row.r.suggested) parts.push(qsTrId("rustrx-la-suggested"))
                        return parts.join(" · ")
                    }
                }
            }

            Component {
                id: removeMenu
                ContextMenu {
                    MenuItem {
                        //% "Remove from the space"
                        text: qsTrId("rustrx-me-space-remove")
                        onClicked: {
                            var space = page.spaceId, child = row.r.room_id, core = rustrx
                            row.remorseAction(qsTrId("rustrx-re-removing"), function () {
                                core.command({ cmd: "space_remove_child", space_id: space, room_id: child })
                            })
                        }
                    }
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: page.loading && page.rooms.length === 0
        }

        ViewPlaceholder {
            enabled: !page.loading && page.rooms.length === 0
            //% "No rooms in this space yet"
            text: qsTrId("rustrx-la-space-empty")
            hintText: page.canManage
                      //% "Add one from the pulley menu."
                      ? qsTrId("rustrx-la-space-empty-hint")
                      : ""
        }

        VerticalScrollDecorator { }
    }
}
