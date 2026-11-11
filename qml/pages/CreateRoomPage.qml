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

// A new room: a name, an optional topic, private and encrypted unless
// made public (then with an address, and without encryption by default).
// The page stays until the server has made the room, then becomes it.
Page {
    id: dialog

    property alias name: nameField.text
    property bool waiting

    function create() {
        waiting = true
        rustrx.command({ cmd: "create_room", name: nameField.text.trim(), topic: topicField.text.trim(),
                         "public": publicSwitch.checked, alias: aliasField.text.trim(),
                         encrypted: encryptSwitch.checked })
    }

    Connections {
        target: rustrx
        onJoined: {
            if (!dialog.waiting) return
            dialog.waiting = false
            pageStack.replace(Qt.resolvedUrl("RoomPage.qml"), { roomId: roomId, roomName: nameField.text.trim() })
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
                //% "New room"
                title: qsTrId("rustrx-he-new-room")
            }

            TextField {
                id: nameField
                width: parent.width
                //% "Room name"
                label: qsTrId("rustrx-la-room-name")
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

            TextSwitch {
                id: publicSwitch
                //% "Public"
                text: qsTrId("rustrx-sw-public")
                //% "Anyone can find and join it in the room directory."
                description: qsTrId("rustrx-la-public-hint")
                onCheckedChanged: encryptSwitch.checked = !checked
            }

            TextField {
                id: aliasField
                visible: publicSwitch.checked
                width: parent.width
                //% "Address, like myroom"
                label: qsTrId("rustrx-la-alias-new")
                placeholderText: label
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }

            TextSwitch {
                id: encryptSwitch
                checked: true
                //% "Encrypted"
                text: qsTrId("rustrx-sw-encrypted")
                //% "Cannot be turned off later."
                description: qsTrId("rustrx-la-encrypted-hint")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: nameField.text.trim().length > 0 && !dialog.waiting
                //% "Create"
                text: qsTrId("rustrx-bt-create")
                onClicked: dialog.create()
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: dialog.waiting
                size: BusyIndicatorSize.Medium
            }
        }
    }
}
