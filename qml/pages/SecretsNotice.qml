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

// Shown once after login on a phone without a working sailfish-secrets:
// Rustrx then keeps its store unencrypted, and says so plainly, with how
// to change it. Agree means the reader knows and chose; it is not asked
// again (the security ring in Settings keeps the red slice).
Dialog {
    id: dialog
    objectName: "secretsNotice"

    readonly property string installCommand: "devel-su pkcon install sailfishsecretsdaemon sailfishsecretsdaemon-secretsplugins-default sailfishsecretsdaemon-cryptoplugins-default"

    backNavigation: false
    canAccept: understood.checked

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            DialogHeader {
                acceptText: qsTrId("rustrx-he-agree")
                cancelText: ""
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
                //% "Your messages are not encrypted on this phone"
                text: qsTrId("rustrx-he-no-secrets")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "This phone has no working Sailfish Secrets, so Rustrx cannot encrypt what it keeps here: your messages, the people you talk to and the keys to your encrypted chats. They are protected only by the phone itself: whoever gets into its user space (an unlocked phone, a developer login, a copy of its files) can read them."
                text: qsTrId("rustrx-la-no-secrets-what")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "To change it, install Sailfish Secrets in the Terminal:"
                text: qsTrId("rustrx-la-no-secrets-install")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.WrapAnywhere
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: dialog.installCommand
            }

            // a button among texts, with the room every other one has (0.8 audit)
            PageButton {
                //% "Copy the command"
                text: qsTrId("rustrx-bt-copy-command")
                onClicked: {
                    Clipboard.text = dialog.installCommand
                    //% "Copied"
                    Notices.show(qsTrId("rustrx-no-copied"), Notice.Short)
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "Then turn on key backup in Settings, log out of Rustrx and log in again: the new store is encrypted, and the backup brings your encrypted history back."
                text: qsTrId("rustrx-la-no-secrets-then")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "If you leave it like this on purpose, that is fine: Rustrx takes it that you know what you are doing and does not ask again. Settings keep showing it in the security ring."
                text: qsTrId("rustrx-la-no-secrets-choice")
            }

            TextSwitch {
                id: understood
                text: qsTrId("rustrx-la-understood")
            }
        }

        VerticalScrollDecorator { }
    }
}
