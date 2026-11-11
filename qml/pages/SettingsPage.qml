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

// Account, encryption (verification, recovery), devices, about.
Page {
    id: page

    property string displayName
    property string recoveryResult
    property string shownKey

    Component.onCompleted: {
        rustrx.command({ cmd: "profile" })
        rustrx.command({ cmd: "storage_info" })
    }

    function verificationText() {
        switch (rustrx.verificationState) {
        //% "This device is verified."
        case "verified": return qsTrId("rustrx-la-device-verified")
        //% "This device is not verified yet. Verify it from another device you are logged in on, or start the verification here."
        case "unverified": return qsTrId("rustrx-la-device-unverified")
        //% "Verification state not known yet."
        default: return qsTrId("rustrx-la-device-unknown")
        }
    }

    function recoveryText() {
        switch (rustrx.recoveryState) {
        //% "Key backup is on and this device holds the recovery secrets. Older encrypted messages are restored from the backup."
        case "enabled": return qsTrId("rustrx-la-recovery-enabled")
        //% "Key backup exists, but this device lacks the recovery key. Enter it below to read older encrypted messages."
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

            //% "Settings"
            PageHeader { title: qsTrId("rustrx-he-settings") }

            //% "Appearance"
            SectionHeader { text: qsTrId("rustrx-se-appearance") }

            TextSwitch {
                //% "Round avatars"
                text: qsTrId("rustrx-la-round-avatars")
                //% "Cut room and people pictures to a circle."
                description: qsTrId("rustrx-la-round-avatars-hint")
                checked: app.roundAvatars
                onCheckedChanged: app.roundAvatars = checked
            }

            TextSwitch {
                //% "Show who has read a message"
                text: qsTrId("rustrx-la-show-read-by")
                //% "Names appear under the last message each person has read."
                description: qsTrId("rustrx-la-show-read-by-hint")
                checked: app.showReadBy
                onCheckedChanged: app.showReadBy = checked
            }

            TextSwitch {
                //% "Send smaller pictures"
                text: qsTrId("rustrx-la-downscale")
                //% "Photos larger than 2048 pixels are scaled down before sending."
                description: qsTrId("rustrx-la-downscale-hint")
                checked: rustrx.downscaleImages
                onCheckedChanged: rustrx.downscaleImages = checked
            }

            //% "Notifications"
            SectionHeader { text: qsTrId("rustrx-se-notifications") }

            TextSwitch {
                //% "Keep running in the background"
                text: qsTrId("rustrx-la-background")
                //% "Notifications also when Rustrx is closed. It starts with the phone, stays connected and checks for messages every five minutes while the phone sleeps. Quit it from the pulley menu or from its notification."
                description: qsTrId("rustrx-la-background-hint")
                checked: rustrx.background
                automaticCheck: false
                onClicked: rustrx.background = !rustrx.background
            }

            TextSwitch {
                visible: rustrx.background
                //% "Show a notification while it runs"
                text: qsTrId("rustrx-la-background-note")
                //% "A quiet line in the events view while Rustrx is closed: tap to open it, or quit it there."
                description: qsTrId("rustrx-la-background-note-hint")
                checked: app.showBackgroundNote
                automaticCheck: false
                onClicked: app.showBackgroundNote = !app.showBackgroundNote
            }

            //% "Privacy"
            SectionHeader { text: qsTrId("rustrx-se-privacy") }

            TextSwitch {
                //% "Send read receipts"
                text: qsTrId("rustrx-la-send-receipts")
                //% "Others see how far you have read. When off, only your own devices know."
                description: qsTrId("rustrx-la-send-receipts-hint")
                checked: app.sendReceipts
                automaticCheck: false
                onClicked: app.sendReceipts = !app.sendReceipts
            }

            TextSwitch {
                //% "Message text in notifications"
                text: qsTrId("rustrx-la-notify-text")
                //% "When off, a notification only says how many messages arrived."
                description: qsTrId("rustrx-la-notify-text-hint")
                checked: app.notifyText
                automaticCheck: false
                onClicked: app.notifyText = !app.notifyText
            }

            TextSwitch {
                //% "Show that I am typing"
                text: qsTrId("rustrx-la-send-typing")
                checked: app.sendTyping
                automaticCheck: false
                onClicked: app.sendTyping = !app.sendTyping
            }

            //% "Account"
            SectionHeader { text: qsTrId("rustrx-se-account") }

            DetailItem {
                //% "Matrix ID"
                label: qsTrId("rustrx-la-matrix-id")
                value: rustrx.userId
            }
            DetailItem {
                //% "Homeserver"
                label: qsTrId("rustrx-la-homeserver")
                value: rustrx.homeserver
            }

            TextField {
                id: nameField
                width: parent.width
                text: displayName
                //% "Display name"
                label: qsTrId("rustrx-la-display-name")
                placeholderText: label
                EnterKey.enabled: text.trim().length > 0 && text !== displayName
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    rustrx.command({ cmd: "set_display_name", name: text.trim() })
                    displayName = text.trim()
                    focus = false
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Log out"
                text: qsTrId("rustrx-bt-log-out")
                onClicked: {
                    //% "Logging out"
                    remorse.execute(qsTrId("rustrx-re-logging-out"), function () {
                        rustrx.command({ cmd: "logout" })
                    })
                }
            }

            //% "Encryption"
            SectionHeader { text: qsTrId("rustrx-se-encryption") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: verificationText()
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: rustrx.verificationState !== "verified"
                //% "Verify this device"
                text: qsTrId("rustrx-bt-verify-device")
                onClicked: {
                    rustrx.command({ cmd: "verification_request_self" })
                    pageStack.push(Qt.resolvedUrl("VerificationPage.qml"), { waiting: true })
                }
            }

            Item { width: 1; height: Theme.paddingLarge }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: recoveryText()
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
                    //% "Restoring…"
                    recoveryResult = qsTrId("rustrx-la-restoring")
                    rustrx.command({ cmd: "recovery_recover", key: text })
                    focus = false
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: rustrx.recoveryState !== "enabled"
                enabled: keyField.text.trim().length > 0
                //% "Restore encrypted messages"
                text: qsTrId("rustrx-bt-restore")
                onClicked: {
                    recoveryResult = qsTrId("rustrx-la-restoring")
                    rustrx.command({ cmd: "recovery_recover", key: keyField.text })
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: rustrx.recoveryState === "disabled"
                //% "Turn on key backup"
                text: qsTrId("rustrx-bt-enable-backup")
                onClicked: {
                    //% "Setting up the backup…"
                    recoveryResult = qsTrId("rustrx-la-enabling-backup")
                    rustrx.command({ cmd: "recovery_enable" })
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: recoveryResult.length > 0
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: recoveryResult
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: shownKey.length > 0
                wrapMode: Text.WrapAnywhere
                font.family: "monospace"
                color: Theme.primaryColor
                text: shownKey
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        Clipboard.text = shownKey
                        //% "Recovery key copied"
                        Notices.show(qsTrId("rustrx-no-key-copied"))
                    }
                }
            }

            // room keys to and from a file, for another client or a backup of one's own
            BackgroundItem {
                width: parent.width
                onClicked: pageStack.push(Qt.resolvedUrl("KeysPage.qml"))
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    //% "Export or import room keys"
                    text: qsTrId("rustrx-la-keys-file")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }

            //% "Storage"
            SectionHeader { text: qsTrId("rustrx-se-storage") }

            DetailItem {
                //% "Messages and keys"
                label: qsTrId("rustrx-la-storage-store")
                value: storage.store !== undefined ? Format.formatFileSize(storage.store) : "…"
            }
            DetailItem {
                //% "Pictures and files"
                label: qsTrId("rustrx-la-storage-media")
                value: storage.media_files !== undefined
                       ? Format.formatFileSize(storage.media_files + (storage.media_cache || 0)) : "…"
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Clear pictures and files"
                text: qsTrId("rustrx-bt-clear-media")
                onClicked: {
                    var core = rustrx
                    //% "Clearing the cached pictures and files"
                    clearRemorse.execute(qsTrId("rustrx-re-clear-media"), function () { core.command({ cmd: "clear_media" }) })
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //% "They are fetched again when a room shows them."
                text: qsTrId("rustrx-la-clear-media-hint")
            }

            //% "Devices"
            SectionHeader { text: qsTrId("rustrx-se-devices") }

            BackgroundItem {
                width: parent.width
                onClicked: pageStack.push(Qt.resolvedUrl("DevicesPage.qml"))
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    //% "Logged-in devices"
                    text: qsTrId("rustrx-la-devices")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }

            //% "About"
            SectionHeader { text: qsTrId("rustrx-se-about") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                //: %1 app version, %2 core version
                //% "Rustrx %1, core %2. A Matrix client built on matrix-rust-sdk. GPLv3+, github.com/moWerk/rustrx"
                text: qsTrId("rustrx-la-about").arg(appVersion).arg(rustrx.coreVersion)
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator { }
    }

    RemorsePopup { id: remorse }

    property var storage: ({})
    RemorsePopup { id: clearRemorse }

    Connections {
        target: rustrx
        onStorageInfo: page.storage = info
        onProfile: displayName = profile.display_name || ""
        //% "Encrypted messages restored. Rooms fill in as keys arrive."
        onRecoveryDone: { recoveryResult = qsTrId("rustrx-la-restored"); keyField.text = "" }
        onRecoveryKey: {
            //% "Backup is on. Write this recovery key down and keep it safe; it is shown only now. Tap it to copy."
            recoveryResult = qsTrId("rustrx-la-key-shown")
            shownKey = key
        }
        onErrorOccurred: {
            if (cmd === "recovery_recover" || cmd === "recovery_enable" || cmd === "recovery_reset")
                recoveryResult = message
        }
    }
}
