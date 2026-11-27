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
import "../components"

// The UnifiedPush distributor push went through is gone and another one is
// there (audit 0.8.4 IPC3): it gets Rustrx's push token only with a yes.
// "Not now" keeps the old one and asks again at the next start. Laid out
// as the verify offer (VerifyNotice).
ShortDialog {
    id: dialog
    objectName: "distributorDialog"

    // bus names, org.unifiedpush.Distributor.<name>
    property string offered
    property string before
    function shortName(bus) { return bus.substring(bus.lastIndexOf(".") + 1) }

    onAccepted: {
        rustrx.push.useDistributor(offered)
        app.applyClosedMode()
    }
    onRejected: rustrx.push.declineDistributor()

    //% "Use it"
    acceptText: qsTrId("rustrx-he-distributor-use")
    //% "Not now"
    cancelText: qsTrId("rustrx-he-distributor-not-now")

    body: [
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.highlightColor
            //: %1 the new UnifiedPush distributor's name (Foghorn, say)
            //% "Use %1 for push?"
            text: qsTrId("rustrx-he-distributor-new").arg(dialog.shortName(dialog.offered))
        },
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            //: %1 the new distributor, %2 the one push went through before
            //% "Push went through %2 until now. %1 is new on this phone."
            text: qsTrId("rustrx-la-distributor-new").arg(dialog.shortName(dialog.offered)).arg(dialog.shortName(dialog.before))
        }
    ]
}
