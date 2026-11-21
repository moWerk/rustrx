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

// One confirmation for a moderator's act (remove, ban, unban, remove a
// message, report, decline a request to join): what it does, and an
// optional reason that goes along with it (the person, the room or the
// server's admins see it). Accepting sends: the caller connects to
// accepted and reads `reason`.
Dialog {
    id: dialog

    property alias title: header.title
    property alias acceptText: header.acceptText
    // what follows, in a sentence
    property alias text: note.text
    property alias reason: field.text
    //% "Reason (optional)"
    property string reasonLabel: qsTrId("rustrx-la-reason-optional")

    Column {
        width: parent.width
        spacing: Theme.paddingMedium

        DialogHeader { id: header }

        Note {
            id: note
            kind: "intro"
        }

        TextField {
            id: field
            width: parent.width
            label: dialog.reasonLabel
            placeholderText: label
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }
    }
}
