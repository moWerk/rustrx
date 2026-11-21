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

// A new room or space: a name, an optional topic, who may join (only
// invited people, people who ask and are let in, the members of the space
// it goes into, or anyone), encrypted unless public (a space has nothing to
// encrypt). Made inside a space when parentId is given. The page stays
// until the server has made it, then becomes it.
Page {
    id: dialog

    property alias name: nameField.text
    property bool waiting
    // a space instead of a room
    property bool space
    // the space it goes into
    property string parentId
    property string parentName

    // the choices of "Who can join", as the core names them
    readonly property var rules: parentId.length ? ["invite", "knock", "restricted", "public"] : ["invite", "knock", "public"]
    property string rule: "invite"
    function ruleHint(r) {
        //% "Anyone can ask to join; you or a moderator let them in."
        if (r === "knock") return qsTrId("rustrx-la-rule-knock-hint")
        //% "Members of the space join without asking."
        if (r === "restricted") return qsTrId("rustrx-la-rule-restricted-hint")
        //% "Anyone can find and join it in the room directory."
        if (r === "public") return qsTrId("rustrx-la-public-hint")
        //% "Only people you invite."
        return qsTrId("rustrx-la-rule-invite-hint")
    }

    function create() {
        waiting = true
        rustrx.command({ cmd: "create_room", name: nameField.text.trim(), topic: topicField.text.trim(),
                         "public": rule === "public", alias: aliasField.text.trim(),
                         encrypted: !space && encryptSwitch.checked, space: space,
                         parent: parentId, join_rule: rule })
    }

    Connections {
        target: rustrx
        onJoined: {
            if (!dialog.waiting) return
            dialog.waiting = false
            var url = dialog.space ? Qt.resolvedUrl("SpacePage.qml") : Qt.resolvedUrl("RoomPage.qml")
            var props = dialog.space ? { spaceId: roomId, spaceName: nameField.text.trim() }
                                     : { roomId: roomId, roomName: nameField.text.trim() }
            // made from Add room or contact: the search goes too, so back
            // leads to the list, not to the search (0.8 audit)
            var below = pageStack.previousPage(dialog)
            if (below && below.objectName === "search" && pageStack.previousPage(below))
                pageStack.replaceAbove(pageStack.previousPage(below), url, props)
            else
                pageStack.replace(url, props)
        }
        onErrorOccurred: if (cmd === "create_room") dialog.waiting = false
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: dialog.space
                       //% "New space"
                       ? qsTrId("rustrx-he-new-space")
                       //% "New room"
                       : qsTrId("rustrx-he-new-room")
                //: %1 the space a new room or space goes into
                //% "in %1"
                description: dialog.parentId.length ? qsTrId("rustrx-la-in-space").arg(dialog.parentName) : ""
            }

            TextField {
                id: nameField
                width: parent.width
                label: dialog.space
                       //% "Space name"
                       ? qsTrId("rustrx-la-space-name")
                       //% "Room name"
                       : qsTrId("rustrx-la-room-name")
                placeholderText: label
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: topicField.focus = true
            }

            TextField {
                id: topicField
                width: parent.width
                //% "Topic (optional)"
                label: qsTrId("rustrx-la-topic-optional")
                placeholderText: label
            }

            // the one choice box (0.8 audit), its hint from the choice
            ChoiceBox {
                id: accessBox
                //% "Who can join"
                label: qsTrId("rustrx-la-who-can-join")
                value: dialog.rule
                choices: dialog.rules.map(function (r) {
                    return { value: r, text: app.joinRuleName(r, dialog.parentName), hint: dialog.ruleHint(r) }
                })
                onChosen: {
                    dialog.rule = value
                    encryptSwitch.checked = value !== "public"
                }
            }

            TextField {
                id: aliasField
                visible: dialog.rule === "public"
                width: parent.width
                label: dialog.space
                       //% "Address, like myspace"
                       ? qsTrId("rustrx-la-alias-new-space")
                       //% "Address, like myroom"
                       : qsTrId("rustrx-la-alias-new")
                placeholderText: label
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }

            TextSwitch {
                id: encryptSwitch
                visible: !dialog.space
                checked: true
                //% "Encrypted"
                text: qsTrId("rustrx-sw-encrypted")
                //% "Cannot be turned off later."
                description: qsTrId("rustrx-la-encrypted-hint")
            }

            PageButton {
                enabled: nameField.text.trim().length > 0 && !dialog.waiting
                //% "Create"
                text: qsTrId("rustrx-bt-create")
                onClicked: dialog.create()
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: dialog.waiting
                visible: running
                size: BusyIndicatorSize.Medium
            }
        }
    }
}
