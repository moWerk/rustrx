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

// Asks before a direct chat is started: starting one invites the person
// and shows them one's address, so neither a link nor a tap in a search
// does it by itself (0.8 audit). The caller starts the chat on accepted.
Dialog {
    id: dialog

    property string userId
    // their display name, if known
    property string name

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            //% "Start chat"
            acceptText: qsTrId("rustrx-he-start-chat")
        }
        Note {
            //: %1 a person's name, or their Matrix id (@name:server)
            //% "Start a direct chat with %1? They are invited and see your Matrix address."
            text: qsTrId("rustrx-la-start-chat-ask").arg(dialog.name.length && dialog.name !== dialog.userId
                                                         ? dialog.name : dialog.userId)
        }
        // the id under a name: two people may share a name, never an id
        Note {
            kind: "status"
            text: dialog.name.length && dialog.name !== dialog.userId ? dialog.userId : ""
        }
    }
}
