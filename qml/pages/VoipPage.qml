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
import "../components"

// A classic one-to-one call (rustrx.voip): who, how far it got, and the
// buttons for it. It follows the call's state and goes when the call ends.
Page {
    id: page
    objectName: "voipPage"

    readonly property var call: rustrx.voip
    allowedOrientations: Orientation.Portrait
    // a running call is ended by its button, not by a swipe
    backNavigation: call.state === "idle"

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
        onChanged: if (page.call.state === "idle") closer.restart()
    }
    // the reason shows a moment, then the page goes
    Timer {
        id: closer
        interval: Tunables.voipEndShownMs
        onTriggered: if (page.status === PageStatus.Active) pageStack.pop()
    }

    // ringing: the lower half answers or declines by a swipe, as in the phone app
    IncomingCallSwipe {
        visible: page.call.state === "incoming"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: parent.height * 0.55
        onAnswered: page.call.answer()
        onDeclined: page.call.reject()
    }

    Column {
        anchors.centerIn: parent
        // above the swipe while ringing
        anchors.verticalCenterOffset: page.call.state === "incoming" ? -parent.height * 0.28 : 0
        width: parent.width - 2 * Theme.horizontalPageMargin
        spacing: Theme.paddingLarge

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: page.call.state !== "incoming"
            source: "image://theme/icon-l-answer"
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraLarge
            color: Theme.highlightColor
            text: page.call.peerName
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.secondaryHighlightColor
            text: {
                switch (page.call.state) {
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
                //% "No answer"
                case "invite_timeout": return qsTrId("rustrx-la-voip-no-answer")
                //% "The connection failed"
                case "ice_failed": return qsTrId("rustrx-la-voip-ice-failed")
                //% "The microphone or speaker could not be used"
                case "user_media_failed": return qsTrId("rustrx-la-voip-media-failed")
                }
                //% "Call ended"
                return qsTrId("rustrx-la-voip-ended")
            }
        }
        Label {
            visible: page.call.state === "connected"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            //% "End-to-end encrypted"
            text: qsTrId("rustrx-la-voip-encrypted")
        }

        Item { width: 1; height: Theme.paddingLarge }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingLarge * 2
            visible: page.call.state === "outgoing" || page.call.state === "connecting" || page.call.state === "connected"
            IconButton {
                icon.source: page.call.muted ? "image://theme/icon-m-mic-mute" : "image://theme/icon-m-mic"
                onClicked: page.call.muted = !page.call.muted
            }
            // the theme has no hang-up icon: the handset, put down
            IconButton {
                icon.source: "image://theme/icon-m-call"
                icon.color: "#e04040"
                rotation: 135
                onClicked: page.call.hangup()
            }
        }
    }
}
