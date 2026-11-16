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

// Everything on one page: the fields keep their content, errors appear
// under the fields, and every way to log in sits on the same screen.
Page {
    id: page
    objectName: "login"

    property var server: ({ probed: false, password: true, sso: false, idps: [], sliding: false, name: "" })
    property string errorText

    Component.onCompleted: {
        // a session the server ended: the same user again keeps messages and keys
        if (rustrx.keptUser.length) {
            userField.text = rustrx.keptUser
            //: %1 the Matrix ID
            //% "The server ended the session. Log in again as %1: your messages and keys are still on this phone."
            errorText = qsTrId("rustrx-la-session-kept").arg(rustrx.keptUser)
        } else if (rustrx.logoutReason === "session_expired") {
            //% "The server no longer accepts the saved session. Log in again."
            errorText = qsTrId("rustrx-la-session-expired")
        }
    }

    function login() {
        errorText = ""
        rustrx.loginPassword(userField.text, passwordField.text, serverField.text)
    }

    function probe() {
        if (userField.text.indexOf(":") < 0 && serverField.text.length === 0) return
        rustrx.probeServer(userField.text, serverField.text)
    }
    // asked again a moment after typing stops, so single sign-on shows up
    // without leaving the field first
    Timer { id: probeLater; interval: Tunables.typingDebounceMs; onTriggered: page.probe() }
    function probeSoon() {
        var id = /^@[^:\s]+:[^.\s]+\.[^\s]+$/.test(userField.text.trim())
        if (id || serverField.text.trim().indexOf(".") > 0) probeLater.restart()
    }

    // the sign-on page's address, shown while the browser login runs: the
    // browser does not open on every device, then it is opened or copied by hand
    property string ssoUrl

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            // room between the buttons and the texts around them
            spacing: Theme.paddingMedium

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
                // a Matrix ID starts with @, which the Sailfish keyboard hides
                // among its symbols: it is there already (a pasted full ID
                // does not end up with two)
                text: "@"
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: passwordField.focus = true
                onActiveFocusChanged: if (!activeFocus) probe()
                onTextChanged: {
                    if (text.indexOf("@@") === 0) text = text.slice(1)
                    page.probeSoon()
                }
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
                onTextChanged: page.probeSoon()
            }

            PasswordField {
                id: passwordField
                width: parent.width
                placeholderText: qsTrId("rustrx-ph-password")
                label: placeholderText
                EnterKey.enabled: text.length > 0 && userField.text.length > 1 && !rustrx.loggingIn
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: login()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                //% "The password goes to the server once and is not kept on the phone; only a session token is."
                text: qsTrId("rustrx-la-password-note")
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
                enabled: !rustrx.loggingIn && userField.text.length > 1 && passwordField.text.length > 0
                onClicked: login()
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: rustrx.loggingIn
                size: BusyIndicatorSize.Medium
            }

            //% "Other ways to log in"
            SectionHeader { text: qsTrId("rustrx-se-other-ways") }

            Label {
                visible: server.probed && server.sso && server.accountService === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                //: %1 the server name
                //% "%1 manages its logins in the browser. If the password is not accepted, log in with single sign-on."
                text: qsTrId("rustrx-la-account-service").arg(server.name)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Log in with single sign-on"
                text: qsTrId("rustrx-bt-sso")
                enabled: !rustrx.loggingIn && server.sso
                onClicked: {
                    errorText = ""
                    page.ssoUrl = ""
                    rustrx.loginSso(userField.text, serverField.text, "")
                }
            }

            // while the browser login runs: the address, to open again or copy
            Column {
                visible: rustrx.loggingIn && page.ssoUrl.length > 0
                width: parent.width
                spacing: Theme.paddingMedium
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    wrapMode: Text.Wrap
                    color: Theme.highlightColor
                    font.pixelSize: Theme.fontSizeSmall
                    //% "Waiting for the login in the browser. If the browser did not open, open the link again or copy it into the browser."
                    text: qsTrId("rustrx-la-sso-waiting")
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                    text: page.ssoUrl
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.paddingLarge
                    Button {
                        //% "Open again"
                        text: qsTrId("rustrx-bt-sso-open-again")
                        onClicked: app.openExternal(page.ssoUrl)
                    }
                    Button {
                        //% "Copy link"
                        text: qsTrId("rustrx-bt-sso-copy")
                        onClicked: {
                            Clipboard.text = page.ssoUrl
                            //% "Link copied"
                            Notices.show(qsTrId("rustrx-no-link-copied"), Notice.Short)
                        }
                    }
                }
                // two buttons fit a row in portrait; the way back gets its own
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    //% "Cancel"
                    text: qsTrId("rustrx-bt-sso-cancel")
                    onClicked: rustrx.command({ cmd: "login_cancel" })
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

            //% "New to Matrix"
            SectionHeader { text: qsTrId("rustrx-se-new-account") }

            // accounts are made on the server's own sign-up page (matrix.org and
            // other servers with an account service no longer take sign-ups from
            // apps); the password login above works for them afterwards
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Create an account"
                text: qsTrId("rustrx-bt-create-account")
                onClicked: app.openExternal(server.signup && server.signup.length
                                                ? server.signup : "https://account.matrix.org/register")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                //: %1 the server name
                //% "Opens the sign-up page of %1 in the browser. Come back afterwards and log in here with the new Matrix ID and password."
                text: qsTrId("rustrx-la-create-account-note").arg(server.signup && server.signup.length ? server.name : "matrix.org")
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
        // the sign-on page's address (the window's openUrl handler opens it too)
        onBrowserUrl: if (rustrx.loggingIn) page.ssoUrl = url
        onServerInfo: {
            server = { probed: true, name: info.server, password: info.password, sso: info.sso,
                       accountService: info.account_service === true,
                       idps: info.idps, sliding: info.sliding_sync, signup: info.signup_url || "" }
        }
        onErrorOccurred: if (cmd === "probe_server") errorText = message
    }
}
