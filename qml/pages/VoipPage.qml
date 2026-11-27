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
import "../components"

// A classic one-to-one call (rustrx.voip): who, how far it got, and the
// buttons for it. It follows the call's state and goes when the call ends.
// Or an Element Call ring (`ring`): only its ringing, Answer and Decline,
// as the classic call's; Answer opens the room and its call.
Page {
    id: page
    objectName: "voipPage"

    readonly property var call: rustrx.voip
    // an Element Call ring: { account, roomId, eventId, name }; null for the classic call
    property var ring: null
    // in the call window over the lock screen (CallWindow.qml): it closes
    // the window instead of leaving the page
    property bool inCallWindow
    signal closeRequested()
    readonly property string callState: ring ? "incoming" : call.state
    readonly property string peerName: ring ? ring.name : call.peerName
    function leave() {
        if (inCallWindow) closeRequested()
        else if (pageStack.currentPage === page) pageStack.pop()
    }
    allowedOrientations: Orientation.Portrait
    // back leads to the chat and the call goes on: the page only shows the
    // call, which lives in rustrx.voip, and app.showVoip() brings it back
    // (0.8 audit: the chat could not be reached during a call). Not while
    // it rings, where the swipe answers, nor dark at the ear.
    backNavigation: page.callState !== "incoming" && !rustrx.callDark

    // the call's length, while it runs
    property int seconds
    Timer {
        running: page.call.state === "connected"
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: page.seconds = Math.max(0, Math.round((Date.now() - page.call.connectedAt) / 1000))
    }
    function clock(s) {
        var m = Math.floor(s / 60), r = s % 60
        return m + ":" + (r < 10 ? "0" : "") + r
    }

    Connections {
        target: page.call
        // a new call that rings within the moment keeps the page
        onChanged: if (page.ring) return; else if (page.call.state === "idle") closer.restart(); else closer.stop()
    }
    // the reason shows a moment, then the page goes (only if no new call
    // started meanwhile; one under another page goes when it comes back)
    Timer {
        id: closer
        interval: Tunables.voipEndShownMs
        onTriggered: if (page.callState === "idle" && (page.inCallWindow || page.status === PageStatus.Active) && !page.failed) page.leave()
    }
    // a call placed here that did not get through stays with "Call again"
    // and "Close" (0.8 audit: the page went and the call had to be found
    // again); a ring nobody took goes as any ended call (it left a missed
    // call), and over the lock screen nothing may place a call
    // (a plain value: set once at the start, then by the state's changes;
    // a binding would turn false at "connecting")
    property bool placedHere: false
    Component.onCompleted: placedHere = callState === "outgoing"
    onCallStateChanged: {
        if (callState === "outgoing") placedHere = true
        else if (callState === "incoming") placedHere = false
    }
    readonly property bool failed: callState === "idle" && placedHere && !inCallWindow
        && ["invite_timeout", "ice_failed", "send_failed", "user_media_failed"].indexOf(call.endReason) >= 0
    // whom to call again: the direct chat's other person
    property string lastRoomId
    property string lastPeerName
    Connections {
        target: page.call
        onChanged: if (page.call.roomId.length) { page.lastRoomId = page.call.roomId; page.lastPeerName = page.call.peerName }
    }
    onStatusChanged: if (status === PageStatus.Active && callState === "idle" && !closer.running) closer.restart()
    // the Element Call ring is over (declined, expired, answered elsewhere):
    // the page goes; Answer has opened the room already
    Connections {
        target: page.ring ? service : null
        onRingingChanged: if (page.ring && !service.ringing[page.ring.roomId]) page.leave()
    }

    // ringing: the lower half answers or declines by a swipe, as in the phone app
    IncomingCallSwipe {
        id: swipe
        visible: page.callState === "incoming"
        // each ring starts with the handset in the middle
        onVisibleChanged: if (visible) reset()
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: parent.height * 0.55
        onAnswered: {
            if (!page.ring) { page.call.answer(); return }
            // the room's call page takes its place (over the lock screen
            // the window goes: the call runs in the app, unlocked)
            var r = page.ring
            if (page.inCallWindow) page.closeRequested()
            else pageStack.pop(undefined, PageStackAction.Immediate)
            rustrx.answerRing(r.account, r.roomId)
        }
        onDeclined: page.ring ? rustrx.declineRing(page.ring.account, page.ring.roomId, page.ring.eventId) : page.call.reject()
    }

    readonly property bool running: callState === "outgoing" || callState === "connecting" || callState === "connected"

    // who and how far, in the room above the swipe or the buttons
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: page.callState === "incoming" ? -parent.height * 0.28
                                      : page.running ? -(controls.height + controls.anchors.bottomMargin) / 2 : 0
        width: parent.width - 2 * Theme.horizontalPageMargin
        spacing: Theme.paddingLarge

        // the handset, the one mark for a call in Rustrx (the room's call
        // line, the room list; the swipe's large one)
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: page.callState !== "incoming"
            source: "image://theme/icon-l-dialer"
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraLarge
            color: Theme.highlightColor
            text: page.peerName
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.secondaryHighlightColor
            text: {
                switch (page.callState) {
                //% "Calling…"
                case "outgoing": return qsTrId("rustrx-la-voip-calling")
                //% "Is calling you"
                case "incoming": return qsTrId("rustrx-la-voip-incoming")
                //% "Connecting…"
                case "connecting": return qsTrId("rustrx-la-voip-connecting")
                case "connected": return page.clock(page.seconds)
                }
                switch (page.call.endReason) {
                //% "Declined"
                case "rejected": return qsTrId("rustrx-la-voip-rejected")
                //% "Answered on another device"
                case "answered_elsewhere": return qsTrId("rustrx-la-voip-elsewhere")
                case "invite_timeout": return page.placedHere
                                              //% "No answer"
                                              ? qsTrId("rustrx-la-voip-no-answer")
                                              : qsTrId("rustrx-la-missed-call")
                //% "The connection failed"
                case "ice_failed": return qsTrId("rustrx-la-voip-ice-failed")
                //% "The microphone or speaker could not be used"
                case "user_media_failed": return qsTrId("rustrx-la-voip-media-failed")
                //% "The call could not be set up: no connection to the server"
                case "send_failed": return qsTrId("rustrx-la-voip-send-failed")
                //% "Calls go only through a relay (Settings > Privacy), and your server offers none"
                case "no_relay": return qsTrId("rustrx-la-voip-no-relay")
                }
                //% "Call ended"
                return qsTrId("rustrx-la-voip-ended")
            }
        }
        // only where it is true: in an unencrypted room the call's keys
        // (the DTLS fingerprints in the SDP) pass the server in the clear
        Label {
            visible: page.callState === "connected" && rustrx.roomInfo(page.call.roomId).encrypted === true
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            //% "End-to-end encrypted"
            text: qsTrId("rustrx-la-voip-encrypted")
        }
    }

    // the buttons low, where the thumb is and where the swipe was while it
    // rang; large, and hang up the largest, apart from the two toggles
    // (0.8 audit: they sat mid-screen, hang up no bigger than mute)
    Column {
        id: controls
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Theme.itemSizeLarge }
        spacing: Theme.paddingLarge * 2
        visible: page.running

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingLarge * 3
            // both toggles light while on, so "muted?" reads from across
            // the table (0.8 audit: only the loudspeaker did)
            IconButton {
                width: Theme.itemSizeLarge
                height: width
                icon.source: page.call.muted ? "image://theme/icon-m-mic-mute" : "image://theme/icon-m-mic"
                highlighted: page.call.muted
                onClicked: page.call.muted = !page.call.muted
            }
            // the earpiece unless this is on
            IconButton {
                width: Theme.itemSizeLarge
                height: width
                icon.source: page.call.speaker ? "image://theme/icon-m-speaker-on" : "image://theme/icon-m-speaker"
                highlighted: page.call.speaker
                onClicked: page.call.speaker = !page.call.speaker
            }
        }

        // the handset put down, on red as in the phone app (the incoming
        // swipe's red, kept in one place)
        BackgroundItem {
            id: hangup
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.itemSizeExtraLarge
            height: width
            _showPress: false
            onClicked: page.call.hangup()
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: swipe.declineColor
                opacity: hangup.pressed ? Theme.opacityHigh : 1
            }
            Icon {
                anchors.centerIn: parent
                rotation: 135
                source: "image://theme/icon-l-dialer?white"
            }
        }
    }

    Column {
        visible: page.failed
        anchors { bottom: parent.bottom; bottomMargin: Theme.itemSizeLarge }
        width: parent.width
        spacing: Theme.itemSizeSmall
        PageButton {
            //% "Call again"
            text: qsTrId("rustrx-bt-voip-again")
            onClicked: {
                var info = rustrx.roomInfo(page.lastRoomId)
                if (info.dm_user) rustrx.placeCall(page.lastRoomId, info.dm_user, page.lastPeerName || info.dm_user)
            }
        }
        PageButton {
            //% "Close"
            text: qsTrId("rustrx-bt-voip-close")
            onClicked: page.leave()
        }
    }

    // at the ear: black over everything, and no touch gets through (a
    // cheek would mute or hang up); uncovered, the call is back at once
    Rectangle {
        anchors.fill: parent
        z: 1000
        color: "black"
        visible: rustrx.callDark
        MouseArea {
            anchors.fill: parent
            preventStealing: true
        }
    }
}
