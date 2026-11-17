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

// One person: their name and Matrix ID, whether we verified them, and
// what can be done: a direct chat, verifying, ignoring; in a room, with
// the power for it, removing them or banning them (and lifting a ban).
Page {
    id: page

    property string userId
    property string roomId
    property string name
    property var info: ({})
    property bool loaded
    property string pendingDm

    function load() { rustrx.command({ cmd: "user_info", user_id: userId, room_id: roomId }) }
    Component.onCompleted: load()

    Connections {
        target: rustrx
        onUserInfo: if (info.user_id === page.userId) { page.info = info; page.loaded = true }
        onErrorOccurred: if (cmd === "user_info" || cmd === "kick" || cmd === "ban" || cmd === "unban"
                             || cmd === "verify_user") page.loaded = true
        // a verification we started with them: its page (emoji to compare)
        onVerification: if (data.we_started && data.other_user === page.userId) app.showVerification(data)
        onDmReady: {
            if (userId !== page.pendingDm) return
            page.pendingDm = ""
            var main = pageStack.find(function (p) { return p.objectName === "main" })
            pageStack.replaceAbove(main, Qt.resolvedUrl("RoomPage.qml"),
                                   { roomId: roomId, roomName: rustrx.roomInfo(roomId).name || userId })
        }
    }

    RemorsePopup { id: remorse }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: page.info.name || page.name || page.userId
                description: page.userId
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.loaded && !page.info.is_self
                wrapMode: Text.Wrap
                color: page.info.verified ? Theme.highlightColor : Theme.secondaryColor
                text: page.info.verified
                      //% "Verified: you compared emoji with them."
                      ? qsTrId("rustrx-la-user-verified")
                      : page.info.has_identity
                        //% "Not verified. Verify to be sure messages from them are theirs."
                        ? qsTrId("rustrx-la-user-unverified")
                        //% "They have not set up verification."
                        : qsTrId("rustrx-la-user-no-identity")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !page.info.is_self
                //% "Direct chat"
                text: qsTrId("rustrx-me-direct-chat")
                onClicked: {
                    page.pendingDm = page.userId
                    rustrx.command({ cmd: "create_dm", user_id: page.userId })
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.loaded && !page.info.is_self && page.info.has_identity && !page.info.verified
                //% "Verify"
                text: qsTrId("rustrx-bt-verify-user")
                onClicked: rustrx.command({ cmd: "verify_user", user_id: page.userId })
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.loaded && !page.info.is_self
                text: page.info.ignored
                      //% "Stop ignoring"
                      ? qsTrId("rustrx-me-unignore")
                      //% "Ignore this person"
                      : qsTrId("rustrx-me-ignore")
                onClicked: {
                    var cmd = page.info.ignored ? "unignore_user" : "ignore_user"
                    rustrx.command({ cmd: cmd, user_id: page.userId })
                    page.load()
                }
            }

            SectionHeader {
                visible: page.roomId.length > 0 && (page.info.can_kick || page.info.can_ban)
                //% "In this room"
                text: qsTrId("rustrx-se-user-room")
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !!page.info.can_kick && page.info.membership === "join"
                //% "Remove from the room"
                text: qsTrId("rustrx-bt-kick")
                onClicked: {
                    //% "Removing"
                    remorse.execute(qsTrId("rustrx-re-kicking"), function () {
                        rustrx.command({ cmd: "kick", room_id: page.roomId, user_id: page.userId })
                    })
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !!page.info.can_ban && page.info.membership !== "ban"
                //% "Ban from the room"
                text: qsTrId("rustrx-bt-ban")
                onClicked: {
                    //% "Banning"
                    remorse.execute(qsTrId("rustrx-re-banning"), function () {
                        rustrx.command({ cmd: "ban", room_id: page.roomId, user_id: page.userId })
                    })
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !!page.info.can_ban && page.info.membership === "ban"
                //% "Lift the ban"
                text: qsTrId("rustrx-bt-unban")
                onClicked: rustrx.command({ cmd: "unban", room_id: page.roomId, user_id: page.userId })
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: !page.loaded
        }
        VerticalScrollDecorator { }
    }
}
