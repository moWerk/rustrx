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

// An account's colour as a small leaf in the Sailfish shape: the top-left
// and bottom-right corners round, the other two square. The merged view
// marks rooms, accounts and new chats with it (app.accountColour).
Canvas {
    id: chip

    property color colour: "transparent"

    width: Theme.paddingLarge
    height: width
    onColourChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        var w = width, h = height, r = Math.min(w, h) * 0.5
        ctx.fillStyle = chip.colour
        ctx.beginPath()
        ctx.moveTo(r, 0)
        ctx.lineTo(w, 0)
        ctx.lineTo(w, h - r)
        ctx.arcTo(w, h, w - r, h, r)
        ctx.lineTo(0, h)
        ctx.lineTo(0, r)
        ctx.arcTo(0, 0, r, 0, r)
        ctx.closePath()
        ctx.fill()
    }
}
