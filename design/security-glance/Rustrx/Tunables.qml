// SPDX-License-Identifier: GPL-3.0-or-later
// The few of Rustrx's tuned numbers the Account glance uses
// (qml/singletons/Tunables.qml in the app).
pragma Singleton
import QtQuick 2.6
QtObject {
    // green only when everything is in place
    readonly property color securityComplete: "#3ecf6e"
    // the ring's width as a share of the page width
    readonly property real securityRingWidth: 0.4
    readonly property int sectionScrollMs: 400
}
