// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Rustrx 1.0

// The Account tab of Rustrx's Settings (qml/pages/settings/AccountTab.qml),
// fed from MockAccount instead of the core, with the glance redesigned.
// Direction "Rail": no separate picture. The list of safeguards is the
// visual: a thin line runs down it through one node per safeguard, lit
// where two neighbours are in place, broken at a missing one.
// A safeguard and its gap are one line here: the legend and the list of
// gaps of 0.4.9 are merged, so nothing is said twice. Texts are plain
// English; in Rustrx each is a qsTrId with a //% source.
SilicaFlickable {
    id: tab

    // the core's view of the account (rustrx, app and settings in Rustrx)
    property QtObject account

    contentHeight: column.height

    readonly property string storeState: account.storeState
    readonly property int unverifiedDevices: account.unverifiedDevices

    // each safeguard: "ok", "risk", or "unknown" (never a risk)
    readonly property string storeSlice: storeState === "encrypted" ? "ok"
                                       : storeState === "unencrypted" || storeState === "no_secrets" ? "risk" : "unknown"
    readonly property string verifySlice: account.verificationState === "verified" ? "ok"
                                        : account.verificationState === "unverified" ? "risk" : "unknown"
    readonly property string recoverySlice: account.recoveryState === "enabled" ? "ok"
                                          : account.recoveryState === "disabled" || account.recoveryState === "incomplete" ? "risk" : "unknown"
    readonly property string devicesSlice: unverifiedDevices < 0 ? "unknown" : unverifiedDevices > 0 ? "risk" : "ok"

    // the safeguards in their fixed order, the phone's store first (it
    // weighs two: the only data at rest). Each: its state, its name in
    // that state, and when missing what that means, how to close the gap
    // and where a tap leads (go; "" where nothing can be done)
    readonly property var safeguards: {
        var store = { status: storeSlice, weight: 2, icon: "icon-m-device-lock", go: "encryption",
                      title: storeSlice === "risk" ? "Not encrypted on the phone" : "Encrypted on the phone" }
        if (storeState === "unencrypted") {
            store.text = "Your messages on this phone are stored without encryption, because this store was made before Rustrx used Sailfish Secrets."
            store.fix = "Turn on key backup first, then log out and log in again: the new store is encrypted."
        } else if (storeState === "no_secrets") {
            store.go = ""
            store.text = "This phone has no working Sailfish Secrets, so Rustrx cannot encrypt what it stores here."
            store.fix = "Nothing Rustrx can do on this phone."
        }
        var verify = { status: verifySlice, weight: 1, icon: "icon-m-device", go: "verify",
                       title: verifySlice === "risk" ? "The phone is not verified" : "The phone is verified",
                       text: "This phone is not verified, so others cannot tell it is really you, and it may miss keys for older messages.",
                       fix: "Tap to verify it with another device of yours." }
        var recovery = { status: recoverySlice, weight: 1, icon: "icon-m-backup", go: "encryption",
                         title: recoverySlice !== "risk" ? "Key backup exists"
                              : account.recoveryState === "incomplete" ? "Recovery key is missing" : "Key backup is missing" }
        if (account.recoveryState === "incomplete") {
            recovery.text = "A key backup exists, but this phone does not have its recovery key."
            recovery.fix = "Tap to enter the recovery key."
        } else {
            recovery.text = "There is no key backup. If you lose this phone, your encrypted messages are gone."
            recovery.fix = "Tap to turn on key backup."
        }
        var devices = { status: devicesSlice, weight: 1, icon: "icon-m-computer", go: "devices",
                        title: devicesSlice === "risk" ? "Other devices unverified" : "Other devices verified",
                        text: unverifiedDevices + " of your other devices are not verified. Anyone holding one could read along.",
                        fix: "Tap to see them: verify each from that device, or remove it." }
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
    // the safeguard whose line is being pressed, or -1: its part of the
    // visual lights up
    property int touched: -1

    // facts worth knowing that are no risk, each with its switch
    readonly property var notes: {
        var n = []
        if (account.debugLog) n.push({ go: "debuglog", text: "The debug log is on. It holds room and user IDs, never message texts or passwords." })
        if (account.sendReceipts) n.push({ go: "privacy", text: "Others see how far you have read." })
        if (account.pushActive) n.push({ go: "app", text: account.gatewayHost + " sees when a message comes and for which room, never what it says." })
        if (account.mediaCached) n.push({ go: "app", text: "Pictures and files you opened are kept unencrypted. Clear them in the App tab." })
        return n
    }

    // in Rustrx: open the Encryption or Devices page, start a verification,
    // switch to the Privacy or App tab; here only the debug log scrolls
    function follow(go) {
        if (go === "debuglog") {
            scroll.to = Math.min(debugHeader.y, contentHeight - height)
            scroll.restart()
        } else {
            console.log("mockup: a tap leads to " + go)
        }
    }
    NumberAnimation { id: scroll; target: tab; property: "contentY"; duration: Tunables.sectionScrollMs; easing.type: Easing.InOutQuad }

    Column {
        id: column
        width: parent.width
        spacing: Theme.paddingMedium

        // below the tab bar (Account, Privacy, App, About), which is not drawn here
        Item { width: 1; height: Theme.paddingLarge }

        // one's own picture, or the name's first letter
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.itemSizeHuge
            height: width
            radius: width / 2
            color: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
            Label {
                anchors.centerIn: parent
                text: account.displayName.charAt(0).toUpperCase()
                font.pixelSize: Theme.fontSizeHuge
            }
        }

        // a text field in Rustrx
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: account.displayName
            font.pixelSize: Theme.fontSizeLarge
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            text: account.userId + "\n" + account.homeserver
        }

        Item { width: 1; height: Theme.paddingLarge }

        // ---- the glance: from here to the Encryption row ----

        // the verdict first: what follows details it
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeLarge
            color: tab.complete ? Tunables.securityComplete : Theme.highlightColor
            text: tab.complete ? "Everything that protects your messages is active."
                : tab.checking ? "Checking…"
                : "Not everything that protects your messages is active."
        }

        Item { width: 1; height: Theme.paddingSmall }

        // the safeguards, one line each; a missing one is the link to
        // its fix
        Column {
            width: parent.width
            Repeater {
                model: tab.safeguards
                SafeguardRow {
                    id: safeguard
                    status: modelData.status
                    complete: tab.complete
                    title: modelData.title
                    detail: modelData.text + " " + modelData.fix
                    fixable: modelData.go.length > 0
                    onClicked: tab.follow(modelData.go)
                    onHighlightedChanged: tab.touched = highlighted ? index : tab.touched === index ? -1 : tab.touched
                    markFills: true
                    RailMark {
                        anchors.fill: parent
                        status: modelData.status
                        complete: tab.complete
                        above: index > 0 ? tab.safeguards[index - 1].status : ""
                        below: index < tab.safeguards.length - 1 ? tab.safeguards[index + 1].status : ""
                        nodeY: safeguard.firstLineCenter
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

        // ---- end of the glance ----

        Item { width: 1; height: Theme.paddingMedium }

        LinkRow { text: "Encryption"; onClicked: tab.follow("encryption") }
        LinkRow { text: "Devices"; onClicked: tab.follow("devices") }

        SectionHeader { id: debugHeader; text: "Debug log" }

        TextSwitch {
            text: "Keep a debug log"
            description: "Rustrx notes what it does in a small file on the phone. If something goes wrong, share it with the developer, so nobody has to make it happen again. It holds room and user IDs, never message texts or passwords."
            checked: account.debugLog
            automaticCheck: false
            onClicked: account.debugLog = !account.debugLog
        }
        PageButton { text: "Share the debug log" }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            text: "A copy goes to Downloads, so you can also attach it to a mail or a forum post."
        }
        PageButton { text: "Delete the debug log" }

        Item { width: 1; height: Theme.paddingLarge }
    }
}
