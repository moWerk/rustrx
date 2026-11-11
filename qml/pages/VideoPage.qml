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

// A video, played from the cache once it has been fetched. Tap to pause.
Page {
    id: page

    property string roomId
    property string eventId
    property string source

    allowedOrientations: Orientation.All

    Component.onCompleted: if (!source.length) rustrx.getMedia(roomId, eventId, false)

    Connections {
        target: rustrx.timeline
        onMediaChanged: {
            if (eventId !== page.eventId || page.source.length) return
            page.source = rustrx.timeline.mediaPath(eventId)
        }
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
        running: !page.source.length || video.status === MediaPlayer.Loading || video.status === MediaPlayer.Buffering
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
