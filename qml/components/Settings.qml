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
import Nemo.Configuration 1.0
import "../names.js" as Names

// The user's settings in dconf. Every property here is the key of the same
// name under the path, and its value here is the default while the key is
// unset. The app and Service.qml each make one (two engines, one dconf).
// Add no property where this is used: it would become a key too.
ConfigurationGroup {
    path: Names.dconfPath

    // appearance
    property bool roundAvatars: true
    property bool showReadBy: true
    property bool highlightFavs: true
    // how many lines of the last message the room list shows (1 to 3)
    property int previewLines: 1
    // the room list's tabs
    property bool separateLists: true
    property bool separateFavs: true
    property bool separateSpaces: true
    // the kind of tab the main page was left on
    property string lastTabKind: "rooms"

    // privacy
    property bool sendReceipts: true
    // a room notification's reply field and its favourite emoji buttons
    property bool notifyReply: true
    property bool notifyReactions: true
    property bool sendTyping: true
    property bool typingHint: true

    // when Rustrx is closed: "push", "background" or "off"; "" until chosen
    property string closedMode: ""
    // notifications: the message itself or only a count, and the stand-in's line
    property bool notifyText: true
    property bool showBackgroundLine: true

    // start in the chat that was open when the app was closed
    property bool reopenLast: false
    property string lastRoom: ""

    // the emoji used, as JSON {emoji: count}
    property string emojiCounts: "{}"
    property bool prototypeNoticeSeen: false
}
