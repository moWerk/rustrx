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

// The app-wide error line: like the system's notice, but it looks like an
// error (a warning mark, the error colour at its edge) and wraps instead of
// scrolling, at the top so it covers neither the composer nor the
// keyboard. A tap closes it. Silica's own notice shows plain text in the
// success look, which made a failure read as done (0.8 audit, item 5).
// Declared once, in the window: it turns with the pages itself, as the
// window's content does not.
Item {
    id: root

    function show(text) {
        label.text = text
        box.opacity = 1
        hideLater.restart()
    }

    anchors.centerIn: parent
    width: pageStack.verticalOrientation ? parent.width : parent.height
    height: pageStack.verticalOrientation ? parent.height : parent.width
    rotation: pageStack.currentOrientation === Orientation.Landscape ? 90
            : pageStack.currentOrientation === Orientation.PortraitInverted ? 180
            : pageStack.currentOrientation === Orientation.LandscapeInverted ? 270 : 0
    z: 1000

    Timer { id: hideLater; interval: Tunables.errorNoticeMs; onTriggered: box.opacity = 0 }

    MouseArea {
        id: box
        x: Theme.horizontalPageMargin
        y: Theme.paddingLarge
        width: root.width - 2 * x
        height: Math.max(Theme.itemSizeExtraSmall, label.height + 2 * Theme.paddingMedium)
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { FadeAnimation { } }
        onClicked: { hideLater.stop(); opacity = 0 }

        Rectangle {
            anchors.fill: parent
            radius: Theme.paddingSmall
            color: Theme.rgba(Theme.overlayBackgroundColor, Theme.opacityOverlay)
            border.width: Math.max(1, Math.round(Theme.paddingSmall / 3))
            border.color: Theme.errorColor
        }
        Icon {
            id: mark
            anchors { left: parent.left; leftMargin: Theme.paddingMedium; verticalCenter: parent.verticalCenter }
            source: "image://theme/icon-s-warning"
            color: Theme.errorColor
        }
        Label {
            id: label
            anchors { left: mark.right; leftMargin: Theme.paddingMedium; right: parent.right; rightMargin: Theme.paddingMedium
                      verticalCenter: parent.verticalCenter }
            wrapMode: Text.Wrap
            maximumLineCount: 4
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.primaryColor
        }
    }
}
