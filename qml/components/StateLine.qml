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
import Rustrx 1.0

// A state that matters for safety, readable at a glance: a thin circle
// with a check in green when it is good, with a cross in red when not,
// empty while not known yet, in front of the text saying it (the
// Encryption page; the green is the security ring's).
Item {
    id: line

    // "good", "bad" or "" (not known yet); not "state", which is the Item's own
    property string level
    property alias text: label.text

    readonly property color tone: level === "good" ? Tunables.securityComplete
                                : level === "bad" ? Theme.errorColor
                                : Theme.secondaryColor

    width: parent ? parent.width : 0
    height: Math.max(circle.height, label.height)

    Rectangle {
        id: circle
        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.iconSizeMedium
        height: width
        radius: width / 2
        color: "transparent"
        border.color: line.tone
        border.width: Math.max(1, Math.round(Theme.paddingSmall / 2))
        Image {
            anchors.centerIn: parent
            visible: line.level.length > 0
            sourceSize.width: Theme.iconSizeSmallPlus
            sourceSize.height: Theme.iconSizeSmallPlus
            source: (line.level === "good" ? "image://theme/icon-m-acknowledge?" : "image://theme/icon-m-clear?") + line.tone
        }
    }

    Label {
        id: label
        anchors { left: circle.right; leftMargin: Theme.paddingLarge; right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
        wrapMode: Text.Wrap
        color: Theme.highlightColor
        font.pixelSize: Theme.fontSizeSmall
    }
}
