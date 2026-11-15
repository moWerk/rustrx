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
import Nemo.Configuration 1.0
import "pages"

ApplicationWindow {
    id: app

    // The login and the main page replace each other at the bottom of the
    // stack; everything else is pushed on top of the main page.
    initialPage: Component { StartPage { } }
    cover: coverSource
    // portrait and landscape: the room list and the chats suit both (the
    // camera page keeps to portrait)
    allowedOrientations: Orientation.All
    _defaultPageOrientations: Orientation.All

    // the rows' "3m" age labels follow this clock
    property real now: Date.now()
    Timer { interval: 60000; repeat: true; running: Qt.application.active; onTriggered: app.now = Date.now() }

    // Source Sans 3 Semibold (Adobe, OFL), shipped unmodified: the system font
    // is the same design but installed only in ExtraLight and Light, so bold
    // text in it never got heavier (the unread badges)
    FontLoader { id: semiboldFont; source: Qt.resolvedUrl("fonts/SourceSans3-Semibold.ttf") }
    readonly property string semiboldFontFamily: semiboldFont.status === FontLoader.Ready ? semiboldFont.name : Theme.fontFamily

    // appearance settings, shared by every page and delegate
    property alias roundAvatars: cfgRoundAvatars.value
    property alias showReadBy: cfgShowReadBy.value
    // privacy: what others learn from us
    property alias sendReceipts: cfgSendReceipts.value
    property alias sendTyping: cfgSendTyping.value
    ConfigurationValue { id: cfgRoundAvatars; key: "/apps/harbour-rustrx/roundAvatars"; defaultValue: true }
    // rooms and people as two tabs (default), or together in one list
    property alias separateLists: cfgSeparateLists.value
    ConfigurationValue { id: cfgSeparateLists; key: "/apps/harbour-rustrx/separateLists"; defaultValue: true }
    // the favourites as a tab of their own (default), or not
    property alias separateFavs: cfgSeparateFavs.value
    ConfigurationValue { id: cfgSeparateFavs; key: "/apps/harbour-rustrx/separateFavs"; defaultValue: true }
    // how many lines of the last message the room list shows (1 to 3)
    property alias previewLines: cfgPreviewLines.value
    ConfigurationValue { id: cfgPreviewLines; key: "/apps/harbour-rustrx/previewLines"; defaultValue: 1 }
    ConfigurationValue { id: cfgShowReadBy; key: "/apps/harbour-rustrx/showReadBy"; defaultValue: true }
    ConfigurationValue { id: cfgSendReceipts; key: "/apps/harbour-rustrx/sendReceipts"; defaultValue: true }
    ConfigurationValue { id: cfgSendTyping; key: "/apps/harbour-rustrx/sendTyping"; defaultValue: true }
    // the emoji used most: the picker's top row and a message's reactions
    // (five; the defaults fill up until five have been used)
    ConfigurationValue {
        id: cfgEmojiCounts
        key: "/apps/harbour-rustrx/emojiCounts"
        defaultValue: "{}"
        onValueChanged: app.favouriteEmoji = app.computeFavourites()
    }
    property var favouriteEmoji: computeFavourites()
    function computeFavourites() {
        var counts = {}
        try { counts = JSON.parse(cfgEmojiCounts.value || "{}") } catch (e) {}
        var used = Object.keys(counts).sort(function (a, b) { return counts[b] - counts[a] }).slice(0, 5)
        var defaults = ["👍", "❤️", "😂", "😮", "😢"]
        for (var i = 0; used.length < 5 && i < defaults.length; i++)
            if (used.indexOf(defaults[i]) < 0) used.push(defaults[i])
        return used
    }
    function countEmoji(emoji) {
        var counts = {}
        try { counts = JSON.parse(cfgEmojiCounts.value || "{}") } catch (e) {}
        counts[emoji] = (counts[emoji] || 0) + 1
        cfgEmojiCounts.value = JSON.stringify(counts)
    }
    // the line under the message field that says the room sees you typing
    property alias typingHint: cfgTypingHint.value
    ConfigurationValue { id: cfgTypingHint; key: "/apps/harbour-rustrx/typingHint"; defaultValue: true }
    // start in the chat that was open when the app was closed (off by default)
    property alias reopenLast: cfgReopenLast.value
    property alias lastRoom: cfgLastRoom.value
    ConfigurationValue { id: cfgReopenLast; key: "/apps/harbour-rustrx/reopenLast"; defaultValue: false }
    ConfigurationValue { id: cfgLastRoom; key: "/apps/harbour-rustrx/lastRoom"; defaultValue: "" }
    property bool reopenTried

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
        // the prototype notice is up: it is answered first, then this runs again
        if (current === "prototypeNotice") return
        if (rustrx.loggedIn) {
            // read before the main page comes in: becoming active, it forgets the room
            var last = !reopenTried && reopenLast ? lastRoom : ""
            reopenTried = true
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
            // nothing asked for: the chat that was open when the app was closed
            if (last.length && !asked.length && !uri.length && !(shared.files || shared.text)) showRoom(last)
        } else if (current !== "login") {
            console.log("route: logged out on page", current, "reason", rustrx.logoutReason)
            pageStack.replaceAbove(null, Qt.resolvedUrl("pages/LoginPage.qml"))
        }
        if (!cfgPrototypeNoticeSeen.value) prototypeNotice.restart()
    }

    Timer { id: routeRetry; interval: 300; onTriggered: route() }

    // the first start: the prototype notice (and the debug log in it), once;
    // it goes only by Agree. Above whatever route() brought in, once the
    // stack is still.
    ConfigurationValue { id: cfgPrototypeNoticeSeen; key: "/apps/harbour-rustrx/prototypeNoticeSeen"; defaultValue: false }
    property bool prototypeNoticeUp
    Timer {
        id: prototypeNotice
        interval: 600
        onTriggered: {
            if (cfgPrototypeNoticeSeen.value || app.prototypeNoticeUp) return
            if (pageStack.busy) { restart(); return }
            app.prototypeNoticeUp = true
            var d = pageStack.push(Qt.resolvedUrl("pages/PrototypeNotice.qml"))
            d.accepted.connect(function () {
                cfgPrototypeNoticeSeen.value = true
                app.prototypeNoticeUp = false
                routeRetry.restart()
            })
        }
    }
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
        onBrowserUrl: Qt.openUrlExternally(url)
        onVerification: {
            // an incoming request opens its page; one we started has its page already
            if (data.state === "requested" && !data.we_started) showVerification(data)
        }
        onErrorOccurred: {
            // the settings page shows the recovery errors in place
            if (cmd === "recovery_recover" || cmd === "recovery_enable" || cmd === "recovery_reset") return
            var what = actionLabel(cmd)
            Notices.show(what.length
                         //: transient error line, %1 what was tried, %2 the server's or the network's message
                         //% "%1 failed: %2"
                         ? qsTrId("rustrx-no-action-failed").arg(what).arg(message)
                         //: transient error line for anything without its own name, %1 the message
                         //% "Something went wrong: %1"
                         : qsTrId("rustrx-no-something-failed").arg(message), Notice.Long)
        }
        // a tapped notification: bring the window up and show the room
        // (on a cold start route() has taken it already)
        onOpenRoomRequested: {
            if (!rustrx.takePendingRoom().length) return
            app.activate()
            service.closeNotification(roomId)
            app.showRoom(roomId)
            // Answer on a call while its room is open already: join now
            // (a room opened anew starts it itself, RoomPage)
            if (rustrx.openRoomId === roomId && rustrx.takePendingCall() === roomId)
                rustrx.command({ cmd: "call_start", room_id: roomId })
        }
        //: a call was started where only moderators could take part; the starter could change that
        //% "Everyone in this room can now take part in calls"
        onCallsAllowed: Notices.show(qsTrId("rustrx-no-calls-allowed"), Notice.Long)
        onMatrixUriRequested: if (rustrx.takePendingUri().length) { app.activate(); app.openMatrixUri(uri) }
        onShareRequested: {
            var shared = rustrx.takePendingShare()
            if (shared.files || shared.text) { app.activate(); app.showShare(files, text) }
        }
    }

    // the core's command, as the user knows the action; "" for the rare ones
    function actionLabel(cmd) {
        switch (cmd) {
        //% "Sending"
        case "send_text": case "send_file": return qsTrId("rustrx-la-act-send")
        //% "Reacting"
        case "react": return qsTrId("rustrx-la-act-react")
        //% "Removing the message"
        case "redact": return qsTrId("rustrx-la-act-redact")
        //% "Editing"
        case "edit": return qsTrId("rustrx-la-act-edit")
        //% "Joining the room"
        case "join_by_id_or_alias": case "join_room": return qsTrId("rustrx-la-act-join")
        //% "Starting the chat"
        case "create_dm": return qsTrId("rustrx-la-act-dm")
        //% "Creating the room"
        case "create_room": return qsTrId("rustrx-la-act-create-room")
        //% "Leaving the room"
        case "leave_room": return qsTrId("rustrx-la-act-leave")
        //% "Inviting"
        case "invite": return qsTrId("rustrx-la-act-invite")
        //% "Changing the room"
        case "set_room_name": case "set_room_topic": return qsTrId("rustrx-la-act-room")
        //% "Changing favourites"
        case "set_favourite": case "set_low_priority": return qsTrId("rustrx-la-act-fav")
        //% "Changing notifications"
        case "set_notification_mode": return qsTrId("rustrx-la-act-notify")
        //% "Voting"
        case "poll_vote": return qsTrId("rustrx-la-act-vote")
        //% "Searching"
        case "search": return qsTrId("rustrx-la-act-search")
        //% "Loading older messages"
        case "paginate_back": return qsTrId("rustrx-la-act-older")
        //% "Loading the picture"
        case "get_media": case "export_media": return qsTrId("rustrx-la-act-media")
        default: return ""
        }
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

    // a page transition in flight (the main page coming in on a cold
    // start) refuses the push: keep the share and try again
    property var pendingShare: null
    function showShare(files, text) {
        if (!rustrx.loggedIn) return
        if (pageStack.busy || !pageStack.find(function (p) { return p.objectName === "main" })) {
            pendingShare = { files: files, text: text }
            shareRetry.restart()
            return
        }
        pendingShare = null
        pageStack.push(Qt.resolvedUrl("pages/SharePage.qml"), { files: files, text: text })
    }
    Timer {
        id: shareRetry
        interval: 300
        repeat: true
        property int tries
        onTriggered: {
            if (!app.pendingShare || ++tries > 40) { stop(); tries = 0; return }
            app.showShare(app.pendingShare.files, app.pendingShare.text)
        }
    }

    // a link tapped inside the app: Matrix links stay here, the rest goes out
    function openLink(link) {
        if (!openMatrixUri(link)) Qt.openUrlExternally(link)
    }

    // a room asked for before the main page is up (the app started from a notification)
    property string pendingRoom
    // a call's page, wherever the call was started (the room's page, a dev
    // command); and the dev commands meant for the UI (Rustrx::dev)
    Connections {
        target: rustrx
        onCallUrl: pageStack.push(app.callPage(), { roomId: roomId, url: url })
        onDevUi: {
            if (request.ui === "activate") {
                app.activate()
            } else if (request.ui === "open_room") {
                app.activate()
                app.showRoom(request.room_id)
            } else if (request.ui === "back") {
                if (pageStack.depth > 1) pageStack.pop()
            }
        }
    }

    // the call page on the add-on Gecko 153 WebView where it is installed
    // (OpenRepos, sailfish-components-webview-qt5-next153): Gecko 115 can
    // not do encrypted calls. Otherwise the system's WebView.
    property string _callPage
    function callPage() {
        if (!_callPage) {
            var next = Qt.resolvedUrl("pages/CallPageNext153.qml")
            var c = Qt.createComponent(next)
            _callPage = c.status === Component.Ready ? next : Qt.resolvedUrl("pages/CallPage.qml")
            console.log("calls: web engine", c.status === Component.Ready ? "Gecko 153 add-on" : "system")
        }
        return _callPage
    }

    function showRoom(roomId) {
        if (rustrx.openRoomId === roomId) return
        var main = pageStack.find(function (p) { return p.objectName === "main" })
        if (!main || !rustrx.roomInfo(roomId).name) { pendingRoom = roomId; pendingRoomRetry.restart(); return }
        pendingRoom = ""
        pageStack.pop(main, PageStackAction.Immediate)
        // an invitation is answered in the list: its tab, where it sits on top
        var info = rustrx.roomInfo(roomId)
        if (info.state === "invited") { main.showTab(info.is_dm ? 1 : 0); return }
        pageStack.push(Qt.resolvedUrl("pages/RoomPage.qml"),
                       { roomId: roomId, roomName: rustrx.roomInfo(roomId).name || "" }, PageStackAction.Immediate)
    }

    property var coverSource: Qt.resolvedUrl("cover/CoverPage.qml")

    // debug: close the app as the tile's close does
    ConfigurationValue { id: debugClose; key: "/apps/harbour-rustrx/debugClose"; defaultValue: false }
    Connections { target: debugClose; onValueChanged: if (debugClose.value === true) rustrx.closeWindow() }
    // debug: back to the home screen (to look at the tile)
    ConfigurationValue { id: debugMinimize; key: "/apps/harbour-rustrx/debugMinimize"; defaultValue: false }
    Connections { target: debugMinimize; onValueChanged: if (debugMinimize.value === true) app.deactivate() }

    Component.onCompleted: route()

    // custom emoji in messages: a little taller than the text, like an emoji
    Binding { target: rustrx.timeline; property: "emoteSize"; value: Math.round(Theme.fontSizeMedium * 1.25) }
}
