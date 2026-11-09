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
import QtMultimedia 5.6

// Take one photo: continuous autofocus in the viewfinder, a focus lock on
// the tap, then the shot with Send and Retake. The page closes after Send.
Page {
    id: page

    signal captured(string path)

    property string shotPath
    property bool shotPending

    function shoot() {
        shotPending = false
        camera.imageCapture.captureToLocation(StandardPaths.pictures)
    }

    Camera {
        id: camera
        captureMode: Camera.CaptureStillImage

        focus {
            focusMode: Camera.FocusContinuous
            focusPointMode: Camera.FocusPointAuto
        }

        // the sensor is mounted landscape; a portrait shot is tagged 90°
        metaData.orientation: 90

        imageCapture {
            onImageSaved: {
                shotPath = path
                camera.unlock()
            }
            onCaptureFailed: {
                shotPending = false
                camera.unlock()
                //% "Could not take the photo"
                Notices.show(qsTrId("rustrx-no-capture-failed"))
            }
        }

        // the tap asks for a focus lock; the shot follows once focus settled
        onLockStatusChanged: {
            if (!shotPending) return
            if (lockStatus === Camera.Locked || lockStatus === Camera.Unlocked) shoot()
        }
    }

    VideoOutput {
        anchors.fill: parent
        visible: shotPath.length === 0
        source: camera
        fillMode: VideoOutput.PreserveAspectFit
        // positive values turn anti-clockwise (Qt); the sensor's own
        // orientation plus a quarter turn clockwise fills a portrait page
        orientation: (720 - camera.orientation - 90) % 360
    }

    MouseArea {
        anchors.fill: parent
        enabled: shotPath.length === 0 && camera.imageCapture.ready && !shotPending
        onClicked: {
            shotPending = true
            if (camera.lockStatus === Camera.Locked) shoot()
            else camera.searchAndLock()
        }
    }

    Label {
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: Theme.paddingLarge * 2
        }
        visible: shotPath.length === 0
        color: Theme.highlightColor
        //% "Focusing…"
        text: shotPending ? qsTrId("rustrx-la-focusing")
              //% "Tap to take the photo"
              : camera.imageCapture.ready ? qsTrId("rustrx-la-tap-to-capture")
              //% "Starting the camera…"
              : qsTrId("rustrx-la-camera-starting")
    }

    // the shot, with the decision below it
    Image {
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: decision.top
        }
        visible: shotPath.length > 0
        source: shotPath.length ? "file://" + shotPath : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        autoTransform: true
        sourceSize.width: Screen.width
    }

    Row {
        id: decision
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: Theme.paddingLarge
        }
        visible: shotPath.length > 0
        spacing: Theme.paddingLarge
        Button {
            //% "Retake"
            text: qsTrId("rustrx-bt-retake")
            onClicked: shotPath = ""
        }
        Button {
            //% "Send"
            text: qsTrId("rustrx-bt-send")
            onClicked: {
                page.captured(shotPath)
                pageStack.pop()
            }
        }
    }
}
