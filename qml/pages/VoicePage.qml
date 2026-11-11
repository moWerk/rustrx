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

    Component.onDestruction: if (!sent) voice.discard()

    // debug: record three seconds by itself (unattended tests)
    ConfigurationValue { id: debugRecord; key: "/apps/harbour-rustrx/debugRecord"; defaultValue: false }
    Timer { running: debugRecord.value === true; interval: 500; onTriggered: { voice.start(); debugStop.start() } }
    Timer { id: debugStop; interval: 3000; onTriggered: voice.stop() }

    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.paddingLarge

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Format.formatDuration(voice.duration / 1000, Formatter.DurationShort)
            font.pixelSize: Theme.fontSizeHuge
            color: voice.recording ? Theme.highlightColor : Theme.primaryColor
        }

        BackgroundItem {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.itemSizeHuge
            height: width
            onClicked: voice.recording ? voice.stop() : voice.start()
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.8
                height: width
                radius: voice.recording ? Theme.paddingMedium : width / 2
                color: voice.recording ? Theme.highlightColor : Theme.rgba(Theme.highlightColor, Theme.opacityLow)
                Behavior on radius { NumberAnimation { duration: 150 } }
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
            visible: !voice.recording && voice.path.length > 0 && voice.duration > 0
            Button {
                //% "Discard"
                text: qsTrId("rustrx-bt-discard")
                onClicked: voice.discard()
            }
            Button {
                //% "Send"
                text: qsTrId("rustrx-bt-send")
                onClicked: {
                    rustrx.sendVoice(page.roomId, voice.path, voice.duration)
                    page.sent = true
                    pageStack.pop()
                }
            }
        }
    }
}
