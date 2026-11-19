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

// Answering the way Sailfish's own phone does it: a swipe sideways (either
// way) answers, a swipe up declines. The handset follows the finger, turning
// green towards answering or tipping over red towards declining; let go
// before a quarter of the screen and it springs back. A tap shows what to
// do. Our own code, after the stock dialer's behaviour.
Item {
    id: swipe

    signal answered
    signal declined

    readonly property real threshold: Math.min(width, height) * 0.25
    // "", "answer" or "decline" once the finger chose a way
    property string way
    property real dx
    property real dy
    readonly property real progress: way === "answer" ? Math.min(1, Math.abs(dx) / threshold)
                                     : way === "decline" ? Math.min(1, Math.max(0, -dy) / threshold) : 0
    readonly property color answerColor: "#00bb15"
    readonly property color declineColor: "#e04040"
    property bool hinting
    property bool done

    function mix(a, b, t) { return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1) }

    MouseArea {
        id: area
        anchors.fill: parent
        enabled: !swipe.done
        property real startX
        property real startY
        onPressed: {
            startX = mouse.x
            startY = mouse.y
            swipe.way = ""
            back.stop()
        }
        onPositionChanged: {
            var x = mouse.x - startX, y = mouse.y - startY
            if (!swipe.way.length) {
                if (Math.abs(x) > Theme.startDragDistance && Math.abs(x) > Math.abs(y)) swipe.way = "answer"
                else if (-y > Theme.startDragDistance && -y > Math.abs(x)) swipe.way = "decline"
            }
            if (swipe.way === "answer") swipe.dx = Math.max(-swipe.width * 0.4, Math.min(swipe.width * 0.4, x))
            else if (swipe.way === "decline") swipe.dy = Math.min(0, y)
        }
        onReleased: {
            if (swipe.progress >= 1) {
                swipe.done = true
                if (swipe.way === "answer") swipe.answered()
                else swipe.declined()
            } else {
                if (!swipe.way.length) {
                    swipe.hinting = true
                    hintTimer.restart()
                }
                back.restart()
            }
        }
        onCanceled: back.restart()
    }

    // let go too early: back to the middle
    ParallelAnimation {
        id: back
        NumberAnimation { target: swipe; property: "dx"; to: 0; duration: 250; easing.type: Easing.OutBack }
        NumberAnimation { target: swipe; property: "dy"; to: 0; duration: 250; easing.type: Easing.OutBack }
        onStopped: if (!swipe.done) swipe.way = ""
    }
    Timer { id: hintTimer; interval: 2500; onTriggered: swipe.hinting = false }

    Column {
        id: controls
        anchors.centerIn: parent
        spacing: Theme.paddingLarge

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingMedium
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                source: "image://theme/icon-m-arrow-left-green"
                opacity: (swipe.way === "answer" || swipe.hinting || !swipe.way.length) && swipe.way !== "decline" ? 1 : 0
                Behavior on opacity { FadeAnimator {} }
                transform: Translate { x: Math.min(0, swipe.dx) * 0.5 }
            }
            Icon {
                id: handset
                anchors.verticalCenter: parent.verticalCenter
                source: "image://theme/icon-l-dialer"
                color: swipe.way === "decline" ? swipe.mix(Theme.primaryColor, swipe.declineColor, swipe.progress)
                                               : swipe.mix(Theme.primaryColor, swipe.answerColor, swipe.progress)
                transform: [
                    Rotation {
                        origin.x: handset.width / 2; origin.y: handset.height / 2
                        angle: swipe.way === "decline" ? 135 * swipe.progress : (swipe.dx < 0 ? -20 : 20) * swipe.progress
                    },
                    Translate { x: swipe.dx; y: swipe.dy }
                ]
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                source: "image://theme/icon-m-arrow-right-green"
                opacity: (swipe.way === "answer" || swipe.hinting || !swipe.way.length) && swipe.way !== "decline" ? 1 : 0
                Behavior on opacity { FadeAnimator {} }
                transform: Translate { x: Math.max(0, swipe.dx) * 0.5 }
            }
        }
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "image://theme/icon-m-arrow-up-red"
            opacity: swipe.way !== "answer" ? 1 : 0
            Behavior on opacity { FadeAnimator {} }
            transform: Translate { y: swipe.dy * 0.5 }
        }
    }

    Label {
        anchors { top: controls.bottom; topMargin: Theme.paddingLarge * 2; horizontalCenter: parent.horizontalCenter }
        width: parent.width - 2 * Theme.horizontalPageMargin
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.secondaryHighlightColor
        opacity: swipe.hinting ? 1 : 0
        Behavior on opacity { FadeAnimator {} }
        //% "Swipe left or right to answer, up to decline"
        text: qsTrId("rustrx-la-voip-swipe-hint")
    }
}
