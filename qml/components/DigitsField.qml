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

// The two digits the other device of a QR flow shows, typed here. It
// empties itself once they are sent, so a second round does not start
// with the old ones (the QR login page kept them; 0.8 audit).
TextField {
    id: field

    signal entered(int code)

    width: parent ? parent.width : 0
    //% "The two digits"
    label: qsTrId("rustrx-la-qr-code")
    placeholderText: label
    inputMethodHints: Qt.ImhDigitsOnly
    validator: RegExpValidator { regExp: /^[0-9]{0,2}$/ }
    EnterKey.enabled: text.length === 2
    EnterKey.iconSource: "image://theme/icon-m-enter-accept"
    EnterKey.onClicked: {
        var code = parseInt(text, 10)
        text = ""
        field.entered(code)
    }
    // shown when the other side asks: the keyboard comes with it
    onVisibleChanged: if (visible) forceActiveFocus()
}
