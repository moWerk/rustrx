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

// Shown until the core has read the saved session, usually a blink. When
// it could not (the store's key did not come: Secrets locked, its prompt
// not answered in time), the session is still there: no login page, but
// the reason and a way to ask Secrets again.
Page {
    id: page
    objectName: "start"
    readonly property bool waiting: !rustrx.restoreFailed || rustrx.restoring

    PageBusyIndicator { running: page.waiting }

    Column {
        visible: !page.waiting
        width: parent.width
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.paddingLarge

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeLarge
            //% "Your messages are locked"
            text: qsTrId("rustrx-la-restore-failed")
        }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.secondaryHighlightColor
            //% "Sailfish Secrets did not give Rustrx the key to its messages, for example because its question was not answered. You are still logged in; nothing is lost."
            text: qsTrId("rustrx-la-restore-failed-hint")
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            //% "Try again"
            text: qsTrId("rustrx-bt-restore-retry")
            onClicked: rustrx.retryRestore()
        }
    }
}
