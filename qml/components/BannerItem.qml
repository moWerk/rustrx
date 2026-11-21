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

// One tappable line in the highlight colour above a list (requests to
// join, this phone unverified); it takes no space while hidden. A long
// text wraps: "Notifications are off ... Tap to switch them on." lost its
// tap hint to the fade on a phone's width (0.8 audit).
BackgroundItem {
    property alias text: label.text

    width: parent ? parent.width : 0
    height: visible ? Math.max(Theme.itemSizeSmall, label.height + 2 * Theme.paddingMedium) : 0

    Label {
        id: label
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * x
        anchors.verticalCenter: parent.verticalCenter
        wrapMode: Text.Wrap
        maximumLineCount: 3
        truncationMode: TruncationMode.Fade
        color: Theme.highlightColor
        font.pixelSize: Theme.fontSizeSmall
    }
}
