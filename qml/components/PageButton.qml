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

// A button on a page among texts: the Silica button, centred, with a
// paragraph's room above and below instead of a line's, so it does not
// sit tight against the hints around it (the author's look, 0.4.9). In a
// Row of buttons (inRow) it is as wide as its button. The room is set here
// and nowhere else (margin).
Item {
    id: box

    property alias text: button.text
    // one of several side by side in a Row
    property bool inRow
    signal clicked()

    // added to the column's spacing on either side
    readonly property real margin: Theme.paddingMedium

    width: inRow || !parent ? button.width : parent.width
    height: button.height + 2 * margin

    Button {
        id: button
        anchors.centerIn: parent
        onClicked: box.clicked()
    }
}
