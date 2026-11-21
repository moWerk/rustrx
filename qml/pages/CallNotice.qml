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

// Before the first call, once: where a call goes. Rustrx's calls are
// Element Call, a web page loaded from call.element.io for every call,
// with the sound and picture through a LiveKit media server (the
// homeserver's, or Element's). Call goes on to the call page in its
// place; Cancel ends the call that was set up for it.
Dialog {
    id: dialog

    property string roomId
    property string url
    property string callPage

    acceptDestination: callPage
    acceptDestinationAction: PageStackAction.Replace
    acceptDestinationProperties: { "roomId": roomId, "url": url }
    onAccepted: app.callNoticeSeen = true
    onRejected: rustrx.command({ cmd: "call_end" })

    // a long paragraph in landscape scrolls instead of being cut off (0.8 audit)
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            DialogHeader {
                //% "Call"
                acceptText: qsTrId("rustrx-bt-call-go")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "Calls in Rustrx are Element Call: a web page that Rustrx loads from call.element.io for each call. The sound and picture go through a media server, your homeserver's or Element's. Rustrx says this once."
                text: qsTrId("rustrx-la-call-notice")
            }
        }

        VerticalScrollDecorator { }
    }
}
