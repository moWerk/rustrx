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

// A QR code as the core sends it (width modules per row, "1" dark): dark
// modules on white with the quiet zone around, as wide as it is given.
Rectangle {
    id: box

    property int modulesWide
    property string modules

    visible: modulesWide > 0
    height: width
    color: "white"
    onModulesChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: parent.width / (box.modulesWide + 8) * 4
        onPaint: {
            var ctx = getContext("2d")
            ctx.fillStyle = "white"
            ctx.fillRect(0, 0, width, height)
            if (!box.modulesWide) return
            var cell = width / box.modulesWide
            ctx.fillStyle = "black"
            for (var y = 0; y < box.modulesWide; y++)
                for (var x = 0; x < box.modulesWide; x++)
                    if (box.modules.charAt(y * box.modulesWide + x) === "1")
                        ctx.fillRect(Math.floor(x * cell), Math.floor(y * cell), Math.ceil(cell), Math.ceil(cell))
        }
    }
}
