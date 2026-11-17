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
import "../transcript.js" as Transcript

// The loaded chat as plain text, to select across messages with the
// system's handles and paste elsewhere (a transcript into another window).
// Opened from a message's menu: that message is selected, and dragging the
// handles takes in more. A snapshot: messages arriving meanwhile are not
// added, so they cannot move the selection.
Page {
    id: page

    property string roomName
    // the message the page opens at (its timeline row id)
    property string startId

    property var built: ({ text: "", starts: [] })

    Component.onCompleted: {
        // the timeline is newest first; the transcript reads oldest first
        var items = []
        var startIndex = -1
        for (var row = rustrx.timeline.count - 1; row >= 0; row--) {
            var m = rustrx.timeline.get(row)
            if (m.id === startId) startIndex = items.length
            items.push(m)
        }
        built = Transcript.build(items)
        area.text = built.text
        if (startIndex >= 0) selectItem(startIndex)
    }

    function selectItem(index) {
        var from = built.starts[index]
        if (from < 0) return
        // up to the next item's text (a message may span lines, a reply
        // starts with its quote)
        var to = built.text.length
        for (var i = index + 1; i < built.starts.length; i++)
            if (built.starts[i] >= 0) { to = built.starts[i] - 1; break }
        area.select(from, to)
        positionTimer.from = from
        positionTimer.start()
    }

    // once laid out: the selected message a third down the screen
    Timer {
        id: positionTimer
        property int from
        interval: 0
        onTriggered: {
            var r = area._editor.positionToRectangle(from)
            var y = area._editor.mapToItem(column, r.x, r.y).y
            flick.contentY = Math.max(0, Math.min(y - flick.height / 3, flick.contentHeight - flick.height))
        }
    }

    // the handles react only with the focus, which a read-only field does
    // not take by itself
    onStatusChanged: if (status === PageStatus.Active) area.forceActiveFocus()

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                //% "Copy all"
                text: qsTrId("rustrx-me-copy-all")
                onClicked: {
                    Clipboard.text = page.built.text
                    Notices.show(qsTrId("rustrx-no-text-copied"), Notice.Short)
                }
            }
            MenuItem {
                //% "Copy selection"
                text: qsTrId("rustrx-me-copy-selection")
                enabled: area.selectedText.length > 0
                onClicked: {
                    Clipboard.text = area.selectedText
                    Notices.show(qsTrId("rustrx-no-text-copied"), Notice.Short)
                }
            }
        }

        Column {
            id: column
            width: parent.width

            //% "Select text"
            PageHeader { title: qsTrId("rustrx-he-select-text"); description: page.roomName }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                //% "Drag the handles to select more; the selection is copied when you let go. Hold a word to start a new selection."
                text: qsTrId("rustrx-la-select-text-hint")
            }

            TextArea {
                id: area
                width: parent.width
                readOnly: true
                focusOnClick: true
                enableSoftwareInputPanel: false
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
            }
        }

        VerticalScrollDecorator {}
    }
}
