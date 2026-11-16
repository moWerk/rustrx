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

// The chat keeps its place. It moves on an action (send, reply, edit,
// a reaction) or to follow new messages at the newest end; otherwise the
// message at the bottom edge keeps its distance from that edge. When a
// page comes over the chat (a picture, a thread, the room's page) that
// message (by its row id, not a raw contentY: the keyboard changes the
// height in between and older history may page in) and its distance
// are noted; back, they are put back once, after the keyboard has
// settled. (Silica's auto scroll to the focused text field, the main
// mover before, is off: see the room's composer.)
QtObject {
    id: anchor

    property Item page
    property Item list
    property QtObject model
    property QtObject trace

    // the message at the bottom edge and its distance from it
    property string rowId
    property real offset
    // its row when it was noted; checked before the whole timeline is searched
    property int rowIndex: -1
    property bool newest
    property bool pending
    property bool applying

    // a bare QtObject takes no children: this list holds them
    default property list<QtObject> _children

    // once the keyboard has stopped moving (its animation changes the
    // height): 300 ms the first time, for a keyboard that does not move
    // at all, 150 ms after each of its steps
    Timer { id: settle; onTriggered: if (anchor.pending) anchor.apply() }
    Connections {
        target: pageStack
        onPanelSizeChanged: {
            var s = anchor.page.status
            if (!anchor.pending || s === PageStatus.Inactive || s === PageStatus.Deactivating) return
            anchor.apply(true)
            if (s === PageStatus.Active) { settle.interval = 150; settle.restart() }
        }
    }
    // while coming back, any move not made by a finger is undone at once:
    // the chat was pulled to the newest end and came back half a second
    // later, visibly
    Connections {
        target: anchor.list
        onContentYChanged: {
            var s = anchor.page.status
            if (anchor.pending && !anchor.applying && !anchor.list.moving
                    && (s === PageStatus.Activating || s === PageStatus.Active))
                anchor.apply(true)
        }
        // a finger takes over: the hold ends
        onMovementStarted: anchor.pending = false
    }

    // the row at a point of the view, as an item and its index
    function rowAt(y) {
        for (var k = 0; k < 30; k++) {
            var i = list.indexAt(list.width / 2, y - k * 8)
            if (i >= 0) return { index: i, item: list.itemAt(list.width / 2, y - k * 8) }
        }
        return null
    }
    // the newest message is in view at the bottom: by the row there, not by
    // the view's edges (with the composer as the list's header and the
    // place put back by hand, atYBeginning did not reliably turn true)
    function atNewest() {
        if (list.count === 0) return true
        var r = rowAt(list.contentY + list.height - 1)
        return !!r && r.index === 0
    }
    function take() {
        newest = atNewest()
        rowId = ""
        rowIndex = -1
        var bottom = list.contentY + list.height
        var r = rowAt(bottom - 1)
        if (!r || !r.item) return
        rowId = model.get(r.index).id || ""
        rowIndex = r.index
        offset = bottom - (r.item.y + r.item.height)
        trace.log("anchor-take", "id=" + rowId.slice(0, 10) + " off=" + Math.round(offset) + " newest=" + newest)
    }
    // keep: put it back but keep holding (while the page comes in and the
    // keyboard moves); without: the last time, the hold ends
    function apply(keep) {
        if (!keep) pending = false
        if (applying) return
        applying = true
        place()
        applying = false
    }
    // where the noted message is now: its old row first (this runs on every
    // frame of the hold), else a search
    function findRow() {
        if (rowIndex >= 0 && rowIndex < model.count && model.get(rowIndex).id === rowId)
            return rowIndex
        rowIndex = -1
        for (var i = 0; i < model.count; i++)
            if (model.get(i).id === rowId) { rowIndex = i; break }
        return rowIndex
    }
    function place() {
        if (newest) { trace.log("anchor-newest"); list.positionViewAtBeginning(); return }
        if (!rowId.length) return
        var index = findRow()
        if (index < 0) { trace.log("anchor-gone"); return }
        // into view first, then exactly to its old distance from the bottom
        list.positionViewAtIndex(index, ListView.Contain)
        for (var y = list.contentY; y < list.contentY + list.height; y += 8) {
            if (list.indexAt(list.width / 2, y) !== index) continue
            var item = list.itemAt(list.width / 2, y)
            if (item) list.contentY = item.y + item.height + offset - list.height
            break
        }
        trace.log("anchor-apply", "index=" + index)
    }

    // the page's status: noted on leaving, held from the first frame back
    function pageStatus(s) {
        if (s === PageStatus.Deactivating) {
            take()
            pending = true
        }
        if (s === PageStatus.Activating && pending)
            apply(true)
        if (s === PageStatus.Active && pending) {
            apply(true)
            settle.interval = 300
            settle.restart()
        }
    }
}
