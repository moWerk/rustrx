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
// Only levels up to one's own are offered (the server refuses more).
HoldComboBox {
    id: box

    property int level
    property int ownLevel: 100
    signal chosen(int level)

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

    width: parent ? parent.width : 0
    currentIndex: custom ? levels.length : Math.max(0, levels.indexOf(level))
    menu: ContextMenu {
        Repeater {
            model: box.levels
            MenuItem { text: box.roleName(modelData) }
        }
        MenuItem {
            visible: box.custom
            text: qsTrId("rustrx-la-role-level").arg(box.level)
        }
    }
    onCurrentIndexChanged: {
        if (currentIndex >= levels.length) return
        var l = levels[currentIndex]
        if (l !== level) chosen(l)
    }
}
