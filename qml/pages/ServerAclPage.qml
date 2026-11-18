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

// Which servers may take part in the room (its server access list), for
// those who may: allowed and denied servers, one per line, * and ? as
// wildcards. One's own server always stays in: a list that would shut it
// out is not saved (every later change, the fix too, would be refused).
Page {
    id: page

    property string roomId
    property var settings: ({})

    readonly property var acl: settings.server_acl || null
    readonly property string own: settings.own_server || ""

    function lines(t) { return t.split("\n").map(function (l) { return l.trim() }).filter(function (l) { return l.length > 0 }) }
    function matches(pattern, server) {
        var re = new RegExp("^" + pattern.replace(/[.+^${}()|[\]\\]/g, "\\$&").replace(/\*/g, ".*").replace(/\?/g, ".") + "$", "i")
        return re.test(server)
    }
    function allows(allow, deny, server) {
        return allow.some(function (p) { return matches(p, server) }) && !deny.some(function (p) { return matches(p, server) })
    }
    readonly property var allowList: lines(allowField.text)
    readonly property var denyList: lines(denyField.text)
    readonly property bool ownStays: allows(allowList, denyList, own)

    Connections {
        target: rustrx
        onRoomSettings: if (settings.room_id === page.roomId) page.settings = settings
    }
    Component.onCompleted: {
        allowField.text = acl ? acl.allow.join("\n") : "*"
        denyField.text = acl ? acl.deny.join("\n") : ""
        ipSwitch.checked = acl ? acl.allow_ip_literals === true : false
    }

    RemorsePopup { id: remorse }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader {
                //% "Server access"
                title: qsTrId("rustrx-he-server-acl")
                description: rustrx.roomInfo(page.roomId).name || ""
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: page.acl
                      //% "People from denied servers cannot join or write here; those inside stay, but their messages are refused."
                      ? qsTrId("rustrx-la-acl-on")
                      //% "No list yet: every server may take part."
                      : qsTrId("rustrx-la-acl-none")
            }

            TextArea {
                id: allowField
                width: parent.width
                //% "Allowed servers, one per line (* for all)"
                label: qsTrId("rustrx-la-acl-allow")
                placeholderText: label
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
            }
            TextArea {
                id: denyField
                width: parent.width
                //% "Denied servers, one per line (*.example.org for its subdomains)"
                label: qsTrId("rustrx-la-acl-deny")
                placeholderText: label
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
            }
            TextSwitch {
                id: ipSwitch
                //% "Allow servers by IP address"
                text: qsTrId("rustrx-la-acl-ip")
                //% "Off keeps out servers without a name, a common source of spam."
                description: qsTrId("rustrx-la-acl-ip-hint")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: page.ownStays ? Theme.secondaryHighlightColor : Theme.errorColor
                text: page.ownStays
                      //: %1 the own server
                      //% "Your server %1 stays allowed."
                      ? qsTrId("rustrx-la-acl-own-ok").arg(page.own)
                      //% "This would shut out your own server %1, and you with it: allow it, and do not deny it."
                      : qsTrId("rustrx-la-acl-own-out").arg(page.own)
            }

            PageButton {
                enabled: page.ownStays
                //% "Save the list"
                text: qsTrId("rustrx-bt-acl-save")
                onClicked: {
                    var allow = page.allowList, deny = page.denyList, ip = ipSwitch.checked, id = page.roomId, core = rustrx
                    //% "Saving the server list"
                    remorse.execute(qsTrId("rustrx-re-acl-save"), function () {
                        core.command({ cmd: "set_server_acl", room_id: id, allow: allow, deny: deny, allow_ip_literals: ip })
                    })
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //% "A moderation bot (Draupnir, Mjolnir) may keep this list itself and put its own back. Then change it through the bot."
                text: qsTrId("rustrx-la-acl-bot")
            }
        }
        VerticalScrollDecorator { }
    }
}
