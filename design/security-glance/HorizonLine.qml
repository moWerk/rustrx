// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Rustrx 1.0

// The safeguards as one line of light from edge to edge (Settings,
// Account, the Horizon direction), the way a pulley menu's edge glows:
// one stretch per safeguard, left to right, as long as it weighs. In
// place the stretch is lit and light rises from it; missing it is a red
// dotted trace with no light; not known yet a faint hairline. Under each
// stretch its safeguard's icon, the same one that leads its line in the
// list below.
Item {
    id: horizon

    // [{ status: "ok" | "risk" | "unknown", weight: n, icon: name }, ...]
    property var slices: []
    // every safeguard in place: green
    property bool complete
    // the stretch whose line in the list is being pressed, or -1
    property int touched: -1

    readonly property real total: {
        var t = 0
        for (var i = 0; i < slices.length; i++) t += slices[i].weight
        return t || 1
    }
    readonly property real gap: Theme.paddingMedium
    readonly property real hair: Math.max(1, Math.round(Theme.paddingSmall / 3))
    readonly property real rise: Theme.itemSizeExtraSmall
    readonly property real iconSize: Theme.iconSizeSmallPlus

    function markColor(status) {
        return status === "risk" ? Theme.errorColor
             : status === "unknown" ? Theme.rgba(Theme.highlightColor, Theme.opacityLow)
             : complete ? Tunables.securityComplete : Theme.highlightColor
    }
    function startOf(i) {
        var s = 0
        for (var k = 0; k < i; k++) s += slices[k].weight
        return s / total
    }

    width: parent ? parent.width : 0
    height: rise + Theme.paddingSmall + Theme.paddingMedium + iconSize

    Repeater {
        model: horizon.slices
        Item {
            id: stretch
            readonly property string status: modelData.status
            readonly property color tone: horizon.markColor(status)
            // the outer stretches run off the screen's edges
            x: Math.round(horizon.startOf(index) * horizon.width + (index === 0 ? 0 : horizon.gap / 2))
            width: Math.round(modelData.weight / horizon.total * horizon.width
                              - (index === 0 || index === horizon.slices.length - 1 ? horizon.gap / 2 : horizon.gap))
            height: horizon.height

            // the light rising from a lit stretch
            Rectangle {
                width: parent.width
                height: horizon.rise
                opacity: stretch.status === "ok" ? 1 : horizon.touched === index ? Theme.opacityHigh : 0
                Behavior on opacity { NumberAnimation { duration: 300 } }
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.rgba(stretch.tone, 0) }
                    GradientStop { position: 1; color: Theme.rgba(stretch.tone, Theme.highlightBackgroundOpacity) }
                }
            }
            // in place: the lit line; not known yet: a hairline
            Rectangle {
                id: lineOn
                visible: stretch.status !== "risk"
                y: horizon.rise
                width: parent.width
                height: stretch.status === "ok" ? Theme.paddingSmall / 2 : horizon.hair
                color: stretch.tone
                SequentialAnimation on opacity {
                    running: stretch.status === "unknown"
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    PauseAnimation { duration: 150 * index }
                    NumberAnimation { to: Theme.opacityLow; duration: 700; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutQuad }
                }
            }
            // missing: a dotted trace where the line should be
            Row {
                visible: stretch.status === "risk"
                y: horizon.rise
                spacing: Theme.paddingSmall
                Repeater {
                    model: Math.max(1, Math.floor((stretch.width + Theme.paddingSmall) / (2 * Theme.paddingSmall)))
                    Rectangle { width: Theme.paddingSmall; height: horizon.hair; color: Theme.errorColor }
                }
            }
            ThemeIcon {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
                size: horizon.iconSize
                name: modelData.icon
                color: stretch.tone
            }
        }
    }
}
