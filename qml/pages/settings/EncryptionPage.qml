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

// Settings, Account, Encryption: verifying this phone, the recovery key and
// key backup, a reset, and room keys to and from a file. Rare, stateful and
// partly dangerous, so on a page of their own. The results live on the
// Settings page (`settings`): a new recovery key is shown only once.
Page {
    id: page

    property Item settings

    function verificationText() {
        switch (rustrx.verificationState) {
        // "this phone" for one's own, "device" for the others (0.8 audit)
        //% "This phone is verified."
        case "verified": return qsTrId("rustrx-la-device-verified")
        //% "This phone is not verified yet. Verify it from another device you are logged in on, or start the verification here."
        case "unverified": return qsTrId("rustrx-la-device-unverified")
        //% "Verification state not known yet."
        default: return qsTrId("rustrx-la-device-unknown")
        }
    }

    function recoveryText() {
        switch (rustrx.recoveryState) {
        //% "Key backup is on and this phone holds the recovery secrets. Older encrypted messages are restored from the backup."
        case "enabled": return qsTrId("rustrx-la-recovery-enabled")
        //% "Key backup exists, but this phone lacks the recovery key. Enter it below to read older encrypted messages."
        case "incomplete": return qsTrId("rustrx-la-recovery-incomplete")
        //% "Key backup is off. Turn it on to keep encrypted messages readable after a new login."
        case "disabled": return qsTrId("rustrx-la-recovery-disabled")
        //% "Backup state not known yet."
        default: return qsTrId("rustrx-la-recovery-unknown")
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            // room between the buttons and the texts around them
            spacing: Theme.paddingMedium

            PageHeader { title: qsTrId("rustrx-se-encryption") }

            StateLine {
                level: rustrx.verificationState === "verified" ? "good"
                     : rustrx.verificationState === "unverified" ? "bad" : ""
                text: page.verificationText()
            }

            PageButton {
                visible: rustrx.verificationState !== "verified"
                //% "Verify this phone"
                text: qsTrId("rustrx-bt-verify-device")
                onClicked: {
                    rustrx.command({ cmd: "verification_request_self" })
                    pageStack.push(Qt.resolvedUrl("../VerificationPage.qml"), { waiting: true })
                }
            }

            Item { width: 1; height: Theme.paddingLarge }

            StateLine {
                level: rustrx.recoveryState === "enabled" ? "good"
                     : rustrx.recoveryState === "incomplete" || rustrx.recoveryState === "disabled" ? "bad" : ""
                text: page.recoveryText()
            }

            TextField {
                id: keyField
                width: parent.width
                visible: rustrx.recoveryState !== "enabled"
                //% "Recovery key"
                label: qsTrId("rustrx-la-recovery-key")
                //% "Recovery key, like EsTc ... (or your passphrase)"
                placeholderText: qsTrId("rustrx-ph-recovery-key")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                EnterKey.enabled: text.trim().length > 0
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    settings.recoveryError = ""
                    //% "Restoring…"
                    settings.recoveryResult = qsTrId("rustrx-la-restoring")
                    rustrx.command({ cmd: "recovery_recover", recovery_key: text })
                    focus = false
                }
            }

            PageButton {
                visible: rustrx.recoveryState !== "enabled"
                enabled: keyField.text.trim().length > 0
                //% "Restore encrypted messages"
                text: qsTrId("rustrx-bt-restore")
                onClicked: {
                    settings.recoveryError = ""
                    settings.recoveryResult = qsTrId("rustrx-la-restoring")
                    rustrx.command({ cmd: "recovery_recover", recovery_key: keyField.text })
                }
            }

            // no other device and no recovery key: start over
            PageButton {
                visible: rustrx.recoveryState === "incomplete" && rustrx.verificationState !== "verified"
                //% "Reset encryption"
                text: qsTrId("rustrx-bt-reset-identity")
                //% "No other device and no recovery key? A reset makes new encryption keys and a new backup with a new recovery key. Older encrypted messages stay unreadable, and your other devices have to be verified again."
                hint: qsTrId("rustrx-la-reset-identity-hint")
                onClicked: {
                    var core = rustrx
                    var shell = settings
                    //% "Resetting the encryption"
                    remorse.execute(qsTrId("rustrx-re-reset-identity"), function () {
                        shell.recoveryError = ""
                        //% "Resetting… the server may ask you to approve it in the browser."
                        shell.recoveryResult = qsTrId("rustrx-la-resetting")
                        core.command({ cmd: "identity_reset" })
                    })
                }
            }
            // the approval page, if the browser did not open by itself; side
            // by side where they fit, stacked for a long translation (0.8 audit)
            ButtonLayout {
                width: parent.width
                visible: settings.resetApprovalUrl.length > 0
                Button {
                    //% "Open approval"
                    text: qsTrId("rustrx-bt-open-approval")
                    onClicked: app.openExternal(settings.resetApprovalUrl)
                }
                Button {
                    text: qsTrId("rustrx-bt-sso-copy")
                    onClicked: {
                        Clipboard.text = settings.resetApprovalUrl
                        Notices.show(qsTrId("rustrx-no-link-copied"), Notice.Short)
                    }
                }
            }

            PageButton {
                visible: rustrx.recoveryState === "disabled"
                //% "Turn on key backup"
                text: qsTrId("rustrx-bt-enable-backup")
                onClicked: {
                    settings.recoveryError = ""
                    //% "Setting up the backup…"
                    settings.recoveryResult = qsTrId("rustrx-la-enabling-backup")
                    rustrx.command({ cmd: "recovery_enable" })
                }
            }

            // the old key cannot be shown again; a new one can be made
            PageButton {
                visible: rustrx.recoveryState === "enabled"
                //% "New recovery key"
                text: qsTrId("rustrx-bt-new-recovery-key")
                //% "Lost the recovery key? A new one replaces it. The backup and your messages stay, only the old key stops working."
                hint: qsTrId("rustrx-la-new-recovery-key-hint")
                onClicked: {
                    var core = rustrx
                    var shell = settings
                    //% "Making a new recovery key"
                    remorse.execute(qsTrId("rustrx-re-new-recovery-key"), function () {
                        shell.recoveryError = ""
                        //% "Making a new recovery key…"
                        shell.recoveryResult = qsTrId("rustrx-la-new-recovery-key")
                        core.command({ cmd: "recovery_reset" })
                    })
                }
            }
            // how it went; a failure in the error colour, not the colour of
            // success (0.8 audit)
            Note {
                kind: "status"
                text: settings.recoveryResult
            }
            Note {
                kind: "error"
                text: settings.recoveryError
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: settings.shownKey.length > 0
                wrapMode: Text.WrapAnywhere
                font.family: "monospace"
                color: Theme.primaryColor
                text: settings.shownKey
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        app.copySecret(settings.shownKey)
                        //% "Recovery key copied. It leaves the clipboard in two minutes."
                        Notices.show(qsTrId("rustrx-no-key-copied"))
                    }
                }
            }

            Item { width: 1; height: Theme.paddingLarge }

            // room keys to and from a file, for another client or a backup of one's own
            LinkRow {
                //% "Export or import room keys"
                text: qsTrId("rustrx-la-keys-file")
                onClicked: pageStack.push(Qt.resolvedUrl("../KeysPage.qml"))
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator { }
    }

    // the reset and the new key; on this page, where they are asked for
    RemorsePopup { id: remorse }

    Connections {
        target: rustrx
        onRecoveryDone: keyField.text = ""
    }
}
