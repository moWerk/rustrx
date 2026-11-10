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

// Everything on one page: the fields keep their content, errors appear
// under the fields, and every way to log in sits on the same screen.
Page {
    id: page
    objectName: "login"

    property var server: ({ probed: false, password: true, sso: false, idps: [], sliding: false, name: "" })
    property string errorText

    Component.onCompleted: {
        if (rustrx.logoutReason === "session_expired")
            //% "The server no longer accepts the saved session. Log in again."
            errorText = qsTrId("rustrx-la-session-expired")
    }

    function login() {
        errorText = ""
        rustrx.loginPassword(userField.text, passwordField.text, serverField.text)
    }

    function probe() {
        if (userField.text.indexOf(":") < 0 && serverField.text.length === 0) return
        rustrx.probeServer(userField.text, serverField.text)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            PageHeader { title: "Rustrx" }

            TextField {
                id: userField
                width: parent.width
                //: login field, engineering English
                //% "Matrix ID, like @name:server.org"
                placeholderText: qsTrId("rustrx-ph-matrix-id")
                //% "Matrix ID"
                label: qsTrId("rustrx-la-matrix-id")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: passwordField.focus = true
                onActiveFocusChanged: if (!activeFocus) probe()
            }

            TextField {
                id: serverField
                width: parent.width
                //% "Homeserver, only if it differs from the server in your ID"
                placeholderText: qsTrId("rustrx-ph-homeserver")
                //% "Homeserver"
                label: qsTrId("rustrx-la-homeserver")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: passwordField.focus = true
                onActiveFocusChanged: if (!activeFocus) probe()
            }

            PasswordField {
                id: passwordField
                width: parent.width
                //% "Password"
                placeholderText: qsTrId("rustrx-ph-password")
                label: placeholderText
                EnterKey.enabled: text.length > 0 && userField.text.length > 0 && !rustrx.loggingIn
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: login()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: errorText.length > 0
                text: errorText
                wrapMode: Text.Wrap
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: server.probed
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: {
                    var ways = []
                    //% "password"
                    if (server.password) ways.push(qsTrId("rustrx-la-way-password"))
                    //% "single sign-on"
                    if (server.sso) ways.push(qsTrId("rustrx-la-way-sso"))
                    //: %1 server, %2 the ways to log in, %3 sync note
                    //% "%1 offers: %2. %3"
                    return qsTrId("rustrx-la-server-offers").arg(server.name).arg(ways.join(", "))
                        //% "Fast sync available."
                        .arg(server.sliding ? qsTrId("rustrx-la-sliding-yes")
                        //% "No fast sync; the first sync takes longer."
                                            : qsTrId("rustrx-la-sliding-no"))
                }
            }

            Item { width: 1; height: Theme.paddingLarge }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Logging in…"
                text: rustrx.loggingIn ? qsTrId("rustrx-bt-logging-in")
                //% "Log in"
                                       : qsTrId("rustrx-bt-log-in")
                enabled: !rustrx.loggingIn && userField.text.length > 0 && passwordField.text.length > 0
                onClicked: login()
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: rustrx.loggingIn
                size: BusyIndicatorSize.Medium
            }

            //% "Other ways to log in"
            SectionHeader { text: qsTrId("rustrx-se-other-ways") }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Log in with single sign-on"
                text: qsTrId("rustrx-bt-sso")
                enabled: !rustrx.loggingIn && server.sso
                onClicked: {
                    errorText = ""
                    rustrx.loginSso(userField.text, serverField.text, "")
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                //% "Single sign-on opens your server's login page in the browser and brings you back here. It becomes available once your server is known, so fill in your Matrix ID first."
                text: qsTrId("rustrx-la-sso-note")
            }

            Item { width: 1; height: Theme.paddingLarge }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                //% "Signing in by scanning a QR code shown by Element on another device is planned for a later version."
                text: qsTrId("rustrx-la-qr-note")
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator { }
    }

    Connections {
        target: rustrx
        onLoginError: errorText = message
        onServerInfo: {
            server = { probed: true, name: info.server, password: info.password, sso: info.sso,
                       idps: info.idps, sliding: info.sliding_sync }
        }
        onErrorOccurred: if (cmd === "probe_server") errorText = message
    }
}
