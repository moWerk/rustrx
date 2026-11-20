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

// Shown once after a login made this phone a new device that is not
// verified or lacks the recovery key. Verify opens the encryption settings,
// as the line on top of the room list does; Later leaves only that line.
Dialog {
    id: dialog
    objectName: "verifyNotice"

    acceptDestination: Qt.resolvedUrl("SettingsPage.qml")
    acceptDestinationProperties: { "showEncryption": true }
    acceptDestinationAction: PageStackAction.Replace

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            DialogHeader {
                //% "Verify"
                acceptText: qsTrId("rustrx-he-verify-accept")
                //% "Later"
                cancelText: qsTrId("rustrx-he-later")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
                //% "Verify this phone"
                text: qsTrId("rustrx-he-verify-phone")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "Logging in made this phone a new device of your account. Until it is verified, it cannot read your older encrypted messages, and your contacts see a device they cannot trust yet."
                text: qsTrId("rustrx-la-verify-phone-why")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "Confirm it on another device you are logged in on, or enter your recovery key."
                text: qsTrId("rustrx-la-verify-phone-how")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                //% "Later: the line on top of the room list leads there too."
                text: qsTrId("rustrx-la-verify-phone-later")
            }
        }
    }
}
