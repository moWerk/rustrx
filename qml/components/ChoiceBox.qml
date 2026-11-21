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

// Every choice box: a HoldComboBox fed with its choices, so none hand-
// copies the follow-the-value fix again (the metadata box and "When Rustrx
// is closed" both lost it; 0.8 audit). It stays on the tapped choice,
// follows `value` when the change arrives (or another device makes one),
// and revert(reason) takes it back to the value in force and says why
// (the author left the behaviour to the least confusing; 2026-11-21).
// A choice's hint shows under the box while it is the one shown.
HoldComboBox {
    id: box

    // [{ value, text, hint? }]
    property var choices: []
    // the value in force
    property var value
    signal chosen(var value)

    function revert(reason) {
        currentIndex = shownIndex
        if (reason && reason.length) Notices.show(reason, Notice.Short)
    }

    width: parent ? parent.width : 0
    readonly property int shownIndex: {
        for (var i = 0; i < choices.length; i++)
            if (choices[i].value === value) return i
        return 0
    }
    // the ComboBox sets currentIndex itself on a tap: a binding there broke
    // at the first one, so the value is followed by hand
    currentIndex: shownIndex
    onShownIndexChanged: currentIndex = shownIndex
    onChoicesChanged: currentIndex = shownIndex
    description: currentIndex >= 0 && currentIndex < choices.length ? (choices[currentIndex].hint || "") : ""

    menu: ContextMenu {
        Repeater {
            model: box.choices
            MenuItem {
                text: modelData.text
                onClicked: if (modelData.value !== box.value) box.chosen(modelData.value)
            }
        }
    }
}
