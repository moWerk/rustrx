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
import QtMultimedia 5.6
import Sailfish.Silica 1.0
import "../components"

// A video, played from the cache once it has been fetched. Tap to pause.
Page {
    id: page

    property string roomId
    property string eventId
    property string source
    // the video could not be fetched: said, instead of spinning on (0.8 audit)
    property string failure

    allowedOrientations: Orientation.All

    Component.onCompleted: if (!source.length) rustrx.getMedia(roomId, eventId, false)

    Connections {
        target: rustrx.timeline
        onMediaChanged: {
            if (eventId !== page.eventId || page.source.length) return
            page.source = rustrx.timeline.mediaPath(eventId)
            if (page.source.length) page.failure = ""
        }
    }
    Connections {
        target: rustrx
        // the error names no event; this page waits for one file only
        onErrorOccurred: if (cmd === "get_media" && !page.source.length) page.failure = app.friendlyError(cmd, message)
    }

    Rectangle { anchors.fill: parent; color: "black" }

    Video {
        id: video
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        source: page.source.length ? "file://" + page.source : ""
        autoPlay: true
        MouseArea {
            anchors.fill: parent
            onClicked: video.playbackState === MediaPlayer.PlayingState ? video.pause() : video.play()
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        size: BusyIndicatorSize.Large
        visible: running
        running: !playError.visible && !page.failure.length && (!page.source.length || video.status === MediaPlayer.Loading || video.status === MediaPlayer.Buffering)
    }

    // a video the phone cannot play (a format its media stack lacks): say
    // so, with the player's reason, instead of spinning on
    Label {
        id: playError
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.horizontalPageMargin
        visible: video.error !== MediaPlayer.NoError || video.status === MediaPlayer.InvalidMedia
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: Theme.errorColor
        //: %1 the player's reason
        //% "This video cannot be played here. %1"
        text: qsTrId("rustrx-la-video-error").arg(video.errorString)
    }

    Note {
        kind: "error"
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignHCenter
        text: page.failure
    }

    Label {
        anchors { bottom: parent.bottom; bottomMargin: Theme.paddingLarge; horizontalCenter: parent.horizontalCenter }
        visible: video.duration > 0
        text: Format.formatDuration(video.position / 1000, Formatter.DurationShort) + " / "
              + Format.formatDuration(video.duration / 1000, Formatter.DurationShort)
        color: Theme.primaryColor
        font.pixelSize: Theme.fontSizeSmall
    }
}
