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

// One picture, full screen: pinch or double tap to zoom. Shows the
// thumbnail at once and swaps in the full image when it has arrived.
Page {
    id: page

    property string roomId
    property string eventId
    property string source
    property bool fullLoaded
    property bool exporting

    allowedOrientations: Orientation.All

    Component.onCompleted: if (!fullLoaded) rustrx.getMedia(roomId, eventId, false)

    Connections {
        target: rustrx.timeline
        // the full image is in the cache
        onMediaChanged: {
            if (eventId !== page.eventId || page.fullLoaded) return
            var path = rustrx.timeline.mediaPath(eventId)
            if (path.length) { page.source = path; page.fullLoaded = true }
        }
    }

    Connections {
        target: rustrx
        onMediaExported: {
            if (!exporting || eventId !== page.eventId) return
            exporting = false
            //: %1 the file path
            //% "Saved to %1"
            Notices.show(qsTrId("rustrx-no-saved").arg(path))
        }
        // the error names no event; one export runs at a time
        onErrorOccurred: if (cmd === "get_media") exporting = false
    }

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        // the picture is as big as the content; zooming resizes the content
        // around a point (resizeContent), so that point stays put
        contentWidth: width
        contentHeight: height
        clip: true
        // a turn of the phone starts over at the whole picture
        onWidthChanged: zoomTo(1, Qt.point(0, 0))
        onHeightChanged: zoomTo(1, Qt.point(0, 0))

        function zoomTo(factor, at) {
            var f = Math.max(1, Math.min(Tunables.imageMaxZoom, factor))
            resizeContent(width * f, height * f, at)
            returnToBounds()
        }

        PullDownMenu {
            MenuItem {
                //% "Save to Downloads"
                text: qsTrId("rustrx-me-save-downloads")
                onClicked: { page.exporting = true; rustrx.exportMedia(page.roomId, page.eventId) }
            }
        }

        Image {
            id: image
            width: flick.contentWidth
            height: flick.contentHeight
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            autoTransform: true
            smooth: true
            source: page.source.length ? "file://" + page.source : ""

            PinchArea {
                anchors.fill: parent
                property real startZoom: 1
                onPinchStarted: startZoom = flick.contentWidth / flick.width
                onPinchUpdated: {
                    // follow the fingers as they move, zoom around their centre
                    flick.contentX += pinch.previousCenter.x - pinch.center.x
                    flick.contentY += pinch.previousCenter.y - pinch.center.y
                    var f = Math.max(1, Math.min(Tunables.imageMaxZoom, startZoom * pinch.scale))
                    flick.resizeContent(flick.width * f, flick.height * f, pinch.center)
                }
                onPinchFinished: flick.returnToBounds()
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: flick.zoomTo(flick.contentWidth > flick.width ? 1 : Tunables.imageTapZoom,
                                                  Qt.point(mouse.x, mouse.y))
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: !page.fullLoaded
        }
    }
}
