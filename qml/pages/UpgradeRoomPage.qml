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
import "../components"

// A room moved to a newer room version, for those who may: what it means
// (a new room; this one ends and points to it), the version (the
// server's default first), then Upgrade.
Dialog {
    id: dialog

    property string roomId
    property var settings: ({})

    readonly property var versions: {
        var v = (settings.versions || []).slice()
        var d = settings.version_default
        v.sort(function (a, b) { return (b === d) - (a === d) || (parseInt(b, 10) || 0) - (parseInt(a, 10) || 0) })
        return v
    }
    canAccept: versions.length > 0 && versionBox.currentIndex >= 0
    onAccepted: rustrx.command({ cmd: "upgrade_room", room_id: roomId, version: versions[versionBox.currentIndex] })

    Column {
        width: parent.width
        spacing: Theme.paddingMedium

        DialogHeader {
            //% "Upgrade"
            acceptText: qsTrId("rustrx-bt-upgrade-short")
            //% "Upgrade the room"
            title: qsTrId("rustrx-bt-upgrade")
        }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            //% "An upgrade makes a new room with the same name, settings and roles. This room ends and points to the new one: members are asked to follow, bridges and bots must be invited again. It cannot be undone."
            text: qsTrId("rustrx-la-upgrade-note")
        }
        DetailItem {
            //% "Now"
            label: qsTrId("rustrx-la-version-now")
            value: dialog.settings.version || "?"
        }
        HoldComboBox {
            id: versionBox
            width: parent.width
            //% "New version"
            label: qsTrId("rustrx-la-version-new")
            menu: ContextMenu {
                Repeater {
                    model: dialog.versions
                    MenuItem {
                        text: modelData === dialog.settings.version_default
                              //: %1 a room version
                              //% "%1 (the server's default)"
                              ? qsTrId("rustrx-la-version-default").arg(modelData) : modelData
                    }
                }
            }
        }
    }
}
