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
    // privacy: what others learn from us
    property alias sendReceipts: cfgSendReceipts.value
    property alias sendTyping: cfgSendTyping.value
    // whether a notification shows the message itself or only a count
    property alias notifyText: cfgNotifyText.value
    ConfigurationValue { id: cfgNotifyText; key: "/apps/harbour-rustrx/notifyText"; defaultValue: true }
    ConfigurationValue { id: cfgRoundAvatars; key: "/apps/harbour-rustrx/roundAvatars"; defaultValue: true }
    ConfigurationValue { id: cfgShowReadBy; key: "/apps/harbour-rustrx/showReadBy"; defaultValue: true }
    ConfigurationValue { id: cfgSendReceipts; key: "/apps/harbour-rustrx/sendReceipts"; defaultValue: true }
    ConfigurationValue { id: cfgSendTyping; key: "/apps/harbour-rustrx/sendTyping"; defaultValue: true }

    // the receipt honours the privacy switch: a private one moves only our own marker
    function markRead(roomId) {
        rustrx.command({ cmd: "mark_read", room_id: roomId, "private": !sendReceipts })
    }

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
            // started by a notification's tap: its room, once the list knows it
            var asked = rustrx.takePendingRoom()
            if (asked.length) showRoom(asked)
            // started by a matrix: link or the share sheet
            var uri = rustrx.takePendingUri()
            if (uri.length) openMatrixUri(uri)
            var shared = rustrx.takePendingShare()
            if (shared.files || shared.text) showShare(shared.files || [], shared.text || "")
        } else if (current !== "login") {
            console.log("route: logged out on page", current, "reason", rustrx.logoutReason)
            pageStack.replaceAbove(null, Qt.resolvedUrl("pages/LoginPage.qml"))
        }
    }

    Timer { id: routeRetry; interval: 300; onTriggered: route() }
    Timer {
        id: pendingRoomRetry
        interval: 500
        repeat: true
        property int tries
        onTriggered: {
            if (!app.pendingRoom.length || ++tries > 40) { stop(); tries = 0; return }
            if (!pageStack.busy) app.showRoom(app.pendingRoom)
        }
    }

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
            // the settings page shows the recovery errors in place
            if (cmd === "recovery_recover" || cmd === "recovery_enable" || cmd === "recovery_reset") return
            //: transient error line, %1 the command, %2 the message
            //% "%1 failed: %2"
            Notices.show(qsTrId("rustrx-no-command-failed").arg(cmd).arg(message), Notice.Long)
        }
        onNewMessages: {
            if (Qt.application.state === Qt.ApplicationActive) return
            app.notify(roomId, roomName, count)
        }
        // a tapped notification: bring the window up and show the room
        onOpenRoomRequested: {
            rustrx.takePendingRoom()
            app.activate()
            app.closeNotification(roomId)
            app.showRoom(roomId)
        }
        onOpenRoomChanged: if (rustrx.openRoomId.length) app.closeNotification(rustrx.openRoomId)
        onMatrixUriRequested: { rustrx.takePendingUri(); app.activate(); app.openMatrixUri(uri) }
        onShareRequested: { rustrx.takePendingShare(); app.activate(); app.showShare(files, text) }
    }

    // one notification per room, updated in place: the events view keeps a
    // line per room, the banner shows the latest message
    property var roomNotifications: ({})
    function notify(roomId, roomName, count) {
        var n = roomNotifications[roomId]
        if (!n) {
            n = notificationComponent.createObject(app, { roomId: roomId })
            roomNotifications[roomId] = n
        }
        var info = rustrx.roomInfo(roomId)
        var last = (info.preview_sender ? info.preview_sender + ": " : "") + (info.preview || "")
        n.pending += count
        n.summary = roomName
        n.previewSummary = roomName
        //: notification body, %n new messages
        //% "%n new messages"
        n.body = last.length && notifyText ? last : qsTrId("rustrx-no-new-messages", n.pending)
        n.previewBody = n.body
        n.itemCount = n.pending
        n.publish()
    }
    function closeNotification(roomId) {
        var n = roomNotifications[roomId]
        if (!n) return
        n.close()
        n.destroy()
        delete roomNotifications[roomId]
    }
    // matrix:r/alias:server, matrix:roomid/id:server, matrix:u/user:server
    // (MSC2312) and https://matrix.to/#/<id or alias>; false when it is neither
    function openMatrixUri(uri) {
        var id = ""
        var m = /^matrix:(r|roomid|u)\/([^?#]+)/.exec(uri)
        if (m) {
            id = (m[1] === "r" ? "#" : m[1] === "u" ? "@" : "!") + decodeURIComponent(m[2].split("/")[0])
        } else {
            m = /^https?:\/\/matrix\.to\/#\/([^?]+)/.exec(uri)
            if (!m) return false
            id = decodeURIComponent(m[1].split("/")[0])
        }
        if (!rustrx.loggedIn) return true
        if (id.charAt(0) === "@") {
            pendingJoin = "dm"
            rustrx.command({ cmd: "create_dm", user_id: id })
        } else if (id.charAt(0) === "!" && rustrx.roomInfo(id).name) {
            showRoom(id)
        } else {
            // an alias, or a room we are not in: joining opens it
            pendingJoin = id
            rustrx.command({ cmd: "join_by_id_or_alias", id: id })
        }
        return true
    }
    property string pendingJoin
    Connections {
        target: rustrx
        onJoined: if (app.pendingJoin.length) { app.pendingJoin = ""; app.showRoom(roomId) }
        onDmReady: if (app.pendingJoin === "dm") { app.pendingJoin = ""; app.showRoom(roomId) }
    }

    function showShare(files, text) {
        if (!rustrx.loggedIn) return
        pageStack.push(Qt.resolvedUrl("pages/SharePage.qml"), { files: files, text: text })
    }

    // a link tapped inside the app: Matrix links stay here, the rest goes out
    function openLink(link) {
        if (!openMatrixUri(link)) Qt.openUrlExternally(link)
    }

    // a room asked for before the main page is up (the app started from a notification)
    property string pendingRoom
    function showRoom(roomId) {
        if (rustrx.openRoomId === roomId) return
        var main = pageStack.find(function (p) { return p.objectName === "main" })
        if (!main || !rustrx.roomInfo(roomId).name) { pendingRoom = roomId; pendingRoomRetry.restart(); return }
        pendingRoom = ""
        pageStack.pop(main, PageStackAction.Immediate)
        pageStack.push(Qt.resolvedUrl("pages/RoomPage.qml"),
                       { roomId: roomId, roomName: rustrx.roomInfo(roomId).name || "" }, PageStackAction.Immediate)
    }

    Component {
        id: notificationComponent
        Notification {
            property string roomId
            property int pending
            appName: "Rustrx"
            appIcon: "harbour-rustrx"
            category: "x-nemo.messaging.im"
            urgency: Notification.Normal
            remoteActions: [{
                "name": "default",
                "service": "net.mowerk.harbour-rustrx",
                "path": "/net/mowerk/harbour_rustrx",
                "iface": "net.mowerk.harbour_rustrx",
                "method": "openRoomFromNotification",
                "arguments": [ roomId ]
            }]
            onClosed: pending = 0
        }
    }

    Component.onCompleted: route()
}
