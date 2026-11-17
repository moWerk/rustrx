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
    readonly property bool canEdit: ownPower >= Tunables.powerModerator
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
    }

    // the header lays out after creation (the topic wraps, the fields size),
    // which moves the list's origin and leaves the view scrolled: re-anchor
    Timer { id: settle; interval: Tunables.listSettleMs; running: true; onTriggered: list.positionViewAtBeginning() }

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
                    ownPower = p === "infinite" ? Tunables.powerAdmin : (p || 0)
                }
            }
        }
        onErrorOccurred: if (cmd === "set_room_name" || cmd === "set_room_topic") refresh()
        onNotificationMode: if (roomId === page.roomId) page.notifyMode = mode
    }

    // the room list carries the name and topic; pick changes up
    Connections {
        target: rustrx.rooms
        onDataChanged: refresh()
    }
    Connections { target: rustrx.spaces; onDataChanged: refresh() }
    Connections { target: rustrx.people; onDataChanged: refresh() }

    SilicaListView {
        id: list
        anchors.fill: parent
        // the settings only; the members have a page of their own
        model: []

        currentIndex: -1   // or the view re-anchors to the first member when the list arrives

        PullDownMenu {
            MenuItem {
                visible: !page.isSpace
                //: experimental: a call through Element Call
                //% "Call (test)"
                text: qsTrId("rustrx-me-call-test")
                onClicked: rustrx.command({ cmd: "call_start", room_id: roomId })
            }
            MenuItem {
                visible: canEdit && !info.is_dm
                //% "Change the room's picture"
                text: qsTrId("rustrx-me-room-picture")
                onClicked: pageStack.push(roomPicturePicker)
            }
            MenuItem {
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

        header: Column {
            width: list.width

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
                    { key: "members", text: qsTrId("rustrx-bt-members", page.memberList.length), icon: "icon-m-people", show: true }
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
                visible: canEdit && !info.is_dm
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
                visible: canEdit && !info.is_dm
                width: parent.width
                //% "Topic"
                label: qsTrId("rustrx-la-topic")
                placeholderText: label
                text: info.topic || ""
            }

            BackgroundItem {
                visible: canEdit && !info.is_dm && topicField.text !== (info.topic || "")
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
                visible: !canEdit && !!(info.topic && info.topic.length)
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
                enabled: canEdit
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

            DetailItem {
                visible: !!(info.alias && info.alias.length)
                //% "Address"
                label: qsTrId("rustrx-la-alias")
                value: info.alias || ""
            }
            // who may join: anyone, people who ask, members of its spaces,
            // or invited people only
            ComboBox {
                visible: !info.is_dm
                enabled: info.can_join_rules === true
                width: parent.width
                label: qsTrId("rustrx-la-who-can-join")
                description: info.can_join_rules === true ? ""
                             //% "Only admins can change it."
                             : qsTrId("rustrx-la-rule-admins-only")
                currentIndex: Math.max(0, page.rules.indexOf(info.join_rule || "invite"))
                menu: ContextMenu {
                    Repeater {
                        model: page.rules
                        MenuItem { text: app.joinRuleName(modelData, "") }
                    }
                }
                onCurrentIndexChanged: {
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
            DetailItem {
                visible: !page.isSpace
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

            ComboBox {
                visible: !page.isSpace
                //% "Notifications"
                label: qsTrId("rustrx-la-notifications")
                enabled: page.notifyMode.length > 0
                readonly property var modes: ["default", "all", "mentions", "mute"]
                currentIndex: Math.max(0, modes.indexOf(page.notifyMode))
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
