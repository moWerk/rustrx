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
    property alias notifyEmoji: cfg.notifyEmoji
    // the stand-in's line in the events view, on unless switched off
    property alias showBackgroundLine: cfg.showBackgroundLine

    Connections {
        target: rustrx
        // every room notifies, also while Rustrx is open, except the room in
        // front of you (dodoradio: a chat open, the others went silent; on
        // the cover, the open one did). The stand-in has no window, and
        // without one Qt reports it active
        onNewMessages: {
            if (!rustrx.standIn && Qt.application.state === Qt.ApplicationActive
                    && roomId === rustrx.openRoomId) return
            service.notify(roomId, roomName, count)
        }
        // read here or in another client: the banner and its line go
        onRoomRead: service.closeNotification(roomId)
        // the notification's "Mark as read": it goes at once, the receipt
        // follows when logged in (a process woken for it logs in first)
        onMarkReadRequested: {
            service.closeNotification(roomId)
            service.markRead(roomId)
        }
        // its reply field and emoji buttons: answered means read
        onReplyRequested: {
            service.closeNotification(roomId)
            service.whenLoggedIn({ cmd: "send_text", room_id: roomId, body: text })
            service.markRead(roomId)
        }
        onReactRequested: {
            service.closeNotification(roomId)
            service.countEmoji(key)
            service.whenLoggedIn({ cmd: "react", room_id: roomId, event_id: eventId, key: key })
            service.markRead(roomId)
        }
        // the core sets offline at every login; online only when asked
        onStateChanged: if (rustrx.loggedIn) {
            service.flushPending()
            if (cfg.showOnline) rustrx.command({ cmd: "set_presence", online: true })
        }
        // from someone we share no room with: no banner, if so set
        onInvited: if (known || cfg.strangerInvites) service.notifyInvite(roomId, roomName, inviter, direct)
        onInviteGone: service.closeInvite(roomId)
        onCallRing: service.ringCall(roomId, roomName, senderName, ring, eventId, expiresMs)
    }
    // a classic call (Voip) rings like an Element Call ring; Answer and
    // Decline reach it through the same notification actions
    Connections {
        target: rustrx.voip
        onIncoming: service.ringCall(roomId, roomName, peerName, true, "", Tunables.voipRingMs)
        onChanged: if (rustrx.voip.state !== "incoming") service.stopRinging(rustrx.voip.roomId)
        onNewKnock: service.notifyKnock(roomName, isSpace, name, reason)
        onPushNotification: service.notifyPush(roomId, eventId, roomName, senderName, body, isDm)
        onKnockRequestsChanged: if (rustrx.knockRequests.length === 0) knockNote.close()
        onCallAnswered: service.stopRinging(roomId)
        onUnreadChanged: if (rustrx.standIn) console.log("stand-in: unread", rustrx.unreadTotal, "in", rustrx.unreadChats)
        onOpenRoomChanged: if (rustrx.openRoomId.length) { service.closeNotification(rustrx.openRoomId); service.stopRinging(rustrx.openRoomId) }
        onQuitRequested: standInNote.close()
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
    property var pendingCommands: []
    function whenLoggedIn(cmd) {
        if (rustrx.loggedIn) rustrx.command(cmd)
        else pendingCommands.push(cmd)
    }
    function flushPending() {
        var cmds = pendingCommands
        pendingCommands = []
        for (var i = 0; i < cmds.length; i++) rustrx.command(cmds[i])
    }
    // shown: false leaves the unread line where it is (the room page moves
    // it when the room is left); eventId: read up to this message only,
    // else the whole room
    function markRead(roomId, shown, eventId) {
        // the privacy switch: a private receipt moves only our own marker
        var c = { cmd: "mark_read", room_id: roomId, "private": !cfg.sendReceipts, marker: shown !== false }
        if (eventId) c.event_id = eventId
        whenLoggedIn(c)
    }

    // the emoji used most (five; the defaults fill up until five have been
    // used), for the picker and the notifications' buttons
    function favouriteEmoji() {
        var counts = {}
        try { counts = JSON.parse(cfg.emojiCounts || "{}") } catch (e) {}
        var used = Object.keys(counts).sort(function (a, b) { return counts[b] - counts[a] }).slice(0, Tunables.recentEmojiCount)
        var defaults = ["👍", "❤️", "😂", "😮", "😢"]
        for (var i = 0; used.length < Tunables.recentEmojiCount && i < defaults.length; i++)
            if (used.indexOf(defaults[i]) < 0) used.push(defaults[i])
        return used
    }
    function countEmoji(emoji) {
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
    // of 972 px). With push on they go to the push connector, so a closed
    // Rustrx answers them without its window.
    // the user's three from Settings; the default when none are left
    readonly property var notificationEmoji: {
        var picked = String(cfg.notifyEmoji || "").split(/\s+/).filter(function (e) { return e.length > 0 }).slice(0, 3)
        return picked.length ? picked : ["👍", "😀", "👎"]
    }
    function roomActions(roomId, eventId) {
        var viaPush = rustrx.push.enabled
        var actions = [ action("default", "openRoomFromNotification", [ roomId ]) ]
        if (cfg.notifyReply) {
            //% "Reply"
            var reply = action("reply", "replyFromNotification", [ roomId ], qsTrId("rustrx-me-notification-reply"), viaPush)
            reply["type"] = "input"
            actions.push(reply)
        }
        //% "Mark as read"
        actions.push(action("mark_read", "markRoomRead", [ roomId ], qsTrId("rustrx-me-notification-read"), viaPush))
        if (cfg.notifyReactions && eventId && eventId.length) {
            var emoji = notificationEmoji
            for (var i = 0; i < emoji.length; i++)
                actions.push(action("react" + i, "reactFromNotification", [ roomId, eventId, emoji[i] ], emoji[i], viaPush))
        }
        return actions
    }

    // one notification per room, updated in place: the events view keeps a
    // line per room, the banner shows the latest message
    property var roomNotifications: ({})
    property var roomPending: ({})
    function roomNote(roomId) {
        var n = roomNotifications[roomId]
        if (!n) {
            n = notificationComponent.createObject(service, { roomId: roomId })
            roomNotifications[roomId] = n
        }
        // a line taken over from an earlier process keeps the category it
        // was posted with (before 0.4.8 x-nemo.messaging.im, no vibration)
        n.category = "x-harbour-rustrx.message"
        return n
    }
    // count: the room's own notification count, as the server and the SDK
    // have it (a push before it, counted here, is replaced by it)
    function notify(roomId, roomName, count) {
        console.log("notify:", count)
        var n = roomNote(roomId)
        var info = rustrx.roomInfo(roomId)
        var last = (info.preview_sender && chatDetails ? info.preview_sender + ": " : "") + (info.preview || "")
        var pending = count
        roomPending[roomId] = pending
        n.remoteActions = roomActions(roomId, info.latest_event_id || "")
        n.summary = chatDetails ? roomName : "Rustrx"
        n.previewSummary = n.summary
        //: notification body, %n new messages
        //% "%n new messages"
        n.body = last.length && notifyText ? last : qsTrId("rustrx-no-new-messages", pending)
        n.previewBody = n.body
        n.itemCount = pending
        n.publish()
    }
    // an invitation: a banner, also while Rustrx is in front (they are easy
    // to miss in the list); a tap opens it, where it is accepted or declined
    function notifyInvite(roomId, roomName, inviter, direct) {
        console.log("notify: invitation")
        var n = roomNote(roomId)
        var who = inviter.length ? inviter : roomName
        //: notification title for an invitation
        //% "Invitation"
        n.summary = qsTrId("rustrx-no-invitation")
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
        n.publish()
    }
    // an invitation answered some other way: its banner goes, also one an
    // earlier Rustrx process posted (the app and the stand-in take turns)
    function closeInvite(roomId) {
        closeNotification(roomId)
        var all = standInNote.notifications()
        for (var i = 0; i < all.length; i++) {
            var actions = all[i].remoteActions
            if (actions && actions.length && actions[0].arguments && actions[0].arguments[0] === roomId)
                all[i].close()
        }
    }

    // an incoming call: a notification that rings with the system's VoIP
    // ringtone (lipstick plays its feedback while it is shown), Answer and
    // Decline; it goes when it expires, is answered or declined, the call
    // ends, or the room is opened. A group call's start only notifies.
    property var ringing: ({})
    function ringCall(roomId, roomName, senderName, ring, eventId, expiresMs) {
        stopRinging(roomId)
        var n = ringComponent.createObject(service, { roomId: roomId, eventId: eventId, ring: ring })
        n.summary = ring
                //: notification title, %1 who calls
                //% "%1 is calling"
                ? qsTrId("rustrx-no-call-ring").arg(senderName)
                //: notification title, %1 the room
                //% "Call in %1"
                : qsTrId("rustrx-no-call-group").arg(roomName)
        n.previewSummary = n.summary
        n.body = ring ? roomName
                //: notification body, %1 who started the call
                //% "%1 started a call"
                : qsTrId("rustrx-no-call-started").arg(senderName)
        n.previewBody = n.body
        n.expiry.interval = Math.max(Tunables.ringMinMs, expiresMs)
        n.expiry.start()
        n.publish()
        var r = ringing
        r[roomId] = n
        ringing = r
        console.log("call: incoming", ring ? "ring" : "notification", "for", expiresMs, "ms")
    }
    function stopRinging(roomId) {
        var n = ringing[roomId]
        if (!n) return
        n.close()
        n.destroy()
        var r = ringing
        delete r[roomId]
        ringing = r
    }
    // the call ended before anyone answered (the room's state says nobody is in it)
    Connections {
        target: rustrx.all
        onDataChanged: {
            for (var id in service.ringing)
                if (!rustrx.roomInfo(id).in_call && service.ringing[id].seenCall) service.stopRinging(id)
                else if (rustrx.roomInfo(id).in_call) service.ringing[id].seenCall = true
        }
    }

    Component {
        id: ringComponent
        Notification {
            id: ringNote
            property string roomId
            property string eventId
            property bool ring
            // the room's state showed the call running once; its end then ends the ring
            property bool seenCall
            property Timer expiry: Timer { onTriggered: service.stopRinging(ringNote.roomId) }
            appName: "Rustrx"
            appIcon: "harbour-rustrx"
            // a ringing call: the VoIP ringtone, the display on
            // (notificationcategories/)
            category: ring ? "x-harbour-rustrx.call" : "x-harbour-rustrx.message"
            urgency: ring ? Notification.Critical : Notification.Normal
            isTransient: false
            remoteActions: [
                service.action("default", "openRoomFromNotification", [ roomId ]),
                //% "Answer"
                service.action("answer", "answerCall", [ roomId ], qsTrId("rustrx-me-call-answer")),
                //% "Decline"
                service.action("decline", "declineCall", [ roomId, eventId ], qsTrId("rustrx-me-call-decline"))
            ]
        }
    }

    // a pushed message (a process woken for it): the room's line, as a
    // message seen by the sync would make it; text as Settings has it
    function notifyPush(roomId, eventId, roomName, senderName, body, isDm) {
        console.log("notify: push")
        var n = roomNote(roomId)
        var pending = (roomPending[roomId] || 0) + 1
        roomPending[roomId] = pending
        var who = senderName && !isDm && chatDetails ? senderName + ": " : ""
        n.remoteActions = roomActions(roomId, eventId)
        n.summary = chatDetails ? roomName : "Rustrx"
        n.previewSummary = n.summary
        n.body = body && notifyText ? who + body : qsTrId("rustrx-no-new-messages", pending)
        n.previewBody = n.body
        n.itemCount = pending
        n.publish()
    }

    // requests to join: one notification, the latest request in it, a tap
    // opens the list
    function notifyKnock(roomName, isSpace, name, reason) {
        //: notification title for a request to join
        //% "Request to join"
        knockNote.summary = qsTrId("rustrx-no-knock")
        knockNote.previewSummary = knockNote.summary
        knockNote.body = !chatDetails
                //% "Someone asks to join"
                ? qsTrId("rustrx-no-knock-hidden")
                : isSpace
                //: %1 who asks, %2 the space
                //% "%1 asks to join the space %2"
                ? qsTrId("rustrx-no-knock-space").arg(name).arg(roomName)
                //: %1 who asks, %2 the room
                //% "%1 asks to join %2"
                : qsTrId("rustrx-no-knock-room").arg(name).arg(roomName)
        knockNote.previewBody = knockNote.body
        knockNote.itemCount = rustrx.knockRequests.length
        knockNote.publish()
    }
    Notification {
        id: knockNote
        appName: "Rustrx"
        appIcon: "harbour-rustrx"
        category: "x-nemo.messaging.im"
        remoteActions: [ service.action("default", "openKnocks") ]
    }

    function closeNotification(roomId) {
        delete roomPending[roomId]
        var n = roomNotifications[roomId]
        if (!n) return
        n.close()
        delete roomNotifications[roomId]
        // ours are made here; one adopted from an earlier process is not
        if (n.roomId !== undefined) n.destroy()
    }

    Component {
        id: notificationComponent
        Notification {
            property string roomId
            appName: "Rustrx"
            appIcon: "harbour-rustrx"
            // our own (notificationcategories/): x-nemo.messaging.im's
            // feedback is sound and light only, Messages vibrates through a
            // preview of its own; this one names both (the QML type has no
            // way to set the hint itself)
            category: "x-harbour-rustrx.message"
            urgency: Notification.Normal
            remoteActions: service.roomActions(roomId, "")
            onClosed: delete service.roomPending[roomId]
        }
    }

    // Background mode: while the phone sleeps the sync is frozen; every five
    // minutes (in the system's shared wake slot) the job wakes it and keeps it
    // awake for a short while, so new messages arrive and notify.
    BackgroundJob {
        id: wakeJob
        enabled: rustrx.background && rustrx.loggedIn
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

    Component.onCompleted: {
        // an earlier process's notifications (the stand-in's, or a killed
        // run's): room lines are taken over, so a new message updates them and
        // opening the room closes them; the stand-in's line goes
        var old = standInNote.notifications()
        for (var i = 0; i < old.length; i++) {
            var first = old[i].remoteActions.length ? old[i].remoteActions[0] : {}
            if (first.method === "openRoomFromNotification" && first.arguments && first.arguments.length
                    && !roomNotifications[first.arguments[0]])
                roomNotifications[first.arguments[0]] = old[i]
            else
                old[i].close()
        }
        // the line can be switched off (Settings); the stand-in runs either way
        if (rustrx.standIn && cfg.showBackgroundLine) standInNote.publish()
    }
}
