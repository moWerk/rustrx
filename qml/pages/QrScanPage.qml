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
import "../components"

// The camera looking for a QR code of a Matrix login (MSC4108). Logged in
// (grant), it is the code a new device shows, to let that device in; the
// page that pushed this one (LinkDevicePage) follows the rest. Logged out,
// it is the code a logged-in device shows, to log in with; QrLoginPage
// takes over. A code of the wrong kind is said so, and the camera looks on.
Page {
    id: page
    // the viewfinder is laid out for portrait (as on CameraPage)
    allowedOrientations: Orientation.Portrait

    property bool grant
    // a picture is being read, a code went to the core and waits for its
    // answer, the answer came and the next page has the login
    property bool reading
    property bool sent
    property bool handedOver
    property string refusal

    function look() {
        // no picture while the lens moves: it would be a blurred one
        if (reading || sent || camera.lockStatus === Camera.Searching) return
        var w = Math.min(video.width, Tunables.qrScanWidth)
        reading = video.grabToImage(function(result) {
            page.reading = false
            if (!page.sent && rustrx.readQr(result)) {
                page.sent = true
                page.refusal = ""
            }
        }, Qt.size(w, Math.round(w * video.height / video.width)))
    }

    function refusalText(reason) {
        //% "This code is from a device that is logged in already. Scan the code the new device shows."
        if (reason === "logged_in_code") return qsTrId("rustrx-la-qr-scan-logged-in")
        //% "This code is from a device that wants to log in. Scan the code a logged-in device shows."
        if (reason === "new_device_code") return qsTrId("rustrx-la-qr-scan-new-device")
        //% "This is not a Matrix login code."
        return qsTrId("rustrx-la-qr-scan-not-login")
    }

    function answer(progress) {
        if (progress.state === "refused") {
            refusal = refusalText(progress.reason)
            again.restart()
            return
        }
        handedOver = true
        if (grant) pageStack.pop()
        else pageStack.replace(Qt.resolvedUrl("QrLoginPage.qml"), { scanned: true, initial: progress })
    }

    // backed out while a code was on its way: that login is not wanted
    Component.onDestruction: if (sent && !handedOver) rustrx.command({ cmd: "login_cancel" })

    Connections {
        target: rustrx
        onQrGrant: if (page.grant && page.sent && !page.handedOver) page.answer(progress)
        onQrLogin: if (!page.grant && page.sent && !page.handedOver) page.answer(progress)
        onLoginError: {
            if (page.grant || !page.sent || page.handedOver) return
            page.handedOver = true
            pageStack.replace(Qt.resolvedUrl("QrLoginPage.qml"), { scanned: true, error: message })
        }
    }

    Timer {
        interval: Tunables.qrScanMs
        repeat: true
        running: page.status === PageStatus.Active && camera.cameraStatus === Camera.ActiveStatus && !page.sent
        onTriggered: page.look()
    }
    // nothing read for a while: a fresh focus search on the middle, held
    // briefly, then continuous focus again (0.8, the author's find: the
    // scan focused worse than the stock camera). Continuous focus alone
    // may settle on the background behind a phone's screen
    Timer {
        interval: Tunables.qrRefocusMs
        repeat: true
        running: page.status === PageStatus.Active && viewfinderFocus.canFocus && !viewfinderFocus.tapped && !page.sent
        onTriggered: viewfinderFocus.refocus()
    }
    // after a refusal the camera looks again, once the hint was readable
    Timer {
        id: again
        interval: 2500
        onTriggered: page.sent = false
    }

    Camera {
        id: camera
        // still image, though it never takes one: any other mode makes Qt
        // ask Sailfish's resource policy for the video recording set, with
        // audio playback and recording, which pauses the music (jp26)
        // (still image mode keeps continuous focus: it is droid's
        // continuous-picture, the mode the stock camera's photos use)
        captureMode: Camera.CaptureStillImage
        // continuous on the middle, asked again once the camera is active
        // (ViewfinderFocus says why)
        focus {
            focusMode: viewfinderFocus.focusMode
            focusPointMode: viewfinderFocus.focusPointMode
        }
        // Android's barcode scene where the phone's camera has one (droid
        // maps it to a scene mode); an off switch in Tunables, in case that
        // scene overrides the focus mode on a phone
        exposure.exposureMode: camera.cameraStatus === Camera.ActiveStatus && Tunables.qrBarcodeExposure
                               ? Camera.ExposureBarcode : Camera.ExposureAuto
    }

    // no orientation: the video backend turns the frames upright (CameraPage)
    VideoOutput {
        id: video
        anchors.fill: parent
        source: camera
        fillMode: VideoOutput.PreserveAspectFit
    }

    // a tap focuses on that spot, as in the stock camera
    ViewfinderFocus {
        id: viewfinderFocus
        anchors.fill: parent
        camera: camera
        video: video
        centred: true
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: hint.height + 2 * Theme.paddingLarge
        color: Theme.rgba("black", Theme.opacityHigh)
        Label {
            id: hint
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            anchors.verticalCenter: parent.verticalCenter
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: page.refusal.length ? Theme.errorColor : Theme.highlightColor
            text: page.refusal.length ? page.refusal
                  : page.sent
                    //% "Reading the code…"
                    ? qsTrId("rustrx-la-qr-scan-reading")
                    : camera.cameraStatus !== Camera.ActiveStatus
                      ? qsTrId("rustrx-la-camera-starting")
                      : page.grant
                        //% "Point the camera at the QR code the new device shows."
                        ? qsTrId("rustrx-la-qr-scan-grant")
                        //% "Point the camera at the QR code of a device that is logged in. In Rustrx it is under Settings, Account, Link a new device."
                        : qsTrId("rustrx-la-qr-scan-login")
        }
    }
}
