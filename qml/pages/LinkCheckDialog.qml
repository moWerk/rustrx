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
import "../components"

// Asked before a link opens whose text reads as an address of another
// host than the link's own (audit UC8): "https://bank.example" in the
// text could open evil.example. The caller opens the link on Open.
Dialog {
    id: dialog

    // the host the text shows, and the one the link goes to
    property string shownHost
    property string goesHost

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            //% "Open link?"
            title: qsTrId("rustrx-he-open-link")
            acceptText: qsTrId("rustrx-bt-open")
        }
        Note {
            //: %1 the host a link's text shows, %2 the host it opens, like bank.example
            //% "The text shows %1, but the link goes to %2."
            text: qsTrId("rustrx-la-link-differs").arg(dialog.shownHost).arg(dialog.goesHost)
        }
    }
}
