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
import "../../components"

// Letting a new device in with this account by QR code (MSC4108): this
// phone shows a code the new one scans, or scans the code the new one
// shows. Two digits are compared, the account service asks in the browser,
// and this phone hands over its keys, so the new device comes in verified.
// Works with Rustrx and with Element on the other side.
Page {
    id: page

    // idle (the choice), qr, scanned, connecting, check, consent, syncing,
    // done, error
    property string state_: "idle"
    property int modulesWide
    property string modules
    property string checkCode
    property string error
    readonly property bool busy: state_ !== "idle" && state_ !== "done" && state_ !== "error"

    function start() {
        error = ""
        state_ = "starting"
        rustrx.command({ cmd: "grant_qr" })
    }

    Component.onDestruction: if (busy) rustrx.command({ cmd: "login_cancel" })

    Connections {
        target: rustrx
        onQrGrant: {
            if (progress.state === "refused") return   // the scanner says so
            if (progress.state === "qr") { page.modulesWide = progress.width; page.modules = progress.modules }
            if (progress.state === "check") page.checkCode = progress.check_code || ""
            if (progress.state === "error") page.error = progress.message || ""
            page.state_ = progress.state
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                //% "Link a new device"
                title: qsTrId("rustrx-he-link-device")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: page.state_ === "error" ? Theme.errorColor : Theme.highlightColor
                text: page.state_ === "error" ? page.error
                      : page.state_ === "idle"
                        //% "Log in to %1 on another phone or computer, in Rustrx or Element. This phone hands over the keys, so the new device comes in verified."
                        ? qsTrId("rustrx-la-link-intro").arg(rustrx.userId)
                        : page.state_ === "qr"
                          //% "On the new device, choose to log in with a QR code and scan this one."
                          ? qsTrId("rustrx-la-link-show")
                          : page.state_ === "scanned"
                            //% "Type the two digits the new device shows."
                            ? qsTrId("rustrx-la-link-digits")
                            : page.state_ === "connecting"
                              ? qsTrId("rustrx-la-qr-connecting")
                            : page.state_ === "check"
                              //% "Type these two digits on the new device."
                              ? qsTrId("rustrx-la-link-check")
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
            }

            PageButton {
                visible: !page.busy
                //% "Show a QR code"
                text: qsTrId("rustrx-bt-link-show")
                onClicked: page.start()
            }

            PageButton {
                visible: !page.busy
                //% "Scan the new device's code"
                text: qsTrId("rustrx-bt-link-scan")
                onClicked: {
                    page.error = ""
                    page.state_ = "idle"
                    pageStack.push(Qt.resolvedUrl("../QrScanPage.qml"), { grant: true })
                }
            }

            QrCode {
                visible: page.state_ === "qr" && page.modulesWide > 0
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(page.width, page.height) - 2 * Theme.horizontalPageMargin
                modulesWide: page.modulesWide
                modules: page.modules
            }

            TextField {
                id: digits
                visible: page.state_ === "scanned"
                width: parent.width
                label: qsTrId("rustrx-la-qr-code")
                placeholderText: label
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegExpValidator { regExp: /^[0-9]{0,2}$/ }
                EnterKey.enabled: text.length === 2
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    rustrx.command({ cmd: "qr_check_code", code: parseInt(text, 10) })
                    page.state_ = "sent"
                    text = ""
                }
            }

            Label {
                visible: page.state_ === "check" && page.checkCode.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: Theme.fontSizeHuge
                color: Theme.highlightColor
                text: page.checkCode
            }

            PageButton {
                visible: page.busy
                text: qsTrId("rustrx-bt-sso-cancel")
                onClicked: { rustrx.command({ cmd: "login_cancel" }); page.state_ = "idle" }
            }
        }
        VerticalScrollDecorator { }
    }
}
