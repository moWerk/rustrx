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

// What a space's manager can add, behind one "Add to this space…" in the
// space page's pulley (four at most there, as Silica asks). The page a
// choice opens takes this one's place, so Back is the space.
Page {
    id: page

    property string spaceId
    property string spaceName
    // the space's rooms, for the existing-room picker to leave out
    property var existing: []

    Column {
        width: parent.width

        //% "Add to this space"
        PageHeader { title: qsTrId("rustrx-he-space-add"); description: page.spaceName }

        // lines that open a page, as everywhere (0.8 audit)
        LinkRow {
            //% "Add an existing room"
            text: qsTrId("rustrx-me-space-add")
            onClicked: pageStack.replace(Qt.resolvedUrl("AddToSpacePage.qml"),
                                         { spaceId: page.spaceId, spaceName: page.spaceName, existing: page.existing })
        }
        LinkRow {
            //% "New room in this space"
            text: qsTrId("rustrx-me-space-new-room")
            onClicked: pageStack.replace(Qt.resolvedUrl("CreateRoomPage.qml"),
                                         { parentId: page.spaceId, parentName: page.spaceName })
        }
        LinkRow {
            //% "New subspace"
            text: qsTrId("rustrx-me-space-new-space")
            onClicked: pageStack.replace(Qt.resolvedUrl("CreateRoomPage.qml"),
                                         { space: true, parentId: page.spaceId, parentName: page.spaceName })
        }
    }
}
