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
import "../../components"

// Logging out without the recovery key on this phone: the store goes, and
// with it the keys of the encrypted rooms (a five-second undo was all that
// stood between a tap and that; found by the 0.8 audit). Accepting logs
// out anyway; "Set up the recovery key" leaves with wantsRecovery, and the
// settings open the Encryption page.
Dialog {
    id: dialog

    // the account, named so the right one is logged out (several accounts)
    property string account
    property bool wantsRecovery

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            DialogHeader {
                //% "Log out anyway"
                acceptText: qsTrId("rustrx-bt-log-out-anyway")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
                text: dialog.account
            }

            Note {
                kind: "intro"
                //% "This phone has no recovery key for this account. After logging out, the messages in encrypted rooms cannot be read here again, and if no other device of yours stays logged in, they cannot be read anywhere."
                text: qsTrId("rustrx-la-logout-no-recovery")
            }

            PageButton {
                //% "Set up the recovery key"
                text: qsTrId("rustrx-bt-logout-set-up-recovery")
                onClicked: {
                    dialog.wantsRecovery = true
                    dialog.reject()
                }
            }
        }

        VerticalScrollDecorator { }
    }
}
