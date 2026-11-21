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
import Rustrx 1.0
import Sailfish.Silica 1.0

// The QR code of a login flow with its minute: "Valid for N s", then
// expired. QrLoginPage and LinkDevicePage had a copy each (0.8 audit).
// The code is as large as the page allows: in landscape it fit only after
// a scroll, so its size comes from the page's shorter side, less room for
// the header and the line above it.
//   QrPanel { page: page; onExpiredChanged: ... }  then show(width, modules)
Column {
    id: panel

    // the page it is on, for its size
    property Item page
    property int modulesWide
    property string modules
    // the minute (Tunables.qrValidSeconds), counted from show()
    property int secondsLeft
    property real shownAt
    readonly property bool expired: shownAt > 0 && secondsLeft === 0

    function show(modulesWide, modules) {
        panel.modulesWide = modulesWide
        panel.modules = modules
        panel.shownAt = Date.now()
        panel.secondsLeft = Tunables.qrValidSeconds
    }

    width: parent ? parent.width : 0
    spacing: Theme.paddingMedium
    visible: modulesWide > 0

    Timer {
        running: panel.visible && panel.secondsLeft > 0
        interval: 1000
        repeat: true
        onTriggered: panel.secondsLeft = Math.max(0, Tunables.qrValidSeconds - Math.floor((Date.now() - panel.shownAt) / 1000))
    }

    QrCode {
        anchors.horizontalCenter: parent.horizontalCenter
        width: {
            var w = panel.width - 2 * Theme.horizontalPageMargin
            if (!panel.page) return w
            // the header, the line above and the countdown below stay in view
            return Math.max(Theme.itemSizeHuge, Math.min(w, panel.page.height - Theme.itemSizeLarge
                                                         - 2 * Theme.itemSizeSmall - countdown.height))
        }
        modulesWide: panel.modulesWide
        modules: panel.modules
        // a code past its minute cannot be used any more
        opacity: panel.expired ? Theme.opacityLow : 1
    }

    Label {
        id: countdown
        anchors.horizontalCenter: parent.horizontalCenter
        color: panel.expired ? Theme.errorColor : Theme.secondaryHighlightColor
        font.pixelSize: Theme.fontSizeSmall
        text: !panel.expired
              //% "Valid for %1 s"
              ? qsTrId("rustrx-la-qr-valid").arg(panel.secondsLeft)
              //% "This code has expired."
              : qsTrId("rustrx-la-qr-expired")
    }
}
