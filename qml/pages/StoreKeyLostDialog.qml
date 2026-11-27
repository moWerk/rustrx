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

// Sailfish Secrets no longer has the store's key while stores it encrypted
// are on the phone (audit SEC3). A new key only with the user's yes: "Start
// over" makes one (the old stores kept aside, rustrx.startOverStore), "Not
// now" leaves every account logged out, and the next start asks again.
// One key covers every account's store.
ShortDialog {
    id: dialog
    objectName: "storeKeyLost"

    onAccepted: rustrx.startOverStore()

    //% "Start over"
    acceptText: qsTrId("rustrx-he-store-key-start-over")
    //% "Not now"
    cancelText: qsTrId("rustrx-he-store-key-not-now")

    body: [
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.highlightColor
            //% "Store key missing"
            text: qsTrId("rustrx-he-store-key-missing")
        },
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            //% "Sailfish Secrets no longer has the key to this phone's messages. Start over with a new store? The old one is kept aside, but cannot be read without its key."
            text: qsTrId("rustrx-la-store-key-missing")
        }
    ]
}
