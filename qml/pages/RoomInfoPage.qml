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

// The room behind the conversation, attached to the right of RoomPage:
// name and topic, alias, encryption, the members, and in the pulley an
// invitation, a report and the way out. What can be changed here is what
// the room's power levels allow (the core's room_settings, its `may`):
// the same page for everyone, a control in place of a line where one may;
// the rooms's roles, addresses, server access and upgrade on pages of
// their own, for those who may.
Page {
    id: page

    property string roomId
    property var info: rustrx.roomInfo(roomId)
    property var memberList: []
    property bool membersLoaded
    // the room's permissions and settings, from the core (room_settings)
    property var settings: ({})
    readonly property var may: settings.may || ({})
    // a space's settings: no chat things (notifications, calls, encryption)
    readonly property bool isSpace: info.is_space === true
    // who may join, as the core names it; the choices offered here (members
    // of its spaces only where it is in one)
    readonly property var rules: info.parents && info.parents.length
                                 ? ["invite", "knock", "restricted", "knock_restricted", "public"]
                                 : ["invite", "knock", "public"]
    readonly property int knocks: app.knockCount(roomId)
    // outside the qsTrId: a function inside its arguments made lupdate drop
    // the //% text, and the row showed its raw id (0.8 audit)
    readonly property int joinedCount: memberList.filter(function (m) { return !m.membership || m.membership === "join" }).length

    function refresh() {
        info = rustrx.roomInfo(roomId)
    }

    property string notifyMode: ""
    Component.onCompleted: {
        rustrx.command({ cmd: "room_members", room_id: roomId })
        rustrx.command({ cmd: "get_notification_mode", room_id: roomId })
        rustrx.command({ cmd: "room_settings", room_id: roomId })
    }

    Connections {
        target: rustrx
        onMembers: {
            if (roomId !== page.roomId) return
            memberList = members
            membersLoaded = true
        }
        onRoomSettings: if (settings.room_id === page.roomId) page.settings = settings
        // a page below (a space's) asked for its own link: only on top
        onRoomLink: {
            if (roomId !== page.roomId || pageStack.currentPage !== page) return
            Clipboard.text = link
            Notices.show(qsTrId("rustrx-no-link-copied"), Notice.Short)
        }
        onErrorOccurred: {
            if (cmd === "set_room_name" || cmd === "set_room_topic") refresh()
            // a refused change: show the settings as they are
            if (["set_history_visibility", "set_guest_access", "enable_encryption", "set_directory_visibility"].indexOf(cmd) >= 0)
                rustrx.command({ cmd: "room_settings", room_id: page.roomId })
            // a refused choice goes back and says why (decision 3, 0.8 audit)
            if (cmd === "set_join_rule") joinBox.revert(app.friendlyError(cmd, message))
            else if (cmd === "set_history_visibility") historyBox.revert(app.friendlyError(cmd, message))
            else if (cmd === "set_notification_mode") notifyBox.revert(app.friendlyError(cmd, message))
        }
        //% "Reported to your server's admins"
        onReported: if (what.room_id === page.roomId) Notices.show(qsTrId("rustrx-no-reported"), Notice.Short)
        onRoomUpgraded: {
            if (roomId !== page.roomId) return
            //% "Upgraded: the room's members are asked to follow to the new one"
            Notices.show(qsTrId("rustrx-no-room-upgraded"), Notice.Long)
        }
        onNotificationMode: if (roomId === page.roomId) page.notifyMode = mode
    }

    // the room list carries the name and topic; pick changes up. The
    // settings again only when this room's entry changed (a new event in it,
    // which a change of its levels or rules is), not for every other room's
    // message (audit R1)
    Connections {
        target: rustrx.rooms
        onDataChanged: {
            var before = JSON.stringify(page.info)
            refresh()
            if (JSON.stringify(page.info) !== before)
                settingsLater.restart()
        }
    }
    // someone changed the room's settings or levels: read them again, once
    Timer { id: settingsLater; interval: Tunables.typingDebounceMs; onTriggered: rustrx.command({ cmd: "room_settings", room_id: page.roomId }) }
    Connections { target: rustrx.spaces; onDataChanged: refresh() }
    Connections { target: rustrx.people; onDataChanged: refresh() }

    // a flickable, not a list: a list without rows scrolls back to its top
    // whenever its header changes size, as when a combo box opens
    // (Luna-Terra-CG's report)
    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            // a direct chat: a classic call, end-to-end encrypted like its messages
            MenuItem {
                visible: !page.isSpace && info.is_dm === true && !!info.dm_user && rustrx.voip.state === "idle"
                //% "Call"
                text: qsTrId("rustrx-me-voip-call")
                onClicked: {
                    rustrx.placeCall(roomId, info.dm_user, info.name || info.dm_user)
                    app.showVoip()
                }
            }
            MenuItem {
                visible: !page.isSpace && rustrx.voip.state === "idle"
                //: a call through Element Call, the other way to call (unencrypted rooms only for now)
                //% "Call with Element Call"
                text: qsTrId("rustrx-me-call-element")
                onClicked: rustrx.command({ cmd: "call_start", room_id: roomId })
            }
            MenuItem {
                visible: page.may.avatar === true && !info.is_dm
                //% "Change the room's picture"
                text: qsTrId("rustrx-me-room-picture")
                // the gallery, as before; a tap on the picture also offers a photo
                onClicked: roomPictureChoice.pickPicture()
            }
            MenuItem {
                visible: !info.is_dm
                //% "Report this room"
                text: qsTrId("rustrx-me-report-room")
                onClicked: {
                    var d = pageStack.push(Qt.resolvedUrl("../components/ReasonDialog.qml"), {
                        //% "Report this room"
                        title: qsTrId("rustrx-he-report-room"),
                        acceptText: qsTrId("rustrx-bt-report"),
                        text: qsTrId("rustrx-la-report-goes-to-server")
                    })
                    d.accepted.connect(function () {
                        var c = { cmd: "report_room", room_id: page.roomId }
                        if (d.reason.trim().length) c.reason = d.reason.trim()
                        rustrx.command(c)
                    })
                }
            }
            MenuItem {
                // shown before the settings arrive too: the server decides then
                visible: page.may.invite !== false
                //% "Invite someone"
                text: qsTrId("rustrx-me-invite")
                onClicked: pageStack.push(inviteDialog)
            }
            MenuItem {
                visible: page.isSpace
                //% "Leave the space and its rooms"
                text: qsTrId("rustrx-me-leave-space-rooms")
                onClicked: {
                    var id = roomId, core = rustrx, nav = app
                    leaveRemorse.execute(qsTrId("rustrx-re-leaving"), function () {
                        core.command({ cmd: "space_leave", space_id: id, with_rooms: true })
                        // the list, also when the remorse ran out early
                        // because the page was swiped away (the app waits;
                        // captured: the page may be gone by then)
                        nav.backToList()
                    })
                }
            }
            MenuItem {
                text: page.isSpace
                      //% "Leave space"
                      ? qsTrId("rustrx-me-leave-space")
                      //% "Leave room"
                      : qsTrId("rustrx-me-leave-room")
                onClicked: {
                    var id = roomId, core = rustrx, nav = app
                    leaveRemorse.execute(qsTrId("rustrx-re-leaving"), function () {
                        core.command({ cmd: "leave_room", room_id: id })
                        // the list, also when the remorse ran out early
                        // because the page was swiped away (the app waits;
                        // captured: the page may be gone by then)
                        nav.backToList()
                    })
                }
            }
        }

        RemorsePopup { id: leaveRemorse }

        Column {
            id: column
            width: flick.width
            // the settings tabs' room between rows, so buttons and switches
            // stand as far apart here as there (0.8 audit)
            spacing: Theme.paddingMedium

            PageHeader {
                title: info.name || ""
                description: info.is_dm ? "" : page.isSpace
                             //% "Space"
                             ? qsTrId("rustrx-he-space")
                             //% "Room"
                             : qsTrId("rustrx-he-room")
            }

            // the room's threads and members, close to the top where they are
            // found, each on a page of its own; lines that open a page, as
            // everywhere (0.8 audit)
            Column {
                width: parent.width
                LinkRow {
                    visible: !page.isSpace
                    height: Theme.itemSizeMedium
                    icon: "image://theme/icon-m-chat"
                    iconSize: Theme.iconSizeMedium
                    //% "Threads"
                    text: qsTrId("rustrx-bt-threads")
                    onClicked: pageStack.push(Qt.resolvedUrl("ThreadsPage.qml"), { roomId: roomId, roomName: info.name || "" })
                }
                LinkRow {
                    height: Theme.itemSizeMedium
                    icon: "image://theme/icon-m-people"
                    iconSize: Theme.iconSizeMedium
                    enabled: page.membersLoaded
                    //: %n the room's members
                    //% "Members (%n)"
                    text: qsTrId("rustrx-bt-members", page.joinedCount)
                    onClicked: pageStack.push(Qt.resolvedUrl("MembersPage.qml"), { roomId: roomId, roomName: info.name || "", members: page.memberList })
                    // where the arrow goes once they are there
                    BusyIndicator {
                        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                        size: BusyIndicatorSize.ExtraSmall
                        running: !page.membersLoaded
                        visible: running
                    }
                }
            }

            // name and topic: fields for moderators, labels for everyone else.
            // Nothing is sent on its own: the name on Enter, the topic by its
            // Save button, which only appears once the text differs.
            TextField {
                id: nameField
                visible: page.may.name === true && !info.is_dm
                width: parent.width
                label: page.isSpace ? qsTrId("rustrx-la-space-name")
                                    : qsTrId("rustrx-la-room-name")
                placeholderText: label
                text: info.name || ""
                EnterKey.enabled: text.trim().length > 0 && text.trim() !== (info.name || "")
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    rustrx.command({ cmd: "set_room_name", room_id: roomId, name: text.trim() })
                    focus = false
                }
            }

            TextArea {
                id: topicField
                visible: page.may.topic === true && !info.is_dm
                width: parent.width
                //% "Topic"
                label: qsTrId("rustrx-la-topic")
                placeholderText: label
                text: info.topic || ""
            }

            // a button, as on the server access page: a text area has no
            // Enter to give (0.8 audit)
            PageButton {
                visible: page.may.topic === true && !info.is_dm && topicField.text !== (info.topic || "")
                //% "Save topic"
                text: qsTrId("rustrx-bt-save-topic")
                onClicked: {
                    rustrx.command({ cmd: "set_room_topic", room_id: roomId, topic: topicField.text })
                    topicField.focus = false
                }
            }

            Note {
                visible: page.may.topic !== true && text.length > 0
                text: info.topic || ""
            }

            // the room's picture, large, well below the name and topic (the
            // author's wish); for moderators a tap changes it, with a photo
            // taken now (back camera first) or one from the gallery (0.8)
            Item { width: 1; height: Theme.paddingLarge }
            PictureChoice {
                id: roomPictureChoice
                visible: !info.is_dm
                width: parent.width
                enabled: page.may.avatar === true
                //% "Room picture"
                title: qsTrId("rustrx-he-room-picture")
                onPicked: {
                    rustrx.setRoomAvatar(page.roomId, path)
                    //% "Setting the room's picture"
                    Notices.show(qsTrId("rustrx-no-room-picture"), Notice.Short)
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.itemSizeHuge
                    height: width
                    radius: width * app.avatarRadius
                    visible: !roomPicture.visible
                    color: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
                    Label {
                        anchors.centerIn: parent
                        text: info.name ? info.name.charAt(0).toUpperCase() : "?"
                        font.pixelSize: Theme.fontSizeHuge
                        color: Theme.primaryColor
                    }
                }
                Image {
                    id: roomPicture
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.itemSizeHuge
                    height: width
                    visible: status === Image.Ready
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: width
                    sourceSize.height: height
                    // the version makes a changed picture load past Qt's image cache
                    source: info.avatar_path ? "file://" + info.avatar_path + "?v=" + (info.avatar_v || 0) : ""
                    // the same mask as the room list
                    layer.enabled: app.avatarRadius > 0
                    layer.effect: RoundMask { radius: app.avatarRadius }
                }
            }

            // the address, or the room's ID where it has none (the author
            // looked for something to post and found nothing); a tap copies
            // a matrix.to link, which opens in any client (onRoomLink)
            BackgroundItem {
                id: addressItem
                readonly property bool hasAlias: !!(info.alias && info.alias.length)
                visible: hasAlias || !info.is_dm
                width: parent.width
                height: aliasItem.height
                // the core's link names the servers that know the room
                onClicked: rustrx.command({ cmd: "room_link", room_id: page.roomId })
                // at the page's left edge, the label above the address (a
                // DetailItem centred it on the label's column)
                Column {
                    id: aliasItem
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    topPadding: Theme.paddingSmall
                    bottomPadding: Theme.paddingSmall
                    Label {
                        width: parent.width
                        text: addressItem.hasAlias
                              //% "Address"
                              ? qsTrId("rustrx-la-alias")
                              //: a room without an address: its ID stands in
                              //% "Room ID"
                              : qsTrId("rustrx-la-room-id")
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Label {
                        width: parent.width
                        text: addressItem.hasAlias ? info.alias : page.roomId
                        wrapMode: Text.WrapAnywhere
                        color: addressItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }
            // who may join: anyone, people who ask, members of its spaces,
            // or invited people only
            ChoiceBox {
                id: joinBox
                visible: !info.is_dm
                enabled: info.can_join_rules === true
                label: qsTrId("rustrx-la-who-can-join")
                value: info.join_rule || "invite"
                choices: page.rules.map(function (r) {
                    return { value: r, text: app.joinRuleName(r, ""),
                             //% "Only admins can change it."
                             hint: info.can_join_rules === true ? "" : qsTrId("rustrx-la-rule-admins-only") }
                })
                onChosen: {
                    if (info.join_rule && info.can_join_rules === true)
                        rustrx.command({ cmd: "set_join_rule", room_id: roomId, rule: value })
                }
            }
            // people asking to join it
            BannerItem {
                visible: page.knocks > 0
                onClicked: pageStack.push(Qt.resolvedUrl("KnockPage.qml"))
                text: qsTrId("rustrx-la-banner-knocks", page.knocks)
            }
            // who reads what was said before they came; a line for everyone,
            // a choice for those who may
            ChoiceBox {
                id: historyBox
                visible: !info.is_dm && page.settings.history_visibility !== undefined
                enabled: page.may.history === true
                //% "Who can read the history"
                label: qsTrId("rustrx-la-history")
                value: page.settings.history_visibility || "shared"
                readonly property string hint: page.may.history === true
                                               //% "Changes apply to what is said from now on."
                                               ? qsTrId("rustrx-la-history-hint") : qsTrId("rustrx-la-rule-admins-only")
                choices: [
                    //% "Members, all of it"
                    { value: "shared", text: qsTrId("rustrx-me-history-shared"), hint: historyBox.hint },
                    //% "Members, since they were invited"
                    { value: "invited", text: qsTrId("rustrx-me-history-invited"), hint: historyBox.hint },
                    //% "Members, since they joined"
                    { value: "joined", text: qsTrId("rustrx-me-history-joined"), hint: historyBox.hint },
                    //% "Anyone, without joining"
                    { value: "world_readable", text: qsTrId("rustrx-me-history-anyone"), hint: historyBox.hint }
                ]
                onChosen: {
                    if (page.may.history === true && page.settings.history_visibility)
                        rustrx.command({ cmd: "set_history_visibility", room_id: roomId, value: value })
                }
            }
            // guests: only where anyone may join anyway
            TextSwitch {
                visible: page.may.guest === true && info.join_rule === "public"
                //% "Guests may join"
                text: qsTrId("rustrx-la-guests")
                //% "People without an account, through servers that allow guests."
                description: qsTrId("rustrx-la-guests-hint")
                checked: page.settings.guests === true
                automaticCheck: false
                onClicked: rustrx.command({ cmd: "set_guest_access", room_id: roomId, allowed: !checked })
            }
            // the server's public room list (where search finds it)
            TextSwitch {
                visible: page.may.aliases === true && page.settings.directory_public !== null && page.settings.directory_public !== undefined
                //% "Listed in the room directory"
                text: qsTrId("rustrx-la-directory")
                //% "Your server's public list of rooms, where search finds it."
                description: qsTrId("rustrx-la-directory-hint")
                checked: page.settings.directory_public === true
                automaticCheck: false
                onClicked: rustrx.command({ cmd: "set_directory_visibility", room_id: roomId, listed: !checked })
            }
            DetailItem {
                visible: !page.isSpace
                //% "Encryption"
                label: qsTrId("rustrx-la-encryption")
                //% "On"
                value: info.encrypted ? qsTrId("rustrx-la-on")
                //% "Off"
                                      : qsTrId("rustrx-la-off")
            }
            // one way: a room cannot be unencrypted again
            PageButton {
                visible: !page.isSpace && !info.encrypted && page.may.encryption === true
                //% "Turn on encryption"
                text: qsTrId("rustrx-bt-encrypt-room")
                //% "It cannot be turned off again. Bridges, bots and newcomers may not read what was said before."
                hint: qsTrId("rustrx-la-encrypt-room-hint")
                onClicked: {
                    //% "Turning on encryption, for good"
                    encryptRemorse.execute(qsTrId("rustrx-re-encrypt-room"), function () {
                        rustrx.command({ cmd: "enable_encryption", room_id: roomId })
                    })
                }
            }
            RemorsePopup { id: encryptRemorse }
            // the room's ID, which every room has; a tap copies it too
            BackgroundItem {
                id: idRow
                width: parent.width
                height: idLabel.height + 2 * Theme.paddingSmall
                onClicked: {
                    Clipboard.text = roomId
                    //% "Room ID copied"
                    Notices.show(qsTrId("rustrx-no-room-id-copied"), Notice.Short)
                }
                Label {
                    id: idLabel
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    anchors.verticalCenter: parent.verticalCenter
                    text: roomId
                    wrapMode: Text.WrapAnywhere
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: idRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            ChoiceBox {
                id: notifyBox
                visible: !page.isSpace
                //% "Notifications"
                label: qsTrId("rustrx-la-notifications")
                enabled: page.notifyMode.length > 0
                value: page.notifyMode
                choices: [
                    //% "Default"
                    { value: "default", text: qsTrId("rustrx-me-notify-default") },
                    //% "All messages"
                    { value: "all", text: qsTrId("rustrx-me-notify-all") },
                    //% "Mentions only"
                    { value: "mentions", text: qsTrId("rustrx-me-notify-mentions") },
                    //% "Muted"
                    { value: "mute", text: qsTrId("rustrx-me-notify-mute") }
                ]
                onChosen: {
                    if (page.notifyMode.length)
                        rustrx.command({ cmd: "set_notification_mode", room_id: roomId, mode: value })
                }
            }

            TextSwitch {
                visible: !page.isSpace
                // Favs, the one name (decision 1, 0.8 audit)
                //% "In Favs"
                text: qsTrId("rustrx-sw-favourite")
                checked: info.is_fav === true
                automaticCheck: false
                onClicked: rustrx.command({ cmd: "set_favourite", room_id: roomId, value: !checked })
            }
            TextSwitch {
                visible: !info.is_dm && !page.isSpace
                //% "Low priority"
                text: qsTrId("rustrx-sw-low-priority")
                //% "Sorted below the other rooms."
                description: qsTrId("rustrx-la-low-priority-desc")
                checked: info.is_low === true
                automaticCheck: false
                onClicked: rustrx.command({ cmd: "set_low_priority", room_id: roomId, value: !checked })
            }

            // for those who may: pages of their own
            SectionHeader {
                visible: page.may.power_levels === true || page.may.aliases === true || page.may.server_acl === true || page.may.upgrade === true
                //% "Moderation"
                text: qsTrId("rustrx-se-moderation")
            }
            Column {
                width: parent.width
                LinkRow {
                    visible: page.may.power_levels === true
                    //% "Roles and permissions"
                    text: qsTrId("rustrx-bt-permissions")
                    onClicked: pageStack.push(Qt.resolvedUrl("RoomPermissionsPage.qml"), { roomId: roomId, settings: page.settings, members: page.memberList })
                }
                LinkRow {
                    visible: page.may.aliases === true
                    //% "Addresses"
                    text: qsTrId("rustrx-bt-addresses")
                    onClicked: pageStack.push(Qt.resolvedUrl("AddressesPage.qml"), { roomId: roomId, settings: page.settings })
                }
                LinkRow {
                    visible: page.may.server_acl === true
                    //% "Server access"
                    text: qsTrId("rustrx-bt-server-acl")
                    onClicked: pageStack.push(Qt.resolvedUrl("ServerAclPage.qml"), { roomId: roomId, settings: page.settings })
                }
                LinkRow {
                    visible: page.may.upgrade === true && page.settings.versions !== undefined && page.settings.versions.length > 0
                    //% "Upgrade the room"
                    text: qsTrId("rustrx-bt-upgrade")
                    //: %1 the room's version
                    //% "Version %1"
                    description: qsTrId("rustrx-la-room-version").arg(page.settings.version || "?")
                    onClicked: pageStack.push(Qt.resolvedUrl("UpgradeRoomPage.qml"), { roomId: roomId, settings: page.settings })
                }
            }
            Item { width: 1; height: Theme.paddingMedium }
        }

        VerticalScrollDecorator { }
    }

    // invite: a Matrix ID, or someone found while typing: the people of
    // the direct chats first (at once), then the server's user directory
    Component {
        id: inviteDialog
        Dialog {
            id: dialog
            property string term: userField.text.trim().replace(/^@/, "").toLowerCase()
            property var found: []
            property string asked
            canAccept: userField.text.trim().indexOf("@") === 0 && userField.text.indexOf(":") > 1
            onAccepted: rustrx.command({ cmd: "invite", room_id: roomId, user_id: userField.text.trim() })
            function invite(userId) {
                userField.text = userId
                accept()
            }
            // already in the room or invited: not offered again
            function inRoom(userId) {
                for (var i = 0; i < page.memberList.length; i++) {
                    var m = page.memberList[i]
                    if (m.user_id === userId && (!m.membership || m.membership === "join" || m.membership === "invite")) return true
                }
                return false
            }
            // one's other accounts on this phone, as quick choices before
            // anything is typed (the author's ask, 0.8)
            readonly property var ownAccounts: {
                var out = [], all = rustrx.accounts
                for (var i = 0; i < all.length; i++) {
                    var a = all[i]
                    if (a.shown || !a.user_id || a.user_id === rustrx.userId || inRoom(a.user_id)) continue
                    if (term.length && (a.name || "").toLowerCase().indexOf(term) < 0 && a.user_id.toLowerCase().indexOf(term) < 0) continue
                    out.push({ user_id: a.user_id, name: a.name || "" })
                }
                return out
            }
            // the direct chats' people that match, then the directory's
            function contacts() {
                var out = [], seen = {}
                if (term.length === 0) return out
                for (var k = 0; k < ownAccounts.length; k++) seen[ownAccounts[k].user_id] = true
                for (var i = 0; i < rustrx.people.count && out.length < Tunables.inviteContactsMax; i++) {
                    var r = rustrx.people.get(i)
                    if (!r.dm_user || seen[r.dm_user]) continue
                    if ((r.name || "").toLowerCase().indexOf(term) < 0 && r.dm_user.toLowerCase().indexOf(term) < 0) continue
                    seen[r.dm_user] = true
                    out.push({ user_id: r.dm_user, name: r.name || "" })
                }
                for (var j = 0; j < found.length && out.length < Tunables.inviteContactsMax; j++) {
                    if (seen[found[j].user_id]) continue
                    seen[found[j].user_id] = true
                    out.push({ user_id: found[j].user_id, name: found[j].name || "" })
                }
                return out
            }
            Timer {
                id: searchLater
                interval: Tunables.typingDebounceMs
                onTriggered: {
                    if (dialog.term.length < Tunables.minSearchChars) return
                    dialog.asked = dialog.term
                    rustrx.command({ cmd: "search", term: dialog.term })
                }
            }
            Connections {
                target: rustrx
                onSearchResults: if (results.term === dialog.asked) dialog.found = results.users
            }

            SilicaFlickable {
                anchors.fill: parent
                contentHeight: column.height
                Column {
                    id: column
                    width: parent.width
                    DialogHeader {
                        //% "Invite"
                        acceptText: qsTrId("rustrx-dh-invite")
                    }
                    TextField {
                        id: userField
                        width: parent.width
                        //% "Name or Matrix ID, like @name:server"
                        label: qsTrId("rustrx-la-invite-search")
                        placeholderText: label
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        // the @ is there already, see the login page
                        text: "@"
                        onTextChanged: {
                            if (text.indexOf("@@") === 0) text = text.slice(1)
                            dialog.found = []
                            searchLater.restart()
                        }
                        focus: true
                        EnterKey.enabled: dialog.canAccept
                        EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                        EnterKey.onClicked: dialog.accept()
                    }
                    SectionHeader {
                        visible: dialog.ownAccounts.length > 0
                        //% "Your other accounts"
                        text: qsTrId("rustrx-se-invite-own-accounts")
                    }
                    Repeater {
                        model: dialog.ownAccounts
                        BackgroundItem {
                            id: ownRow
                            width: column.width
                            height: Theme.itemSizeMedium
                            onClicked: dialog.invite(modelData.user_id)
                            RowTexts {
                                x: Theme.horizontalPageMargin
                                width: parent.width - 2 * x
                                anchors.verticalCenter: parent.verticalCenter
                                highlighted: ownRow.highlighted
                                text: modelData.name || modelData.user_id
                                description: modelData.name ? modelData.user_id : ""
                            }
                        }
                    }
                    SectionHeader {
                        visible: dialog.ownAccounts.length > 0 && foundList.count > 0
                        //% "People"
                        text: qsTrId("rustrx-se-people")
                    }
                    Repeater {
                        id: foundList
                        model: dialog.contacts()
                        BackgroundItem {
                            id: foundRow
                            width: column.width
                            height: Theme.itemSizeMedium
                            onClicked: dialog.invite(modelData.user_id)
                            RowTexts {
                                x: Theme.horizontalPageMargin
                                width: parent.width - 2 * x
                                anchors.verticalCenter: parent.verticalCenter
                                highlighted: foundRow.highlighted
                                text: modelData.name || modelData.user_id
                                description: modelData.name ? modelData.user_id : ""
                            }
                        }
                    }
                }
                VerticalScrollDecorator { }
            }
        }
    }
}
