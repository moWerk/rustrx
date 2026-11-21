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
    // emoji picker height before the keyboard's is known, of the page's height
    readonly property real emojiPanelPortrait: 0.36
    readonly property real emojiPanelLandscape: 0.5
    // the fewest columns in the emoji grid
    readonly property int emojiMinColumns: 6
    // a link on the Account tab scrolls down to its section this fast
    readonly property int sectionScrollMs: 400
    // a tab just switched to lays out this long before it scrolls
    readonly property int tabSettleMs: 300
    // a copied recovery key leaves the clipboard after this, if still there
    readonly property int clipboardKeyMs: 120000
    // its colour when every safeguard is in place (the theme has no green)
    readonly property color securityComplete: "#3ecf6e"
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
    // a reply's quote tapped: older pages loaded at most to find the original
    readonly property int jumpMaxPages: 10
    // a room opening: older pages loaded at most to find its unread line
    readonly property int readSeekPages: 10
    // the original's highlight fades in this long (and stays three times it)
    readonly property int jumpFlashMs: 400
    // members offered while typing an @mention
    readonly property int mentionSuggestions: 5
    // people offered in the invite dialog while typing
    readonly property int inviteContactsMax: 8
    // lines of a quoted message
    readonly property int quoteLines: 3
    // most lines of the room list preview (the Settings menu lists 1 to 3 by hand)
    readonly property int previewLinesMax: 3
    // shortest filter or search term
    readonly property int minSearchChars: 2
    // shortest room address to look up
    readonly property int minJoinChars: 3
    // joining by part of a name also searches this server's public
    // directory, the biggest one, besides the own server's
    readonly property string bigDirectoryServer: "matrix.org"
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
    // another account: the switch waits this long for the pages that went
    readonly property int accountSwitchMs: 100
    // after a new login, the offer to verify waits this long after the last
    // change of the encryption state (it settles over the first syncs)
    readonly property int verifyNoticeDelayMs: 3000
    // the QR scanner reads a picture of the viewfinder this often, at most
    // this wide (wider ones cost time on the UI thread and read no better)
    // a QR login's rendezvous lives this long from the moment the code is
    // shown (Synapse's MSC4108 sessions: 60 s, not extended by use, not
    // configurable); the scan, the digits and the browser's confirmation
    // all have to fit
    readonly property int qrValidSeconds: 60
    // the accounts' colours in the merged view, by account key (keys stay
    // with an account for its life): mid tones that read on light and dark
    // ambiences alike
    readonly property var accountColours: ["#3fa7d6", "#e0457b", "#f2a541", "#59c3a7", "#a77ce8", "#e8743b", "#8fbf3f", "#d65db1"]
    readonly property int qrScanMs: 350
    readonly property int qrScanWidth: 720
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
    // read receipts from the open room, one per this long at most
    readonly property int markReadMs: 1500
    // the chat moved: what is on screen is looked at this much later
    readonly property int readScanMs: 400
    // the emoji panel slides in and out
    readonly property int panelSlideMs: 250
    // the chat's place is put back once the keyboard is still: the first
    // wait, then the wait after each of its steps
    readonly property int anchorFirstMs: 300
    readonly property int anchorStepMs: 150
    // the wake job holds the device awake this long
    readonly property int wakeHoldMs: 20000
    // shortest notification expiry (a ringing call)
    readonly property int ringMinMs: 5000
    // a classic call rings this long (Keys::kVoipInviteLifetimeMs; the engine also stops it)
    readonly property int voipRingMs: 60000
    // how long the call page shows why a call ended
    readonly property int voipEndShownMs: 2000
}
