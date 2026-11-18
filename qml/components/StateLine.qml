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

// A state that matters for safety, readable at a glance: its icon
// (StateIcon: a green check, a red cross) in front of the text saying it,
// on the Encryption page.
Item {
    id: line

    // "good", "bad" or "" (not known yet)
    property alias level: icon.level
    property alias text: label.text

    width: parent ? parent.width : 0
    height: Math.max(Theme.iconSizeMedium, label.height)

    StateIcon {
        id: icon
        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
    }

    Label {
        id: label
        anchors { left: parent.left; leftMargin: Theme.horizontalPageMargin + Theme.iconSizeMedium + Theme.paddingLarge; right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
        wrapMode: Text.Wrap
        color: Theme.highlightColor
        font.pixelSize: Theme.fontSizeSmall
    }
}
