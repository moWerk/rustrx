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

// Take one photo and hand its path back; the page closes itself.
Page {
    id: page

    signal captured(string path)

    Camera {
        id: camera
        captureMode: Camera.CaptureStillImage
        imageCapture {
            onImageSaved: {
                page.captured(path)
                pageStack.pop()
            }
            onCaptureFailed: {
                //% "Could not take the photo"
                Notices.show(qsTrId("rustrx-no-capture-failed"))
            }
        }
    }

    VideoOutput {
        anchors.fill: parent
        source: camera
        fillMode: VideoOutput.PreserveAspectFit
        orientation: -camera.orientation
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            var dir = StandardPaths.pictures + "/Rustrx"
            camera.imageCapture.captureToLocation(dir + "/rustrx-" + Qt.formatDateTime(new Date(), "yyyyMMdd-hhmmss") + ".jpg")
        }
    }

    Label {
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: Theme.paddingLarge * 2
        }
        //% "Tap to take the photo"
        text: qsTrId("rustrx-la-tap-to-capture")
        color: Theme.highlightColor
    }
}
