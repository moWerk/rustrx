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

// The two text lines of a list row: a title and a smaller second line,
// faded at the end, lit while the row is pressed. Six rows had drifted
// apart in sizes and colours (0.8 audit). Anchor it in the row; set
// `highlighted` from the row.
Column {
    property alias text: first.text
    property alias description: second.text
    property bool highlighted
    // a title in the row's accent (the shown account, a joined room)
    property bool accent

    Label {
        id: first
        width: parent.width
        truncationMode: TruncationMode.Fade
        color: parent.highlighted || parent.accent ? Theme.highlightColor : Theme.primaryColor
    }
    Label {
        id: second
        visible: text.length > 0
        width: parent.width
        truncationMode: TruncationMode.Fade
        font.pixelSize: Theme.fontSizeExtraSmall
        color: parent.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
    }
}
