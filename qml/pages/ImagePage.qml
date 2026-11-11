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
    }

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentWidth: Math.max(width, image.width * image.scale)
        contentHeight: Math.max(height, image.height * image.scale)
        clip: true

        PullDownMenu {
            MenuItem {
                //% "Save to Downloads"
                text: qsTrId("rustrx-me-save-downloads")
                onClicked: { page.exporting = true; rustrx.exportMedia(page.roomId, page.eventId) }
            }
        }

        Image {
            id: image
            anchors.centerIn: parent
            width: flick.width
            height: flick.height
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            autoTransform: true
            smooth: true
            source: page.source.length ? "file://" + page.source : ""
            transformOrigin: Item.Center

            PinchArea {
                anchors.fill: parent
                pinch.target: image
                pinch.minimumScale: 1
                pinch.maximumScale: 6
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: image.scale = image.scale > 1 ? 1 : 2.5
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
