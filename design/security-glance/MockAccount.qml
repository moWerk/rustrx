// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6

// What the Account tab reads from Rustrx's core (rustrx.*, app.*,
// settings.*), as plain properties the mockup's controls set.
QtObject {
    property string userId: "@sam:example.org"
    property string homeserver: "https://example.org"
    property string displayName: "Sam"
    // "encrypted", "unencrypted", "no_secrets" or "" (not known yet)
    property string storeState: "encrypted"
    // "verified", "unverified" or ""
    property string verificationState: "verified"
    // "enabled", "incomplete", "disabled" or ""
    property string recoveryState: "enabled"
    // other own devices with keys and without verification; -1 not known
    property int unverifiedDevices: 0
    property bool debugLog: true
    property bool sendReceipts: true
    property bool pushActive: true
    property string gatewayHost: "push.example.org"
    property bool mediaCached: true

    function scenario(name) {
        if (name === "good") { storeState = "encrypted"; verificationState = "verified"; recoveryState = "enabled"; unverifiedDevices = 0 }
        else if (name === "gaps") { storeState = "encrypted"; verificationState = "verified"; recoveryState = "disabled"; unverifiedDevices = 2 }
        else if (name === "bad") { storeState = "unencrypted"; verificationState = "unverified"; recoveryState = "incomplete"; unverifiedDevices = 1 }
        else if (name === "checking") { storeState = ""; verificationState = ""; recoveryState = ""; unverifiedDevices = -1 }
    }
}
