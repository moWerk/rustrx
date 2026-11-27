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
import "../../components"

// Letting a new device in with this account by QR code (MSC4108): this
// phone shows a code the new one scans, or scans the code the new one
// shows. Two digits are compared, the account service asks in the browser,
// and this phone hands over its keys, so the new device comes in verified.
// Works with Rustrx and with Element on the other side.
ShortPage {
    id: page

    // idle (the choice), starting, qr, scanned, connecting, check, sent,
    // consent, syncing, done, error
    property string state_: "idle"
    property string checkCode
    property string error
    readonly property bool busy: state_ !== "idle" && state_ !== "done" && state_ !== "error"
    // the flow was ended here on purpose: its "cancelled" is no error; and
    // a new code was asked for, which starts once the old flow is gone
    property bool cancelling
    property bool restarting

    function start() {
        cancelling = false
        error = ""
        checkCode = ""
        state_ = "starting"
        rustrx.command({ cmd: "grant_qr" })
    }
    function cancel() {
        cancelling = true
        rustrx.command({ cmd: "login_cancel" })
        state_ = "idle"
    }
    // the code's minute ran out: a new one in place (0.8 audit)
    function newCode() {
        restarting = true
        rustrx.command({ cmd: "login_cancel" })
        state_ = "starting"
    }
    // the core lets go of the old flow just after it says so
    Timer { id: restartLater; interval: 300; onTriggered: page.start() }

    Component.onDestruction: if (busy) rustrx.command({ cmd: "login_cancel" })

    // the account service's page for the consent, for "Open again" and
    // "Copy link" when the browser did not come up (0.8 audit)
    property string consentUrl
    Connections {
        target: rustrx
        onBrowserUrl: if (page.state_ === "consent" || page.busy) page.consentUrl = url
        onQrGrant: {
            if (progress.state === "refused") return   // the scanner says so
            if (progress.state === "error" && (page.cancelling || page.restarting)) {
                page.cancelling = false
                if (page.restarting) {
                    page.restarting = false
                    restartLater.start()
                }
                return
            }
            if (progress.state === "qr") panel.show(progress.width, progress.modules)
            if (progress.state === "check") page.checkCode = progress.check_code || ""
            if (progress.state === "error") page.error = progress.reason === "foreign_page"
                    // its consent page not on the account service's host (NET-2)
                    //: %1 the host of the page the new device sent
                    //% "The new device sent a page that is not your server's (%1). Nothing was linked."
                    ? qsTrId("rustrx-la-link-foreign-page").arg(progress.host || "")
                    : progress.message || ""
            page.state_ = progress.state
        }
    }

    //% "Link a new device"
    title: qsTrId("rustrx-he-link-device")

    body: [
        FlowStatus {
            error: page.state_ === "error" ? page.error : ""
            done: page.state_ === "done"
            busy: page.state_ === "starting" || page.state_ === "connecting" || page.state_ === "sent"
                  || page.state_ === "syncing"
            text: page.state_ === "idle"
                  //% "Log in to %1 on another phone or computer, in Rustrx or Element. This phone hands over the keys, so the new device comes in verified."
                  ? qsTrId("rustrx-la-link-intro").arg(rustrx.userId)
                  : page.state_ === "qr"
                    // the login page's "Log in with a QR code" shows a code
                    // of its own; the scan is "Scan a QR code" (0.8 audit)
                    //% "On the new device, choose Scan a QR code (in Element: Sign in with QR code) and scan this one."
                    ? qsTrId("rustrx-la-link-show-scan")
                    : page.state_ === "scanned"
                      //% "Type the two digits the new device shows."
                      ? qsTrId("rustrx-la-link-digits")
                      : page.state_ === "connecting"
                        ? qsTrId("rustrx-la-qr-connecting")
                        : page.state_ === "check"
                          //% "Type these two digits on the new device."
                          ? qsTrId("rustrx-la-link-check")
                          : page.state_ === "sent"
                            ? qsTrId("rustrx-la-qr-sent")
                            : page.state_ === "consent"
                              //% "Confirm the new device in the browser, then come back here."
                              ? qsTrId("rustrx-la-link-consent")
                              : page.state_ === "syncing"
                                //% "Handing over the keys…"
                                ? qsTrId("rustrx-la-link-syncing")
                                : page.state_ === "done"
                                  //% "The new device is logged in and verified."
                                  ? qsTrId("rustrx-la-link-done")
                                  : qsTrId("rustrx-la-qr-starting")
        },
        QrPanel {
            id: panel
            page: page
            visible: page.state_ === "qr" && modulesWide > 0
        },
        DigitsField {
            visible: page.state_ === "scanned"
            onEntered: {
                rustrx.command({ cmd: "qr_check_code", code: code })
                page.state_ = "sent"
            }
        },
        BigCode {
            text: page.state_ === "check" ? page.checkCode : ""
        }
    ]

    buttons: [
        // showing a code needs the server's rendezvous; scanning the new
        // device's code does not (its rendezvous is on its side)
        PageButton {
            visible: !page.busy && page.state_ !== "done" && rustrx.qrRendezvous
            //% "Show a QR code"
            text: qsTrId("rustrx-bt-link-show")
            //% "Open the scanner on the new device first: from the moment the code is shown, the scan, the two digits and the confirmation in the browser have one minute together."
            hint: qsTrId("rustrx-la-link-first")
            onClicked: page.start()
        },
        PageButton {
            visible: !page.busy && page.state_ !== "done"
            //% "Scan the new device's code"
            text: qsTrId("rustrx-bt-link-scan")
            onClicked: {
                page.error = ""
                page.state_ = "idle"
                pageStack.push(Qt.resolvedUrl("../QrScanPage.qml"), { grant: true })
            }
        },
        PageButton {
            visible: page.state_ === "qr" && panel.expired
            text: qsTrId("rustrx-bt-qr-new")
            onClicked: page.newCode()
        },
        ButtonLayout {
            width: parent ? parent.width : 0
            visible: page.state_ === "consent" && page.consentUrl.length > 0
            Button {
                text: qsTrId("rustrx-bt-sso-open-again")
                onClicked: app.openExternal(page.consentUrl)
            }
            Button {
                text: qsTrId("rustrx-bt-sso-copy")
                onClicked: {
                    Clipboard.text = page.consentUrl
                    Notices.show(qsTrId("rustrx-no-link-copied"), Notice.Short)
                }
            }
        },
        PageButton {
            visible: page.busy
            text: qsTrId("rustrx-bt-sso-cancel")
            onClicked: page.cancel()
        },
        // done had no button at all (0.8 audit)
        PageButton {
            visible: page.state_ === "done"
            //% "Done"
            text: qsTrId("rustrx-bt-done")
            onClicked: pageStack.pop()
        }
    ]
}
