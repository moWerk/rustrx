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
import Nemo.Notifications 1.0

// Another program holds Rustrx's bus name, so no Rustrx can start (audit
// 0.8.4 IPC6): a push-woken process posts this once per boot and leaves
// (main.cpp, nameTaken). No buttons; a tap closes it.
Notification {
    appName: "Rustrx"
    appIcon: "harbour-rustrx"
    urgency: Notification.Normal
    //: notification title: Rustrx's bus name is held by another program
    //% "Rustrx could not start"
    summary: qsTrId("rustrx-no-name-taken")
    //% "Another program on this phone holds Rustrx's name."
    body: qsTrId("rustrx-no-name-taken-body")
    previewSummary: summary
    previewBody: body
}
