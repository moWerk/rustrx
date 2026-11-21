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
import QtMultimedia 5.6
import QtSensors 5.2
import "../components"

// Take one photo: continuous autofocus in the viewfinder, a tap focuses on
// that spot, the shutter locks the focus and takes the shot, then Discard or
// Send (Use, for a picture). The page closes after Send; `captured` has the
// file. A room's photo by default; one's own picture opens on the front
// camera (the author's wish, 0.8), with a switch to the other side.
Page {
    id: page
    // the viewfinder is laid out for portrait
    allowedOrientations: Orientation.Portrait

    // start on the front camera (a selfie), where the phone has one
    property bool front
    // "send": a photo for the room; "picture": a profile or room picture
    property string mode: "send"

    signal captured(string path)

    property string shotPath
    property bool shotPending
    // how the phone is held, as stock's CaptureOverlay counts it
    property int heldTurn: 0

    // the cameras by side; the back one is the default camera where it is
    // on the back (stock picks the same way)
    function cameraId(position) {
        var cams = QtMultimedia.availableCameras
        if (position === Camera.BackFace && QtMultimedia.defaultCamera.position === Camera.BackFace)
            return QtMultimedia.defaultCamera.deviceId
        for (var i = 0; i < cams.length; i++)
            if (cams[i].position === position) return cams[i].deviceId
        return ""
    }
    readonly property string frontId: cameraId(Camera.FrontFace)
    readonly property string backId: cameraId(Camera.BackFace)

    OrientationSensor {
        id: orientationSensor
        active: shotPath.length === 0
        onReadingChanged: {
            switch (reading.orientation) {
            case OrientationReading.TopUp: heldTurn = 0; break
            case OrientationReading.TopDown: heldTurn = 180; break
            case OrientationReading.LeftUp: heldTurn = 270; break
            case OrientationReading.RightUp: heldTurn = 90; break
            default: break   // face up or down: keep the last value
            }
        }
    }

    // the shutter: a focus lock first, where the camera can focus; a camera
    // that cannot (fixed-focus front cameras) or never answers is not waited
    // for longer than Tunables.focusLockMs, so "Focusing…" always ends
    function trigger() {
        if (shotPending || !camera.imageCapture.ready) return
        shotPending = true
        var fixed = camera.focus.focusMode === Camera.FocusInfinity || camera.focus.focusMode === Camera.FocusHyperfocal
        if (!viewfinderFocus.canFocus || fixed || camera.lockStatus === Camera.Locked) {
            shoot()
            return
        }
        camera.searchAndLock()
        if (camera.lockStatus === Camera.Unlocked) shoot()   // nothing searches
        else lockWait.restart()
    }

    function shoot() {
        lockWait.stop()
        shotPending = false
        camera.imageCapture.captureToLocation(StandardPaths.pictures)
    }

    function switchCamera() {
        lockWait.stop()
        shotPending = false
        viewfinderFocus.reset()
        front = !front
    }

    Timer {
        id: lockWait
        interval: Tunables.focusLockMs
        onTriggered: if (page.shotPending) page.shoot()
    }

    Camera {
        id: camera
        captureMode: Camera.CaptureStillImage
        deviceId: page.front && page.frontId.length ? page.frontId : page.backId

        focus {
            focusMode: viewfinderFocus.focusMode
            focusPointMode: viewfinderFocus.focusPointMode
        }

        // the photo's EXIF orientation follows how the phone is held, with
        // stock's formula; the front one counts the other way round, as it
        // is mirrored. Portrait upright on the jp26's back camera is 270,
        // as measured before. Media.cpp (sent photos and pictures alike) and
        // the preview turn the picture by this tag; the pixels stay as the
        // sensor saw them, so a selfie is saved unmirrored (droid's stills
        // are, as in stock)
        metaData.orientation: viewfinderFocus.facingUser
                              ? (720 + camera.orientation - page.heldTurn) % 360
                              : (720 + camera.orientation + page.heldTurn) % 360

        imageCapture {
            onImageSaved: {
                shotPath = path
                // a tapped focus point stays until its time is up (stock)
                if (!viewfinderFocus.tapped) camera.unlock()
            }
            onCaptureFailed: {
                shotPending = false
                if (!viewfinderFocus.tapped) camera.unlock()
                //% "Could not take the photo"
                Notices.show(qsTrId("rustrx-no-capture-failed"))
            }
        }

        // the shutter asked for a focus lock; the shot follows once it settled
        onLockStatusChanged: {
            if (!shotPending) return
            if (lockStatus === Camera.Locked || lockStatus === Camera.Unlocked) shoot()
        }
    }

    // no orientation and no mirror here: Sailfish's video texture backend
    // turns droid's frames upright by their orientation tag and mirrors the
    // front camera itself; the stock camera's VideoOutput sets neither.
    // (The old turn, 720 - orientation - 90, came to 0 on the jp26's back
    // camera, orientation 270, and would turn other cameras wrong)
    VideoOutput {
        id: video
        anchors.fill: parent
        visible: shotPath.length === 0
        source: camera
        fillMode: VideoOutput.PreserveAspectFit
    }

    // a tap on the viewfinder focuses there
    ViewfinderFocus {
        id: viewfinderFocus
        anchors.fill: parent
        visible: shotPath.length === 0
        camera: camera
        video: video
    }

    Label {
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: shutter.top
            bottomMargin: Theme.paddingLarge
        }
        visible: shotPath.length === 0
        color: Theme.highlightColor
        //% "Focusing…"
        text: shotPending ? qsTrId("rustrx-la-focusing")
              : camera.imageCapture.ready
                ? (viewfinderFocus.canFocus
                   //% "Tap the picture to focus"
                   ? qsTrId("rustrx-la-tap-to-focus") : "")
                //% "Starting the camera…"
                : qsTrId("rustrx-la-camera-starting")
    }

    IconButton {
        id: shutter
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: Theme.paddingLarge * 2
        }
        visible: shotPath.length === 0
        enabled: camera.imageCapture.ready && !shotPending
        icon.source: "image://theme/icon-camera-shutter"
        onClicked: page.trigger()
    }

    // the other side's camera, where the phone has both
    IconButton {
        anchors {
            verticalCenter: shutter.verticalCenter
            left: shutter.right
            leftMargin: Theme.paddingLarge * 2
        }
        visible: shotPath.length === 0 && page.frontId.length > 0 && page.backId.length > 0
        enabled: !shotPending
        icon.source: "image://theme/icon-camera-switch"
        onClicked: page.switchCamera()
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

    // the voice page's decision in its words: Discard lets you take
    // another; the two far apart, and clear of the gesture edge (0.8 audit)
    ButtonLayout {
        id: decision
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            bottomMargin: Theme.paddingLarge * 2
        }
        visible: shotPath.length > 0
        columnSpacing: Theme.paddingLarge * 2
        rowSpacing: Theme.paddingLarge
        Button {
            //% "Discard"
            text: qsTrId("rustrx-bt-discard")
            onClicked: shotPath = ""
        }
        Button {
            text: page.mode === "picture"
                  //: takes the photo as the profile or room picture
                  //% "Use"
                  ? qsTrId("rustrx-bt-use")
                  : qsTrId("rustrx-bt-send")
            onClicked: {
                page.captured(shotPath)
                pageStack.pop()
            }
        }
    }
}
