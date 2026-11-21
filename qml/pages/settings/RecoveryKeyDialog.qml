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

// A new recovery key, shown once: it stood last on the Encryption page,
// copy only, gone with the settings, and nothing asked whether it was kept
// (0.8 audit). Here it is the whole page, and the page closes only after
// "I have saved it" (no back swipe before).
Dialog {
    id: dialog

    property string key

    canAccept: saved.checked
    backNavigation: saved.checked

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            DialogHeader {
                //% "Your recovery key"
                title: qsTrId("rustrx-he-recovery-key")
                acceptText: qsTrId("rustrx-bt-done")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "With this key, any new device of yours reads your encrypted messages. It is shown only now: write it down or keep it in a password manager, somewhere other than this phone."
                text: qsTrId("rustrx-la-recovery-key-intro")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.WrapAnywhere
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.primaryColor
                text: dialog.key
            }

            PageButton {
                //% "Copy"
                text: qsTrId("rustrx-bt-copy-key")
                onClicked: {
                    app.copySecret(dialog.key)
                    Notices.show(qsTrId("rustrx-no-key-copied"))
                }
            }

            TextSwitch {
                id: saved
                //% "I have saved it"
                text: qsTrId("rustrx-la-recovery-key-saved")
            }
        }

        VerticalScrollDecorator { }
    }
}
