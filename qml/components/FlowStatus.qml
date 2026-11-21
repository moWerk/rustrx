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
import Rustrx 1.0
import Sailfish.Silica 1.0

// Where a flow stands (QR login, Link a new device, verification): the
// large green check when it is done, the line saying the state, a spinner
// while the other side has to act. One look for the three pages (0.8
// audit). The check is the security ring's green; the text stays in the
// highlight colour, green text was hard to read on a light ambience.
Column {
    id: status

    property string text
    // what went wrong, instead of the text, in the error colour
    property string error
    property bool done
    property bool busy

    width: parent ? parent.width : 0
    spacing: Theme.paddingLarge

    Image {
        visible: status.done && !status.error.length
        anchors.horizontalCenter: parent.horizontalCenter
        sourceSize.width: Theme.iconSizeExtraLarge * 1.5
        sourceSize.height: Theme.iconSizeExtraLarge * 1.5
        source: "image://theme/icon-l-acknowledge?" + Tunables.securityComplete
    }

    Note {
        kind: status.error.length ? "error" : "intro"
        horizontalAlignment: Text.AlignHCenter
        text: status.error.length ? status.error : status.text
    }

    BusyIndicator {
        anchors.horizontalCenter: parent.horizontalCenter
        size: BusyIndicatorSize.Medium
        running: status.busy && !status.error.length
        visible: running
    }
}
