// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Rustrx 1.0

// The Account tab of Rustrx's Settings as it is in 0.4.8-24
// (qml/pages/settings/AccountTab.qml), cut to what the glance needs and
// fed from MockAccount instead of the core. This is the part to redesign:
// the verdict, the ring, its legend, the gaps and notes, the links. Texts
// are plain English here; in Rustrx each is a qsTrId with a //% source.
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

    // what is missing, in the order to fix it; go: where a tap leads
    readonly property var risks: {
        var r = []
        if (account.recoveryState === "disabled")
            r.push({ go: "encryption", text: "There is no key backup. If you lose this phone, your encrypted messages are gone.",
                     fix: "Tap to turn on key backup." })
        if (account.recoveryState === "incomplete")
            r.push({ go: "encryption", text: "A key backup exists, but this phone does not have its recovery key.",
                     fix: "Tap to enter the recovery key." })
        if (storeState === "unencrypted")
            r.push({ go: "encryption", text: "Your messages on this phone are stored without encryption, because this store was made before Rustrx used Sailfish Secrets.",
                     fix: "Turn on key backup first, then log out and log in again: the new store is encrypted." })
        if (storeState === "no_secrets")
            r.push({ go: "", text: "This phone has no working Sailfish Secrets, so Rustrx cannot encrypt what it stores here.",
                     fix: "Nothing Rustrx can do on this phone." })
        if (account.verificationState === "unverified")
            r.push({ go: "verify", text: "This phone is not verified, so others cannot tell it is really you, and it may miss keys for older messages.",
                     fix: "Tap to verify it with another device of yours." })
        if (unverifiedDevices > 0)
            r.push({ go: "devices", text: unverifiedDevices + " of your other devices are not verified. Anyone holding one could read along.",
                     fix: "Tap to see them: verify each from that device, or remove it." })
        return r
    }

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

        // the verdict first: the ring and its legend detail it
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: securityRing.complete ? Tunables.securityComplete : Theme.highlightColor
            text: tab.risks.length > 0 ? "Not everything that protects your messages is active."
                  : tab.storeSlice === "unknown" || tab.verifySlice === "unknown"
                    || tab.recoverySlice === "unknown" || tab.devicesSlice === "unknown"
                  ? "Checking…"
                  : "Everything that protects your messages is active."
        }

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
                    { slice: tab.storeSlice, ok: "Encrypted on the phone", risk: "Not encrypted on the phone" },
                    { slice: tab.verifySlice, ok: "The phone is verified", risk: "The phone is not verified" },
                    { slice: tab.recoverySlice, ok: "Key backup exists",
                      risk: account.recoveryState === "incomplete" ? "Recovery key is missing" : "Key backup is missing" },
                    { slice: tab.devicesSlice, ok: "Other devices verified", risk: "Other devices unverified" }
                ]
                Label {
                    font.pixelSize: Theme.fontSizeSmall
                    text: (index + 1) + ". " + (modelData.slice === "risk" ? modelData.risk : modelData.ok)
                    color: modelData.slice === "ok" ? Tunables.securityComplete
                         : modelData.slice === "risk" ? Theme.errorColor
                         : Theme.highlightDimmerColor
                }
            }
        }

        Item { width: 1; height: Theme.paddingSmall }

        // every missing safeguard, then the notes: lines with an arrow
        Repeater {
            model: tab.risks
            LinkRow {
                enabled: modelData.go.length > 0
                onClicked: tab.follow(modelData.go)
                icon: "icon-s-filled-warning"
                iconColor: Theme.errorColor
                text: modelData.text
                description: modelData.fix
            }
        }
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
