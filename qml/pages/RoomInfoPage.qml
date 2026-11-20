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
import Sailfish.Pickers 1.0
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
        onErrorOccurred: {
            if (cmd === "set_room_name" || cmd === "set_room_topic") refresh()
            // a refused change: show the settings as they are
            if (["set_history_visibility", "set_guest_access", "enable_encryption", "set_directory_visibility"].indexOf(cmd) >= 0)
                rustrx.command({ cmd: "room_settings", room_id: page.roomId })
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
                onClicked: pageStack.push(roomPicturePicker)
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
                    var id = roomId, core = rustrx
                    leaveRemorse.execute(qsTrId("rustrx-re-leaving"), function () {
                        core.command({ cmd: "space_leave", space_id: id, with_rooms: true })
                        pageStack.pop(pageStack.find(function (p) { return p.objectName === "main" }))
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
                    var id = roomId, core = rustrx
                    leaveRemorse.execute(qsTrId("rustrx-re-leaving"), function () {
                        core.command({ cmd: "leave_room", room_id: id })
                        pageStack.pop(pageStack.find(function (p) { return p.objectName === "main" }))
                    })
                }
            }
        }

        Component {
            id: roomPicturePicker
            ImagePickerPage {
                //% "Room picture"
                title: qsTrId("rustrx-he-room-picture")
                onSelectedContentPropertiesChanged: {
                    if (!selectedContentProperties.filePath) return
                    rustrx.setRoomAvatar(roomId, selectedContentProperties.filePath)
                    //% "Setting the room's picture"
                    Notices.show(qsTrId("rustrx-no-room-picture"), Notice.Short)
                }
            }
        }

        RemorsePopup { id: leaveRemorse }

        Column {
            id: column
            width: flick.width

            PageHeader {
                title: info.name || ""
                description: info.is_dm ? "" : page.isSpace
                             //% "Space"
                             ? qsTrId("rustrx-he-space")
                             //% "Room"
                             : qsTrId("rustrx-he-room")
            }

            // the room's threads and members, close to the top where they are
            // found, each on a page of its own
            Item { width: 1; height: Theme.paddingMedium }
            Repeater {
                model: [
                    //% "Threads"
                    { key: "threads", text: qsTrId("rustrx-bt-threads"), icon: "icon-m-menu", show: !page.isSpace },
                    //: %n the room's members
                    //% "Members (%n)"
                    { key: "members", text: qsTrId("rustrx-bt-members", page.memberList.filter(function (m) { return !m.membership || m.membership === "join" }).length), icon: "icon-m-people", show: true }
                ]
                BackgroundItem {
                    id: entry
                    visible: modelData.show
                    width: parent.width
                    height: visible ? Theme.itemSizeMedium : 0
                    enabled: modelData.key !== "members" || page.membersLoaded
                    onClicked: {
                        if (modelData.key === "threads")
                            pageStack.push(Qt.resolvedUrl("ThreadsPage.qml"), { roomId: roomId, roomName: info.name || "" })
                        else
                            pageStack.push(Qt.resolvedUrl("MembersPage.qml"), { roomId: roomId, roomName: info.name || "", members: page.memberList })
                    }
                    Row {
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.paddingLarge
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            sourceSize.width: Theme.iconSizeMedium
                            sourceSize.height: Theme.iconSizeMedium
                            source: "image://theme/" + modelData.icon + "?" + (entry.highlighted ? Theme.highlightColor : Theme.primaryColor)
                            opacity: entry.enabled ? 1 : Theme.opacityLow
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.text
                            color: entry.highlighted ? Theme.highlightColor : Theme.primaryColor
                            opacity: entry.enabled ? 1 : Theme.opacityLow
                        }
                    }
                    BusyIndicator {
                        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                        size: BusyIndicatorSize.ExtraSmall
                        running: modelData.key === "members" && !page.membersLoaded
                    }
                }
            }
            Item { width: 1; height: Theme.paddingMedium }

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

            BackgroundItem {
                visible: page.may.topic === true && !info.is_dm && topicField.text !== (info.topic || "")
                width: parent.width
                height: Theme.itemSizeSmall
                onClicked: {
                    rustrx.command({ cmd: "set_room_topic", room_id: roomId, topic: topicField.text })
                    topicField.focus = false
                }
                Label {
                    anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                    //% "Save topic"
                    text: qsTrId("rustrx-bt-save-topic")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }

            Label {
                visible: page.may.topic !== true && !!(info.topic && info.topic.length)
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: info.topic || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }

            // the room's picture, large; for moderators a tap changes it
            Item { width: 1; height: Theme.paddingMedium }
            BackgroundItem {
                visible: !info.is_dm
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.itemSizeHuge
                height: width
                enabled: page.may.avatar === true
                onClicked: pageStack.push(roomPicturePicker)
                Rectangle {
                    anchors.fill: parent
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
                    anchors.fill: parent
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

            Item { width: 1; height: Theme.paddingMedium }

            // the address, a tap copies it, for sharing the room
            BackgroundItem {
                visible: !!(info.alias && info.alias.length)
                width: parent.width
                height: aliasItem.height
                onClicked: {
                    Clipboard.text = info.alias
                    //% "Address copied"
                    Notices.show(qsTrId("rustrx-no-address-copied"), Notice.Short)
                }
                DetailItem {
                    id: aliasItem
                    width: parent.width
                    //% "Address"
                    label: qsTrId("rustrx-la-alias")
                    value: info.alias || ""
                }
            }
            // who may join: anyone, people who ask, members of its spaces,
            // or invited people only
            HoldComboBox {
                visible: !info.is_dm
                enabled: info.can_join_rules === true
                width: parent.width
                label: qsTrId("rustrx-la-who-can-join")
                description: info.can_join_rules === true ? ""
                             //% "Only admins can change it."
                             : qsTrId("rustrx-la-rule-admins-only")
                // the box follows the room (the ComboBox sets currentIndex itself,
                // which broke a plain binding: a refused or remote change never showed)
                readonly property int shownIndex: Math.max(0, page.rules.indexOf(info.join_rule || "invite"))
                currentIndex: shownIndex
                onShownIndexChanged: currentIndex = shownIndex
                menu: ContextMenu {
                    Repeater {
                        model: page.rules
                        MenuItem { text: app.joinRuleName(modelData, "") }
                    }
                }
                onCurrentIndexChanged: {
                    if (currentIndex < 0) return
                    var r = page.rules[currentIndex]
                    if (info.join_rule && r !== info.join_rule && info.can_join_rules === true)
                        rustrx.command({ cmd: "set_join_rule", room_id: roomId, rule: r })
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
            HoldComboBox {
                id: historyBox
                visible: !info.is_dm && page.settings.history_visibility !== undefined
                enabled: page.may.history === true
                width: parent.width
                readonly property var values: ["shared", "invited", "joined", "world_readable"]
                //% "Who can read the history"
                label: qsTrId("rustrx-la-history")
                description: page.may.history === true
                             //% "Changes apply to what is said from now on."
                             ? qsTrId("rustrx-la-history-hint") : qsTrId("rustrx-la-rule-admins-only")
                readonly property int shownIndex: Math.max(0, values.indexOf(page.settings.history_visibility || "shared"))
                currentIndex: shownIndex
                onShownIndexChanged: currentIndex = shownIndex
                menu: ContextMenu {
                    //% "Members, all of it"
                    MenuItem { text: qsTrId("rustrx-me-history-shared") }
                    //% "Members, since they were invited"
                    MenuItem { text: qsTrId("rustrx-me-history-invited") }
                    //% "Members, since they joined"
                    MenuItem { text: qsTrId("rustrx-me-history-joined") }
                    //% "Anyone, without joining"
                    MenuItem { text: qsTrId("rustrx-me-history-anyone") }
                }
                onCurrentIndexChanged: {
                    if (currentIndex < 0) return
                    var v = values[currentIndex]
                    if (page.may.history === true && page.settings.history_visibility && v !== page.settings.history_visibility)
                        rustrx.command({ cmd: "set_history_visibility", room_id: roomId, value: v })
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
                onClicked: {
                    //% "Turning on encryption, for good"
                    encryptRemorse.execute(qsTrId("rustrx-re-encrypt-room"), function () {
                        rustrx.command({ cmd: "enable_encryption", room_id: roomId })
                    })
                }
            }
            Label {
                visible: !page.isSpace && !info.encrypted && page.may.encryption === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //% "It cannot be turned off again. Bridges, bots and newcomers may not read what was said before."
                text: qsTrId("rustrx-la-encrypt-room-hint")
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

            HoldComboBox {
                visible: !page.isSpace
                //% "Notifications"
                label: qsTrId("rustrx-la-notifications")
                enabled: page.notifyMode.length > 0
                readonly property var modes: ["default", "all", "mentions", "mute"]
                readonly property int shownIndex: Math.max(0, modes.indexOf(page.notifyMode))
                currentIndex: shownIndex
                onShownIndexChanged: currentIndex = shownIndex
                menu: ContextMenu {
                    //% "Default"
                    MenuItem { text: qsTrId("rustrx-me-notify-default") }
                    //% "All messages"
                    MenuItem { text: qsTrId("rustrx-me-notify-all") }
                    //% "Mentions only"
                    MenuItem { text: qsTrId("rustrx-me-notify-mentions") }
                    //% "Muted"
                    MenuItem { text: qsTrId("rustrx-me-notify-mute") }
                }
                onCurrentIndexChanged: {
                    if (currentIndex < 0) return
                    var m = modes[currentIndex]
                    if (page.notifyMode.length && m !== page.notifyMode)
                        rustrx.command({ cmd: "set_notification_mode", room_id: roomId, mode: m })
                }
            }

            TextSwitch {
                visible: !page.isSpace
                //% "Favourite"
                text: qsTrId("rustrx-sw-favourite")
                checked: info.is_fav === true
                automaticCheck: false
                onClicked: rustrx.command({ cmd: "set_favourite", room_id: roomId, value: !checked })
            }
            TextSwitch {
                visible: !info.is_dm && !page.isSpace
                //% "Low priority"
                text: qsTrId("rustrx-sw-low-priority")
                //% "Sorted below the other rooms"
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
            // the direct chats' people that match, then the directory's
            function contacts() {
                var out = [], seen = {}
                if (term.length === 0) return out
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
                    Repeater {
                        model: dialog.contacts()
                        BackgroundItem {
                            width: column.width
                            height: Theme.itemSizeMedium
                            onClicked: dialog.invite(modelData.user_id)
                            Column {
                                x: Theme.horizontalPageMargin
                                width: parent.width - 2 * x
                                anchors.verticalCenter: parent.verticalCenter
                                Label {
                                    width: parent.width
                                    truncationMode: TruncationMode.Fade
                                    color: highlighted ? Theme.highlightColor : Theme.primaryColor
                                    text: modelData.name || modelData.user_id
                                }
                                Label {
                                    visible: !!modelData.name
                                    width: parent.width
                                    truncationMode: TruncationMode.Fade
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    color: highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                                    text: modelData.user_id
                                }
                            }
                        }
                    }
                }
                VerticalScrollDecorator { }
            }
        }
    }
}
