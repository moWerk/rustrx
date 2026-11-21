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
    // a line under the button that says what it does, in the ambience
    // colour (the author's choice): one item, so the button and its note
    // can no longer show apart (0.8 audit)
    property alias hint: hintLabel.text
    // the button itself, for enabled and the like
    property alias button: button
    signal clicked()

    // added to the column's spacing on either side
    readonly property real margin: Theme.paddingMedium

    width: inRow || !parent ? button.width : parent.width
    height: button.height + 2 * margin + (hintLabel.visible ? hintLabel.height + Theme.paddingSmall : 0)

    Button {
        id: button
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: box.margin }
        onClicked: box.clicked()
    }
    Label {
        id: hintLabel
        visible: text.length > 0
        anchors { top: button.bottom; topMargin: Theme.paddingSmall; horizontalCenter: parent.horizontalCenter }
        width: Math.min(implicitWidth, (box.parent ? box.parent.width : box.width) - 2 * Theme.horizontalPageMargin)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryHighlightColor
    }
}
