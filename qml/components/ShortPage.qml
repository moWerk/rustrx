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

// A page with little on it (a QR code, a line and a button): on a tall
// screen it sat in the top quarter (the author's point, 0.8 audit). The
// header stays at the top, `body` is centred a little below the middle of
// the room left, and `buttons` sit in the lower third. When it does not
// fit (landscape, the keyboard up) everything stacks and scrolls.
//   ShortPage { title: ...; body: [ ... ]; buttons: [ ... ] }
Page {
    id: page

    property alias title: header.title
    property alias description: header.description
    property alias body: bodyColumn.data
    property alias buttons: buttonColumn.data
    property alias flickable: flick
    // the room the keyboard takes at the bottom
    readonly property real keyboard: Qt.inputMethod.visible ? Qt.inputMethod.keyboardRectangle.height : 0

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: Math.max(height - page.keyboard, content.height)

        Column {
            id: content
            width: parent.width

            PageHeader { id: header }

            // what is left below the header, shared out: the body a little
            // below the middle, the buttons in the lower third
            readonly property real free: Math.max(0, flick.height - page.keyboard - header.height
                                                  - bodyColumn.height - buttonColumn.height - Theme.paddingLarge)
            Item { width: 1; height: content.free * 0.4 }
            Column {
                id: bodyColumn
                width: parent.width
                spacing: Theme.paddingLarge
            }
            Item { width: 1; height: content.free * 0.35 }
            Column {
                id: buttonColumn
                width: parent.width
                spacing: Theme.paddingMedium
            }
            Item { width: 1; height: Theme.paddingLarge + content.free * 0.25 }
        }

        VerticalScrollDecorator { }
    }
}
