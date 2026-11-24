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
import Rustrx 1.0
import "../names.js" as Names

// The user's settings: the look in dconf, and what another app could turn
// against the user in Rustrx's own file (PrivateSettings, audit SEC1: every
// app may read and write dconf). The pages use the names below. The app and
// Service.qml each make one (two engines; both hear every change).
QtObject {
    // appearance
    // avatar corners: the radius in percent of the side, 0 square, 50 a
    // circle. roundAvatars is the old switch, read once (off: square)
    property alias avatarCorners: look.avatarCorners
    property alias roundAvatars: look.roundAvatars
    property alias showReadBy: look.showReadBy
    property alias highlightFavs: look.highlightFavs
    // how many lines of the last message the room list shows (1 to 3)
    property alias previewLines: look.previewLines
    // the room list's tabs
    property alias separateLists: look.separateLists
    property alias separateFavs: look.separateFavs
    property alias separateSpaces: look.separateSpaces
    // GIFs in the chat move by themselves while in view (else at a tap),
    // and play once unless looping is on (App > Behaviour; the author, 0.8)
    property alias gifAutoplay: look.gifAutoplay
    property alias gifLoop: look.gifLoop
    // the kind of tab the main page was left on
    property alias lastTabKind: look.lastTabKind

    // privacy
    property alias sendReceipts: own.sendReceipts
    // a room counts as read once its newest messages were on screen; when
    // off, opening it is enough
    property alias readWhenShown: own.readWhenShown
    // a room notification's reply field and its favourite emoji buttons
    property alias notifyReply: look.notifyReply
    property alias notifyReactions: look.notifyReactions
    // a notification when someone joins a room one moderates (opt out)
    property alias notifyJoins: look.notifyJoins
    // those emoji buttons, space separated (three at most: they share one
    // row with Reply and Mark as read)
    property alias notifyEmoji: look.notifyEmoji
    property alias sendTyping: own.sendTyping
    property alias typingHint: look.typingHint
    // shown as online to others (presence); off: offline, always
    property alias showOnline: own.showOnline
    // chat names and senders in notifications and on the cover
    property alias chatDetails: own.chatDetails
    // invitations from people we share no room with: a banner and their
    // picture; off: quiet in the list, no picture
    property alias strangerInvites: own.strangerInvites

    // when Rustrx is closed: "push", "background" or "off"; "" until chosen
    property alias closedMode: own.closedMode
    // notifications: the message itself or only a count, and the stand-in's line
    property alias notifyText: own.notifyText
    property alias showBackgroundLine: look.showBackgroundLine

    // start in the chat that was open when the app was closed
    property alias reopenLast: own.reopenLast
    // every page upright, whatever the phone's rotation (pictures and videos still turn)
    property alias portraitOnly: look.portraitOnly
    property alias lastRoom: own.lastRoom
    // whose it is (the account's key)
    property alias lastRoomAccount: own.lastRoomAccount

    // the emoji used, as JSON {emoji: count}
    property alias emojiCounts: look.emojiCounts
    property alias prototypeNoticeSeen: look.prototypeNoticeSeen
    // the page about a phone without sailfish-secrets was agreed to
    property alias secretsNoticeSeen: own.secretsNoticeSeen
    // the one notice before the first call (CallNotice)
    property alias callNoticeSeen: own.callNoticeSeen

    // Rustrx's own file; the defaults are in src/PrivateSettings.cpp
    property QtObject ownFile: PrivateSettings { id: own }

    // dconf: every property here is the key of the same name under the
    // path, and its value here is the default while the key is unset. Add
    // no property here that is not a key.
    property QtObject dconfGroup: ConfigurationGroup {
        id: look
        path: Names.dconfPath

        property int avatarCorners: 50
        property bool roundAvatars: true
        property bool showReadBy: true
        property bool highlightFavs: true
        property int previewLines: 1
        property bool separateLists: true
        property bool separateFavs: true
        property bool separateSpaces: true
        property bool gifAutoplay: true
        property bool gifLoop: false
        property string lastTabKind: "rooms"
        property bool notifyReply: true
        property bool notifyReactions: true
        property bool notifyJoins: true
        property string notifyEmoji: "👍 😀 👎"
        property bool typingHint: true
        property bool showBackgroundLine: true
        property bool portraitOnly: false
        property string emojiCounts: "{}"
        property bool prototypeNoticeSeen: false
    }
}
