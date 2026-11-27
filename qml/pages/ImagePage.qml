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
import "../components"

// One picture, full screen: pinch or double tap to zoom. Shows the
// thumbnail at once and swaps in the full image when it has arrived.
Page {
    id: page

    property string roomId
    property string eventId
    property string source
    property bool fullLoaded
    property bool exporting
    // a GIF moves here, always and looping (AnimatedImage; Image shows the
    // first frame only), once its full file is in
    property string mime
    readonly property bool isGif: mime === "image/gif"
    // the full picture could not be fetched: no spinner forever (0.8 audit)
    property string failure

    // A photo is decoded at most this large, not whole: a 12 MP one took
    // 48 MB and its texture as much again, a 50 MP one was past many GPUs'
    // texture limit (audit MI6). Two and a half times the screen's short
    // side: as sharp as the screen at a double tap's zoom on a picture as
    // wide as the screen. Zoomed in further than that, the picture is
    // decoded again at up to deepSize, which every phone's GPU takes.
    readonly property int fitSize: Math.ceil(Math.min(Screen.width, Screen.height) * Tunables.imageTapZoom)
    readonly property int deepSize: 4096
    // zoomed past what fitSize holds, once: the larger decode stays
    property bool deepWanted
    readonly property bool scaledDown: image.status === Image.Ready && !deep.visible
                                       && Math.max(image.implicitWidth, image.implicitHeight) >= fitSize - 1
    readonly property bool pastSharp: scaledDown && image.paintedWidth > image.implicitWidth * 1.1
    onPastSharpChanged: if (pastSharp) deepWanted = true

    allowedOrientations: Orientation.All

    Component.onCompleted: if (!fullLoaded) rustrx.getMedia(roomId, eventId, false)

    Connections {
        target: rustrx.timeline
        // the full image is in the cache
        onMediaChanged: {
            if (eventId !== page.eventId || page.fullLoaded) return
            var path = rustrx.timeline.mediaPath(eventId)
            if (!path.length) return
            // a file that is no picture drawn here (audit UC2): the thumbnail stays
            if (rustrx.isPicture(path)) page.source = path
            page.fullLoaded = true
            page.failure = ""
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
        // the error names no event; one fetch or export runs here at a time
        onErrorOccurred: if (cmd === "get_media") {
            if (!page.exporting && !page.fullLoaded) page.failure = app.friendlyError(cmd, message)
            page.exporting = false
        }
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
                enabled: !page.exporting
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
            sourceSize.width: page.fitSize
            sourceSize.height: page.fitSize
            // under a moving GIF it lets go (a transparent one showed the still
            // through), and under the larger decode
            source: page.source.length && !gif.visible && !deep.visible ? rustrx.picture(page.source) : ""

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

        // the same picture decoded larger, for a deep zoom
        Image {
            id: deep
            width: flick.contentWidth
            height: flick.contentHeight
            visible: status === Image.Ready
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            autoTransform: true
            smooth: true
            sourceSize.width: page.deepSize
            sourceSize.height: page.deepSize
            source: page.deepWanted && page.fullLoaded && !page.isGif && page.source.length ? rustrx.picture(page.source) : ""
        }

        // Qt 5.6's AnimatedImage ignores sourceSize: a GIF decodes at its own size
        AnimatedImage {
            id: gif
            width: flick.contentWidth
            height: flick.contentHeight
            visible: status === Image.Ready
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            smooth: true
            source: page.isGif && page.fullLoaded && page.source.length ? rustrx.picture(page.source) : ""
            playing: Qt.application.active && page.status === PageStatus.Active
        }

        // while the full picture comes, and while it is saved
        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: (!page.fullLoaded && !page.failure.length) || page.exporting
            visible: running
        }

        Note {
            kind: "error"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.paddingLarge
            text: page.failure
        }
    }
}
