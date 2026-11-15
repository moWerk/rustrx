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

// Shown once, on the first start: Rustrx is a prototype until 1.0, and it
// keeps a debug log. It goes only by Agree, which waits for "I have
// understood the above"; the log can be switched off right here.
Dialog {
    id: dialog
    objectName: "prototypeNotice"

    // no way around it but Agree
    backNavigation: false
    canAccept: understood.checked

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            DialogHeader {
                //% "Agree"
                acceptText: qsTrId("rustrx-he-agree")
                cancelText: ""
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
                //% "Rustrx is a prototype"
                text: qsTrId("rustrx-he-prototype")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "Until version 1.0 Rustrx is a prototype: it is used every day, but things can still break or change, and some parts are unfinished."
                text: qsTrId("rustrx-la-prototype")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "To fix problems without anyone having to make them happen again, Rustrx keeps a debug log: a small file on this phone that notes what the app does and its errors. It holds room and user IDs (also other people's) and server names, never message texts or passwords. It does not leave the phone unless you share it yourself (Settings, Debug log)."
                text: qsTrId("rustrx-la-prototype-log")
            }

            TextSwitch {
                //% "Keep the debug log"
                text: qsTrId("rustrx-la-prototype-keep-log")
                //% "Can be changed any time in Settings."
                description: qsTrId("rustrx-la-prototype-keep-log-hint")
                checked: rustrx.debugLog
                automaticCheck: false
                onClicked: rustrx.debugLog = !rustrx.debugLog
            }

            TextSwitch {
                id: understood
                //% "I have understood the above"
                text: qsTrId("rustrx-la-understood")
            }
        }

        VerticalScrollDecorator { }
    }
}
