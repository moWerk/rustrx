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

// One person: their name and Matrix ID, whether we verified them, and
// what can be done: a direct chat, verifying, ignoring, reporting; in a
// room their role, and with the power for it changing the role, muting,
// removing, banning (and lifting a ban), revoking an invitation, each
// with an optional reason. What we may comes from the room's power
// levels (user_info's can_* from the core).
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
                             || cmd === "verify_user" || cmd === "set_user_level") { page.loaded = true; page.load() }
        //% "Reported to your server's admins"
        onReported: if (what.user_id === page.userId) Notices.show(qsTrId("rustrx-no-reported"), Notice.Short)
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

    // a moderator's act with its optional reason (components/ReasonDialog)
    function confirm(props, act) {
        var d = pageStack.push(Qt.resolvedUrl("../components/ReasonDialog.qml"), props)
        d.accepted.connect(function () { act(d.reason.trim()) })
    }
    // c: the command with its name written out ({ cmd: "kick", ... })
    function moderate(c, reason) {
        c.room_id = page.roomId
        c.user_id = page.userId
        if (reason.length) c.reason = reason
        page.loaded = false
        rustrx.command(c)
    }
    readonly property string who: page.info.name || page.name || page.userId
    // the level a role stands for (the core's moderation.rs)
    readonly property var roleLevels: [0, 50, 100]
    function roleName(role, level) {
        switch (role) {
        //% "Owner"
        case "creator": return qsTrId("rustrx-la-role-creator")
        //% "Admin"
        case "admin": return qsTrId("rustrx-la-role-admin")
        //% "Moderator"
        case "moderator": return qsTrId("rustrx-la-role-moderator")
        //% "Member"
        default: return qsTrId("rustrx-la-role-member")
        }
    }
    function setLevel(level) {
        page.loaded = false
        rustrx.command({ cmd: "set_user_level", room_id: page.roomId, user_id: page.userId, level: level })
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            visible: page.loaded && !page.info.is_self
            MenuItem {
                //% "Report to your server"
                text: qsTrId("rustrx-me-report-user")
                onClicked: page.confirm({
                    //% "Report %1"
                    title: qsTrId("rustrx-he-report-user").arg(page.who),
                    //% "Report"
                    acceptText: qsTrId("rustrx-bt-report"),
                    //% "The admins of your own server get the report, not the room's moderators."
                    text: qsTrId("rustrx-la-report-goes-to-server")
                }, function (reason) {
                    var c = { cmd: "report_user", user_id: page.userId }
                    if (reason.length) c.reason = reason
                    rustrx.command(c)
                })
            }
        }

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

            PageButton {
                visible: !page.info.is_self
                //% "Direct chat"
                text: qsTrId("rustrx-me-direct-chat")
                onClicked: {
                    page.pendingDm = page.userId
                    rustrx.command({ cmd: "create_dm", user_id: page.userId })
                }
            }
            PageButton {
                visible: page.loaded && !page.info.is_self && page.info.has_identity && !page.info.verified
                //% "Verify"
                text: qsTrId("rustrx-bt-verify-user")
                onClicked: rustrx.command({ cmd: "verify_user", user_id: page.userId })
            }
            PageButton {
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
                visible: page.roomId.length > 0 && page.info.membership !== undefined
                //% "In this room"
                text: qsTrId("rustrx-se-user-room")
            }

            // the role: a line for everyone, a choice for those who may
            DetailItem {
                visible: page.roomId.length > 0 && page.info.membership !== undefined && !page.info.can_set_level
                //% "Role"
                label: qsTrId("rustrx-la-role")
                value: page.roleName(page.info.role, page.info.power)
            }
            HoldComboBox {
                id: roleBox
                visible: !!page.info.can_set_level
                width: parent.width
                label: qsTrId("rustrx-la-role")
                // a level that is no role's (say 75) shows as itself
                readonly property bool custom: page.roleLevels.indexOf(page.info.power) < 0
                // only roles up to our own level can be given
                readonly property var offered: page.roleLevels.filter(function (l) { return l <= (page.info.own_level || 0) })
                currentIndex: custom ? offered.length : Math.max(0, offered.indexOf(page.info.power))
                menu: ContextMenu {
                    Repeater {
                        model: roleBox.offered
                        MenuItem { text: page.roleName(modelData === 100 ? "admin" : modelData === 50 ? "moderator" : "member") }
                    }
                    MenuItem {
                        visible: roleBox.custom
                        //: a power level that is none of the named roles
                        //% "Level %1"
                        text: qsTrId("rustrx-la-role-level").arg(page.info.power)
                    }
                }
                onCurrentIndexChanged: {
                    if (!page.loaded || currentIndex >= offered.length) return
                    var level = offered[currentIndex]
                    if (level === page.info.power) return
                    if (page.info.is_self && level < page.info.power) {
                        //% "Lowering your own role: no one below you can give it back"
                        remorse.execute(qsTrId("rustrx-re-self-demote"), function () { page.setLevel(level) })
                    } else {
                        page.setLevel(level)
                    }
                }
                description: page.info.is_self
                             //% "You can lower your own role, but not raise it again."
                             ? qsTrId("rustrx-la-role-self-hint") : ""
            }

            // muted: a level below the one for sending messages
            TextSwitch {
                visible: !!page.info.can_mute && page.info.membership === "join"
                readonly property int sendLevel: page.info.events_default || 0
                //% "Muted"
                text: qsTrId("rustrx-la-muted")
                //% "They can read, but not write here."
                description: qsTrId("rustrx-la-muted-hint")
                checked: (page.info.power || 0) < sendLevel || (sendLevel === 0 && (page.info.power || 0) < 0)
                automaticCheck: false
                onClicked: page.setLevel(checked ? sendLevel : sendLevel - 1)
            }

            PageButton {
                visible: !!page.info.can_kick && page.info.membership === "invite"
                //% "Revoke the invitation"
                text: qsTrId("rustrx-bt-revoke-invite")
                onClicked: page.confirm({
                    title: qsTrId("rustrx-bt-revoke-invite"),
                    //% "Revoke"
                    acceptText: qsTrId("rustrx-bt-revoke")
                }, function (reason) { page.moderate({ cmd: "kick" }, reason) })
            }
            PageButton {
                visible: !!page.info.can_kick && page.info.membership === "join"
                //% "Remove from the room"
                text: qsTrId("rustrx-bt-kick")
                onClicked: page.confirm({
                    //% "Remove %1"
                    title: qsTrId("rustrx-he-kick").arg(page.who),
                    //% "Remove"
                    acceptText: qsTrId("rustrx-bt-remove"),
                    //% "They can come back if the room lets them."
                    text: qsTrId("rustrx-la-kick-note")
                }, function (reason) { page.moderate({ cmd: "kick" }, reason) })
            }
            PageButton {
                visible: !!page.info.can_ban && page.info.membership !== "ban"
                //% "Ban from the room"
                text: qsTrId("rustrx-bt-ban")
                onClicked: page.confirm({
                    //% "Ban %1"
                    title: qsTrId("rustrx-he-ban").arg(page.who),
                    //% "Ban"
                    acceptText: qsTrId("rustrx-bt-ban-short"),
                    //% "They are removed and cannot come back until the ban is lifted."
                    text: qsTrId("rustrx-la-ban-note")
                }, function (reason) { page.moderate({ cmd: "ban" }, reason) })
            }
            PageButton {
                visible: !!page.info.can_ban && page.info.membership === "ban"
                //% "Lift the ban"
                text: qsTrId("rustrx-bt-unban")
                onClicked: page.confirm({
                    title: qsTrId("rustrx-bt-unban"),
                    //% "Lift"
                    acceptText: qsTrId("rustrx-bt-lift")
                }, function (reason) { page.moderate({ cmd: "unban" }, reason) })
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
