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
import "../markup.js" as Markup

// All threads of a room, the latest activity first: who started each and
// with what, how many replies, the latest, and how many are new here.
// A tap opens the thread.
Page {
    id: page

    property string roomId
    property string roomName
    property var threads: []
    property bool loading: true

    Component.onCompleted: rustrx.command({ cmd: "thread_list", room_id: roomId })
    Component.onDestruction: rustrx.command({ cmd: "close_thread_list" })

    Connections {
        target: rustrx
        onThreadList: {
            if (roomId !== page.roomId) return
            page.threads = threads
            page.loading = loading
        }
    }

    function escapeText(t) { return Markup.escaped(t).replace(/\s+/g, " ") }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: page.threads

        header: PageHeader {
            //% "Threads"
            title: qsTrId("rustrx-he-threads")
            description: page.roomName
        }

        delegate: BackgroundItem {
            id: row
            readonly property var t: modelData
            readonly property int fresh: (rustrx.threadsSeen, Math.max(0, t.count - rustrx.threadSeen(t.root.event_id, t.count)))
            width: list.width
            height: column.height + 2 * Theme.paddingMedium
            onClicked: pageStack.push(Qt.resolvedUrl("ThreadPage.qml"),
                                      { roomId: page.roomId, roomName: page.roomName, rootId: t.root.event_id })

            Column {
                id: column
                x: Theme.horizontalPageMargin
                y: Theme.paddingMedium
                width: parent.width - 2 * x

                // who started it, and when
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: row.t.root.sender_name + " · " + Format.formatDate(new Date(row.t.root.ts), Formatter.TimepointRelative)
                }
                // what it is about
                Label {
                    width: parent.width
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    textFormat: Text.StyledText
                    font.bold: row.fresh > 0
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                    text: rustrx.emojify(page.escapeText(row.t.root.body), Math.round(font.pixelSize * Tunables.emojiScaleList))
                }
                // replies, new ones, the latest
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    textFormat: Text.StyledText
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: {
                        var s = qsTrId("rustrx-la-thread-replies", row.t.count)
                        if (row.fresh > 0)
                            s += " · <b><font color=\"" + Theme.highlightColor + "\">" + qsTrId("rustrx-la-thread-new", row.fresh) + "</font></b>"
                        if (row.t.latest && row.t.latest.body)
                            s += " · " + page.escapeText(row.t.latest.sender_name) + ": "
                                 + rustrx.emojify(page.escapeText(row.t.latest.body), Math.round(font.pixelSize * Tunables.emojiScale))
                        return s
                    }
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: page.loading && page.threads.length === 0
        }

        ViewPlaceholder {
            enabled: !page.loading && page.threads.length === 0
            //% "No threads in this room"
            text: qsTrId("rustrx-la-threads-none")
            //% "Long press a message and choose Reply in thread to start one."
            hintText: qsTrId("rustrx-la-threads-none-hint")
        }

        VerticalScrollDecorator { }
    }
}
