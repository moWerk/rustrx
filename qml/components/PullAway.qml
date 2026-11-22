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

// A swipe up closes what lies in it, the emoji panel's recipe turned over:
// a Flickable as tall as its content, dragged past its bounds, so the
// content follows the finger. Its rows keep their taps; only a drag is
// taken. Let go more than a row's height up (or half its own), it says so;
// less springs back.
Flickable {
    // let go past the mark: the share of its height it was pulled up by
    // (the content is put back at once, for the user to move on from there)
    signal pulledAway(real share)

    contentHeight: height
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.DragOverBounds

    onDraggingChanged: {
        if (dragging || height <= 0) return
        if (contentY > Math.min(Theme.itemSizeSmall, height / 2)) {
            var share = Math.min(1, contentY / height)
            // before the release's own spring back starts
            contentY = 0
            pulledAway(share)
        }
    }
}
