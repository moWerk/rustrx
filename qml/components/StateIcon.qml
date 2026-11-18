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

// A safety state as Silica's own icons have it: the check in its circle,
// green (the security ring's) when good, the cross in its circle, red,
// when not; nothing while not known. One place for the Encryption page,
// the devices and the ring's legend.
Image {
    id: icon

    // "good", "bad" or "" (not known yet); not "state", which is the Item's own
    property string level
    property real size: Theme.iconSizeMedium

    visible: level.length > 0
    sourceSize.width: size
    sourceSize.height: size
    source: level === "good" ? "image://theme/icon-m-acknowledge?" + Tunables.securityComplete
          : level === "bad" ? "image://theme/icon-m-clear?" + Theme.errorColor
          : ""
}
