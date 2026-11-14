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

// A message's source: its JSON as the server sent it (decrypted where it
// was encrypted), and its latest edit's below. Selectable; Copy in the pulley.
Page {
    id: page

    property string eventId
    property string json
    property string edit
    property string failure

    Connections {
        target: rustrx
        onEventSource: if (eventId === page.eventId) { page.json = json; page.edit = edit }
        onErrorOccurred: if (cmd === "event_source") page.failure = message
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            enabled: page.json.length > 0
            MenuItem {
                //% "Copy"
                text: qsTrId("rustrx-me-copy-source")
                onClicked: {
                    Clipboard.text = page.edit.length ? page.json + "\n\n" + page.edit : page.json
                    //% "Source copied"
                    Notices.show(qsTrId("rustrx-no-source-copied"), Notice.Short)
                }
            }
        }

        Column {
            id: column
            width: parent.width

            //% "Message source"
            PageHeader { title: qsTrId("rustrx-he-source"); description: page.eventId }

            TextArea {
                width: parent.width
                readOnly: true
                visible: page.json.length > 0
                text: page.json
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.primaryColor
            }

            //% "Latest edit"
            SectionHeader { visible: page.edit.length > 0; text: qsTrId("rustrx-se-latest-edit") }
            TextArea {
                width: parent.width
                readOnly: true
                visible: page.edit.length > 0
                text: page.edit
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.primaryColor
            }

            Label {
                visible: page.failure.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                text: page.failure
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: page.json.length === 0 && page.failure.length === 0
        }

        VerticalScrollDecorator { }
    }
}
