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
import "../components"

// Logging in with a QR code: this phone shows the code, a device that is
// logged in already (Element: "Link new device") scans it and shows two
// digits, which go into the field here. That device hands over the keys,
// so this one comes in verified. The login page started it (loginQr).
Page {
    id: page

    property string state_: "starting"
    property int width_
    property string modules
    property string userCode
    property string error

    Connections {
        target: rustrx
        onQrLogin: {
            page.state_ = progress.state
            if (progress.state === "qr") { page.width_ = progress.width; page.modules = progress.modules; qr.requestPaint() }
            if (progress.state === "waiting") page.userCode = progress.user_code || ""
        }
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

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: page.error.length ? Theme.errorColor : Theme.highlightColor
                text: page.error.length ? page.error
                      : page.state_ === "qr"
                        //% "On a device that is logged in, open Element and choose to link a new device, then scan this code."
                        ? qsTrId("rustrx-la-qr-scan")
                        : page.state_ === "scanned"
                          //% "Type the two digits the other device shows."
                          ? qsTrId("rustrx-la-qr-digits")
                          : page.state_ === "waiting"
                            //% "Confirm the login on the other device."
                            ? qsTrId("rustrx-la-qr-confirm")
                            : page.state_ === "syncing" || page.state_ === "done"
                              //% "Logged in. Bringing over the keys…"
                              ? qsTrId("rustrx-la-qr-syncing")
                              //% "Asking the server…"
                              : qsTrId("rustrx-la-qr-starting")
            }

            // the code: dark modules on white, with the quiet zone around
            Rectangle {
                visible: page.state_ === "qr" && page.width_ > 0
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(page.width, page.height) - 2 * Theme.horizontalPageMargin
                height: width
                color: "white"
                Canvas {
                    id: qr
                    anchors.fill: parent
                    anchors.margins: parent.width / (page.width_ + 8) * 4
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.fillStyle = "white"
                        ctx.fillRect(0, 0, width, height)
                        if (!page.width_) return
                        var cell = width / page.width_
                        ctx.fillStyle = "black"
                        for (var y = 0; y < page.width_; y++)
                            for (var x = 0; x < page.width_; x++)
                                if (page.modules.charAt(y * page.width_ + x) === "1")
                                    ctx.fillRect(Math.floor(x * cell), Math.floor(y * cell), Math.ceil(cell), Math.ceil(cell))
                    }
                }
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
                visible: page.state_ === "waiting" && page.userCode.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: Theme.fontSizeHuge
                color: Theme.highlightColor
                text: page.userCode
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
