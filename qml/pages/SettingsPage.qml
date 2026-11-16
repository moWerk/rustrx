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
import Sailfish.Pickers 1.0
import Sailfish.Share 1.0
import "../components"

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

    // opened from the main page's line about verification or the recovery
    // key: the encryption section at the top of the screen
    property bool showEncryption
    onStatusChanged: if (status === PageStatus.Active && showEncryption) {
        showEncryption = false
        settingsFlick.contentY = Math.max(0, Math.min(encryptionHeader.y,
                                                       settingsFlick.contentHeight - settingsFlick.height))
    }

    SilicaFlickable {
        id: settingsFlick
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            // room between the buttons and the texts around them
            spacing: Theme.paddingMedium

            //% "Settings"
            PageHeader { title: qsTrId("rustrx-he-settings") }

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
                //% "Show who has read a message"
                text: qsTrId("rustrx-la-show-read-by")
                //% "Names appear under the last message each person has read."
                description: qsTrId("rustrx-la-show-read-by-hint")
                checked: app.showReadBy
                onCheckedChanged: app.showReadBy = checked
            }

            TextSwitch {
                //% "Message text in notifications"
                text: qsTrId("rustrx-la-notify-text")
                //% "When off, a notification only says how many messages arrived."
                description: qsTrId("rustrx-la-notify-text-hint")
                checked: service.notifyText
                automaticCheck: false
                onClicked: service.notifyText = !service.notifyText
            }

            TextSwitch {
                //% "Show others that I am typing"
                text: qsTrId("rustrx-la-send-typing")
                checked: app.sendTyping
                automaticCheck: false
                onClicked: app.sendTyping = !app.sendTyping
            }

            TextSwitch {
                //% "Say so under the message field"
                text: qsTrId("rustrx-la-typing-hint")
                //% "A line under the field reminds you that the room sees you typing."
                description: qsTrId("rustrx-la-typing-hint-hint")
                enabled: app.sendTyping
                checked: app.typingHint
                automaticCheck: false
                onClicked: app.typingHint = !app.typingHint
            }

            //% "Notifications"
            SectionHeader { text: qsTrId("rustrx-se-notifications") }

            // push through a UnifiedPush distributor (Foghorn): notifications
            // with Rustrx closed and nothing kept running; the background
            // mode below is the fallback without one
            TextSwitch {
                enabled: rustrx.push.available
                //% "Push notifications"
                text: qsTrId("rustrx-la-push")
                description: !rustrx.push.available
                             //% "Install a UnifiedPush distributor such as Foghorn (OpenRepos) to get notifications with Rustrx closed, without keeping it running."
                             ? qsTrId("rustrx-la-push-none")
                             : rustrx.push.active
                             //: %1 the push gateway's host
                             //% "On, through %1. The gateway and the push server see when a message comes and for which room, never what it says."
                             ? qsTrId("rustrx-la-push-active").arg(rustrx.push.gatewayHost)
                             : rustrx.push.enabled
                             //: %1 what push is doing (registering, waiting for an address, an error)
                             //% "Setting up: %1"
                             ? qsTrId("rustrx-la-push-setting-up").arg(rustrx.push.status || "…")
                             //% "Off: notifications only while Rustrx runs, or with background mode."
                             : qsTrId("rustrx-la-push-off")
                checked: rustrx.push.available && rustrx.push.enabled
                automaticCheck: false
                onClicked: rustrx.push.enabled = !rustrx.push.enabled
            }

            TextSwitch {
                //% "Keep running in the background"
                text: qsTrId("rustrx-la-background")
                //% "Notifications also when Rustrx is closed. It starts with the phone and stays connected without a window, shown by a line in the events view: tap it to open Rustrx, or quit it there. While the phone sleeps it checks for messages every five minutes."
                description: qsTrId("rustrx-la-background-hint")
                checked: rustrx.background
                automaticCheck: false
                onClicked: rustrx.background = !rustrx.background
            }
            Label {
                visible: rustrx.push.active && rustrx.background
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                //% "While push works, background mode is not needed and does not start; it takes over if push stops."
                text: qsTrId("rustrx-la-background-push")
            }

            TextSwitch {
                enabled: rustrx.background
                //% "Show the background line"
                text: qsTrId("rustrx-la-background-line")
                //% "The line in the events view while Rustrx runs in the background. Without it, Rustrx runs unseen: open it from its icon, and switch background mode off to end it."
                description: qsTrId("rustrx-la-background-line-hint")
                checked: service.showBackgroundLine
                onCheckedChanged: service.showBackgroundLine = checked
            }

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
                //% "Separate people from rooms"
                text: qsTrId("rustrx-la-separate-lists")
                //% "Rooms and people as two tabs. When off, they share one list; the filter menu sorts and searches it."
                description: qsTrId("rustrx-la-separate-lists-hint")
                checked: app.separateLists
                onCheckedChanged: app.separateLists = checked
            }

            TextSwitch {
                //% "Separate favourites"
                text: qsTrId("rustrx-la-separate-favs")
                //% "The favourites as a tab of their own. When off, “Favourites on top” in the filter menu keeps them at hand."
                description: qsTrId("rustrx-la-separate-favs-hint")
                checked: app.separateFavs
                onCheckedChanged: app.separateFavs = checked
            }

            TextSwitch {
                //% "Spaces"
                text: qsTrId("rustrx-la-separate-spaces")
                //% "The spaces you are in (groups of rooms) as a tab: their rooms, joining them, asking to join, and requests to join them."
                description: qsTrId("rustrx-la-separate-spaces-hint")
                checked: app.separateSpaces
                onCheckedChanged: app.separateSpaces = checked
            }

            ComboBox {
                width: parent.width
                //% "Message preview"
                label: qsTrId("rustrx-la-preview-lines")
                //% "Lines of the last message under a room's name."
                description: qsTrId("rustrx-la-preview-lines-hint")
                currentIndex: Math.max(0, Math.min(2, app.previewLines - 1))
                menu: ContextMenu {
                    //% "1 line"
                    MenuItem { text: qsTrId("rustrx-me-lines-1") }
                    //% "2 lines"
                    MenuItem { text: qsTrId("rustrx-me-lines-2") }
                    //% "3 lines"
                    MenuItem { text: qsTrId("rustrx-me-lines-3") }
                }
                onCurrentIndexChanged: app.previewLines = currentIndex + 1
            }

            TextSwitch {
                //% "Reopen the last chat"
                text: qsTrId("rustrx-la-reopen-last")
                //% "Rustrx starts in the room or chat that was open when it was closed."
                description: qsTrId("rustrx-la-reopen-last-hint")
                checked: app.reopenLast
                onCheckedChanged: app.reopenLast = checked
            }

            TextSwitch {
                //% "Send smaller pictures"
                text: qsTrId("rustrx-la-downscale")
                //% "Photos larger than 2048 pixels are scaled down before sending."
                description: qsTrId("rustrx-la-downscale-hint")
                checked: rustrx.downscaleImages
                onCheckedChanged: rustrx.downscaleImages = checked
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

            // one's own picture; a tap picks a new one
            BackgroundItem {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.itemSizeHuge
                height: width
                onClicked: pageStack.push(ownPicturePicker)
                Rectangle {
                    anchors.fill: parent
                    radius: app.roundAvatars ? width / 2 : 0
                    visible: !ownPicture.visible
                    color: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
                    Label {
                        anchors.centerIn: parent
                        text: (displayName || rustrx.userId.slice(1) || "?").charAt(0).toUpperCase()
                        font.pixelSize: Theme.fontSizeHuge
                        color: Theme.primaryColor
                    }
                }
                Image {
                    id: ownPicture
                    anchors.fill: parent
                    visible: status === Image.Ready
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: width
                    sourceSize.height: height
                    // the version makes a changed picture load past Qt's image cache
                    source: ownAvatar.length ? "file://" + ownAvatar + "?v=" + ownAvatarVersion : ""
                    layer.enabled: app.roundAvatars
                    layer.effect: ShaderEffect {
                        fragmentShader: "
                            varying highp vec2 qt_TexCoord0;
                            uniform sampler2D source;
                            uniform lowp float qt_Opacity;
                            void main() {
                                highp float d = length(qt_TexCoord0 - vec2(0.5));
                                lowp float a = 1.0 - smoothstep(0.49, 0.5, d);
                                gl_FragColor = texture2D(source, qt_TexCoord0) * a * qt_Opacity;
                            }"
                    }
                }
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

            SectionHeader {
                id: encryptionHeader
                //% "Encryption"
                text: qsTrId("rustrx-se-encryption")
            }

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

            // no other device and no recovery key: start over
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: rustrx.recoveryState === "incomplete" && rustrx.verificationState !== "verified"
                //% "Reset encryption"
                text: qsTrId("rustrx-bt-reset-identity")
                onClicked: {
                    var core = rustrx
                    //% "Resetting the encryption"
                    resetRemorse.execute(qsTrId("rustrx-re-reset-identity"), function () {
                        //% "Resetting… the server may ask you to approve it in the browser."
                        recoveryResult = qsTrId("rustrx-la-resetting")
                        core.command({ cmd: "identity_reset" })
                    })
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: rustrx.recoveryState === "incomplete" && rustrx.verificationState !== "verified"
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //% "No other device and no recovery key? A reset makes new encryption keys and a new backup with a new recovery key. Older encrypted messages stay unreadable, and your other devices have to be verified again."
                text: qsTrId("rustrx-la-reset-identity-hint")
            }
            // the approval page, if the browser did not open by itself
            Row {
                visible: resetApprovalUrl.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingLarge
                Button {
                    //% "Open approval"
                    text: qsTrId("rustrx-bt-open-approval")
                    onClicked: Qt.openUrlExternally(resetApprovalUrl)
                }
                Button {
                    text: qsTrId("rustrx-bt-sso-copy")
                    onClicked: {
                        Clipboard.text = resetApprovalUrl
                        Notices.show(qsTrId("rustrx-no-link-copied"), Notice.Short)
                    }
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

            DeviceList { width: parent.width }

            //% "Debug log"
            SectionHeader { text: qsTrId("rustrx-se-debug-log") }

            TextSwitch {
                //% "Keep a debug log"
                text: qsTrId("rustrx-la-debug-log")
                //% "Rustrx notes what it does in a small file on the phone. If something goes wrong, share it with the developer, so nobody has to make it happen again. It holds room and user IDs, never message texts or passwords."
                description: qsTrId("rustrx-la-debug-log-hint")
                checked: rustrx.debugLog
                automaticCheck: false
                onClicked: rustrx.debugLog = !rustrx.debugLog
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Share the debug log"
                text: qsTrId("rustrx-bt-share-log")
                onClicked: {
                    var path = rustrx.shareableLog()
                    if (path === "")
                        return
                    logShare.resources = ["file://" + path]
                    logShare.trigger()
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //% "A copy goes to Downloads, so you can also attach it to a mail or a forum post."
                text: qsTrId("rustrx-la-share-log-hint")
            }
            ShareAction {
                id: logShare
                mimeType: "text/plain"
                //% "Rustrx debug log"
                title: qsTrId("rustrx-he-share-log")
            }

            //% "About"
            SectionHeader { text: qsTrId("rustrx-se-about") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                linkColor: Theme.highlightColor
                textFormat: Text.StyledText
                onLinkActivated: Qt.openUrlExternally(link)
                //: %1 app version, %2 core version; lines separated by <br>, keep the link
                //% "Rustrx %1, core %2<br>A Matrix client built on <a href='https://github.com/matrix-org/matrix-rust-sdk'>matrix-rust-sdk</a><br>GPLv3+, from moWerk with love<br><a href='https://github.com/moWerk/rustrx'>github.com/moWerk/rustrx</a>"
                text: qsTrId("rustrx-la-about-lines").arg(appVersion).arg(rustrx.coreVersion)
            }

            // the emoji pictures' attribution (CC-BY 4.0 asks for it)
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                linkColor: Theme.highlightColor
                textFormat: Text.StyledText
                onLinkActivated: Qt.openUrlExternally(link)
                //: attribution of the emoji pictures; keep the links and names as they are
                //% "Emoji: <a href='https://github.com/jdecked/twemoji'>Twemoji</a> by Twitter, Inc. and other contributors and Jason Sofonia &amp; Justine De Caires, <a href='https://creativecommons.org/licenses/by/4.0/'>CC-BY 4.0</a>"
                text: qsTrId("rustrx-la-about-twemoji")
            }

            // how it was made, graded by LLMGD (SPEC section 3: understood and
            // tested by the author, not read line by line, no outside review: A3;
            // the author directs and decides the design, the LLM writes: origin O1)
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                linkColor: Theme.highlightColor
                textFormat: Text.StyledText
                onLinkActivated: Qt.openUrlExternally(link)
                //: the LLM disclosure grade; keep the link and the grade as they are
                //% "<a href='https://github.com/moWerk/llmgd-specs'>LLMGD-3</a> · origin O1: written by an LLM, directed, understood and tested on the phone by the author; not yet reviewed by others"
                text: qsTrId("rustrx-la-about-llmgd")
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator { }
    }

    property string ownAvatar
    property real ownAvatarVersion

    Component {
        id: ownPicturePicker
        ImagePickerPage {
            //% "Your picture"
            title: qsTrId("rustrx-he-own-picture")
            onSelectedContentPropertiesChanged: {
                if (!selectedContentProperties.filePath) return
                rustrx.setOwnAvatar(selectedContentProperties.filePath)
                //% "Setting your picture"
                Notices.show(qsTrId("rustrx-no-own-picture"), Notice.Short)
            }
        }
    }

    RemorsePopup { id: remorse }
    RemorsePopup { id: resetRemorse }
    property string resetApprovalUrl

    property var storage: ({})
    RemorsePopup { id: clearRemorse }

    Connections {
        target: rustrx
        onStorageInfo: page.storage = info
        onProfile: {
            displayName = profile.display_name || ""
            ownAvatar = profile.avatar || ""
            ownAvatarVersion = profile.avatar_v || 0
        }
        //% "Encrypted messages restored. Rooms fill in as keys arrive."
        onRecoveryDone: { recoveryResult = qsTrId("rustrx-la-restored"); keyField.text = "" }
        onIdentityResetApproval: {
            resetApprovalUrl = url
            //% "Approve the reset on your server's page in the browser (within two minutes). If it did not open, use the buttons above."
            recoveryResult = qsTrId("rustrx-la-reset-approve")
        }
        onRecoveryKey: {
            resetApprovalUrl = ""
            //% "Backup is on. Write this recovery key down and keep it safe; it is shown only now. Tap it to copy."
            recoveryResult = qsTrId("rustrx-la-key-shown")
            shownKey = key
        }
        onErrorOccurred: {
            if (cmd === "recovery_recover" || cmd === "recovery_enable" || cmd === "recovery_reset" || cmd === "identity_reset")
                recoveryResult = message
        }
    }
}
