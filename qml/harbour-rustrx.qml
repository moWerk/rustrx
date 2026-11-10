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
import Nemo.Notifications 1.0
import Nemo.Configuration 1.0
import "pages"

ApplicationWindow {
    id: app

    // The login and the main page replace each other at the bottom of the
    // stack; everything else is pushed on top of the main page.
    initialPage: Component { StartPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    // the rows' "3m" age labels follow this clock
    property real now: Date.now()
    Timer { interval: 60000; repeat: true; running: Qt.application.active; onTriggered: app.now = Date.now() }

    // appearance settings, shared by every page and delegate
    property alias roundAvatars: cfgRoundAvatars.value
    property alias showReadBy: cfgShowReadBy.value
    ConfigurationValue { id: cfgRoundAvatars; key: "/apps/harbour-rustrx/roundAvatars"; defaultValue: true }
    ConfigurationValue { id: cfgShowReadBy; key: "/apps/harbour-rustrx/showReadBy"; defaultValue: true }

    // flow ids of verifications that already have a page
    property var shownVerifications: ({})

    function route() {
        if (!rustrx.ready) return
        // a page transition in flight swallows the replace; come back to it
        if (pageStack.busy) { routeRetry.restart(); return }
        var current = pageStack.currentPage ? pageStack.currentPage.objectName : ""
        if (rustrx.loggedIn) {
            if (current !== "main" && (current === "login" || current === "start"))
                pageStack.replaceAbove(null, Qt.resolvedUrl("pages/MainPage.qml"))
        } else if (current !== "login") {
            console.log("route: logged out on page", current, "reason", rustrx.logoutReason)
            pageStack.replaceAbove(null, Qt.resolvedUrl("pages/LoginPage.qml"))
        }
    }

    Timer { id: routeRetry; interval: 300; onTriggered: route() }

    // "Today", "Yesterday", else the long local date
    function dayLabel(ms) {
        var d = new Date(ms), now = new Date()
        var day = function (x) { return x.getFullYear() * 400 + x.getMonth() * 32 + x.getDate() }
        //% "Today"
        if (day(d) === day(now)) return qsTrId("rustrx-la-today")
        now.setDate(now.getDate() - 1)
        //% "Yesterday"
        if (day(d) === day(now)) return qsTrId("rustrx-la-yesterday")
        return rustrx.formatDate(ms)
    }

    function showVerification(data) {
        if (shownVerifications[data.flow_id]) return
        shownVerifications[data.flow_id] = true
        pageStack.push(Qt.resolvedUrl("pages/VerificationPage.qml"), { flowId: data.flow_id, info: data })
    }

    Connections {
        target: rustrx
        onStateChanged: route()
        onOpenUrl: Qt.openUrlExternally(url)
        onVerification: {
            // an incoming request opens its page; one we started has its page already
            if (data.state === "requested" && !data.we_started) showVerification(data)
        }
        onErrorOccurred: {
            //: transient error line, %1 the command, %2 the message
            //% "%1 failed: %2"
            Notices.show(qsTrId("rustrx-no-command-failed").arg(cmd).arg(message), Notice.Long)
        }
        onNewMessages: {
            if (Qt.application.state === Qt.ApplicationActive) return
            notification.summary = roomName
            //: notification body, %n new messages
            //% "%n new messages"
            notification.body = qsTrId("rustrx-no-new-messages", count)
            notification.publish()
        }
    }

    Notification {
        id: notification
        appName: "Rustrx"
        appIcon: "harbour-rustrx"
        category: "x-nemo.messaging.im"
        urgency: Notification.Normal
    }

    Component.onCompleted: route()
}
