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
import QtQuick.Window 2.2
import Sailfish.Silica 1.0

// The call page over the lock screen: a window of its own, which lipstick
// shows over the lock screen and the device lock as the phone app's call
// (Rustrx::showOverLock). Only the call page is in it, so nothing else of
// Rustrx shows without the unlock code. Made by the window when a call
// rings while the phone is locked; it goes when the call is over or the
// user swipes it away.
Window {
    id: callWindow

    // an Element Call ring for the page (VoipPage.ring), null for the classic call
    property var ring: null
    signal done()

    // the page's pageStack is this window's (the main window's otherwise)
    property alias pageStack: __silica_applicationwindow_instance.pageStack

    // swiped away: gone, as when the call is over
    property bool shown
    onVisibleChanged: {
        if (visible) shown = true
        else if (shown) done()
    }

    // Silica's own components find their window by this id (as in the
    // phone app's call window)
    ApplicationWindow {
        id: __silica_applicationwindow_instance
        cover: undefined
        allowedOrientations: Orientation.Portrait
        _defaultPageOrientations: Orientation.Portrait
        _defaultLabelFormat: Text.PlainText
        initialPage: Component {
            VoipPage {
                inCallWindow: true
                ring: callWindow.ring
                onCloseRequested: callWindow.done()
            }
        }
    }
}
