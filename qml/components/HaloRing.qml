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

// The security ring as glass and light (Settings, Account, the Halo
// direction): a hairline circle, one arc per safeguard clockwise from 12
// o'clock. An arc in place is lit: a bright line with a soft light around
// it. A missing one stays an unlit red hairline, one not known yet a faint
// one. So lit against unlit tells the state before the colour does, which
// matters where the ambience's highlight colour is close to red.
// With only >= 0 it is a safeguard's key in the list: the same ring,
// small, with just that safeguard's arc drawn.
Item {
    id: ring

    // [{ status: "ok" | "risk" | "unknown", weight: n }, ...]
    property var slices: []
    // every safeguard in place: green
    property bool complete
    // the list's key: draw only this arc, no lock
    property int only: -1
    // the arc whose line in the list is being pressed, or -1
    property int touched: -1

    readonly property bool key: only >= 0
    readonly property real total: {
        var t = 0
        for (var i = 0; i < slices.length; i++) t += slices[i].weight
        return t || 1
    }
    readonly property bool storeAtRisk: slices.length > 0 && slices[0].status === "risk"
    readonly property bool anyRisk: {
        for (var i = 0; i < slices.length; i++) if (slices[i].status === "risk") return true
        return false
    }
    // the gap between two arcs, as a fraction of the circle
    readonly property real gap: key ? 0.06 : 0.014
    readonly property real hair: Math.max(1, Math.round(Theme.paddingSmall / 3))
    // the glass circle: as fine as a line gets when it is a key
    readonly property real base: key ? Math.max(1, Math.round(hair / 2)) : hair
    readonly property real core: key ? Math.round(Theme.paddingSmall * 2 / 3) : Theme.paddingSmall
    readonly property real glow: key ? core : Theme.paddingLarge
    // every line is centred on this radius
    readonly property real radius: (width - glow) / 2

    function arcColor(status) {
        return status === "risk" ? Theme.errorColor
             : status === "unknown" ? Theme.rgba(Theme.highlightColor, Theme.opacityLow)
             : complete ? Tunables.securityComplete : Theme.highlightColor
    }
    function startOf(i) {
        var s = 0
        for (var k = 0; k < i; k++) s += slices[k].weight
        return s / total
    }

    // the glass: a full hairline circle under the arcs
    ProgressCircleBase {
        anchors.centerIn: parent
        width: 2 * ring.radius + ring.base; height: width
        borderWidth: ring.base
        value: 0
        progressColor: "transparent"
        backgroundColor: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
    }

    Repeater {
        model: ring.slices
        Item {
            id: arc
            readonly property bool lit: modelData.status === "ok"
            readonly property bool drawn: !ring.key || ring.only === index
            readonly property real line: lit || ring.key ? ring.core : ring.hair
            anchors.fill: parent
            visible: drawn
            rotation: 360 * (ring.startOf(index) + ring.gap / 2)

            // the light around a lit arc, in two steps; also around the
            // arc whose line is pressed
            Repeater {
                model: ring.key ? 0 : 2
                ProgressCircleBase {
                    readonly property real spread: index === 0 ? ring.glow : ring.glow / 2
                    anchors.centerIn: parent
                    width: 2 * ring.radius + spread; height: width
                    borderWidth: spread
                    value: arc.value
                    backgroundColor: "transparent"
                    progressColor: Theme.rgba(ring.arcColor(arc.status), 0.12)
                    opacity: arc.lit || ring.touched === arc.slot ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }
            }
            readonly property real value: modelData.weight / ring.total - ring.gap
            readonly property string status: modelData.status
            readonly property int slot: index

            ProgressCircleBase {
                id: stroke
                anchors.centerIn: parent
                width: 2 * ring.radius + arc.line; height: width
                borderWidth: arc.line
                value: arc.value
                backgroundColor: "transparent"
                progressColor: ring.arcColor(arc.status)
                SequentialAnimation on opacity {
                    running: arc.status === "unknown" && !ring.key
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    PauseAnimation { duration: 150 * arc.slot }
                    NumberAnimation { to: Theme.opacityLow; duration: 700; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutQuad }
                }
            }
        }
    }

    Image {
        visible: !ring.key
        anchors.centerIn: parent
        sourceSize.width: Theme.iconSizeLarge
        sourceSize.height: Theme.iconSizeLarge
        source: (ring.storeAtRisk ? "image://theme/icon-m-warning?" : "image://theme/icon-m-device-lock?")
                + (ring.storeAtRisk ? Theme.errorColor
                   : ring.complete ? Tunables.securityComplete
                   : ring.anyRisk ? Theme.secondaryHighlightColor
                   : Theme.highlightColor)
    }
}
