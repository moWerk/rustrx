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
import Rustrx 1.0
import Sailfish.Silica 1.0

// The emoji picker in the keyboard's place: inserts at the cursor, or
// reacts to a message (EmojiPanel). The picker and the keyboard share one
// place at the bottom of the screen. Silica shrinks the page by the
// keyboard as it slides (keyboardSize); the picker sits at the screen's
// bottom, lifted by lift, and the conversation ends above whichever is
// higher (aboveKeyboard, the list's bottom margin). Switching from one to
// the other keeps the height: the picker takes the keyboard's, lies under
// it and is uncovered as the keyboard slides away, or is covered by the
// keyboard sliding up and only then goes. With nothing open it slides in
// and out on its own.
// The page keeps what a pick does (the signals below).
Item {
    id: dock

    // the page it sits on (its height, orientation and composer) and the chat trace
    property Item page
    property Item trace

    property alias open: panel.open
    property alias mode: panel.mode
    property alias eventId: panel.eventId
    property alias stickers: panel.stickers
    property alias packs: panel.packs
    signal picked(string emoji)
    signal pickedImage(var image)
    signal textReaction()

    readonly property real keyboardSize: pageStack.panelSize
    property real panelHeight: defaultHeight()
    property real lift: 0
    property bool liftAnimated
    // the keyboard is coming back in the picker's place
    property bool toKeys
    // the picker's part above the keyboard (none while the keyboard covers it)
    readonly property real aboveKeyboard: Math.max(0, lift - keyboardSize)

    // at the screen's bottom, below the page while the keyboard is up
    width: parent.width
    y: (page ? page.height : 0) + keyboardSize - lift
    height: panelHeight
    visible: lift > 0

    Behavior on lift {
        enabled: dock.liftAnimated
        NumberAnimation { duration: Tunables.panelSlideMs; easing.type: Easing.InOutQuad }
    }
    function defaultHeight() {
        return Math.round(Screen.height * (!page || page.isPortrait ? Tunables.emojiPanelPortrait : Tunables.emojiPanelLandscape))
    }
    Connections {
        target: dock.page
        onIsPortraitChanged: { dock.panelHeight = dock.defaultHeight(); if (panel.open) dock.lift = dock.panelHeight }
    }

    function show(newMode, forEvent) {
        toKeys = false
        if (keyboardSize > 0) {
            // under the keyboard, at its height, uncovered as it goes
            panelHeight = keyboardSize
            liftAnimated = false
            lift = panelHeight
            panel.show(newMode, forEvent)
            Qt.inputMethod.hide()
        } else {
            panel.show(newMode, forEvent)
            liftAnimated = true
            lift = panelHeight
        }
    }
    // the emoji button again, or a tap into the field: the keyboard covers
    // the picker, which goes once it is covered
    function toKeyboard() {
        toKeys = true
        if (page.composer) page.composer.field.forceActiveFocus()
        Qt.inputMethod.show()
    }
    onKeyboardSizeChanged: {
        if (trace) trace.logKeyboard()
        // the keyboard's own height, for the picker next time
        if (Qt.inputMethod.visible && !panel.open && keyboardSize > 0)
            panelHeight = keyboardSize
        if (toKeys && panel.open && keyboardSize >= lift - 1) {
            toKeys = false
            liftAnimated = false
            lift = 0
            panel.open = false
        }
    }
    onLiftChanged: if (trace) trace.logLift()

    EmojiPanel {
        id: panel
        height: parent.height
        // closed by itself (a reaction, a sticker): it slides away
        onOpenChanged: if (!open && dock.lift > 0) { dock.liftAnimated = true; dock.lift = 0 }
        onPicked: dock.picked(emoji)
        onPickedImage: dock.pickedImage(image)
        onTextReaction: dock.textReaction()
    }
    // the keyboard back (a tap into the field): it takes the picker's place
    Connections {
        target: Qt.inputMethod
        onVisibleChanged: {
            if (dock.trace) dock.trace.log("im", "vis=" + Qt.inputMethod.visible)
            if (Qt.inputMethod.visible && panel.open) dock.toKeys = true
        }
    }
}
