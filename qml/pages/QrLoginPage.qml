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
Page {
    id: page
    // done: the window's route() leaves the page for its Continue
    objectName: state_ === "done" && !continued ? "qrLoginDone" : ""
    property bool continued

    property string state_: "starting"
    property int width_
    property string modules
    property string userCode
    property string checkCode
    property string error
    // this phone scanned; the first answer came with the scan
    property bool scanned
    property var initial
    readonly property int modulesWide: width_
    // the code's minute (Tunables.qrValidSeconds), counted from when it came
    property int secondsLeft
    property real shownAt
    Timer {
        running: page.state_ === "qr" && page.secondsLeft > 0
        interval: 1000
        repeat: true
        onTriggered: page.secondsLeft = Math.max(0, Tunables.qrValidSeconds - Math.floor((Date.now() - page.shownAt) / 1000))
    }

    function take(progress) {
        page.state_ = progress.state
        if (progress.state === "qr") {
            page.width_ = progress.width
            page.modules = progress.modules
            page.shownAt = Date.now()
            page.secondsLeft = Tunables.qrValidSeconds
        }
        if (progress.state === "waiting") page.userCode = progress.user_code || ""
        if (progress.state === "check") page.checkCode = progress.check_code || ""
    }
    Component.onCompleted: if (initial) take(initial)

    Connections {
        target: rustrx
        onQrLogin: page.take(progress)
        onLoginError: page.error = message
        // logged in: the window's route() takes the stack over
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                //% "Log in with a QR code"
                title: qsTrId("rustrx-he-qr-login")
            }

            // done: the large green check, the security ring's green
            Image {
                visible: page.state_ === "done"
                anchors.horizontalCenter: parent.horizontalCenter
                sourceSize.width: Theme.iconSizeExtraLarge * 1.5
                sourceSize.height: Theme.iconSizeExtraLarge * 1.5
                source: "image://theme/icon-l-acknowledge?" + Tunables.securityComplete
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                // done in the green of the account's complete safeguards
                color: page.error.length ? Theme.errorColor : page.state_ === "done" ? Tunables.securityComplete : Theme.highlightColor
                text: page.error.length ? page.error
                      : page.state_ === "qr"
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
                            : page.state_ === "syncing"
                              //% "Logged in. Bringing over the keys…"
                              ? qsTrId("rustrx-la-qr-syncing")
                              : page.state_ === "done"
                                //% "All set. This phone is logged in and verified, and it has your keys, so your encrypted messages can be read here."
                                ? qsTrId("rustrx-la-qr-done")
                              //% "Asking the server…"
                              : qsTrId("rustrx-la-qr-starting")
            }

            QrCode {
                visible: page.state_ === "qr" && page.width_ > 0
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(page.width, page.height) - 2 * Theme.horizontalPageMargin
                modulesWide: page.width_
                modules: page.modules
            }

            // the rendezvous behind the code lives a minute from now on
            Label {
                visible: page.state_ === "qr" && page.modulesWide > 0
                anchors.horizontalCenter: parent.horizontalCenter
                color: page.secondsLeft > 0 ? Theme.secondaryHighlightColor : Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                text: page.secondsLeft > 0
                      //% "Valid for %1 s"
                      ? qsTrId("rustrx-la-qr-valid").arg(page.secondsLeft)
                      //% "This code has expired."
                      : qsTrId("rustrx-la-qr-expired")
            }

            TextField {
                id: digits
                visible: page.state_ === "scanned"
                width: parent.width
                //% "The two digits"
                label: qsTrId("rustrx-la-qr-code")
                placeholderText: label
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegExpValidator { regExp: /^[0-9]{0,2}$/ }
                EnterKey.enabled: text.length === 2
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    rustrx.command({ cmd: "qr_check_code", code: parseInt(text, 10) })
                    page.state_ = "sent"
                }
            }

            Label {
                visible: page.state_ === "check" && page.checkCode.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: Theme.fontSizeHuge
                color: Theme.highlightColor
                text: page.checkCode
            }

            Label {
                visible: page.state_ === "waiting" && page.userCode.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: Theme.fontSizeHuge
                color: Theme.highlightColor
                text: page.userCode
            }

            PageButton {
                visible: page.state_ === "done"
                //% "Continue"
                text: qsTrId("rustrx-bt-qr-continue")
                onClicked: { page.continued = true; app.route() }
            }

            PageButton {
                visible: !page.error.length && page.state_ !== "done"
                //% "Cancel"
                text: qsTrId("rustrx-bt-sso-cancel")
                onClicked: { rustrx.command({ cmd: "login_cancel" }); pageStack.pop() }
            }
        }
        VerticalScrollDecorator { }
    }
}
