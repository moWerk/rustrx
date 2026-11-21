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
import "../components"

// Logging in with a QR code: this phone shows the code, a device that is
// logged in already (Rustrx: "Link a new device", Element: "Link new
// device") scans it and shows two digits, which go into the field here.
// Or (scanned) this phone read the code of the logged-in device
// (QrScanPage) and shows two digits for it. That device hands over the
// keys, so this one comes in verified. The login page started it.
ShortPage {
    id: page
    // done: the window's route() leaves the page for its Continue
    objectName: state_ === "done" && !continued ? "qrLoginDone" : ""
    property bool continued

    property string state_: "starting"
    property string userCode
    property string checkCode
    property string error
    // this phone scanned; the first answer came with the scan
    property bool scanned
    property var initial
    // what the login page sent, for a new code
    property string user
    property string homeserver
    // a new code was asked for: the running login ends first, and its
    // "cancelled" is not an error
    property bool restarting

    function take(progress) {
        page.state_ = progress.state
        if (progress.state === "qr") panel.show(progress.width, progress.modules)
        if (progress.state === "waiting") page.userCode = progress.user_code || ""
        if (progress.state === "check") page.checkCode = progress.check_code || ""
    }
    Component.onCompleted: if (initial) take(initial)
    // left by back while the login runs: it ends, or the login page's
    // buttons stay off until the core gives up (0.8 audit)
    Component.onDestruction: if (rustrx.loggingIn && state_ !== "done") rustrx.command({ cmd: "login_cancel" })

    // an expired code, or an error: a new code in place (0.8 audit)
    function newCode() {
        page.error = ""
        if (rustrx.loggingIn) {
            page.restarting = true
            rustrx.command({ cmd: "login_cancel" })
        } else {
            begin()
        }
    }
    function begin() {
        page.state_ = "starting"
        page.userCode = ""
        page.checkCode = ""
        rustrx.loginQr(page.user, page.homeserver)
    }
    // the core lets go of the old login just after it says so
    Timer { id: restartLater; interval: 300; onTriggered: page.begin() }

    Connections {
        target: rustrx
        onQrLogin: page.take(progress)
        onLoginError: {
            if (page.restarting) {
                page.restarting = false
                restartLater.start()
                return
            }
            page.error = message
        }
        // logged in: the window's route() takes the stack over
    }

    //% "Log in with a QR code"
    title: qsTrId("rustrx-he-qr-login")

    body: [
        FlowStatus {
            error: page.error
            done: page.state_ === "done"
            busy: page.state_ === "starting" || page.state_ === "connecting" || page.state_ === "sent"
                  || page.state_ === "syncing"
            text: page.state_ === "qr"
                  //% "On a device that is logged in, choose to link a new device (Rustrx: Settings, Account, Link a new device), then scan this code."
                  ? qsTrId("rustrx-la-qr-scan")
                  : page.state_ === "scanned"
                    //% "Type the two digits the other device shows."
                    ? qsTrId("rustrx-la-qr-digits")
                    : page.state_ === "connecting"
                      //% "Connecting to the other device…"
                      ? qsTrId("rustrx-la-qr-connecting")
                      : page.state_ === "check"
                        //% "Type these two digits on the other device."
                        ? qsTrId("rustrx-la-qr-check")
                        : page.state_ === "waiting"
                          //% "Confirm the login on the other device."
                          ? qsTrId("rustrx-la-qr-confirm")
                          : page.state_ === "sent"
                            // it fell back to "Asking the server…" and read
                            // as a new start (0.8 audit)
                            //% "Checking the digits…"
                            ? qsTrId("rustrx-la-qr-sent")
                            : page.state_ === "syncing"
                              //% "Logged in. Bringing over the keys…"
                              ? qsTrId("rustrx-la-qr-syncing")
                              : page.state_ === "done"
                                //% "All set. This phone is logged in and verified, and it has your keys, so your encrypted messages can be read here."
                                ? qsTrId("rustrx-la-qr-done")
                                //% "Asking the server…"
                                : qsTrId("rustrx-la-qr-starting")
        },
        QrPanel {
            id: panel
            page: page
            visible: page.state_ === "qr" && !page.error.length && modulesWide > 0
        },
        DigitsField {
            visible: page.state_ === "scanned" && !page.error.length
            onEntered: {
                rustrx.command({ cmd: "qr_check_code", code: code })
                page.state_ = "sent"
            }
        },
        BigCode {
            text: page.error.length ? ""
                  : page.state_ === "check" ? page.checkCode
                  : page.state_ === "waiting" ? page.userCode : ""
        }
    ]

    buttons: [
        PageButton {
            visible: page.state_ === "done"
            //% "Continue"
            text: qsTrId("rustrx-bt-qr-continue")
            onClicked: { page.continued = true; app.route() }
        },
        // the code's minute is short; a scanned code has nothing to renew
        PageButton {
            visible: !page.scanned && page.state_ !== "done" && !page.restarting && !restartLater.running
                     && (page.error.length > 0 || (page.state_ === "qr" && panel.expired))
            //% "Show a new code"
            text: qsTrId("rustrx-bt-qr-new")
            onClicked: page.newCode()
        },
        PageButton {
            visible: page.state_ !== "done"
            //% "Cancel"
            text: qsTrId("rustrx-bt-sso-cancel")
            onClicked: { if (rustrx.loggingIn) rustrx.command({ cmd: "login_cancel" }); pageStack.pop() }
        }
    ]
}
