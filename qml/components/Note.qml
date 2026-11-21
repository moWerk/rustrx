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

// Text on a page in one of a few fixed kinds, so the same kind of text
// looks the same everywhere (69 hand-made labels had 17 size and colour
// combinations; 0.8 audit). At the page margins, wrapping, plain text.
//   intro      what the page is for, at its top
//   paragraph  running text
//   status     how something stands now (a state, a result)
//   footnote   a hint under a control, small, in the ambience colour
//   error      what went wrong, in the error colour
Label {
    property string kind: "paragraph"

    x: Theme.horizontalPageMargin
    width: parent ? parent.width - 2 * x : 0
    visible: text.length > 0
    wrapMode: Text.Wrap
    font.pixelSize: kind === "intro" ? Theme.fontSizeMedium
                  : kind === "footnote" ? Theme.fontSizeExtraSmall
                  : Theme.fontSizeSmall
    color: kind === "error" ? Theme.errorColor
         : kind === "footnote" || kind === "status" ? Theme.secondaryHighlightColor
         : Theme.highlightColor
}
