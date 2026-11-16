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

// Chat position trace: every move of the room's list with what caused it,
// in the app log. Off unless switched on:
// dconf write /apps/harbour-rustrx/debugChatPos true
// Also the repeatable fling for frame timing (RUSTRX_FRAMES=1): a few
// seconds after the room page comes up, the list is flung N times, up and
// down in turn, at one speed, so two builds see the same motion:
// dconf write /apps/harbour-rustrx/debugFlick 6
// an Item, not a QtObject: Qt 5.6 parses no `default property list<T>`,
// and the timers and connections below need a parent that takes children
Item {
    id: trace

    // the room page (its status and composer), its emoji dock (keyboard and
    // picker heights), its list and model
    property Item page
    property Item dock
    property Item list
    property QtObject model

    readonly property bool enabled: debugChatPos.value === true
    property real lastY: NaN
    property real lastCh: NaN
    property real lastKb
    // the first move after a status change is always logged
    property bool fresh

    // the fling (debugFlick); these belong in Tunables once it is free to edit
    readonly property int flickCount: Math.max(0, Math.floor(Number(debugFlick.value) || 0))
    readonly property int flickDelayMs: 3000
    readonly property int flickSpacingMs: 1500
    // pixels a second, of the theme's pixel ratio
    readonly property real flickSpeed: 2500
    property int flicksDone

    DebugValue { id: debugChatPos; name: "debugChatPos"; defaultValue: false }
    DebugValue { id: debugFlick; name: "debugFlick"; defaultValue: 0 }
    Connections {
        target: trace.flickCount > 0 ? trace.page : null
        onStatusChanged: {
            flickStep.stop()
            if (trace.page.status !== PageStatus.Active) return
            trace.flicksDone = 0
            flickStep.interval = trace.flickDelayMs
            flickStep.start()
        }
    }
    // the first fling towards older messages, then back, and so on
    Timer {
        id: flickStep
        onTriggered: {
            if (trace.flicksDone >= trace.flickCount || trace.page.status !== PageStatus.Active) return
            var v = (trace.flicksDone % 2 === 0 ? 1 : -1) * trace.flickSpeed * Theme.pixelRatio
            trace.flicksDone++
            console.log("debugflick: " + trace.flicksDone + "/" + trace.flickCount + " v=" + Math.round(v))
            trace.list.flick(0, v)
            interval = trace.flickSpacingMs
            start()
        }
    }
    Connections {
        target: trace.model
        onRowsInserted: if (trace.enabled) trace.log("ins", "rows=" + first + ".." + last)
        onRowsRemoved: if (trace.enabled) trace.log("rem", "rows=" + first + ".." + last)
        onRowsMoved: if (trace.enabled) trace.log("mov", "rows=" + start + ".." + end + ">" + row)
        onDataChanged: if (trace.enabled) trace.log("data", "rows=" + topLeft.row + ".." + bottomRight.row)
        onModelReset: trace.log("reset")
        onLayoutChanged: trace.log("layout")
    }
    Connections {
        target: trace.page && trace.page.composer ? trace.page.composer.field : null
        onActiveFocusChanged: trace.log("inputfocus", "af=" + trace.page.composer.field.activeFocus)
    }

    function logStatus() {
        fresh = true
        log("status")
    }
    function logY() {
        if (enabled && (fresh || isNaN(lastY) || Math.abs(list.contentY - lastY) > 20)) {
            fresh = false
            log("y")
        }
    }
    function logContentHeight() {
        if (enabled && (isNaN(lastCh) || Math.abs(list.contentHeight - lastCh) > 20)) log("ch")
    }
    function logKeyboard() {
        var kb = dock.keyboardSize
        if (enabled && (kb === 0 || Math.abs(kb - lastKb) > 40)) { lastKb = kb; log("kb") }
    }
    function logLift() {
        if (enabled && (!dock.liftAnimated || dock.lift === 0 || dock.lift >= dock.panelHeight)) log("lift")
    }
    // a row that grew or shrank (a picture, a menu)
    function logRow(index, before, after) {
        if (enabled && before > 0 && Math.abs(after - before) > 20)
            log("row", "i=" + index + " " + Math.round(before) + ">" + Math.round(after))
    }

    function statusName(s) {
        return s === PageStatus.Inactive ? "inactive" : s === PageStatus.Activating ? "activating"
             : s === PageStatus.Active ? "active" : s === PageStatus.Deactivating ? "deactivating" : String(s)
    }
    // the row at a content y, stepping past the spacing and footer gaps
    function rowAt(y, step) {
        for (var d = 0; d <= 40; d += 8) {
            var i = list.indexAt(list.contentX + list.width / 2, y + step * d)
            if (i >= 0) return i
        }
        return -1
    }
    function log(ev, extra) {
        if (!enabled) return
        var top = rowAt(list.contentY + 1, 1)
        var bot = rowAt(list.contentY + list.height - 1, -1)
        // the composer covers the bottom edge: the row right above it
        if (bot < 0 && list.headerItem && list.headerItem.y > list.contentY)
            bot = rowAt(list.headerItem.y - 1, -1)
        var tb = ""
        if (top >= 0) {
            var r = model.get(top)
            tb = r.kind === "event" ? (r.body || "").replace(/\s+/g, " ").slice(0, 16) : r.kind
        }
        var f = app.activeFocusItem
        var fn = f === undefined ? "n/a" : f ? (f.objectName || String(f).split("(")[0]) : "none"
        var c = page.composer
        console.log("chatpos: " + ev + " y=" + Math.round(list.contentY) + " oy=" + Math.round(list.originY)
                    + " ch=" + Math.round(list.contentHeight) + " h=" + Math.round(list.height)
                    + " hdr=" + (list.headerItem ? Math.round(list.headerItem.height) : -1)
                    + " kb=" + Math.round(dock.keyboardSize) + " lift=" + Math.round(dock.lift)
                    + " top=" + top + " \"" + tb + "\" bot=" + bot + " status=" + statusName(page.status)
                    + " focus=" + fn + " inp=" + (c && c.field.activeFocus ? 1 : 0)
                    + " src=" + (list.dragging ? "drag" : list.flicking ? "flick" : "-")
                    + (extra ? " " + extra : ""))
        lastY = list.contentY
        lastCh = list.contentHeight
    }
}
