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
import Nemo.Notifications 1.0
import Nemo.KeepAlive 1.2
import "components"

// Notifications and, in background mode, the wake job, apart from the pages
// (harbour-rustrx.qml reaches this as `service`).
//
// A window here lives exactly as long as its process: the home screen
// crashed when a running app's window was taken down or made later
// (lipstick 0.37.6, a WindowPixmapItem on a deleted surface). So in
// background mode closing the app ends it as everywhere on Sailfish, and a
// stand-in without a window (rustrx.standIn, the user unit) takes over the
// notifications until the app is opened again.
Item {
    id: service

    // this engine's own copy of the settings (components/Settings.qml)
    Settings { id: cfg }
    // whether a notification shows the message itself or only a count
    property alias notifyText: cfg.notifyText
    // who and which chat, outside the app (notifications; the cover reads it too)
    property alias chatDetails: cfg.chatDetails
    property alias notifyReply: cfg.notifyReply
    property alias notifyReactions: cfg.notifyReactions
    property alias notifyJoins: cfg.notifyJoins
    property alias notifyEmoji: cfg.notifyEmoji
    // the stand-in's line in the events view, on unless switched off
    property alias showBackgroundLine: cfg.showBackgroundLine

    Connections {
        target: rustrx
        // every room notifies, also while Rustrx is open, except the room in
        // front of you (dodoradio: a chat open, the others went silent; on
        // the cover, the open one did). The stand-in has no window, and
        // without one Qt reports it active
        // every account's rooms notify (account: whose); not the shown
        // account's room in front of you
        onNewMessages: {
            if (!rustrx.standIn && Qt.application.state === Qt.ApplicationActive && !rustrx.screenLocked
                    && account === rustrx.accountKey && roomId === rustrx.openRoomId) return
            service.notify(account, roomId, roomName, count)
        }
        // read here or in another client: the banner and its line go
        onRoomRead: { service.closeNotification(account, roomId); service.closeJoins(account, roomId) }
        // the notification's "Mark as read": it goes at once, the receipt
        // follows when logged in (a process woken for it logs in first)
        onMarkReadRequested: {
            service.closeNotification(account, roomId)
            service.markRead(roomId, undefined, undefined, account)
        }
        // its reply field and emoji buttons: answered means read. A reply
        // is followed until the server has it, and one that failed is said
        // in a notification of its own, its text kept as the room's draft
        // (0.8 audit: it was lost without a trace, the room marked read)
        onReplyRequested: {
            service.closeNotification(account, roomId)
            service.whenLoggedIn(account, { cmd: "notification_reply", room_id: roomId, body: text })
        }
        onReplySent: service.markRead(roomId, undefined, undefined, account)
        onReplyFailed: service.notifyReplyFailed(account, roomId, kept)
        onMemberJoined: if (service.notifyJoins) service.notifyJoin(account, roomId, roomName, name)
        onReactRequested: {
            service.closeNotification(account, roomId)
            service.countEmoji(key)
            service.whenLoggedIn(account, { cmd: "react", room_id: roomId, event_id: eventId, key: key })
            service.markRead(roomId, undefined, undefined, account)
        }
        // the core sets offline at every login; online only when asked
        onAccountsChanged: {
            service.flushPending()
            var all = rustrx.accounts
            var now = {}
            for (var i = 0; i < all.length; i++) {
                if (!all[i].logged_in) continue
                now[all[i].key] = true
                if (!service.presenceSet[all[i].key] && cfg.showOnline)
                    rustrx.commandTo(all[i].key, { cmd: "set_presence", online: true })
            }
            // one logged out and in again is set again
            service.presenceSet = now
        }
        // from someone we share no room with: no banner, if so set
        onInvited: if (known || cfg.strangerInvites) service.notifyInvite(account, roomId, roomName, inviter, direct)
        onInviteGone: service.closeInvite(account, roomId)
        onCallRing: service.ringCall(account, roomId, roomName, senderName, ring, eventId, expiresMs)
        onNewKnock: service.notifyKnock(account, roomName, isSpace, name, reason)
        onPushNotification: service.notifyPush(account, roomId, eventId, roomName, senderName, body, isDm)
        // each account's requests have their own line
        onAccountKnocksChanged: if (rustrx.knockCountIn(account) === 0) service.closeKnocks(account)
        onCallAnswered: service.stopRinging(roomId, false)
        onUnreadChanged: if (rustrx.standIn) console.log("stand-in: unread", rustrx.unreadTotal, "in", rustrx.unreadChats)
        onOpenRoomChanged: if (rustrx.openRoomId.length) { service.closeNotification(rustrx.accountKey, rustrx.openRoomId); service.stopRinging(rustrx.openRoomId, false); service.closeMissed(rustrx.accountKey, rustrx.openRoomId) }
        onQuitRequested: standInNote.close()
    }
    // the accounts whose presence was set since they logged in here
    property var presenceSet: ({})
    // a classic call (Voip) rings like an Element Call ring; Answer and
    // Decline reach it through the same notification actions
    Connections {
        target: rustrx.voip
        onIncoming: service.ringCall(rustrx.voipAccount(), roomId, roomName, peerName, true, "", ringMs)
        onChanged: if (rustrx.voip.state !== "incoming") service.stopRinging(rustrx.voip.roomId, service.voipUnanswered())
    }

    // a notification's action: a call to Rustrx's D-Bus interface; args and
    // label (the button's text) only where the action has them
    // viaPush: to the push connector's name, so that with Rustrx closed the
    // window-less push process takes it instead of the app opening
    function action(name, method, args, label, viaPush) {
        var a = {
            "name": name,
            "service": viaPush ? rustrx.connectorName : rustrx.busName,
            "path": rustrx.objectPath,
            "iface": rustrx.interfaceName,
            "method": method
        }
        if (args) a["arguments"] = args
        if (label) a["displayName"] = label
        return a
    }

    // commands from a notification's buttons: a process woken for one
    // (push wake) logs in first, so they wait for it
    // account: whose (Account key); none means the shown account
    property var pendingCommands: []
    function whenLoggedIn(account, cmd) {
        if (account === undefined) account = rustrx.accountKey
        if (rustrx.accountLoggedIn(account)) rustrx.commandTo(account, cmd)
        else pendingCommands.push({ account: account, cmd: cmd })
    }
    function flushPending() {
        var waiting = pendingCommands
        pendingCommands = []
        for (var i = 0; i < waiting.length; i++) whenLoggedIn(waiting[i].account, waiting[i].cmd)
    }
    // shown: false leaves the unread line where it is (the room page moves
    // it when the room is left); eventId: read up to this message only,
    // else the whole room; account: whose room (the shown account's if none)
    function markRead(roomId, shown, eventId, account) {
        // the privacy switch: a private receipt moves only our own marker
        var c = { cmd: "mark_read", room_id: roomId, "private": !cfg.sendReceipts, marker: shown !== false }
        if (eventId) c.event_id = eventId
        whenLoggedIn(account, c)
    }

    // the emoji used most (five; the defaults fill up until five have been
    // used), for the picker and the notifications' buttons
    // what may stand in the quick reactions (the author's rule): an emoji,
    // or text of at most 4 characters, never a Matrix address (a room id
    // landed there on dodoradio's phone and ran over its neighbours)
    function quickReaction(key) {
        if (!key || /:\/\//.test(key) || /^[!@#$+][^\s:]*:\S/.test(key)) return false
        return rustrx.emojiUrl(key).length > 0 || key.length <= 4
    }
    function favouriteEmoji() {
        var counts = {}
        try { counts = JSON.parse(cfg.emojiCounts || "{}") } catch (e) {}
        var used = Object.keys(counts).filter(quickReaction)
                .sort(function (a, b) { return counts[b] - counts[a] }).slice(0, Tunables.recentEmojiCount)
        var defaults = ["👍", "❤️", "😂", "😮", "😢"]
        for (var i = 0; used.length < Tunables.recentEmojiCount && i < defaults.length; i++)
            if (used.indexOf(defaults[i]) < 0) used.push(defaults[i])
        return used
    }
    function countEmoji(emoji) {
        if (!quickReaction(emoji)) return
        var counts = {}
        try { counts = JSON.parse(cfg.emojiCounts || "{}") } catch (e) {}
        counts[emoji] = (counts[emoji] || 0) + 1
        cfg.emojiCounts = JSON.stringify(counts)
    }

    // a room notification's buttons, in this order: open (a tap), Reply (a
    // text field in the notification), Mark as read, three emoji on the
    // latest message; reply and emoji can be switched off in Settings.
    // lipstick puts the buttons in one row while they fit and stacks every
    // one of them otherwise: these fit a phone's row (Jolla Phone 2026: 871
    // of 972 px). With push on and a distributor there they go to the push
    // connector, so a closed Rustrx answers them without its window; without
    // a distributor nobody may own that name and they would be lost, so they
    // go to the app's own name (with Rustrx closed a tap then opens it).
    // the user's three from Settings; the default when none are left
    readonly property var notificationEmoji: {
        var picked = String(cfg.notifyEmoji || "").split(/\s+/).filter(function (e) { return e.length > 0 }).slice(0, 3)
        return picked.length ? picked : ["👍", "😀", "👎"]
    }
    // the emoji a line's buttons offer for eventId
    function offeredEmoji(eventId) {
        return cfg.notifyReactions && eventId && eventId.length ? notificationEmoji : []
    }
    // a room line goes up: its Reply, Mark as read and emoji are taken only
    // while it is (rustrx.notePosted, the audit's IPC1)
    function publishRoom(n, account, roomId, eventId) {
        rustrx.notePosted(account, roomId, eventId || "", offeredEmoji(eventId))
        n.publish()
    }
    // each names its account (as text: the arguments arrive as written)
    function roomActions(account, roomId, eventId) {
        var viaPush = rustrx.push.enabled && rustrx.push.available
        var a = String(account)
        var actions = [ action("default", "openAccountRoom", [ a, roomId ]) ]
        if (cfg.notifyReply) {
            //% "Reply"
            var reply = action("reply", "replyFromAccountNotification", [ a, roomId ], qsTrId("rustrx-me-notification-reply"), viaPush)
            reply["type"] = "input"
            actions.push(reply)
        }
        //% "Mark as read"
        actions.push(action("mark_read", "markAccountRoomRead", [ a, roomId ], qsTrId("rustrx-me-notification-read"), viaPush))
        if (cfg.notifyReactions && eventId && eventId.length) {
            var emoji = offeredEmoji(eventId)
            for (var i = 0; i < emoji.length; i++)
                actions.push(action("react" + i, "reactFromAccountNotification", [ a, roomId, eventId, emoji[i] ], emoji[i], viaPush))
        }
        return actions
    }

    // With several accounts, which one a row or page is for: its name, or
    // its user id where another account has the same name; "" with one
    // account. Text, so it also tells accounts apart with the colours off.
    function accountLabel(key) {
        var all = rustrx.accounts
        if (all.length < 2) return ""
        var mine = null
        for (var i = 0; i < all.length; i++) if (all[i].key === key) mine = all[i]
        if (!mine) return ""
        var name = mine.name || ""
        for (var j = 0; j < all.length; j++) if (all[j].key !== key && all[j].name === name) name = ""
        return name.length ? name : (mine.user_id || "")
    }
    // a message notification's picture: its sender's (a direct chat's own
    // picture, else the room row's latest sender), rounded as the room list
    // has it; "" (the app's icon) without one, while chat details stay
    // inside Rustrx, or when the row's latest event is not eventId (a push)
    function senderIcon(info, eventId) {
        var path = info.is_dm ? info.avatar_path : info.preview_sender_avatar
        if (!chatDetails || !path) return ""
        if (eventId && !info.is_dm && info.latest_event_id !== eventId) return ""
        return rustrx.notificationIcon(path, cfg.roundAvatars ? cfg.avatarCorners / 100 : 0)
    }

    // one notification per room and account, updated in place: the events
    // view keeps a line per room, the banner shows the latest message
    property var roomNotifications: ({})
    property var roomPending: ({})
    function noteKey(account, roomId) { return account + "/" + roomId }
    function roomNote(account, roomId) {
        takeOverOld()
        var k = noteKey(account, roomId)
        var n = roomNotifications[k]
        if (!n) {
            n = notificationComponent.createObject(service, { roomId: roomId, account: account })
            roomNotifications[k] = n
        }
        // a line taken over from an earlier process keeps the category it
        // was posted with (before 0.4.8 x-nemo.messaging.im, no vibration)
        n.category = "x-harbour-rustrx.message"
        return n
    }
    // count: the room's own notification count, as the server and the SDK
    // have it (a push before it, counted here, is replaced by it)
    function notify(account, roomId, roomName, count) {
        if (holdsLine(account, roomId)) {
            heldLines[noteKey(account, roomId)] = { roomName: roomName, count: count }
            console.log("notify: held while the room rings, account", account)
            return
        }
        console.log("notify:", count, "account", account)
        var n = roomNote(account, roomId)
        var info = rustrx.roomInfoIn(account, roomId)
        var last = (info.preview_sender && chatDetails ? info.preview_sender + ": " : "") + (info.preview || "")
        var pending = count
        roomPending[noteKey(account, roomId)] = pending
        var latest = info.latest_event_id || ""
        n.remoteActions = roomActions(account, roomId, latest)
        n.icon = senderIcon(info, "")
        n.summary = chatDetails ? roomName : "Rustrx"
        n.previewSummary = n.summary
        //: notification body, %n new messages
        //% "%n new messages"
        n.body = last.length && notifyText ? last : qsTrId("rustrx-no-new-messages", pending)
        n.previewBody = n.body
        n.itemCount = pending
        publishRoom(n, account, roomId, latest)
    }
    // an invitation: a banner, also while Rustrx is in front (they are easy
    // to miss in the list); a tap opens it, where it is accepted or declined
    function notifyInvite(account, roomId, roomName, inviter, direct) {
        takeOverOld()
        console.log("notify: invitation, account", account)
        var n = roomNote(account, roomId)
        var who = inviter.length ? inviter : roomName
        //: notification title for an invitation
        //% "Invitation"
        n.summary = qsTrId("rustrx-no-invitation")
        // no inviter's picture: the room's line may have shown a sender's
        n.icon = ""
        n.previewSummary = n.summary
        n.body = !chatDetails
                //% "Someone invites you to a chat"
                ? qsTrId("rustrx-no-invite-hidden")
                : direct
                //: %1 who invites
                //% "%1 wants to chat with you"
                ? qsTrId("rustrx-no-invite-dm").arg(who)
                //: %1 who invites, %2 the room
                //% "%1 invites you to %2"
                : qsTrId("rustrx-no-invite-room").arg(who).arg(roomName)
        n.previewBody = n.body
        n.itemCount = 1
        // a new line has the room's buttons, without emoji
        publishRoom(n, account, roomId, "")
    }
    // an invitation answered some other way: its banner goes, also one an
    // earlier Rustrx process posted (the app and the stand-in take turns)
    function closeInvite(account, roomId) {
        closeNotification(account, roomId)
        var all = standInNote.notifications()
        for (var i = 0; i < all.length; i++) {
            var actions = all[i].remoteActions
            if (actions && actions.length && noteKeyOf(actions[0]) === noteKey(account, roomId))
                all[i].close()
        }
    }
    // the room and account an earlier process's line is for, by its tap's
    // action (before 0.7 it named no account: the first one); "" for others
    function noteKeyOf(first) {
        var args = first.arguments || []
        if (first.method === "openAccountRoom" && args.length === 2) return noteKey(args[0], args[1])
        if (first.method === "openRoomFromNotification" && args.length === 1) return noteKey(0, args[0])
        return ""
    }

    // an incoming call: a notification that rings with the system's VoIP
    // ringtone (lipstick plays its feedback while it is shown), Answer and
    // Decline; it goes when it expires, is answered or declined, the call
    // ends, or the room is opened. A group call's start only notifies.
    property var ringing: ({})
    function ringCall(account, roomId, roomName, senderName, ring, eventId, expiresMs) {
        takeOverOld()
        stopRinging(roomId)
        var n = ringComponent.createObject(service, { roomId: roomId, eventId: eventId, ring: ring, account: account,
                                                      senderName: senderName })
        // who calls and from where stays inside Rustrx when chat names do (Privacy)
        n.summary = !chatDetails
                //: notification title when chat names stay inside Rustrx
                //% "Incoming call"
                ? qsTrId("rustrx-no-call-anonymous")
                : ring
                //: notification title, %1 who calls
                //% "%1 is calling"
                ? qsTrId("rustrx-no-call-ring").arg(senderName)
                //: notification title, %1 the room
                //% "Call in %1"
                : qsTrId("rustrx-no-call-group").arg(roomName)
        n.previewSummary = n.summary
        n.body = !chatDetails ? "Rustrx"
                : ring ? roomName
                //: notification body, %1 who started the call
                //% "%1 started a call"
                : qsTrId("rustrx-no-call-started").arg(senderName)
        n.previewBody = n.body
        n.expiry.interval = Math.max(Tunables.ringMinMs, expiresMs)
        n.expiry.start()
        // Answer and Decline only while it rings
        rustrx.noteRinging(account, roomId, eventId, n.expiry.interval)
        n.publish()
        var r = ringing
        r[roomId] = n
        ringing = r
        console.log("call: incoming", ring ? "ring" : "notification", "for", expiresMs, "ms")
        // with Rustrx closed: the app starts with the call, its notification up
        rustrx.ringPosted(ring)
    }
    // unanswered: it expired, the caller gave up or another device of ours
    // declined; false: answered or declined here, or the room opened;
    // undefined: a new ring takes its place
    function stopRinging(roomId, unanswered) {
        var n = ringing[roomId]
        if (!n) return
        // a classic call's (no event id) that nobody took: a missed call,
        // which is then the only notification for it (the author's wish)
        var missed = unanswered === true && n.ring && n.eventId === ""
        if (missed) notifyMissed(n.account, roomId, n.senderName)
        rustrx.noteRingEnded(n.account, roomId)
        n.close()
        if (n.adopted) n.adopted.close()
        n.destroy()
        var r = ringing
        delete r[roomId]
        ringing = r
        if (unanswered === true && !missed) releaseHeld(n.account, roomId)
        else if (unanswered !== undefined) delete heldLines[noteKey(n.account, roomId)]
    }
    // a missed classic call, as the phone app has it: who called, "Missed
    // call", the missed-call icon of its category, quiet; a tap opens the
    // room. One per call, all of a room's closed when the room is opened.
    property var missedNotes: ({})
    function notifyMissed(account, roomId, senderName) {
        takeOverOld()
        // an adopted ring knows no name: the call engine still has it
        if (!senderName && rustrx.voip.roomId === roomId) senderName = rustrx.voip.peerName
        var n = missedComponent.createObject(service, { account: account, roomId: roomId })
        var missed =
                //: notification body (or, while chat names stay inside Rustrx, its title) for a call nobody answered
                //% "Missed call"
                qsTrId("rustrx-la-missed-call")
        // who called stays inside Rustrx when chat names do (Privacy)
        n.summary = chatDetails && senderName ? senderName : missed
        n.previewSummary = n.summary
        n.body = chatDetails && senderName ? missed : "Rustrx"
        n.previewBody = n.body
        n.publish()
        keepMissed(noteKey(account, roomId), n)
        console.log("call: missed, account", account)
    }
    function keepMissed(k, n) {
        var all = missedNotes
        all[k] = (all[k] || []).concat([n])
        missedNotes = all
    }
    function closeMissed(account, roomId) {
        var k = noteKey(account, roomId)
        var notes = missedNotes[k]
        if (!notes) return
        for (var i = 0; i < notes.length; i++) {
            notes[i].close()
            // ours are made here; one adopted from an earlier process is not
            if (notes[i].missedHere) notes[i].destroy()
        }
        var all = missedNotes
        delete all[k]
        missedNotes = all
    }
    Component {
        id: missedComponent
        Notification {
            property string roomId
            property int account
            property bool missedHere: true
            appName: "Rustrx"
            // its icon from the category (app_icon), as the phone app's
            category: "x-harbour-rustrx.missed"
            isTransient: false
            remoteActions: [ service.action("default", "openAccountRoom", [ String(account), roomId ]) ]
        }
    }
    // how the classic call that stopped ringing ended
    function voipUnanswered() {
        var why = rustrx.voip.endReason
        return rustrx.voip.state === "idle" && (why === "invite_timeout" || why === "hangup"
                || (why === "answered_elsewhere" && rustrx.voip.declinedElsewhere()))
    }
    // a room's message line waits while the room rings (the author's wish):
    // the last one asked for, posted when the ring ends unanswered and the
    // room is still unread, dropped when it was answered or declined here
    // or a missed call stands for it
    property var heldLines: ({})
    function holdsLine(account, roomId) {
        var n = ringing[roomId]
        return !!n && n.account === account
    }
    function releaseHeld(account, roomId) {
        var k = noteKey(account, roomId)
        var held = heldLines[k]
        if (!held) return
        delete heldLines[k]
        var info = rustrx.roomInfoIn(account, roomId)
        // a push-woken process may not know the room yet: posted as it came
        var known = info.unread !== undefined
        if (known && !(info.unread > 0) && !(info.notifications > 0)) return
        console.log("notify: line after a ring, account", account)
        if (held.push) {
            // the pushes that came while it rang count, as without the ring
            roomPending[k] = (roomPending[k] || 0) + held.pushes - 1
            notifyPush(account, roomId, held.eventId, held.roomName, held.senderName, held.body, held.isDm)
        } else notify(account, roomId, held.roomName, known && info.notifications > 0 ? info.notifications : held.count)
    }
    // the call ended before anyone answered (the room's state says nobody is in it)
    Connections {
        target: rustrx.all
        onDataChanged: {
            for (var id in service.ringing) {
                var info = rustrx.roomInfoIn(service.ringing[id].account, id)
                if (!info.in_call && service.ringing[id].seenCall) service.stopRinging(id, true)
                else if (info.in_call) service.ringing[id].seenCall = true
            }
        }
    }

    Component {
        id: ringComponent
        Notification {
            id: ringNote
            property string roomId
            property string eventId
            property bool ring
            property int account
            // who calls, for a missed call ("" for one adopted from an earlier process)
            property string senderName
            // the room's state showed the call running once; its end then ends the ring
            property bool seenCall
            // an earlier process's ring this one stands for (adoptRing): it
            // is closed in its place, this one is never posted
            property var adopted
            property Timer expiry: Timer { onTriggered: service.stopRinging(ringNote.roomId, true) }
            appName: "Rustrx"
            appIcon: "harbour-rustrx"
            // a ringing call: the VoIP ringtone, the display on
            // (notificationcategories/)
            category: ring ? "x-harbour-rustrx.call" : "x-harbour-rustrx.message"
            urgency: ring ? Notification.Critical : Notification.Normal
            isTransient: false
            remoteActions: [
                service.action("default", "openAccountRoom", [ String(account), roomId ]),
                //% "Answer"
                service.action("answer", "answerAccountCall", [ String(account), roomId ], qsTrId("rustrx-me-call-answer")),
                //% "Decline"
                service.action("decline", "declineAccountCall", [ String(account), roomId, eventId ], qsTrId("rustrx-me-call-decline"))
            ]
        }
    }

    // a pushed message (a process woken for it): the room's line, as a
    // message seen by the sync would make it; text as Settings has it
    function notifyPush(account, roomId, eventId, roomName, senderName, body, isDm) {
        takeOverOld()
        if (holdsLine(account, roomId)) {
            var before = heldLines[noteKey(account, roomId)]
            heldLines[noteKey(account, roomId)] = { push: true, eventId: eventId, roomName: roomName, senderName: senderName,
                                                    body: body, isDm: isDm, pushes: (before && before.push ? before.pushes : 0) + 1 }
            console.log("notify: push held while the room rings, account", account)
            return
        }
        console.log("notify: push, account", account)
        var n = roomNote(account, roomId)
        var pending = (roomPending[noteKey(account, roomId)] || 0) + 1
        roomPending[noteKey(account, roomId)] = pending
        var who = senderName && !isDm && chatDetails ? senderName + ": " : ""
        n.remoteActions = roomActions(account, roomId, eventId)
        n.icon = senderIcon(rustrx.roomInfoIn(account, roomId), eventId)
        n.summary = chatDetails ? roomName : "Rustrx"
        n.previewSummary = n.summary
        n.body = body && notifyText ? who + body : qsTrId("rustrx-no-new-messages", pending)
        n.previewBody = n.body
        n.itemCount = pending
        publishRoom(n, account, roomId, eventId)
    }

    // requests to join: one notification per account, the latest request
    // in it; a tap opens that account's list (0.8 audit: it opened the shown
    // account's, and closed when the shown account had none)
    property var knockNotes: ({})
    // someone new in a room one moderates (Settings > App, on unless
    // switched off; the author's wish): one line per room, its latest
    // newcomer and how many came; a tap opens the room, reading it ends it
    property var joinNotes: ({})
    function notifyJoin(account, roomId, roomName, name) {
        takeOverOld()
        var k = noteKey(account, roomId)
        var n = joinNotes[k]
        if (!n) {
            n = joinComponent.createObject(service, { account: account, roomId: roomId })
            joinNotes[k] = n
        }
        n.count += 1
        //: notification title, %1 the room
        //% "New in %1"
        n.summary = chatDetails ? qsTrId("rustrx-no-join-title").arg(roomName)
                                //% "Someone joined a room you moderate"
                                : qsTrId("rustrx-no-join-hidden")
        n.previewSummary = n.summary
        n.body = !chatDetails ? "Rustrx"
                : n.count > 1
                //: %1 the latest newcomer, %n how many joined since
                //% "%1 and others joined (%n)"
                ? qsTrId("rustrx-no-join-many", n.count).arg(name)
                //: %1 who joined
                //% "%1 joined"
                : qsTrId("rustrx-no-join-one").arg(name)
        n.previewBody = n.body
        n.itemCount = n.count
        n.publish()
    }
    function closeJoins(account, roomId) {
        var k = noteKey(account, roomId)
        var n = joinNotes[k]
        if (!n) return
        n.close()
        n.destroy()
        delete joinNotes[k]
    }
    Component {
        id: joinComponent
        Notification {
            property int account
            property string roomId
            property int count
            appName: "Rustrx"
            appIcon: "harbour-rustrx"
            // a quiet line, no vibration: news, not a message
            category: "x-nemo.messaging.im"
            remoteActions: [ service.action("default", "openAccountRoom", [ String(account), roomId ]) ]
        }
    }

    function notifyKnock(account, roomName, isSpace, name, reason) {
        takeOverOld()
        var n = knockNotes[account]
        if (!n) {
            n = knockComponent.createObject(service, { account: account })
            knockNotes[account] = n
        }
        //: notification title for a request to join
        //% "Request to join"
        n.summary = qsTrId("rustrx-no-knock")
        n.previewSummary = n.summary
        n.body = !chatDetails
                //% "Someone asks to join"
                ? qsTrId("rustrx-no-knock-hidden")
                : isSpace
                //: %1 who asks, %2 the space
                //% "%1 asks to join the space %2"
                ? qsTrId("rustrx-no-knock-space").arg(name).arg(roomName)
                //: %1 who asks, %2 the room
                //% "%1 asks to join %2"
                : qsTrId("rustrx-no-knock-room").arg(name).arg(roomName)
        n.previewBody = n.body
        n.itemCount = Math.max(1, rustrx.knockCountIn(account))
        n.publish()
    }
    function closeKnocks(account) {
        var n = knockNotes[account]
        if (!n) return
        n.close()
        n.destroy()
        delete knockNotes[account]
    }
    Component {
        id: knockComponent
        Notification {
            property int account
            appName: "Rustrx"
            appIcon: "harbour-rustrx"
            category: "x-nemo.messaging.im"
            remoteActions: [ service.action("default", "openAccountKnocks", [ String(account) ]) ]
        }
    }

    // a reply from a notification that did not reach the server: said in
    // the room's line, a tap opens the room with the text as its draft
    // (kept: the core added it there)
    function notifyReplyFailed(account, roomId, kept) {
        takeOverOld()
        console.log("notify: reply not sent, account", account)
        var n = roomNote(account, roomId)
        var name = chatDetails ? (rustrx.roomInfoIn(account, roomId).name || "") : ""
        n.remoteActions = [ action("default", "openAccountRoom", [ String(account), roomId ]) ]
        // its only button opens the room
        rustrx.noteClosed(account, roomId)
        n.icon = ""
        n.summary = name.length
                //: notification title, %1 the room
                //% "Your reply to %1 was not sent"
                ? qsTrId("rustrx-no-reply-failed").arg(name)
                //% "Your reply was not sent"
                : qsTrId("rustrx-no-reply-failed-hidden")
        n.previewSummary = n.summary
        n.body = kept
                //% "Tap to open the chat, your text is waiting there."
                ? qsTrId("rustrx-no-reply-failed-kept")
                //% "Tap to open the chat."
                : qsTrId("rustrx-no-reply-failed-open")
        n.previewBody = n.body
        n.itemCount = 1
        n.publish()
    }

    function closeNotification(account, roomId) {
        takeOverOld()
        var k = noteKey(account, roomId)
        delete roomPending[k]
        // read meanwhile: nothing left to post after a ring
        delete heldLines[k]
        // also a line an earlier process posted that is gone from the list
        rustrx.noteClosed(account, roomId)
        var n = roomNotifications[k]
        if (!n) return
        n.close()
        delete roomNotifications[k]
        // ours are made here; one adopted from an earlier process is not
        if (n.roomId !== undefined) n.destroy()
    }

    Component {
        id: notificationComponent
        Notification {
            property string roomId
            property int account
            appName: "Rustrx"
            appIcon: "harbour-rustrx"
            // our own (notificationcategories/): x-nemo.messaging.im's
            // feedback is sound and light only, Messages vibrates through a
            // preview of its own; this one names both (the QML type has no
            // way to set the hint itself)
            category: "x-harbour-rustrx.message"
            urgency: Notification.Normal
            remoteActions: service.roomActions(account, roomId, "")
            onClosed: delete service.roomPending[service.noteKey(account, roomId)]
        }
    }

    // Background mode: while the phone sleeps the sync is frozen; every five
    // minutes (in the system's shared wake slot) the job wakes it and keeps it
    // awake for a short while, so new messages arrive and notify.
    BackgroundJob {
        id: wakeJob
        enabled: rustrx.background && rustrx.accounts.some(function (a) { return a.logged_in })
        frequency: BackgroundJob.FiveMinutes
        onTriggered: { console.log("background: woken"); wakeHold.restart() }
    }
    Timer { id: wakeHold; interval: Tunables.wakeHoldMs; onTriggered: wakeJob.finished() }

    // the stand-in's line in the events view: tap opens Rustrx, Quit ends it
    Notification {
        id: standInNote
        appName: "Rustrx"
        appIcon: "harbour-rustrx"
        //% "Matrix client is running"
        summary: qsTrId("rustrx-no-background")
        //% "Tap to open Rustrx"
        body: qsTrId("rustrx-no-background-body")
        urgency: Notification.Low
        isTransient: false
        remoteActions: [
            service.action("default", "open"),
            //% "Quit"
            service.action("quit", "quit", null, qsTrId("rustrx-me-quit"))
        ]
    }

    // an earlier process's notifications (the stand-in's, or a killed
    // run's): room lines are taken over, so a new message updates them and
    // opening the room closes them; the stand-in's line goes. Asking for
    // them blocks on the home screen: the window-less processes call this
    // at once, the app after its first frame (main.cpp); a room's line
    // needed before then (every publish) takes them over first, or the takeover
    // would close or adopt a line this process has just posted
    property bool oldTakenOver
    function takeOverOld() {
        if (oldTakenOver) return
        oldTakenOver = true
        rustrx.perf("notes_taken")
        var old = standInNote.notifications()
        for (var i = 0; i < old.length; i++) {
            // a missed call's tap opens its room too: kept as one, never a room line
            if (old[i].category === "x-harbour-rustrx.missed") {
                var mk = noteKeyOf(old[i].remoteActions.length ? old[i].remoteActions[0] : {})
                if (mk.length) keepMissed(mk, old[i])
                continue
            }
            // a ring's tap opens its room too: it is a ring first
            var ring = ringOf(old[i].remoteActions)
            if (ring) {
                adoptRing(old[i], ring)
                continue
            }
            var first = old[i].remoteActions.length ? old[i].remoteActions[0] : {}
            var k = noteKeyOf(first)
            if (k.length && !roomNotifications[k]) {
                roomNotifications[k] = old[i]
                if (!rustrx.notesKnown()) adoptButtons(old[i].remoteActions)
            } else {
                old[i].close()
            }
        }
    }
    // an earlier process's ring, by its Answer and Decline (before 0.7
    // they named no account: the first one): { account, roomId, eventId }
    // (a classic call's eventId is ""), null for other lines
    function ringOf(actions) {
        var ring = null, eventId = ""
        for (var i = 0; i < actions.length; i++) {
            var args = actions[i].arguments || []
            var method = actions[i].method
            if (method === "answerAccountCall" && args.length === 2) ring = { account: Number(args[0]), roomId: args[1] }
            else if (method === "answerCall" && args.length === 1) ring = { account: 0, roomId: args[0] }
            else if (method === "declineAccountCall" && args.length === 3) eventId = args[2]
            else if (method === "declineCall" && args.length === 2) eventId = args[1]
        }
        if (ring) ring.eventId = eventId
        return ring
    }
    // a ring the stand-in posted (a call handed over by a launcher tap, a
    // link or another room's tap) rings on as one of this process's own and
    // goes as they do: expired, answered, declined, the call over, the room
    // opened. One that is over or whose room is open goes now.
    function adoptRing(old, ring) {
        var left = rustrx.ringLeftMs(ring.account, ring.roomId)
        var open = rustrx.openRoomId.length > 0 && ring.account === rustrx.accountKey && ring.roomId === rustrx.openRoomId
        if (left <= 0 || open || ringing[ring.roomId]) {
            if (!ringing[ring.roomId]) rustrx.noteRingEnded(ring.account, ring.roomId)
            old.close()
            return
        }
        var n = ringComponent.createObject(service, { roomId: ring.roomId, eventId: ring.eventId, account: ring.account,
                                                      ring: old.category === "x-harbour-rustrx.call", adopted: old })
        n.expiry.interval = left
        n.expiry.start()
        var r = ringing
        r[ring.roomId] = n
        ringing = r
        console.log("call: ring taken over from an earlier process, for", left, "ms")
    }
    // the first run of a Rustrx that keeps the list: lines posted before
    // the update keep their buttons
    function adoptButtons(actions) {
        var room = null, eventId = "", emoji = []
        for (var i = 0; i < actions.length; i++) {
            var args = actions[i].arguments || []
            if ((actions[i].method === "markAccountRoomRead" || actions[i].method === "replyFromAccountNotification") && args.length >= 2)
                room = { account: Number(args[0]), roomId: args[1] }
            else if (actions[i].method === "reactFromAccountNotification" && args.length === 4) {
                eventId = args[2]
                emoji.push(args[3])
            }
        }
        if (room) rustrx.notePosted(room.account, room.roomId, eventId, emoji)
    }

    Component.onCompleted: {
        // before its own line, which would be taken for an old one
        if (rustrx.standIn) takeOverOld()
        // the line can be switched off (Settings); the stand-in runs either way
        if (rustrx.standIn && cfg.showBackgroundLine) standInNote.publish()
    }
}
