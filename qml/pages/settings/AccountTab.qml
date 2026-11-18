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
import Sailfish.Share 1.0
import "../../components"

// Settings, Account: who one is, how safe the account is (a ring, and every
// missing safeguard in words with its fix), the ways to the encryption and
// the devices, and the debug log. Log out is in the pulley menu.
SilicaFlickable {
    id: tab

    property Item settings

    contentHeight: column.height

    // whether what Rustrx stores on the phone is encrypted: "encrypted",
    // "unencrypted", "no_secrets", or "" while not known
    readonly property string storeState: rustrx.storeState !== undefined ? rustrx.storeState : ""

    // own devices besides this one that have keys and are not verified;
    // -1 while not known (or from a core that does not say)
    property int unverifiedDevices: -1
    Component.onCompleted: rustrx.command({ cmd: "devices" })
    Connections {
        target: rustrx
        onDevices: {
            var told = false, n = 0
            for (var i = 0; i < devices.length; i++) {
                var d = devices[i]
                if (d.keys !== undefined) told = true
                // a login without encryption keys has nothing to verify
                if (!d.current && d.keys === true && d.verified === false) n++
            }
            tab.unverifiedDevices = told ? n : -1
        }
    }

    // each safeguard: "ok", "risk", or "unknown" (never a risk)
    readonly property string storeSlice: storeState === "encrypted" ? "ok"
                                       : storeState === "unencrypted" || storeState === "no_secrets" ? "risk" : "unknown"
    readonly property string verifySlice: rustrx.verificationState === "verified" ? "ok"
                                        : rustrx.verificationState === "unverified" ? "risk" : "unknown"
    readonly property string recoverySlice: rustrx.recoveryState === "enabled" ? "ok"
                                          : rustrx.recoveryState === "disabled" || rustrx.recoveryState === "incomplete" ? "risk" : "unknown"
    readonly property string devicesSlice: unverifiedDevices < 0 ? "unknown" : unverifiedDevices > 0 ? "risk" : "ok"

    // what is missing, in the order to fix it: the backup before a new
    // login (logging out wipes the store), then verifying
    readonly property var risks: {
        var r = []
        if (rustrx.recoveryState === "disabled")
            r.push({ go: "encryption",
                     //% "There is no key backup. If you lose this phone, your encrypted messages are gone."
                     text: qsTrId("rustrx-la-risk-no-backup"),
                     //% "Tap to turn on key backup."
                     fix: qsTrId("rustrx-la-risk-no-backup-fix") })
        if (rustrx.recoveryState === "incomplete")
            r.push({ go: "encryption",
                     //% "A key backup exists, but this phone does not have its recovery key."
                     text: qsTrId("rustrx-la-risk-key-missing"),
                     //% "Tap to enter the recovery key."
                     fix: qsTrId("rustrx-la-risk-key-missing-fix") })
        if (storeState === "unencrypted")
            r.push({ go: "encryption",
                     //% "Your messages on this phone are stored without encryption, because this store was made before Rustrx used Sailfish Secrets."
                     text: qsTrId("rustrx-la-risk-store-unencrypted"),
                     //% "Turn on key backup first, then log out and log in again: the new store is encrypted."
                     fix: qsTrId("rustrx-la-risk-store-unencrypted-fix") })
        if (storeState === "no_secrets")
            r.push({ go: "",
                     //% "This phone has no working Sailfish Secrets, so Rustrx cannot encrypt what it stores here."
                     text: qsTrId("rustrx-la-risk-no-secrets"),
                     //% "Nothing Rustrx can do on this phone."
                     fix: qsTrId("rustrx-la-risk-no-secrets-fix") })
        if (rustrx.verificationState === "unverified")
            r.push({ go: "verify",
                     //% "This phone is not verified, so others cannot tell it is really you, and it may miss keys for older messages."
                     text: qsTrId("rustrx-la-risk-unverified"),
                     //% "Tap to verify it with another device of yours."
                     fix: qsTrId("rustrx-la-risk-unverified-fix") })
        if (unverifiedDevices > 0)
            r.push({ go: "devices",
                     //: %n other devices of the user without verification
                     //% "%n of your other devices are not verified. Anyone holding one could read along."
                     text: qsTrId("rustrx-la-risk-devices", unverifiedDevices),
                     //% "Tap to see them: verify each from that device, or remove it."
                     fix: qsTrId("rustrx-la-risk-devices-fix") })
        return r
    }

    // facts worth knowing that are no risk: each comes from a default the
    // author chose, and has its switch
    readonly property var notes: {
        var n = []
        if (rustrx.debugLog)
            //% "The debug log is on. It holds room and user IDs, never message texts or passwords."
            n.push({ go: "", text: qsTrId("rustrx-la-note-debug-log") })
        if (app.sendReceipts)
            //% "Others see how far you have read."
            n.push({ go: "privacy", text: qsTrId("rustrx-la-note-receipts") })
        if (rustrx.push.active)
            //: %1 the push gateway's host
            //% "%1 sees when a message comes and for which room, never what it says."
            n.push({ go: "app", text: qsTrId("rustrx-la-note-push").arg(rustrx.push.gatewayHost) })
        if (settings && settings.storage.media_files > 0)
            //% "Pictures and files you opened are kept unencrypted. Clear them in the App tab."
            n.push({ go: "app", text: qsTrId("rustrx-la-note-media") })
        return n
    }

    function follow(go) {
        if (go === "encryption") settings.openEncryption()
        else if (go === "devices") pageStack.push(Qt.resolvedUrl("DevicesPage.qml"))
        else if (go === "verify") {
            rustrx.command({ cmd: "verification_request_self" })
            pageStack.push(Qt.resolvedUrl("../VerificationPage.qml"), { waiting: true })
        } else if (go.length) settings.showTab(go)
    }

    PullDownMenu {
        MenuItem {
            //% "Log out"
            text: qsTrId("rustrx-bt-log-out")
            onClicked: settings.logOut()
        }
    }

    Column {
        id: column
        width: parent.width
        spacing: Theme.paddingMedium

        // the tab bar is drawn over a tab with a pulley menu; content starts below it
        Item { width: 1; height: (tab.pullDownMenu ? settings.tabBarHeight : 0) + Theme.paddingLarge }

        // one's own picture; a tap picks a new one
        BackgroundItem {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.itemSizeHuge
            height: width
            onClicked: settings.pickPicture()
            OwnAvatar {
                anchors.fill: parent
                path: settings.ownAvatar
                version: settings.ownAvatarVersion
                name: settings.displayName
                letterSize: Theme.fontSizeHuge
            }
        }

        TextField {
            width: parent.width
            text: settings.displayName
            horizontalAlignment: TextInput.AlignHCenter
            //% "Display name"
            label: qsTrId("rustrx-la-display-name")
            placeholderText: label
            EnterKey.enabled: text.trim().length > 0 && text !== settings.displayName
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: {
                rustrx.command({ cmd: "set_display_name", name: text.trim() })
                settings.displayName = text.trim()
                focus = false
            }
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            text: rustrx.userId + "\n" + rustrx.homeserver
        }

        Item { width: 1; height: Theme.paddingLarge }

        SecurityRing {
            id: securityRing
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(Theme.itemSizeHuge * 1.5, tab.width * Tunables.securityRingWidth)
            height: width
            storeAtRisk: tab.storeSlice === "risk"
            // the store weighs two: it is the only data at rest on the phone
            slices: [
                { state: tab.storeSlice, weight: 2 },
                { state: tab.verifySlice, weight: 1 },
                { state: tab.recoverySlice, weight: 1 },
                { state: tab.devicesSlice, weight: 1 }
            ]
        }

        // the ring's arcs in words, in the ring's order (clockwise from 12
        // o'clock, so top down here): green in place, red missing
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            Repeater {
                model: [
                    //% "Encrypted on the phone"
                    { slice: tab.storeSlice, ok: qsTrId("rustrx-la-arc-store-ok"),
                      //% "Stored unencrypted"
                      risk: qsTrId("rustrx-la-arc-store-risk") },
                    //% "This phone verified"
                    { slice: tab.verifySlice, ok: qsTrId("rustrx-la-arc-verified-ok"),
                      //% "This phone not verified"
                      risk: qsTrId("rustrx-la-arc-verified-risk") },
                    //% "Key backup"
                    { slice: tab.recoverySlice, ok: qsTrId("rustrx-la-arc-backup-ok"),
                      risk: rustrx.recoveryState === "incomplete"
                            //% "Recovery key missing"
                            ? qsTrId("rustrx-la-arc-backup-key-missing")
                            //% "No key backup"
                            : qsTrId("rustrx-la-arc-backup-risk") },
                    //% "Other devices verified"
                    { slice: tab.devicesSlice, ok: qsTrId("rustrx-la-arc-devices-ok"),
                      //% "Unverified devices"
                      risk: qsTrId("rustrx-la-arc-devices-risk") }
                ]
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    font.pixelSize: Theme.fontSizeSmall
                    text: modelData.slice === "risk" ? modelData.risk : modelData.ok
                    color: modelData.slice === "ok" ? Tunables.securityComplete
                         : modelData.slice === "risk" ? Theme.errorColor
                         : Theme.highlightDimmerColor
                }
            }
        }

        Item { width: 1; height: Theme.paddingSmall }

        Label {
            visible: tab.risks.length === 0
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            // green with the ring when all is in place
            color: securityRing.complete ? Tunables.securityComplete : Theme.highlightColor
            text: tab.storeSlice === "unknown" || tab.verifySlice === "unknown"
                  || tab.recoverySlice === "unknown" || tab.devicesSlice === "unknown"
                  //% "Checking…"
                  ? qsTrId("rustrx-la-security-checking")
                  //% "Everything that protects your messages is on."
                  : qsTrId("rustrx-la-security-ok")
        }

        // every missing safeguard, a tap leads to its fix: in a box with an
        // arrow, as a button is, so it reads as something to tap
        Repeater {
            model: tab.risks
            BackgroundItem {
                id: riskRow
                x: Theme.horizontalPageMargin
                width: column.width - 2 * x
                height: riskText.height + 2 * Theme.paddingMedium
                enabled: modelData.go.length > 0
                onClicked: tab.follow(modelData.go)
                // the box drawn here; BackgroundItem's own square highlight off
                highlightedColor: "transparent"
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.paddingMedium
                    visible: riskRow.enabled
                    color: riskRow.highlighted ? Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
                                               : Theme.rgba(Theme.primaryColor, Theme.opacityFaint / 2)
                    border.color: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
                    border.width: Math.max(1, Math.round(Theme.paddingSmall / 4))
                }
                Image {
                    id: riskArrow
                    visible: riskRow.enabled
                    anchors { right: parent.right; rightMargin: Theme.paddingSmall; verticalCenter: parent.verticalCenter }
                    source: "image://theme/icon-m-right?" + (riskRow.highlighted ? Theme.highlightColor : Theme.primaryColor)
                }
                Image {
                    id: riskIcon
                    x: Theme.paddingMedium
                    y: Theme.paddingMedium
                    sourceSize.width: Theme.iconSizeSmall
                    sourceSize.height: Theme.iconSizeSmall
                    source: "image://theme/icon-s-filled-warning?" + Theme.errorColor
                }
                Column {
                    id: riskText
                    anchors { left: riskIcon.right; leftMargin: Theme.paddingMedium; right: riskArrow.left; rightMargin: Theme.paddingSmall }
                    y: Theme.paddingMedium
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: riskRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                        text: modelData.text
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: riskRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        text: modelData.fix
                    }
                }
            }
        }

        Repeater {
            model: tab.notes
            BackgroundItem {
                id: noteRow
                width: column.width
                height: noteText.height + Theme.paddingMedium
                enabled: modelData.go.length > 0
                onClicked: tab.follow(modelData.go)
                Label {
                    id: noteText
                    anchors { left: parent.left; right: parent.right; leftMargin: Theme.horizontalPageMargin + Theme.iconSizeSmall + Theme.paddingMedium; rightMargin: Theme.horizontalPageMargin }
                    y: Theme.paddingSmall
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: noteRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: modelData.text
                }
            }
        }

        Item { width: 1; height: Theme.paddingMedium }

        // the recovery key, its backup, a reset and the room keys file
        BackgroundItem {
            id: encryptionRow
            width: parent.width
            onClicked: settings.openEncryption()
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                //% "Encryption"
                text: qsTrId("rustrx-se-encryption")
                color: encryptionRow.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            Image {
                anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                source: "image://theme/icon-m-right?" + (encryptionRow.highlighted ? Theme.highlightColor : Theme.primaryColor)
            }
        }

        // the sessions, loaded only when asked
        BackgroundItem {
            id: devicesRow
            width: parent.width
            onClicked: pageStack.push(Qt.resolvedUrl("DevicesPage.qml"))
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                //% "Devices"
                text: qsTrId("rustrx-se-devices")
                color: devicesRow.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            Image {
                anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                source: "image://theme/icon-m-right?" + (devicesRow.highlighted ? Theme.highlightColor : Theme.primaryColor)
            }
        }

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
        PageButton {
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
        PageButton {
            //% "Delete the debug log"
            text: qsTrId("rustrx-bt-clear-log")
            onClicked: {
                //% "Deleting the debug log"
                settings.remorse.execute(qsTrId("rustrx-re-clearing-log"), function () {
                    rustrx.clearLog()
                    //% "Debug log deleted, with its copies in Downloads"
                    Notices.show(qsTrId("rustrx-no-log-cleared"), Notice.Short)
                })
            }
        }
        ShareAction {
            id: logShare
            mimeType: "text/plain"
            //% "Rustrx debug log"
            title: qsTrId("rustrx-he-share-log")
        }

        Item { width: 1; height: Theme.paddingLarge }
    }

    VerticalScrollDecorator { }
}
