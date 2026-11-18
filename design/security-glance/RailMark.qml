// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Rustrx 1.0

// A safeguard's place on the rail (Settings, Account, the Rail direction):
// a thin line runs down the list through one node per safeguard. In place
// the node is a lit dot and the line to a neighbour in place is lit too;
// missing it is an open red ring and the line stops short of it, so a gap
// in the protection is a gap in the line; not known yet it is a faint,
// breathing ring. It fills its row's height: the rows touch, so the
// pieces join into one line.
Item {
    id: mark

    // "ok", "risk" or "unknown"; "" for no neighbour (first or last row)
    property string status: "unknown"
    property string above: ""
    property string below: ""
    // every safeguard in place: green
    property bool complete
    // where the node sits: the middle of the row's first line
    property real nodeY: height / 2

    readonly property real line: Math.max(1, Math.round(Theme.paddingSmall / 3))
    readonly property real node: Theme.iconSizeExtraSmall
    readonly property color lit: complete ? Tunables.securityComplete : Theme.highlightColor
    readonly property color faint: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)

    function joined(other) { return status === "ok" && other === "ok" }
    function broken(other) { return status === "risk" || other === "risk" }

    Rectangle {
        visible: mark.above.length > 0 && !mark.broken(mark.above)
        anchors.horizontalCenter: parent.horizontalCenter
        width: mark.line
        y: 0
        height: mark.nodeY - mark.node / 2
        color: mark.joined(mark.above) ? mark.lit : mark.faint
    }
    Rectangle {
        visible: mark.below.length > 0 && !mark.broken(mark.below)
        anchors.horizontalCenter: parent.horizontalCenter
        width: mark.line
        y: mark.nodeY + mark.node / 2
        height: mark.height - y
        color: mark.joined(mark.below) ? mark.lit : mark.faint
    }

    // in place: a dot with the light around it
    Rectangle {
        visible: mark.status === "ok"
        x: (mark.width - width) / 2
        y: mark.nodeY - height / 2
        width: mark.node; height: width; radius: width / 2
        color: Theme.rgba(mark.lit, Theme.opacityFaint)
        Rectangle {
            anchors.centerIn: parent
            width: Theme.paddingMedium; height: width; radius: width / 2
            color: mark.lit
        }
    }
    // missing: an open ring, nothing joins it
    Rectangle {
        visible: mark.status === "risk"
        x: (mark.width - width) / 2
        y: mark.nodeY - height / 2
        width: mark.node; height: width; radius: width / 2
        color: "transparent"
        border { width: mark.line; color: Theme.errorColor }
    }
    // not known yet
    Rectangle {
        id: pending
        visible: mark.status === "unknown"
        x: (mark.width - width) / 2
        y: mark.nodeY - height / 2
        width: Theme.paddingMedium; height: width; radius: width / 2
        color: "transparent"
        border { width: mark.line; color: Theme.highlightColor }
        opacity: Theme.opacityLow
        SequentialAnimation on opacity {
            running: pending.visible
            loops: Animation.Infinite
            NumberAnimation { to: Theme.opacityOverlay; duration: 900; easing.type: Easing.InOutQuad }
            NumberAnimation { to: Theme.opacityLow; duration: 900; easing.type: Easing.InOutQuad }
        }
    }
}
