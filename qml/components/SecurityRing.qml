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

// The account's security as a ring (Settings, Account): one arc per
// safeguard, clockwise from 12 o'clock, all green when every one is in
// place; else lit in the ambience's colour when in use, red when missing,
// dim while not known yet. A lock in the middle, a warning sign instead
// when what is stored on the phone is not encrypted.
Item {
    id: ring

    // [{ state: "ok" | "risk" | "unknown", weight: n }, ...]
    property var slices: []
    // true when the stored data is the missing part: no lock then
    property bool storeAtRisk

    readonly property real total: {
        var t = 0
        for (var i = 0; i < slices.length; i++) t += slices[i].weight
        return t || 1
    }
    readonly property int risks: count("risk")
    readonly property int unknowns: count("unknown")
    // every safeguard known and in place: the ring and the lock go green
    readonly property bool complete: slices.length > 0 && risks === 0 && unknowns === 0
    // the gap between two arcs, as a fraction of the circle
    readonly property real gap: 0.012

    function count(state) {
        var n = 0
        for (var i = 0; i < slices.length; i++) if (slices[i].state === state) n++
        return n
    }
    function startOf(i) {
        var s = 0
        for (var k = 0; k < i; k++) s += slices[k].weight
        return s / total
    }

    // ProgressCircleBase, not ProgressCircle: that one swaps its colours
    // whenever the value drops, and a risk appearing is a drop. The base
    // draws one arc from 12 o'clock to value; rotating it places the arc.
    Repeater {
        model: ring.slices
        ProgressCircleBase {
            anchors.fill: parent
            // thin, as Silica's own circles are
            borderWidth: Theme.paddingSmall
            rotation: 360 * (ring.startOf(index) + ring.gap / 2)
            value: modelData.weight / ring.total - ring.gap
            backgroundColor: "transparent"
            progressColor: ring.complete ? Tunables.securityComplete
                         : modelData.state === "ok" ? Theme.highlightColor
                         : modelData.state === "risk" ? Theme.errorColor
                         : Theme.highlightDimmerColor
        }
    }

    Image {
        id: symbol
        anchors.centerIn: parent
        sourceSize.width: Theme.iconSizeLarge
        sourceSize.height: Theme.iconSizeLarge
        source: ring.storeAtRisk
                ? "image://theme/icon-m-warning?" + Theme.errorColor
                : "image://theme/icon-m-device-lock?" + (ring.complete ? Tunables.securityComplete
                                                       : ring.risks > 0 ? Theme.secondaryHighlightColor
                                                       : ring.unknowns === ring.slices.length ? Theme.highlightDimmerColor
                                                       : Theme.highlightColor)
    }
    // the lock with a warning badge: something else is missing
    Image {
        visible: !ring.storeAtRisk && ring.risks > 0
        anchors { horizontalCenter: symbol.right; verticalCenter: symbol.bottom; horizontalCenterOffset: -Theme.paddingSmall; verticalCenterOffset: -Theme.paddingSmall }
        sourceSize.width: Theme.iconSizeSmall
        sourceSize.height: Theme.iconSizeSmall
        source: "image://theme/icon-s-filled-warning?" + Theme.errorColor
    }
}
