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
// the devices, and the debug log. The pulley switches to another account
// (Settings then show its settings) or logs in to one more.
SilicaFlickable {
    id: tab

    property Item settings

    contentHeight: column.height

    PullDownMenu {
        //% "Log in to another account"
        MenuItem { text: qsTrId("rustrx-me-add-account"); onClicked: rustrx.addAccount() }
        Repeater {
            model: rustrx.accounts
            MenuItem {
                visible: !modelData.shown
                //: the pulley: show that account (%1 its name or address)
                //% "Show %1"
                text: qsTrId("rustrx-me-show-account").arg(modelData.name || modelData.user_id)
                onClicked: app.showAccount(modelData.key, true)
            }
        }
    }

    // whether what Rustrx stores on the phone is encrypted: "encrypted",
    // "unencrypted", "no_secrets", or "" while not known
    readonly property string storeState: rustrx.storeState !== undefined ? rustrx.storeState : ""

    // own devices besides this one that have keys and are not verified;
    // -1 while not known (or from a core that does not say)
    property int unverifiedDevices: -1
    Component.onCompleted: rustrx.command({ cmd: "devices" })
    Connections {
        target: rustrx
        onShownAccountChanged: {
            tab.unverifiedDevices = -1
            rustrx.command({ cmd: "devices" })
        }
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

    // the safeguards in their fixed order, the phone's store first (it
    // weighs two: the only data at rest). Each: its state, its name in that
    // state, and when missing what that means, how to close the gap and
    // where a tap leads (go; "" where nothing can be done). The glance is
    // the Halo of design/security-glance/ (DIRECTIONS.md).
    readonly property var safeguards: {
        var store = { status: storeSlice, weight: 2, go: "encryption",
                      title: storeSlice === "risk"
                             //% "Not encrypted on the phone"
                             ? qsTrId("rustrx-la-arc-store-risk")
                             //% "Encrypted on the phone"
                             : qsTrId("rustrx-la-arc-store-ok") }
        if (storeState === "unencrypted") {
            //% "Your messages on this phone are stored without encryption, because this store was made before Rustrx used Sailfish Secrets."
            store.text = qsTrId("rustrx-la-risk-store-unencrypted")
            //% "Turn on key backup first, then log out and log in again: the new store is encrypted."
            store.fix = qsTrId("rustrx-la-risk-store-unencrypted-fix")
        } else if (storeState === "no_secrets") {
            store.go = ""
            //% "This phone has no working Sailfish Secrets, so Rustrx cannot encrypt what it stores here."
            store.text = qsTrId("rustrx-la-risk-no-secrets")
            //% "Install Sailfish Secrets (sailfishsecretsdaemon with its default plugins), then log out and log in again."
            store.fix = qsTrId("rustrx-la-risk-no-secrets-fix2")
        }
        var verify = { status: verifySlice, weight: 1, go: "verify",
                       title: verifySlice === "risk"
                              //% "The phone is not verified"
                              ? qsTrId("rustrx-la-arc-verified-risk")
                              //% "The phone is verified"
                              : qsTrId("rustrx-la-arc-verified-ok"),
                       //% "This phone is not verified, so others cannot tell it is really you, and it may miss keys for older messages."
                       text: qsTrId("rustrx-la-risk-unverified"),
                       //% "Tap to verify it with another device of yours."
                       fix: qsTrId("rustrx-la-risk-unverified-fix") }
        var recovery = { status: recoverySlice, weight: 1, go: "encryption",
                         title: recoverySlice !== "risk"
                                //% "Key backup exists"
                                ? qsTrId("rustrx-la-arc-backup-ok")
                                : rustrx.recoveryState === "incomplete"
                                //% "Recovery key is missing"
                                ? qsTrId("rustrx-la-arc-backup-key-missing")
                                //% "Key backup is missing"
                                : qsTrId("rustrx-la-arc-backup-risk") }
        if (rustrx.recoveryState === "incomplete") {
            //% "A key backup exists, but this phone does not have its recovery key."
            recovery.text = qsTrId("rustrx-la-risk-key-missing")
            //% "Tap to enter the recovery key."
            recovery.fix = qsTrId("rustrx-la-risk-key-missing-fix")
        } else {
            //% "There is no key backup. If you lose this phone, your encrypted messages are gone."
            recovery.text = qsTrId("rustrx-la-risk-no-backup")
            //% "Tap to turn on key backup."
            recovery.fix = qsTrId("rustrx-la-risk-no-backup-fix")
        }
        var devices = { status: devicesSlice, weight: 1, go: "devices",
                        title: devicesSlice === "risk"
                               //% "Other devices unverified"
                               ? qsTrId("rustrx-la-arc-devices-risk")
                               //% "Other devices verified"
                               : qsTrId("rustrx-la-arc-devices-ok"),
                        //: %n other devices of the user without verification
                        //% "%n of your other devices are not verified. Anyone holding one could read along."
                        text: qsTrId("rustrx-la-risk-devices", Math.max(unverifiedDevices, 0)),
                        //% "Tap to see them: verify each from that device, or remove it."
                        fix: qsTrId("rustrx-la-risk-devices-fix") }
        return [store, verify, recovery, devices]
    }
    function count(status) {
        var n = 0
        for (var i = 0; i < safeguards.length; i++) if (safeguards[i].status === status) n++
        return n
    }
    // every safeguard known and in place: the one moment for green
    readonly property bool complete: count("ok") === safeguards.length
    readonly property bool checking: count("risk") === 0 && !complete
    // the safeguard whose line is being pressed, or -1: its arc lights up
    property int touched: -1

    // facts worth knowing that are no risk: each comes from a default the
    // author chose, and has its switch
    readonly property var notes: {
        var n = []
        if (rustrx.debugLog)
            //% "The debug log is on. It holds room and user IDs, never message texts or passwords."
            n.push({ go: "debuglog", text: qsTrId("rustrx-la-note-debug-log") })
        if (app.sendReceipts)
            //% "Others see how far you have read."
            n.push({ go: "privacy", text: qsTrId("rustrx-la-note-receipts") })
        if (rustrx.push.active)
            //: %1 the push gateway's host
            //% "%1 sees when a message comes and for which room, never what it says."
            n.push({ go: "app", text: qsTrId("rustrx-la-note-push").arg(rustrx.push.gatewayHost) })
        if (settings && settings.storage.media_files > 0)
            //% "Pictures and files you opened are kept unencrypted. Clear them in the App tab."
            n.push({ go: "storage", text: qsTrId("rustrx-la-note-media") })
        return n
    }

    function follow(go) {
        if (go === "debuglog") scrollTo(debugHeader)
        else if (go === "encryption") settings.openEncryption()
        else if (go === "devices") pageStack.push(Qt.resolvedUrl("DevicesPage.qml"))
        else if (go === "verify") {
            rustrx.command({ cmd: "verification_request_self" })
            pageStack.push(Qt.resolvedUrl("../VerificationPage.qml"), { waiting: true })
        } else if (go === "storage") settings.showTab("app", true)
        else if (go.length) settings.showTab(go)
    }

    // down to a section of this tab, its header just under the tab bar
    function scrollTo(item) {
        var y = item.mapToItem(column, 0, 0).y - (pullDownMenu ? settings.tabBarHeight : 0)
        scroll.to = Math.max(originY, Math.min(y, contentHeight - height + originY))
        scroll.restart()
    }
    NumberAnimation { id: scroll; target: tab; property: "contentY"; duration: Tunables.sectionScrollMs; easing.type: Easing.InOutQuad }

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

        // the server notifies of nothing for this account: its rule was
        // switched off, often in another app, and nothing here said so
        // (davodego's log: push worked, no message ever notified)
        Label {
            visible: rustrx.notificationsOff
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            //% "Notifications are switched off for this account on your server, maybe in another app. No message notifies until they are on again."
            text: qsTrId("rustrx-la-account-notifications-off")
        }
        PageButton {
            visible: rustrx.notificationsOff
            //% "Switch notifications on"
            text: qsTrId("rustrx-bt-account-notifications-on")
            onClicked: rustrx.command({ cmd: "set_account_notifications", enabled: true })
        }

        // the ring, its safeguards and the notes: how safe the messages are
        //% "Security status"
        SectionHeader { text: qsTrId("rustrx-se-security-status") }

        // the verdict first: what follows details it
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: tab.complete ? Tunables.securityComplete : Theme.highlightColor
            text: tab.complete
                  //% "Everything that protects your messages is active."
                  ? qsTrId("rustrx-la-security-ok")
                  : tab.checking
                  //% "Checking…"
                  ? qsTrId("rustrx-la-security-checking")
                  //% "Not everything that protects your messages is active."
                  : qsTrId("rustrx-la-security-gaps")
        }

        // the ring with twice the column's room above and below
        Item { width: 1; height: 0 }
        HaloRing {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(Theme.itemSizeHuge * 1.6, tab.width * 0.5, tab.height * 0.5)
            height: width
            slices: tab.safeguards
            complete: tab.complete
            touched: tab.touched
        }
        Item { width: 1; height: 0 }

        // the safeguards, one line each, led by a small copy of the ring
        // with its own arc; a missing one is the link to its fix. As wide
        // as the widest line, centred, so they belong to the ring
        Column {
            id: guards
            property real widest
            function measure() {
                var w = 0
                for (var i = 0; i < guardLines.count; i++) {
                    var line = guardLines.itemAt(i)
                    if (line) w = Math.max(w, line.naturalWidth)
                }
                widest = w
            }
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, widest > 0 ? widest : parent.width)
            Repeater {
                id: guardLines
                model: tab.safeguards
                onItemAdded: guards.measure()
                onItemRemoved: guards.measure()
                SafeguardRow {
                    onNaturalWidthChanged: guards.measure()
                    status: modelData.status
                    complete: tab.complete
                    title: modelData.title
                    detail: modelData.text + " " + modelData.fix
                    fixable: modelData.go.length > 0
                    onClicked: tab.follow(modelData.go)
                    onHighlightedChanged: tab.touched = highlighted ? index : tab.touched === index ? -1 : tab.touched
                    HaloRing {
                        anchors.fill: parent
                        slices: tab.safeguards
                        complete: tab.complete
                        only: index
                    }
                }
            }
        }

        Item { width: 1; height: Theme.paddingSmall }

        // the notes: worth knowing, no risk
        Repeater {
            model: tab.notes
            LinkRow {
                enabled: modelData.go.length > 0
                onClicked: tab.follow(modelData.go)
                text: modelData.text
                secondary: true
            }
        }

        // the keys, where they are, and bringing a new device in
        //% "Keys and devices"
        SectionHeader { text: qsTrId("rustrx-se-keys-devices") }

        // the recovery key, its backup, a reset and the room keys file
        LinkRow {
            //% "Encryption"
            text: qsTrId("rustrx-se-encryption")
            onClicked: settings.openEncryption()
        }

        // the sessions, loaded only when asked
        LinkRow {
            //% "Devices"
            text: qsTrId("rustrx-se-devices")
            onClicked: pageStack.push(Qt.resolvedUrl("DevicesPage.qml"))
        }

        // another phone or computer in with this account, by QR code; only
        // where the server can (its account service logs devices in by a
        // code: not on most servers, matrix.org without the rendezvous)
        LinkRow {
            visible: rustrx.deviceLogin
            //% "Link a new device"
            text: qsTrId("rustrx-he-link-device")
            onClicked: pageStack.push(Qt.resolvedUrl("LinkDevicePage.qml"))
        }

        // how the account talks to its server
        //% "Connection"
        SectionHeader { text: qsTrId("rustrx-se-connection") }

        // the classic sync for a server whose sliding sync sends only part of
        // the rooms (Beeper: one; dodoradio's find); cannot be told from
        // outside, so a switch. The account's session restarts with it
        TextSwitch {
            //% "Classic sync"
            text: qsTrId("rustrx-la-classic-sync")
            //% "For servers whose fast sync leaves rooms out (Beeper shows only some). Slower to start; the account reconnects when you switch."
            description: qsTrId("rustrx-la-classic-sync-hint")
            checked: rustrx.classicSync
            automaticCheck: false
            onClicked: rustrx.setClassicSync(!rustrx.classicSync)
        }

        //% "Debug log"
        SectionHeader { id: debugHeader; text: qsTrId("rustrx-se-debug-log") }

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

        // a button, not a pulley (a pulley here was pulled by mistake, the
        // author's find), and last: right under the user ID it sat one tap
        // from deleting the store (0.8 audit)
        Item { width: 1; height: Theme.paddingLarge * 2 }
        PageButton {
            //% "Log out now"
            text: qsTrId("rustrx-bt-log-out-now")
            onClicked: settings.logOut()
        }

        Item { width: 1; height: Theme.paddingLarge }
    }

    VerticalScrollDecorator { }
}
