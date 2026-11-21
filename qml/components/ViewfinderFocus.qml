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

// The focus of a viewfinder, the way the stock camera does it (jolla-camera's
// CaptureView and CaptureOverlay): continuous autofocus, and a tap focuses on
// that spot for a while. It fills the VideoOutput; the Camera binds its
// focus.focusMode and focus.focusPointMode to the values here. Used by
// CameraPage and QrScanPage (the author's find: the QR scan did not focus
// as nicely as the stock camera, 0.8).
MouseArea {
    id: area

    property Camera camera
    property VideoOutput video
    // codes: when nothing is tapped, focus on the middle of the picture
    property bool centred
    // a tap or refocus() holds its own focus point until the release
    property bool tapped
    // the page shows the front camera unmirrored (it flips droid's mirror
    // back): then the tap needs no mirroring either
    property bool unmirrored

    readonly property bool active: camera !== null && camera.cameraStatus === Camera.ActiveStatus
    // the camera turns its lens; fixed-focus front cameras do not, and then
    // nothing may wait for a focus lock. QtMultimedia may claim a mode it
    // cannot do, so the callers still wait no longer than Tunables.focusLockMs
    readonly property bool canFocus: active
                                     && (camera.focus.isFocusModeSupported(Camera.FocusContinuous)
                                         || camera.focus.isFocusModeSupported(Camera.FocusAuto))
    readonly property bool facingUser: camera !== null && camera.position === Camera.FrontFace

    // Asked again once the camera is active: set while it is still unloaded,
    // QtMultimedia may refuse continuous focus as unsupported and stay on
    // one-shot auto, which never refocuses (stock's comment on the same
    // binding). This is the likely cause of the QR scan's soft focus. A tap
    // uses one-shot auto at its point, as stock does: a lock in continuous
    // mode holds whatever focus it had instead of searching
    readonly property int focusMode: !active ? Camera.FocusAuto
                                     : tapped ? Camera.FocusAuto
                                     : camera.focus.isFocusModeSupported(Camera.FocusContinuous) ? Camera.FocusContinuous
                                     : Camera.FocusAuto
    // codes: the centre mode, or a custom point in the middle where the
    // camera only knows custom points (stock uses only auto and custom)
    readonly property int focusPointMode: !active ? Camera.FocusPointAuto
                                          : tapped ? Camera.FocusPointCustom
                                          : !centred ? Camera.FocusPointAuto
                                          : camera.focus.isFocusPointModeSupported(Camera.FocusPointCenter) ? Camera.FocusPointCenter
                                          : camera.focus.isFocusPointModeSupported(Camera.FocusPointCustom) ? Camera.FocusPointCustom
                                          : Camera.FocusPointAuto

    enabled: canFocus

    // focus on a point of the sensor's picture (0..1 each way), for holdMs
    function focusOn(point, holdMs) {
        release.interval = holdMs
        release.restart()
        camera.unlock()
        tapped = true
        camera.focus.customFocusPoint = point
        camera.searchAndLock()
    }

    // a fresh focus search on the middle, kept briefly (QrScanPage, when
    // nothing was read for a while)
    function refocus() {
        if (!canFocus || tapped) return
        ring.visible = false
        focusOn(Qt.point(0.5, 0.5), Tunables.qrRefocusHoldMs)
    }

    // back to continuous focus
    function reset() {
        release.stop()
        if (!tapped) return
        tapped = false
        if (!camera) return
        if (centred) camera.focus.customFocusPoint = Qt.point(0.5, 0.5)
        camera.unlock()
    }

    // a tap, in this item's coordinates: turned into the sensor's own
    // coordinates through `frame`, as stock does
    onClicked: {
        var p = frame.mapFromItem(area, mouse.x, mouse.y)
        var fx = p.x / frame.width
        var fy = p.y / frame.height
        // droid shows the front camera mirrored; stock mirrors the point back
        if (facingUser && !unmirrored) {
            if (camera.orientation % 180 === 0) fx = 1 - fx
            else fy = 1 - fy
        }
        if (fx < 0 || fx > 1 || fy < 0 || fy > 1) return   // in the black bars
        ring.x = mouse.x - ring.width / 2
        ring.y = mouse.y - ring.height / 2
        ring.visible = true
        focusOn(Qt.point(fx, fy), Tunables.tapFocusResetMs)
    }

    // the sensor's picture as it lies on the screen: gst-droid turns the
    // frames upright itself (the stock VideoOutput sets no orientation), so
    // the shown picture is the sensor's turned by camera.orientation
    Item {
        id: frame
        readonly property rect shown: area.video ? area.video.contentRect : Qt.rect(0, 0, area.width, area.height)
        readonly property bool sideways: area.camera !== null && area.camera.orientation % 180 !== 0
        x: shown.x + (shown.width - width) / 2
        y: shown.y + (shown.height - height) / 2
        width: sideways ? shown.height : shown.width
        height: sideways ? shown.width : shown.height
        rotation: area.camera ? -area.camera.orientation : 0
    }

    Timer {
        id: release
        onTriggered: area.reset()
    }

    // where the tap focuses; the highlight colour once the focus holds
    Rectangle {
        id: ring
        visible: false
        width: Theme.itemSizeLarge
        height: width
        radius: width / 2
        color: "transparent"
        border.width: Math.round(2 * Theme.pixelRatio)
        border.color: area.camera && area.camera.lockStatus === Camera.Locked ? Theme.highlightColor : "white"
        opacity: area.tapped ? 1 : 0
        Behavior on opacity { FadeAnimation {} }
    }

    // a lock belongs to the mode it was made in (stock does the same)
    Connections {
        target: area.camera ? area.camera.focus : null
        onFocusModeChanged: area.camera.unlock()
    }
}
