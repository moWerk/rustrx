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

// One's own picture as the avatar corners setting draws it, or the name's
// first letter (Settings: the account and the corners preview).
Item {
    id: avatar

    property string path
    property real version
    property string name
    property int letterSize: Theme.fontSizeLarge

    Rectangle {
        anchors.fill: parent
        radius: width * app.avatarRadius
        visible: !picture.visible
        color: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
        Label {
            anchors.centerIn: parent
            text: (avatar.name || rustrx.userId.slice(1) || "?").charAt(0).toUpperCase()
            font.pixelSize: avatar.letterSize
            color: Theme.primaryColor
        }
    }
    Image {
        id: picture
        anchors.fill: parent
        visible: status === Image.Ready
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: width
        sourceSize.height: height
        // the version makes a changed picture load past Qt's image cache
        source: avatar.path.length ? "file://" + avatar.path + "?v=" + avatar.version : ""
        layer.enabled: app.avatarRadius > 0
        layer.effect: RoundMask { radius: app.avatarRadius }
    }
}
