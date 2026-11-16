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
pragma Singleton
import QtQuick 2.6

// The app's tuned numbers in one place (import Rustrx 1.0, then Tunables.x).
// Registered in main.cpp for both engines. Bare factors and milliseconds
// only: no Theme or Screen here, so Service.qml's engine (the stand-in has
// no window) does not pull in Silica. The call sites multiply.
// Its own directory: in qml/ it would also be a plain type of the directory
// that harbour-rustrx.qml and Service.qml see, next to the singleton.
QtObject {
    // text and emoji sizes, as factors of the size named in each comment
    // Twemoji in messages, reactions and quotes, of the text it sits in
    readonly property real emojiScale: 1.25
    // Twemoji in lists (room names and previews, members, thread roots);
    // smaller than emojiScale today, which may not be meant
    readonly property real emojiScaleList: 1.2
    // custom emote height in messages, of fontSizeMedium (sent to C++)
    readonly property real emoteScale: 1.25
    // a picture in an emoji grid cell, of iconSizeMedium
    readonly property real emojiGlyphScale: 0.75
    // preview text, state icons and cover labels, of fontSizeSmall
    readonly property real metaTextScale: 0.8
    // the read check mark in a room row, of fontSizeSmall (a tad over metaTextScale)
    readonly property real readCheckScale: 0.85
    // room name in a row, of fontSizeMedium
    readonly property real roomNameScale: 1.1
    // unread badge digits, of fontSizeExtraSmall
    readonly property real badgeTextScale: 0.9
    // tab titles once crowdedTabCount tabs are shown, of fontSizeMedium
    readonly property real crowdedTabScale: 0.9
    readonly property int crowdedTabCount: 4

    // pictures and bubbles
    // a room picture inside its slot (list row, room header), of the slot
    readonly property real avatarScale: 0.9
    // air above and below a room row, of (itemSizeMedium - iconSizeLarge)
    readonly property real roomRowMarginFactor: 1.5
    // image and video bubbles, of the screen width
    readonly property real mediaMaxWidth: 0.6
    // height over width when an event carries no size
    readonly property real mediaFallbackAspect: 0.75
    // quote bar and mention bar, in pixels
    readonly property int barWidth: 2
    // quote background, black at this opacity
    readonly property real quoteShade: 0.1
    // preview cards (join, space), opacity of highlightBackgroundColor
    readonly property real cardOpacity: 0.3
    // emoji picker background, opacity of highlightDimmerColor
    readonly property real panelOpacity: 0.9
    // emoji picker height before the keyboard's is known, of Screen.height
    readonly property real emojiPanelPortrait: 0.36
    readonly property real emojiPanelLandscape: 0.5
    // the fewest columns in the emoji grid
    readonly property int emojiMinColumns: 6
    // image viewer: largest pinch zoom, double tap zoom
    readonly property real imageMaxZoom: 6
    readonly property real imageTapZoom: 2.5

    // favourite glow (Settings: highlight favourites)
    // row band opacity at rest, plus this much at the top of a breath
    readonly property real favGlowBase: 0.03
    readonly property real favGlowAmp: 0.09
    // one half of a breath
    readonly property int favGlowPulseMs: 1500
    // the star's sparkle shows in the breath's last quarter
    readonly property real favSparkleFrom: 0.75
    readonly property string favStarColor: "#ffd54a"
    readonly property string favSparkleColor: "#fff4b8"

    // counts and limits
    // events asked for per back pagination (the core's own default)
    readonly property int paginateBatch: 30
    // custom emoji of one message offered to add to the own pack
    readonly property int emoteOfferMax: 3
    // lines of a quoted message
    readonly property int quoteLines: 3
    // most lines of the room list preview (the Settings menu lists 1 to 3 by hand)
    readonly property int previewLinesMax: 3
    // shortest filter or search term
    readonly property int minSearchChars: 2
    // shortest room address to look up
    readonly property int minJoinChars: 3
    // shortest key export passphrase
    readonly property int minPassphrase: 8
    // most used emoji kept for the picker's top row and the reactions
    readonly property int recentEmojiCount: 5
    // from this count on the cover shows thousands as "K"
    readonly property int coverKiloFrom: 1000
    // Matrix power levels (the core uses the same, commands.rs)
    readonly property int powerModerator: 50
    readonly property int powerAdmin: 100

    // timing, in milliseconds
    // the "3m" age labels tick
    readonly property int ageTickMs: 60000
    // page stack not ready yet: try again after this long
    readonly property int retryIntervalMs: 300
    // a pending room waits a little longer per try
    readonly property int pendingRoomPollMs: 500
    // tries before giving up (40 x 300 ms = 12 s, 40 x 500 ms = 20 s)
    readonly property int retryMaxTries: 40
    // the first start's prototype notice, once the stack is still
    readonly property int noticeDelayMs: 600
    // draft save and server probe after the last keystroke
    readonly property int typingDebounceMs: 800
    // search as you type; shorter than typingDebounceMs
    readonly property int searchDebounceMs: 400
    // send typing=false after this long without a key
    readonly property int typingIdleMs: 4000
    // a room just opened counts as opening this long
    readonly property int roomOpenGraceMs: 6000
    // a message menu closes before the composer gets the focus back
    readonly property int menuCloseMs: 350
    // the emoji panel slides in and out
    readonly property int panelSlideMs: 250
    // room info re-anchors its list after the header laid out
    readonly property int listSettleMs: 250
    // the chat's place is put back once the keyboard is still: the first
    // wait, then the wait after each of its steps
    readonly property int anchorFirstMs: 300
    readonly property int anchorStepMs: 150
    // the wake job holds the device awake this long
    readonly property int wakeHoldMs: 20000
    // shortest notification expiry (a ringing call)
    readonly property int ringMinMs: 5000
    // x-nemo-priority of a ringing call
    readonly property int ringPriority: 120
}
