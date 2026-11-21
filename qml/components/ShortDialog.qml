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

// ShortPage's layout for a Dialog: the header (accept and cancel) at the
// top, `body` centred a little below the middle of the room left, so a
// few lines do not sit in the top quarter of a tall screen (0.8 audit).
// It stacks and scrolls when it does not fit.
Dialog {
    id: dialog

    property alias acceptText: header.acceptText
    property alias cancelText: header.cancelText
    property alias title: header.title
    property alias body: bodyColumn.data

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: Math.max(height, content.height)

        Column {
            id: content
            width: parent.width

            DialogHeader { id: header }

            readonly property real free: Math.max(0, flick.height - header.height - bodyColumn.height - Theme.paddingLarge)
            Item { width: 1; height: content.free * 0.4 }
            Column {
                id: bodyColumn
                width: parent.width
                spacing: Theme.paddingLarge
            }
            Item { width: 1; height: Theme.paddingLarge + content.free * 0.6 }
        }

        VerticalScrollDecorator { }
    }
}
