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

// A voice message: tap to start, tap to stop, then send or throw away.
// Leaving the page without sending throws it away.
Page {
    id: page

    property string roomId
    property bool sent
    // counted from the tap: the recorder reports its duration a few seconds late
    property real startedAt: 0
    property real elapsed: 0
    // this page's recording only: the recorder keeps the previous one's duration
    property bool recorded
    readonly property real shownMs: !recorded ? 0 : voice.recording ? elapsed : Math.max(voice.duration, elapsed)

    function toggle() {
        if (voice.recording) {
            voice.stop()
            return
        }
        elapsed = 0
        startedAt = Date.now()
        recorded = true
        voice.start()
    }

    Timer {
        interval: 100
        repeat: true
        running: voice.recording
        onTriggered: page.elapsed = Date.now() - page.startedAt
    }

    Component.onDestruction: if (!sent) voice.discard()

    // debug: record three seconds by itself (unattended tests)
    ConfigurationValue { id: debugRecord; key: "/apps/harbour-rustrx/debugRecord"; defaultValue: false }
    Timer { running: debugRecord.value === true; interval: 500; onTriggered: { page.toggle(); debugShot.start() } }
    Timer { id: debugShot; interval: 1500; onTriggered: debugStop.start() }
    Timer { id: debugStop; interval: 1500; onTriggered: voice.stop() }

    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.paddingLarge

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Format.formatDuration(page.shownMs / 1000, Formatter.DurationShort)
            font.pixelSize: Theme.fontSizeHuge
            color: voice.recording ? Theme.highlightColor : Theme.primaryColor
        }

        // the record button, with the tuning fork's ripple from Beatfork:
        // two rings grow out of it and fade, every 1.2 s while recording
        Item {
            id: button
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.itemSizeHuge
            height: width

            property int rippleCount: 0
            Timer {
                interval: 1200
                repeat: true
                running: voice.recording
                triggeredOnStart: true
                onTriggered: button.rippleCount++
            }

            Rectangle {
                id: ripple1
                anchors.centerIn: parent
                width: parent.width * 0.8
                height: width
                radius: width / 2
                color: "transparent"
                border.color: Theme.highlightColor
                border.width: Theme.paddingSmall / 2
                opacity: 0.0
                ParallelAnimation {
                    id: ripple1Anim
                    NumberAnimation { target: ripple1; property: "scale"; from: 1.0; to: 2.0; duration: 700; easing.type: Easing.OutQuad }
                    NumberAnimation { target: ripple1; property: "opacity"; from: 0.7; to: 0.0; duration: 700; easing.type: Easing.InQuad }
                }
            }
            Rectangle {
                id: ripple2
                anchors.centerIn: parent
                width: parent.width * 0.8
                height: width
                radius: width / 2
                color: "transparent"
                border.color: Theme.highlightColor
                border.width: Theme.paddingSmall / 3
                opacity: 0.0
                SequentialAnimation {
                    id: ripple2Anim
                    PauseAnimation { duration: 200 }
                    ParallelAnimation {
                        NumberAnimation { target: ripple2; property: "scale"; from: 1.0; to: 1.7; duration: 600; easing.type: Easing.OutQuad }
                        NumberAnimation { target: ripple2; property: "opacity"; from: 0.45; to: 0.0; duration: 600; easing.type: Easing.InQuad }
                    }
                }
            }
            onRippleCountChanged: if (voice.recording) { ripple1Anim.restart(); ripple2Anim.restart() }

            BackgroundItem {
                anchors.centerIn: parent
                width: parent.width * 0.8
                height: width
                _showPress: false
                onClicked: page.toggle()
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: voice.recording || parent.pressed ? Theme.highlightColor : Theme.rgba(Theme.highlightColor, Theme.opacityLow)
                }
            }
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            text: voice.error.length ? voice.error
                  //% "Recording, tap to stop"
                  : voice.recording ? qsTrId("rustrx-la-voice-recording")
                  //% "Tap to record"
                  : qsTrId("rustrx-la-voice-tap")
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingLarge
            visible: page.recorded && !voice.recording && voice.path.length > 0
            Button {
                //% "Discard"
                text: qsTrId("rustrx-bt-discard")
                onClicked: { voice.discard(); page.elapsed = 0; page.recorded = false }
            }
            Button {
                //% "Send"
                text: qsTrId("rustrx-bt-send")
                onClicked: {
                    rustrx.sendVoice(page.roomId, voice.path, voice.duration > 0 ? voice.duration : page.shownMs)
                    page.sent = true
                    pageStack.pop()
                }
            }
        }
    }
}
