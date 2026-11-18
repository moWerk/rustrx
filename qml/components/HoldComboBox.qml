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

// A ComboBox that also takes Silica's press-and-drag, as a list item's
// menu does: press and hold opens the menu, the finger drags to a choice
// (it lights up), letting go picks it. A tap opens the menu as before.
// Silica's ComboBox opens only on a click (the author's find); the drag
// goes through the ContextMenu's own highlighting and activation
// (_highlightMenuItem, _activatedMenuItem, _contentColumn, as Silica's
// ContextMenu handles a press on itself), and each use checks that they
// are there, so a Silica without them keeps the tap.
ComboBox {
    id: box

    readonly property bool _canDrag: !!menu && typeof menu._highlightMenuItem === "function"
                                     && typeof menu._activatedMenuItem === "function" && !!menu._contentColumn

    highlighted: hold.pressed && hold.containsMouse && !hold.dragging

    MouseArea {
        id: hold
        property bool dragging
        // the box's own line, not the menu opened under it
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: box.contentItem.height
        // an open menu takes its taps itself
        enabled: box.enabled && (!box._menuOpen || dragging)
        // the flickable may scroll the page until the menu is held
        preventStealing: dragging

        onClicked: if (!dragging) box._controller.openMenu()
        onPressAndHold: {
            if (!box._canDrag) { box._controller.openMenu(); return }
            dragging = true
            box._controller.openMenu()
        }
        onPositionChanged: {
            if (!dragging || !box.menu) return
            var p = mapToItem(box.menu, mouse.x, mouse.y)
            box.menu._highlightMenuItem(p.y - box.menu._contentColumn.y)
        }
        onReleased: {
            if (!dragging) return
            dragging = false
            if (box.menu && box.menu._highlightedItem)
                box.menu._activatedMenuItem(box.menu._highlightedItem)
        }
        onCanceled: dragging = false
    }
}
