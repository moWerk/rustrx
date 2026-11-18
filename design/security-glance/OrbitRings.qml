// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Rustrx 1.0

// The safeguards as circles around the lock (Settings, Account, the
// Orbits direction): one closed circle per safeguard, the first outermost
// and heaviest, since everything else lies inside what the phone stores.
// In place a circle is closed and lit; missing it is red and open, a way
// in; not known yet a short arc circles it, as a busy indicator does.
// Another safeguard is another circle.
// With only >= 0 it is a safeguard's key in the list: the same circles,
// small and faint, with just that safeguard's one drawn in its state.
Item {
    id: orbits

    // [{ status: "ok" | "risk" | "unknown", weight: n }, ...]
    property var slices: []
    // every safeguard in place: green
    property bool complete
    // the list's key: draw only this circle in its state, no lock
    property int only: -1
    // the circle whose line in the list is being pressed, or -1
    property int touched: -1

    readonly property bool key: only >= 0
    readonly property bool storeAtRisk: slices.length > 0 && slices[0].status === "risk"
    readonly property bool anyRisk: {
        for (var i = 0; i < slices.length; i++) if (slices[i].status === "risk") return true
        return false
    }
    readonly property real hair: Math.max(1, Math.round(Theme.paddingSmall / 3))
    // a circle's faint path: as fine as a line gets when it is a key
    readonly property real path: key ? Math.max(1, Math.round(hair / 2)) : hair
    readonly property real glow: key ? 0 : Theme.paddingMedium
    // the outermost circle's radius, and the innermost's share of it
    // (room for the lock)
    readonly property real outer: (width - Math.max(glow, hair)) / 2
    readonly property real inner: key ? 0.25 : 0.5
    // the opening of a missing circle, as a fraction of it
    readonly property real opening: 0.16

    function radiusOf(i) {
        return slices.length < 2 ? outer : outer * (1 - (1 - inner) * i / (slices.length - 1))
    }
    function lineOf(i, status) {
        if (key) return hair
        return status === "ok" ? (slices[i].weight > 1 ? Theme.paddingSmall : Theme.paddingSmall / 2) : hair
    }
    function markColor(status) {
        return status === "risk" ? Theme.errorColor
             : status === "unknown" ? Theme.rgba(Theme.highlightColor, Theme.opacityLow)
             : complete ? Tunables.securityComplete : Theme.highlightColor
    }

    Repeater {
        model: orbits.slices
        Item {
            id: orbit
            readonly property string status: modelData.status
            readonly property int slot: index
            readonly property bool drawn: !orbits.key || orbits.only === index
            readonly property real r: orbits.radiusOf(index)
            readonly property real line: orbits.lineOf(index, status)
            readonly property color tone: orbits.markColor(status)
            anchors.fill: parent

            // the path: always there, faint
            ProgressCircleBase {
                anchors.centerIn: parent
                width: 2 * orbit.r + orbits.path; height: width
                borderWidth: orbits.path
                value: 0
                progressColor: "transparent"
                backgroundColor: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
            }
            // the light around a circle in place, or the one pressed
            ProgressCircleBase {
                visible: !orbits.key
                anchors.centerIn: parent
                width: 2 * orbit.r + orbits.glow; height: width
                borderWidth: orbits.glow
                value: 1
                backgroundColor: "transparent"
                progressColor: Theme.rgba(orbit.tone, 0.16)
                opacity: orbit.status === "ok" || orbits.touched === index ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 200 } }
            }
            // in place: closed; missing: open, each a little further round
            ProgressCircleBase {
                visible: orbit.drawn && orbit.status !== "unknown"
                anchors.centerIn: parent
                width: 2 * orbit.r + orbit.line; height: width
                borderWidth: orbit.line
                rotation: orbit.status === "risk" ? 360 * (0.08 + 0.19 * index + orbits.opening / 2) : 0
                value: orbit.status === "risk" ? 1 - orbits.opening : 1
                backgroundColor: "transparent"
                progressColor: orbit.tone
            }
            // not known yet: a short arc going round
            ProgressCircleBase {
                id: seeker
                visible: orbit.drawn && orbit.status === "unknown"
                anchors.centerIn: parent
                width: 2 * orbit.r + orbit.line; height: width
                borderWidth: orbit.line
                value: 0.22
                backgroundColor: "transparent"
                progressColor: Theme.highlightColor
                RotationAnimation on rotation {
                    running: seeker.visible && !orbits.key
                    loops: Animation.Infinite
                    from: 90 * orbit.slot; to: 90 * orbit.slot + 360
                    duration: 2400 + 600 * orbit.slot
                }
            }
        }
    }

    ThemeIcon {
        visible: !orbits.key
        anchors.centerIn: parent
        size: Theme.iconSizeMedium
        name: orbits.storeAtRisk ? "icon-m-warning" : "icon-m-device-lock"
        color: orbits.storeAtRisk ? Theme.errorColor
             : orbits.complete ? Tunables.securityComplete
             : orbits.anyRisk ? Theme.secondaryHighlightColor
             : Theme.highlightColor
    }
}
