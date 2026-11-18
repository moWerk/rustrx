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

// One line that leads somewhere, as Silica's settings have them: the text
// (with a smaller second line, if any), an arrow to the right; a small
// icon in front where the line is a warning. Without a target (enabled
// false) the arrow goes and the line is plain text. The Account tab's
// links all look like this: its gaps, its notes, Encryption and Devices.
BackgroundItem {
    id: row

    property alias text: title.text
    property alias description: detail.text
    // an image://theme source without its colour, or ""
    property string icon
    property color iconColor: Theme.primaryColor
    // a note rather than a heading: smaller, in the secondary colour
    property bool secondary

    width: parent ? parent.width : 0
    height: Math.max(Theme.itemSizeSmall, texts.height + 2 * Theme.paddingMedium)

    Image {
        id: lead
        visible: row.icon.length > 0
        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        sourceSize.width: Theme.iconSizeSmall
        sourceSize.height: Theme.iconSizeSmall
        source: row.icon.length ? row.icon + "?" + row.iconColor : ""
    }

    Column {
        id: texts
        anchors {
            left: parent.left
            leftMargin: Theme.horizontalPageMargin + (lead.visible ? Theme.iconSizeSmall + Theme.paddingMedium : 0)
            right: parent.right
            rightMargin: Theme.horizontalPageMargin + (arrow.visible ? arrow.width - Theme.paddingMedium : 0)
            verticalCenter: parent.verticalCenter
        }
        Label {
            id: title
            width: parent.width
            wrapMode: Text.Wrap
            // a heading alone (Encryption) in the list size; a line with a
            // second one (a gap and its fix) or a note smaller
            font.pixelSize: row.secondary || detail.text.length > 0 ? Theme.fontSizeSmall : Theme.fontSizeMedium
            color: row.highlighted ? Theme.highlightColor : row.secondary ? Theme.secondaryColor : Theme.primaryColor
        }
        Label {
            id: detail
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }

    Image {
        id: arrow
        visible: row.enabled
        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium; verticalCenter: parent.verticalCenter }
        source: "image://theme/icon-m-right?" + (row.highlighted ? Theme.highlightColor : Theme.primaryColor)
    }
}
