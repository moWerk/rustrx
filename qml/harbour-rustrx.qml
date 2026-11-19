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
import "pages"
import "components"

ApplicationWindow {
    id: app

    // The login and the main page replace each other at the bottom of the
    // stack; everything else is pushed on top of the main page.
    initialPage: Component { StartPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    // portrait and landscape: the room list and the chats suit both (the
    // camera page keeps to portrait); "Lock to portrait" keeps every page
    // upright but the picture and video viewers, which allow both themselves,
    // and gives way while a slide-out keyboard is open
    allowedOrientations: Orientation.All
    _defaultPageOrientations: cfg.portraitOnly && !keyboardSlide.open ? Orientation.Portrait : Orientation.All
    // Labels show plain text unless they ask for StyledText: Silica's
    // default (AutoText) would turn a name like "Bob <img src=...>" into a
    // picture fetched from anywhere
    _defaultLabelFormat: Text.PlainText

    // the rows' "3m" age labels follow this clock
    property real now: Date.now()
    Timer { interval: Tunables.ageTickMs; repeat: true; running: Qt.application.active; onTriggered: app.now = Date.now() }

    // Source Sans 3 Semibold (Adobe, OFL), shipped unmodified: the system font
    // is the same design but installed only in ExtraLight and Light, so bold
    // text in it never got heavier (the unread badges)
    FontLoader { id: semiboldFont; source: Qt.resolvedUrl("fonts/SourceSans3-Semibold.ttf") }
    readonly property string semiboldFontFamily: semiboldFont.status === FontLoader.Ready ? semiboldFont.name : Theme.fontFamily

    // the user's settings in dconf (components/Settings.qml); the aliases
    // below are the names the pages use
    property alias settings: cfg
    Settings {
        id: cfg
        // the emoji used most: the picker's top row and a message's reactions
        onEmojiCountsChanged: app.favouriteEmoji = service.favouriteEmoji()
    }
    // appearance, shared by every page and delegate
    property alias avatarCorners: cfg.avatarCorners
    // the avatars' corner radius as a share of the side (RoundMask)
    readonly property real avatarRadius: cfg.avatarCorners / 100
    property alias showReadBy: cfg.showReadBy
    property alias highlightFavs: cfg.highlightFavs
    property alias previewLines: cfg.previewLines
    // rooms and people as two tabs, or together in one list; favourites and
    // spaces as tabs of their own, or not
    property alias separateLists: cfg.separateLists
    property alias separateFavs: cfg.separateFavs
    property alias separateSpaces: cfg.separateSpaces
    // privacy: what others learn from us
    property alias sendReceipts: cfg.sendReceipts
    property alias readWhenShown: cfg.readWhenShown
    property alias sendTyping: cfg.sendTyping
    // the line under the message field that says the room sees you typing
    property alias typingHint: cfg.typingHint
    property alias showOnline: cfg.showOnline
    property alias chatDetails: cfg.chatDetails
    property alias strangerInvites: cfg.strangerInvites
    property alias callNoticeSeen: cfg.callNoticeSeen
    // when Rustrx is closed: "push", "background" or "off"; "" until chosen
    property alias closedMode: cfg.closedMode
    // start in the chat that was open when the app was closed
    property alias reopenLast: cfg.reopenLast
    property alias lastRoom: cfg.lastRoom
    property alias portraitOnly: cfg.portraitOnly

    // the choice made real; unchosen, push where a distributor is installed,
    // else the background stand-in (asked again at every start, so installing
    // Foghorn later moves to push)
    function applyClosedMode() {
        var m = closedMode.length ? closedMode : (rustrx.push.available ? "push" : "background")
        if (m === "push" && !rustrx.push.available) m = "background"
        rustrx.push.enabled = m === "push"
        rustrx.background = m === "background"
    }
    // favourites with new messages glow in the lists (highlightFavs).
    // The glow's one breath, shared by every glowing row so they breathe
    // together: three seconds, while a glowing row is shown and Rustrx is
    // in front (rows count themselves in favGlowRows)
    property int favGlowRows: 0
    property real favGlow: 0
    SequentialAnimation on favGlow {
        running: app.highlightFavs && app.favGlowRows > 0 && Qt.application.active
        loops: Animation.Infinite
        // longer low than high: a slow rise that hurries to the peak, a quick
        // fall that lingers near the bottom
        NumberAnimation { from: 0; to: 1; duration: Tunables.favGlowPulseMs; easing.type: Easing.InCubic }
        NumberAnimation { from: 1; to: 0; duration: Tunables.favGlowPulseMs; easing.type: Easing.OutCubic }
        onStopped: app.favGlow = 0
    }
    // the emoji used most: Service.qml's list, which the notifications'
    // buttons use too (read again when the counts change)
    property var favouriteEmoji: service.favouriteEmoji()
    function countEmoji(emoji) {
        service.countEmoji(emoji)
    }
    property bool reopenTried

    // the receipt as the privacy switch has it (Service.qml, one place)
    function markRead(roomId, shown, eventId) {
        service.markRead(roomId, shown, eventId)
    }

    // flow ids of verifications that already have a page
    property var shownVerifications: ({})

    function route() {
        if (!rustrx.ready) return
        // a page transition in flight swallows the replace; come back to it
        if (pageStack.busy) { routeRetry.restart(); return }
        var current = pageStack.currentPage ? pageStack.currentPage.objectName : ""
        // the prototype notice is up: it is answered first, then this runs again
        if (current === "prototypeNotice" || current === "secretsNotice") return
        if (rustrx.loggedIn) {
            // read before the main page comes in: becoming active, it forgets the room
            var last = !reopenTried && reopenLast ? lastRoom : ""
            reopenTried = true
            if (current === "login" || current === "start")
                pageStack.replaceAbove(null, Qt.resolvedUrl("pages/MainPage.qml"))
            // started by a notification's tap: its room, once the list knows it
            var asked = rustrx.takePendingRoom()
            if (asked.length) showRoom(asked)
            // started by a matrix: link or the share sheet
            var uri = rustrx.takePendingUri()
            if (uri.length) openMatrixUri(uri)
            var knocks = rustrx.takePendingKnocks()
            var shared = rustrx.takePendingShare()
            if (shared.files || shared.text) showShare(shared.files || [], shared.text || "")
            // nothing asked for: the chat that was open when the app was closed
            if (last.length && !asked.length && !uri.length && !knocks && !(shared.files || shared.text)) showRoom(last)
            // started by the requests' notification
            if (knocks) showKnocks()
        } else if (current !== "login") {
            console.log("route: logged out on page", current, "reason", rustrx.logoutReason)
            pageStack.replaceAbove(null, Qt.resolvedUrl("pages/LoginPage.qml"))
        }
        if (!cfg.prototypeNoticeSeen) prototypeNotice.restart()
        if (rustrx.loggedIn && rustrx.storeState === "no_secrets" && !cfg.secretsNoticeSeen) secretsNotice.restart()
    }

    Timer { id: routeRetry; interval: Tunables.retryIntervalMs; onTriggered: route() }

    // the classic call's page: shown when a call starts here or rings while
    // Rustrx is in front; it closes itself when the call ends
    function showVoip() {
        var top = pageStack.currentPage
        if (top && top.objectName === "voipPage") return
        if (pageStack.busy) { voipRetry.restart(); return }
        pageStack.push(Qt.resolvedUrl("pages/VoipPage.qml"))
    }
    Timer { id: voipRetry; interval: Tunables.retryIntervalMs; onTriggered: if (rustrx.voip.state !== "idle") app.showVoip() }
    Connections {
        target: rustrx.voip
        // ringing while Rustrx is in front (also a ring handed over by the
        // stand-in), or answered from the notification: the page follows
        onChanged: {
            var s = rustrx.voip.state
            if (s === "connecting" || s === "connected" || (s === "incoming" && Qt.application.active)) app.showVoip()
        }
    }

    // a copied secret (the recovery key) out of the clipboard after a
    // while, unless something else was copied since; here, so it outlives
    // the page it was copied on
    function copySecret(text) {
        Clipboard.text = text
        clipboardSecret.text = text
        clipboardSecret.restart()
    }
    Timer {
        id: clipboardSecret
        property string text
        interval: Tunables.clipboardKeyMs
        onTriggered: {
            if (Clipboard.text === text) Clipboard.text = ""
            text = ""
        }
    }

    // the first start: the prototype notice (and the debug log in it), once;
    // it goes only by Agree. Above whatever route() brought in, once the
    // stack is still.
    property bool prototypeNoticeUp
    Timer {
        id: prototypeNotice
        interval: Tunables.noticeDelayMs
        onTriggered: {
            if (cfg.prototypeNoticeSeen || app.prototypeNoticeUp) return
            if (pageStack.busy) { restart(); return }
            app.prototypeNoticeUp = true
            var d = pageStack.push(Qt.resolvedUrl("pages/PrototypeNotice.qml"))
            d.accepted.connect(function () {
                cfg.prototypeNoticeSeen = true
                app.prototypeNoticeUp = false
                routeRetry.restart()
            })
        }
    }
    // logged in on a phone without sailfish-secrets: what that means, once,
    // after the prototype notice; it goes only by Agree
    property bool secretsNoticeUp
    Timer {
        id: secretsNotice
        interval: Tunables.noticeDelayMs
        onTriggered: {
            if (cfg.secretsNoticeSeen || app.secretsNoticeUp || !rustrx.loggedIn) return
            if (pageStack.busy || app.prototypeNoticeUp || !cfg.prototypeNoticeSeen) { restart(); return }
            app.secretsNoticeUp = true
            var d = pageStack.push(Qt.resolvedUrl("pages/SecretsNotice.qml"))
            d.accepted.connect(function () {
                cfg.secretsNoticeSeen = true
                app.secretsNoticeUp = false
            })
        }
    }
    Timer {
        id: pendingRoomRetry
        interval: Tunables.pendingRoomPollMs
        repeat: true
        property int tries
        onTriggered: {
            if (!app.pendingRoom.length || ++tries > Tunables.retryMaxTries) { stop(); tries = 0; return }
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
        onBrowserUrl: openExternal(url)
        onVerification: {
            // an incoming request opens its page; one we started has its page already
            if (data.state === "requested" && !data.we_started) showVerification(data)
        }
        onErrorOccurred: {
            // a chat asked for by a link that could not be made: no later
            // room may open in its place
            if (cmd === "create_dm") pendingDm = false
            // the pages that sent these show their errors in place: settings
            // (recovery), keys, message source, devices, the login's server
            if (["recovery_recover", "recovery_enable", "recovery_reset", "identity_reset",
                 "export_keys", "import_keys", "event_source", "devices", "probe_server",
                 "join_by_id_or_alias", "knock", "room_preview", "search_rooms"].indexOf(cmd) >= 0) return
            // receipts go out while reading and are tried again in the core:
            // a weak network is no news to the reader
            if (cmd === "mark_read") return
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
        // the requests' notification tapped
        onKnocksRequested: if (rustrx.takePendingKnocks()) { app.activate(); app.showKnocks() }
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
        // the servers that know the room ("via"), for one on another server
        var via = []
        var q = uri.indexOf("?")
        if (q >= 0) {
            var params = uri.slice(q + 1).split("&")
            for (var i = 0; i < params.length; i++) {
                var kv = params[i].split("=")
                if (kv[0] === "via" && kv[1]) via.push(decodeURIComponent(kv[1]))
            }
        }
        if (id.charAt(0) === "@") {
            // a link must not start a chat (and tell them our id) by itself
            pageStack.push(dmDialog, { userId: id })
        } else if (id.charAt(0) === "!" && rustrx.roomInfo(id).name && rustrx.roomInfo(id).state === "joined") {
            if (rustrx.roomInfo(id).is_space)
                pageStack.push(Qt.resolvedUrl("pages/SpacePage.qml"), { spaceId: id, spaceName: rustrx.roomInfo(id).name })
            else
                showRoom(id)
        } else {
            // an alias, or a room or space we are not in: what it is first,
            // then join or ask to join
            pageStack.push(Qt.resolvedUrl("pages/JoinPage.qml"), { address: id, via: via })
        }
        return true
    }
    Component {
        id: dmDialog
        Dialog {
            property string userId
            onAccepted: {
                app.pendingDm = true
                rustrx.command({ cmd: "create_dm", user_id: userId })
            }
            Column {
                width: parent.width
                DialogHeader {
                    //% "Start chat"
                    acceptText: qsTrId("rustrx-he-start-chat")
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    wrapMode: Text.Wrap
                    color: Theme.highlightColor
                    //: %1 a Matrix user id from a link
                    //% "Start a direct chat with %1? They are invited and see your Matrix address."
                    text: qsTrId("rustrx-la-start-chat-ask").arg(userId)
                }
            }
        }
    }
    // a direct chat asked for by a matrix:u/ link: shown once it is there
    property bool pendingDm
    Connections {
        target: rustrx
        onJoined: if (app.pendingDm) { app.pendingDm = false; app.showRoom(roomId) }
        onDmReady: if (app.pendingDm) { app.pendingDm = false; app.showRoom(roomId) }
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
        interval: Tunables.retryIntervalMs
        repeat: true
        property int tries
        onTriggered: {
            if (!app.pendingShare || ++tries > Tunables.retryMaxTries) { stop(); tries = 0; return }
            app.showShare(app.pendingShare.files, app.pendingShare.text)
        }
    }

    // a link tapped inside the app: Matrix links stay here, the rest goes out
    function openLink(link) {
        // a covered spoiler in a quote: it shows only in its own message
        if (link === "rustrx:spoiler") return
        if (!openMatrixUri(link)) openExternal(link)
    }

    // Everything that leaves the app for another one goes through here. Links
    // come from other people's messages and servers: only web, mail and map
    // links are handed on, never file:, data: or anything a handler might
    // act on without asking.
    function openExternal(url) {
        var scheme = (/^([a-z][a-z0-9+.-]*):/i.exec(String(url)) || ["", ""])[1].toLowerCase()
        if (["https", "http", "mailto", "geo"].indexOf(scheme) >= 0) {
            Qt.openUrlExternally(url)
            return true
        }
        console.log("openExternal: not opened, scheme", scheme)
        //: %1 the part of a link before the colon, e.g. "file"
        //% "Not opened: %1 links are not handed on"
        Notices.show(qsTrId("rustrx-no-link-refused").arg(scheme || "?"))
        return false
    }

    // a room asked for before the main page is up (the app started from a notification)
    property string pendingRoom
    // a call's page, wherever the call was started (the room's page, a dev
    // command); and the dev commands meant for the UI (Rustrx::dev)
    Connections {
        target: rustrx
        // the first call: where calls go, once (CallNotice), then the call
        onCallUrl: cfg.callNoticeSeen
                   ? pageStack.push(app.callPage(), { roomId: roomId, url: url })
                   : pageStack.push(Qt.resolvedUrl("pages/CallNotice.qml"), { roomId: roomId, url: url, callPage: app.callPage() })
        onDevUi: {
            if (request.ui === "activate") {
                app.activate()
            } else if (request.ui === "open_room") {
                app.activate()
                app.showRoom(request.room_id)
            } else if (request.ui === "settings") {
                // Settings, on a tab by its kind ("account", "privacy", "app", "about")
                app.activate()
                pageStack.push(Qt.resolvedUrl("pages/SettingsPage.qml"), { startTab: request.tab || "" })
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

    // a join rule as people say it; space: the space's name where the rule
    // lets its members in
    function joinRuleName(rule, space) {
        //% "Only invited people"
        if (rule === "invite") return qsTrId("rustrx-la-rule-invite")
        //% "Ask to join"
        if (rule === "knock") return qsTrId("rustrx-la-rule-knock")
        //% "Anyone"
        if (rule === "public") return qsTrId("rustrx-la-rule-public")
        //: %1 a space
        //% "Members of %1"
        if (rule === "restricted") return space && space.length ? qsTrId("rustrx-la-rule-restricted").arg(space)
        //% "Members of its spaces"
                                                                  : qsTrId("rustrx-la-rule-restricted-any")
        //% "Space members, others ask"
        if (rule === "knock_restricted") return qsTrId("rustrx-la-rule-knock-restricted")
        return rule
    }

    // each room's permissions and settings as the core last sent them
    // (room_settings): the message rows ask what one may do in a room
    property var roomSettingsById: ({})
    Connections {
        target: rustrx
        onRoomSettings: {
            var all = {}
            for (var k in app.roomSettingsById) all[k] = app.roomSettingsById[k]
            all[settings.room_id] = settings
            app.roomSettingsById = all
        }
    }
    function roomMay(roomId) {
        var s = roomSettingsById[roomId]
        return s && s.may ? s.may : ({})
    }
    function roomPinned(roomId) {
        var s = roomSettingsById[roomId]
        return s && s.pinned ? s.pinned : []
    }

    // the open requests to join one room or space
    function knockCount(roomId) {
        var n = 0, all = rustrx.knockRequests
        for (var i = 0; i < all.length; i++) if (all[i].room_id === roomId) n++
        return n
    }

    function showKnocks() {
        if (pageStack.busy) { knocksRetry.restart(); return }
        var top = pageStack.currentPage
        if (top && top.objectName === "knocks") return
        pageStack.push(Qt.resolvedUrl("pages/KnockPage.qml"))
    }
    Timer { id: knocksRetry; interval: Tunables.retryIntervalMs; onTriggered: app.showKnocks() }

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

    // debug: close the app as the tile's close does
    DebugValue { id: debugClose; name: "debugClose"; defaultValue: false }
    Connections { target: debugClose; onValueChanged: if (debugClose.value === true) rustrx.closeWindow() }
    // debug: back to the home screen (to look at the tile)
    DebugValue { id: debugMinimize; name: "debugMinimize"; defaultValue: false }
    Connections { target: debugMinimize; onValueChanged: if (debugMinimize.value === true) app.deactivate() }

    Component.onCompleted: {
        // the old "Round avatars" switch, off: square corners, once
        if (!cfg.roundAvatars) { cfg.avatarCorners = 0; cfg.roundAvatars = true }
        applyClosedMode(); route()
    }

    // custom emoji in messages: a little taller than the text, like an emoji
    Binding { target: rustrx.timeline; property: "emoteSize"; value: Math.round(Theme.fontSizeMedium * Tunables.emoteScale) }
    Binding { target: rustrx.thread; property: "emoteSize"; value: Math.round(Theme.fontSizeMedium * Tunables.emoteScale) }
}
