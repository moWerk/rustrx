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
import Nemo.Notifications 1.0
import Nemo.Configuration 1.0
import Nemo.KeepAlive 1.2

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

    // whether a notification shows the message itself or only a count
    property alias notifyText: cfgNotifyText.value
    ConfigurationValue { id: cfgNotifyText; key: "/apps/harbour-rustrx/notifyText"; defaultValue: true }

    Connections {
        target: rustrx
        // the stand-in has no window, and without one Qt reports it active
        onNewMessages: {
            if (!rustrx.standIn && Qt.application.state === Qt.ApplicationActive) return
            service.notify(roomId, roomName, count)
        }
        onUnreadChanged: if (rustrx.standIn) console.log("stand-in: unread", rustrx.unreadTotal, "in", rustrx.unreadChats)
        onOpenRoomChanged: if (rustrx.openRoomId.length) service.closeNotification(rustrx.openRoomId)
        onQuitRequested: standInNote.close()
    }

    // one notification per room, updated in place: the events view keeps a
    // line per room, the banner shows the latest message
    property var roomNotifications: ({})
    property var roomPending: ({})
    function notify(roomId, roomName, count) {
        console.log("notify: +" + count)
        var n = roomNotifications[roomId]
        if (!n) {
            n = notificationComponent.createObject(service, { roomId: roomId })
            roomNotifications[roomId] = n
        }
        var info = rustrx.roomInfo(roomId)
        var last = (info.preview_sender ? info.preview_sender + ": " : "") + (info.preview || "")
        var pending = (roomPending[roomId] || 0) + count
        roomPending[roomId] = pending
        n.summary = roomName
        n.previewSummary = roomName
        //: notification body, %n new messages
        //% "%n new messages"
        n.body = last.length && notifyText ? last : qsTrId("rustrx-no-new-messages", pending)
        n.previewBody = n.body
        n.itemCount = pending
        n.publish()
    }
    function closeNotification(roomId) {
        delete roomPending[roomId]
        var n = roomNotifications[roomId]
        if (!n) return
        n.close()
        delete roomNotifications[roomId]
    }

    Component {
        id: notificationComponent
        Notification {
            property string roomId
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
    Timer { id: wakeHold; interval: 20000; onTriggered: wakeJob.finished() }

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
        remoteActions: [{
            "name": "default",
            "service": "net.mowerk.harbour-rustrx",
            "path": "/net/mowerk/harbour_rustrx",
            "iface": "net.mowerk.harbour_rustrx",
            "method": "open"
        }, {
            "name": "quit",
            //% "Quit"
            "displayName": qsTrId("rustrx-me-quit"),
            "service": "net.mowerk.harbour-rustrx",
            "path": "/net/mowerk/harbour_rustrx",
            "iface": "net.mowerk.harbour_rustrx",
            "method": "quit"
        }]
    }

    Component.onCompleted: {
        // an earlier process's notifications (the stand-in's, or a killed
        // run's): room lines are taken over, so a new message updates them and
        // opening the room closes them; the stand-in's line goes
        var old = standInNote.notifications()
        for (var i = 0; i < old.length; i++) {
            var action = old[i].remoteActions.length ? old[i].remoteActions[0] : {}
            if (action.method === "openRoomFromNotification" && action.arguments && action.arguments.length
                    && !roomNotifications[action.arguments[0]])
                roomNotifications[action.arguments[0]] = old[i]
            else
                old[i].close()
        }
        if (rustrx.standIn) standInNote.publish()
    }
}
