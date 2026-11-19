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
    Component.onCompleted: {
        load()
        // what one may in the room (removing their messages): app.roomMay
        if (roomId.length) rustrx.command({ cmd: "room_settings", room_id: roomId })
    }

    Connections {
        target: rustrx
        onUserInfo: if (info.user_id === page.userId) { page.info = info; page.loaded = true }
        onErrorOccurred: {
            if (cmd === "user_info" || cmd === "kick" || cmd === "ban" || cmd === "unban"
                    || cmd === "verify_user" || cmd === "set_user_level") { page.loaded = true; page.load() }
            if (cmd === "redact_recent") page.removing = false
        }
        //% "Reported to your server's admins"
        onReported: if (what.user_id === page.userId) Notices.show(qsTrId("rustrx-no-reported"), Notice.Short)
        onSpaceBanned: {
            if (result.user_id !== page.userId) return
            page.load()
            //: %n rooms the person was banned from (the space counts as one)
            //% "Banned in %n rooms"
            Notices.show(qsTrId("rustrx-no-space-banned", result.rooms)
                         //: %n rooms where one could not ban (no power there, or not joined)
                         //% ", not in %n"
                         + (result.skipped > 0 ? qsTrId("rustrx-no-space-banned-skipped", result.skipped) : ""), Notice.Long)
        }
        onRedactProgress: {
            if (progress.user_id !== page.userId || progress.room_id !== page.roomId) return
            page.removing = !progress.finished
            page.removed = progress.done
            page.toRemove = progress.total
            //: %n messages removed
            //% "Removed %n of their messages"
            if (progress.finished) Notices.show(qsTrId("rustrx-no-redacted-recent", progress.done), Notice.Short)
        }
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
    // a space's member: a ban there reaches its rooms too
    readonly property bool inSpace: page.roomId.length > 0 && rustrx.roomInfo(page.roomId).is_space === true
    // removing their recent messages, while it runs
    property bool removing
    property int removed
    property int toRemove
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
                // the tapped entry acts, the box follows the room (LevelComboBox)
                readonly property int shownIndex: custom ? offered.length : Math.max(0, offered.indexOf(page.info.power))
                currentIndex: shownIndex
                onShownIndexChanged: currentIndex = shownIndex
                function choose(level) {
                    if (!page.loaded || level === page.info.power) return
                    if (page.info.is_self && level < page.info.power) {
                        //% "Lowering your own role: no one below you can give it back"
                        remorse.execute(qsTrId("rustrx-re-self-demote"), function () { page.setLevel(level) })
                        // until the remorse runs out, the box shows the role as it is
                        currentIndex = shownIndex
                    } else {
                        page.setLevel(level)
                    }
                }
                menu: ContextMenu {
                    Repeater {
                        model: roleBox.offered
                        MenuItem {
                            text: page.roleName(modelData === 100 ? "admin" : modelData === 50 ? "moderator" : "member")
                            onClicked: roleBox.choose(modelData)
                        }
                    }
                    MenuItem {
                        visible: roleBox.custom
                        //: a power level that is none of the named roles
                        //% "Level %1"
                        text: qsTrId("rustrx-la-role-level").arg(page.info.power)
                    }
                }
                description: page.info.is_self
                             //% "You can lower your own role, but not raise it again."
                             ? qsTrId("rustrx-la-role-self-hint") : ""
            }

            // muted: a level below the one for sending messages. Only for
            // those without a role: muting a moderator demoted them, and
            // unmuting could not give their level back (their role is
            // changed above first, on purpose)
            TextSwitch {
                visible: !!page.info.can_mute && page.info.membership === "join" && (page.info.power || 0) < 50
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
                visible: !!page.info.can_ban && page.inSpace
                //% "Ban from the space and its rooms"
                text: qsTrId("rustrx-bt-space-ban")
                onClicked: page.confirm({
                    title: qsTrId("rustrx-bt-space-ban"),
                    acceptText: qsTrId("rustrx-bt-ban-short"),
                    //% "They are banned in the space and in each of its rooms where you may ban."
                    text: qsTrId("rustrx-la-space-ban-note")
                }, function (reason) {
                    var c = { cmd: "space_ban", space_id: page.roomId, user_id: page.userId }
                    if (reason.length) c.reason = reason
                    rustrx.command(c)
                })
            }
            // their recent messages, for moderators who may remove others' messages
            PageButton {
                visible: !page.info.is_self && !page.inSpace && page.info.membership !== undefined
                         && app.roomMay(page.roomId).redact_other === true
                enabled: !page.removing
                text: page.removing
                      //: %1 done, %2 all
                      //% "Removing their messages: %1 of %2"
                      ? qsTrId("rustrx-bt-redact-recent-running").arg(page.removed).arg(page.toRemove)
                      //% "Remove their recent messages"
                      : qsTrId("rustrx-bt-redact-recent")
                onClicked: page.confirm({
                    title: qsTrId("rustrx-bt-redact-recent"),
                    acceptText: qsTrId("rustrx-bt-remove"),
                    //% "Up to 100 of their messages among the room's last 200 are removed for everyone, one after the other."
                    text: qsTrId("rustrx-la-redact-recent-note")
                }, function (reason) {
                    var c = { cmd: "redact_recent", room_id: page.roomId, user_id: page.userId }
                    if (reason.length) c.reason = reason
                    page.removing = true
                    rustrx.command(c)
                })
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
