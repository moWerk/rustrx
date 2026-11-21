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
import Sailfish.Share 1.0
import "../components"

// Everything on one page: the fields keep their content, errors appear
// under the fields, and every way to log in sits on the same screen.
Page {
    id: page
    objectName: "login"
    // above the room list, with another account logged in: back leads to
    // that account (app.loginLeft); otherwise the only page
    property bool stacked
    Component.onDestruction: if (stacked) app.loginLeft()

    property var server: ({ probed: false, password: true, sso: false, idps: [], sliding: false, name: "" })
    property string errorText
    // a server with an account service (MAS: matrix.org and others): the
    // login happens on its own page in the browser, so the password is
    // never typed into Rustrx. The password field comes only when asked for.
    readonly property bool oauth: server.probed && server.accountService === true
    property bool passwordWanted
    readonly property bool showPassword: !oauth || passwordWanted

    function loginOauth(signup) {
        errorText = ""
        ssoUrl = ""
        rustrx.loginOauth(userField.text, serverField.text, signup)
    }

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
        // the kept user's name goes in while the page is built, before both fields are there
        if (!userField || !serverField) return
        var id = /^@[^:\s]+:[^.\s]+\.[^\s]+$/.test(userField.text.trim())
        if (id || serverField.text.trim().indexOf(".") > 0) probeLater.restart()
    }

    // the sign-on page's address, shown while the browser login runs: the
    // browser does not open on every device, then it is opened or copied by hand
    property string ssoUrl

    // the other accounts with a login: logging in to one more can be given
    // up, back to one of them
    readonly property var otherAccounts: rustrx.accounts.filter(function (a) { return !a.shown && a.logged_in })

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            visible: page.otherAccounts.length > 0
            Repeater {
                model: page.otherAccounts
                MenuItem {
                    text: qsTrId("rustrx-me-show-account").arg(modelData.name || modelData.user_id)
                    onClicked: app.showAccount(modelData.key)
                }
            }
        }

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

            Item { width: 1; height: Theme.paddingMedium; visible: page.oauth }

            PageButton {
                visible: page.oauth
                //: %1 the server name
                //% "Log in at %1"
                text: qsTrId("rustrx-bt-oauth").arg(server.name)
                enabled: !rustrx.loggingIn
                onClicked: page.loginOauth(false)
            }

            Label {
                visible: page.oauth
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                //: %1 the server name
                //% "Opens the login page of %1 in the browser and brings you back here. Your password stays between you and %1; Rustrx only gets a token."
                text: qsTrId("rustrx-la-oauth-note").arg(server.name)
            }

            PageButton {
                // a server with the QR rendezvous (MSC4108): another device
                // logged in already lets this one in, keys and all
                visible: page.oauth && server.qrLogin === true
                //% "Log in with a QR code"
                text: qsTrId("rustrx-he-qr-login")
                enabled: !rustrx.loggingIn
                onClicked: {
                    errorText = ""
                    rustrx.loginQr(userField.text, serverField.text)
                    // kept there for "Show a new code"
                    pageStack.push(Qt.resolvedUrl("QrLoginPage.qml"), { user: userField.text, homeserver: serverField.text })
                }
            }

            PageButton {
                visible: page.oauth && !page.passwordWanted && server.password
                //% "Use a password instead"
                text: qsTrId("rustrx-bt-password-instead")
                onClicked: page.passwordWanted = true
            }

            PasswordField {
                id: passwordField
                visible: page.showPassword
                width: parent.width
                placeholderText: qsTrId("rustrx-ph-password")
                label: placeholderText
                EnterKey.enabled: text.length > 0 && userField.text.length > 1 && !rustrx.loggingIn
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: login()
            }

            Label {
                visible: page.showPassword
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

            // a login that fails: the log from right here, since Settings
            // (where it is shared otherwise) needs a login first
            PageButton {
                visible: errorText.length > 0 && rustrx.debugLog
                text: qsTrId("rustrx-bt-share-log")
                onClicked: {
                    var path = rustrx.shareableLog()
                    if (path === "")
                        return
                    logShare.resources = ["file://" + path]
                    logShare.trigger()
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: errorText.length > 0 && rustrx.debugLog
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTrId("rustrx-la-share-log-hint")
            }
            ShareAction {
                id: logShare
                mimeType: "text/plain"
                title: qsTrId("rustrx-he-share-log")
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
                    if (server.sso && !page.oauth) ways.push(qsTrId("rustrx-la-way-sso"))
                    //% "login in the browser"
                    if (page.oauth) ways.push(qsTrId("rustrx-la-way-oauth"))
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

            PageButton {
                visible: page.showPassword
                //% "Logging in…"
                text: rustrx.loggingIn ? qsTrId("rustrx-bt-logging-in")
                //% "Log in"
                                       : qsTrId("rustrx-bt-log-in")
                enabled: !rustrx.loggingIn && userField.text.length > 1 && passwordField.text.length > 0
                onClicked: login()
            }

            // idle it left a hole before "New account" (0.8 audit)
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: rustrx.loggingIn
                visible: running
                size: BusyIndicatorSize.Medium
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
                // side by side where they fit, stacked where a translation
                // is long; a Row ran off the page (0.8 audit)
                ButtonLayout {
                    width: parent.width
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
                PageButton {
                    //% "Cancel"
                    text: qsTrId("rustrx-bt-sso-cancel")
                    onClicked: rustrx.command({ cmd: "login_cancel" })
                }
            }


            SectionHeader {
                visible: !page.oauth
                //% "Other ways to log in"
                text: qsTrId("rustrx-se-other-ways")
            }

            PageButton {
                visible: !page.oauth
                //% "Log in with single sign-on"
                text: qsTrId("rustrx-bt-sso")
                enabled: !rustrx.loggingIn && server.sso
                onClicked: {
                    errorText = ""
                    page.ssoUrl = ""
                    rustrx.loginSso(userField.text, serverField.text, "")
                }
            }

            Label {
                visible: !page.oauth
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
            PageButton {
                //% "Create an account"
                text: qsTrId("rustrx-bt-create-account")
                enabled: !rustrx.loggingIn
                onClicked: {
                    // made on the account service's page and logged in at once
                    if (page.oauth) page.loginOauth(true)
                    else app.openExternal("https://account.matrix.org/register")
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: page.oauth
                      //: %1 the server name
                      //% "Makes the account on the page of %1 in the browser and logs you in here."
                      ? qsTrId("rustrx-la-create-account-oauth").arg(server.name)
                      //: %1 the server name
                      //% "Opens the sign-up page of %1 in the browser. Come back afterwards and log in here with the new Matrix ID."
                      : qsTrId("rustrx-la-create-account-note").arg("matrix.org")
            }

            Item { width: 1; height: Theme.paddingLarge }

            // the code a logged-in device shows names the server, so this
            // needs no address typed first
            PageButton {
                //% "Scan a QR code"
                text: qsTrId("rustrx-bt-qr-scan")
                enabled: !rustrx.loggingIn
                onClicked: {
                    errorText = ""
                    pageStack.push(Qt.resolvedUrl("QrScanPage.qml"), { grant: false })
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                //% "A device that is logged in shows the code: Rustrx under Settings, Account, Link a new device; Element under Link new device. This phone then comes in verified."
                text: qsTrId("rustrx-la-qr-note")
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator { }
    }

    Connections {
        target: rustrx
        // the user's own Cancel (or a new QR code) is no error to show in
        // red (0.8 audit)
        onLoginError: if (message !== "login cancelled") errorText = message
        // the sign-on page's address (the window's openUrl handler opens it too)
        onBrowserUrl: if (rustrx.loggingIn) page.ssoUrl = url
        onServerInfo: {
            server = { probed: true, name: info.server, password: info.password, sso: info.sso,
                       accountService: info.account_service === true, qrLogin: info.qr_login === true,
                       idps: info.idps, sliding: info.sliding_sync, signup: info.signup_url || "" }
        }
        onErrorOccurred: if (cmd === "probe_server") errorText = message
    }
}
