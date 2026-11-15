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

// One of our rooms (or spaces) into a space: a tap adds it and goes back.
Page {
    id: page

    property string spaceId
    property string spaceName
    // the space's rooms now: not offered again
    property var children: []

    function inSpace(id) {
        if (id === spaceId) return true
        for (var i = 0; i < children.length; i++) if (children[i].room_id === id) return true
        return false
    }

    // our rooms and spaces, the space itself and its rooms left out
    property var candidates: {
        var out = [], models = [rustrx.targets, rustrx.spaces]
        for (var m = 0; m < models.length; m++) {
            for (var i = 0; i < models[m].count; i++) {
                var r = models[m].get(i)
                if (!inSpace(r.room_id)) out.push(r)
            }
        }
        return out
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: page.candidates

        header: PageHeader {
            //% "Add to the space"
            title: qsTrId("rustrx-he-space-add")
            description: page.spaceName
        }

        delegate: BackgroundItem {
            width: list.width
            height: Theme.itemSizeMedium
            onClicked: {
                rustrx.command({ cmd: "space_add_child", space_id: page.spaceId, room_id: modelData.room_id })
                pageStack.pop()
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                truncationMode: TruncationMode.Fade
                color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                text: modelData.is_space ? "▣ " + modelData.name : modelData.name
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            //% "No other rooms to add"
            text: qsTrId("rustrx-la-space-add-none")
        }

        VerticalScrollDecorator { }
    }
}
