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

// Who may do one thing in a room, as a role: members, moderators, admins
// (the levels 0, 50, 100); a level that is none of them shows as itself.
// Only levels up to one's own are offered (the server refuses more). A
// ChoiceBox: it follows `level` and sends chosen(value) with the tapped
// level (0.8 audit; it was one of the hand copies of that fix).
ChoiceBox {
    id: box

    property int level
    property int ownLevel: 100

    readonly property var levels: [0, 50, 100].filter(function (l) { return l <= box.ownLevel || l === box.level })
    readonly property bool custom: levels.indexOf(level) < 0

    function roleName(l) {
        //% "Members"
        if (l <= 0) return qsTrId("rustrx-la-level-members")
        //% "Moderators"
        if (l < 100) return qsTrId("rustrx-la-level-moderators")
        //% "Admins"
        return qsTrId("rustrx-la-level-admins")
    }

    value: level
    choices: levels.map(function (l) { return { value: l, text: box.roleName(l) } })
                   .concat(custom ? [{ value: level, text: qsTrId("rustrx-la-role-level").arg(level) }] : [])
}
