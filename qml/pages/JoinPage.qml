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
import "../components"

// A room or space not joined yet, by its address or a link: what it is
// (the server's preview), then Join, or Ask to join where its rule wants
// that (with an optional message). A private space may not give a preview;
// joining and asking are offered all the same. Part of a name finds rooms
// and spaces while typing, in the own server's public directory and in
// matrix.org's (the biggest); a tap takes one's address.
Page {
    id: page

    // the address (#alias:server or !id), and the servers from a link
    property string address
    property var via: []

    property var preview: ({})
    property bool loading
    property bool waiting
    property bool asked

    // found by part of a name: [{room_id, name, alias, member_count, is_space, server}]
    property var found: []
    property string searched
    // directory answers still out
    property int searching
    readonly property string ownServer: rustrx.userId.indexOf(":") > 0 ? rustrx.userId.slice(rustrx.userId.indexOf(":") + 1) : ""
    // a full address or room ID goes to the preview; anything else is searched
    function isAddress(t) { return /^[#!][^:\s]+:\S+$/.test(t) }
    function search() {
        var term = addressField.text.trim().replace(/^#/, "")
        if (term.length < Tunables.minSearchChars || isAddress(addressField.text.trim())) return
        searched = term
        found = []
        var big = ownServer !== Tunables.bigDirectoryServer
        searching = big ? 2 : 1
        rustrx.command({ cmd: "search_rooms", term: term })
        if (big) rustrx.command({ cmd: "search_rooms", term: term, server: Tunables.bigDirectoryServer })
    }
    function take(room) {
        addressField.text = room.alias || room.room_id
        page.via = room.server ? [room.server] : []
        found = []
        look()
    }
    Timer { id: searchLater; interval: Tunables.typingDebounceMs; onTriggered: page.search() }

    readonly property bool known: preview.ok === true
    readonly property string rule: known ? preview.join_rule : ""
    readonly property string state_: known ? preview.state : ""

    function look() {
        var id = addressField.text.trim()
        if (id.length < Tunables.minJoinChars) return
        address = id
        preview = ({})
        asked = false
        loading = true
        rustrx.command({ cmd: "room_preview", room_or_alias: id, via: via })
    }
    function join() {
        waiting = true
        rustrx.command({ cmd: "join_by_id_or_alias", room_or_alias: address, via: via })
    }
    function knock() {
        waiting = true
        rustrx.command({ cmd: "knock", room_or_alias: address, reason: reasonField.text.trim(), via: via })
    }

    Component.onCompleted: if (address.length) { addressField.text = address; look() }

    Connections {
        target: rustrx
        onRoomPreview: {
            if (preview.room_or_alias !== page.address) return
            page.loading = false
            page.preview = preview
        }
        onJoined: {
            if (!page.waiting) return
            page.waiting = false
            if (rustrx.roomInfo(roomId).is_space || page.preview.is_space)
                pageStack.replace(Qt.resolvedUrl("SpacePage.qml"), { spaceId: roomId, spaceName: page.preview.name || "" })
            else
                pageStack.replace(Qt.resolvedUrl("RoomPage.qml"), { roomId: roomId, roomName: page.preview.name || "" })
        }
        onKnocked: { page.waiting = false; page.asked = true }
        onRoomSearchResults: {
            if (results.term !== page.searched) return
            page.searching = Math.max(0, page.searching - 1)
            var all = page.found.slice(), seen = {}
            for (var i = 0; i < all.length; i++) seen[all[i].room_id] = true
            for (var j = 0; j < results.rooms.length; j++) {
                var r = results.rooms[j]
                if (seen[r.room_id]) continue
                seen[r.room_id] = true
                r.server = results.server || ""
                all.push(r)
            }
            // the biggest first: the room one is looking for, most likely
            all.sort(function (a, b) { return (b.member_count || 0) - (a.member_count || 0) })
            page.found = all
        }
        onErrorOccurred: {
            if (cmd === "join_by_id_or_alias" || cmd === "knock" || cmd === "room_preview") { page.waiting = false; page.loading = false }
            if (cmd === "search_rooms") page.searching = Math.max(0, page.searching - 1)
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                //% "Join a space or room"
                title: qsTrId("rustrx-he-join")
            }

            TextField {
                id: addressField
                width: parent.width
                //% "Name or address, like #room:server"
                label: qsTrId("rustrx-la-join-name-or-address")
                placeholderText: label
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                EnterKey.iconSource: "image://theme/icon-m-search"
                EnterKey.enabled: text.trim().length >= Tunables.minSearchChars
                EnterKey.onClicked: {
                    if (page.isAddress(text.trim())) { page.via = []; page.look() }
                    else { searchLater.stop(); page.search() }
                }
                onTextChanged: {
                    if (page.isAddress(text.trim()) || text.trim() === page.address) return
                    page.preview = ({})
                    page.address = ""
                    searchLater.restart()
                }
            }

            // found by part of a name, until one is taken
            Repeater {
                model: page.known || page.loading ? [] : page.found
                BackgroundItem {
                    id: foundRow
                    width: column.width
                    height: Theme.itemSizeMedium
                    onClicked: page.take(modelData)
                    Column {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            color: foundRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                            text: modelData.name || modelData.alias || modelData.room_id
                        }
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: foundRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                            text: (modelData.is_space ? qsTrId("rustrx-la-kind-space") : qsTrId("rustrx-la-kind-room"))
                                  + " · " + qsTrId("rustrx-la-n-members", modelData.member_count || 0)
                                  + (modelData.joined
                                     //% "joined"
                                     ? " · " + qsTrId("rustrx-la-found-joined") : "")
                                  + (modelData.alias ? " · " + modelData.alias : "")
                        }
                    }
                }
            }
            Label {
                visible: !page.known && !page.loading && page.searched.length > 0 && page.found.length === 0 && page.searching === 0
                         && !page.isAddress(addressField.text.trim()) && addressField.text.trim().length >= Tunables.minSearchChars
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                //% "Nothing found in the public directories. A private room needs its full address, like #room:server."
                text: qsTrId("rustrx-la-found-none")
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: page.loading || (page.searching > 0 && page.found.length === 0)
                visible: running
                size: BusyIndicatorSize.Medium
            }

            // what it is
            Row {
                visible: page.known
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                spacing: Theme.paddingLarge
                Rectangle {
                    width: Theme.iconSizeLarge
                    height: width
                    radius: width * app.avatarRadius
                    color: joinPicture.status === Image.Ready ? "transparent" : Theme.rgba(Theme.highlightBackgroundColor, Tunables.cardOpacity)
                    Image {
                        id: joinPicture
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        source: page.preview.avatar_mxc ? rustrx.emotePath(page.preview.avatar_mxc) : ""
                        // clip: true would cut a square
                        layer.enabled: app.avatarRadius > 0
                        layer.effect: RoundMask { radius: app.avatarRadius }
                    }
                }
                Column {
                    width: parent.width - Theme.iconSizeLarge - parent.spacing
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        color: Theme.highlightColor
                        font.pixelSize: Theme.fontSizeLarge
                        text: page.preview.name || page.preview.alias || page.address
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        text: (page.preview.is_space
                               //% "Space"
                               ? qsTrId("rustrx-la-kind-space")
                               //% "Room"
                               : qsTrId("rustrx-la-kind-room"))
                              //: %n members
                              //% "%n members"
                              + " · " + qsTrId("rustrx-la-n-members", page.preview.member_count || 0)
                              + " · " + app.joinRuleName(page.rule, "")
                    }
                }
            }

            Label {
                visible: page.known && !!page.preview.topic
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: page.preview.topic || ""
            }

            // the server would not tell (a private space): still try
            Label {
                visible: page.preview.ok === false
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                //% "Could not load a preview. You can still try to join, or ask to join."
                text: qsTrId("rustrx-la-preview-failed")
            }

            // where we stand
            Label {
                visible: page.asked || page.state_ === "knocked" || page.state_ === "invited" || page.state_ === "joined" || page.state_ === "banned"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: page.asked || page.state_ === "knocked"
                      //% "You asked to join. You will get an invitation once they let you in."
                      ? qsTrId("rustrx-la-asked")
                      : page.state_ === "invited"
                      //% "You are invited: the invitation is with your rooms."
                      ? qsTrId("rustrx-la-preview-invited")
                      : page.state_ === "banned"
                      //% "You are banned from it."
                      ? qsTrId("rustrx-la-preview-banned")
                      //% "You are in it already."
                      : qsTrId("rustrx-la-preview-joined")
            }

            // asking needs no more than a word, and may have one
            TextField {
                id: reasonField
                readonly property bool show: !page.asked && page.state_ !== "knocked" && page.state_ !== "joined"
                                             && page.state_ !== "invited" && page.state_ !== "banned"
                                             && (page.rule === "knock" || page.rule === "knock_restricted" || page.preview.ok === false)
                visible: show
                width: parent.width
                //% "Message (optional)"
                label: qsTrId("rustrx-la-knock-reason")
                placeholderText: label
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingLarge
                PageButton {
                    inRow: true
                    visible: page.address.length > 0 && !page.loading && !page.asked && page.state_ !== "banned"
                             && page.state_ !== "knocked" && page.rule !== "knock" && page.rule !== "invite"
                             && (page.known || page.preview.ok === false)
                    enabled: !page.waiting
                    text: page.state_ === "joined" || page.state_ === "invited"
                          //% "Open"
                          ? qsTrId("rustrx-bt-open")
                          //% "Join"
                          : qsTrId("rustrx-bt-join")
                    onClicked: page.join()
                }
                PageButton {
                    inRow: true
                    visible: reasonField.show
                    enabled: !page.waiting
                    //% "Ask to join"
                    text: qsTrId("rustrx-bt-knock")
                    onClicked: page.knock()
                }
            }

            Label {
                visible: page.known && page.rule === "invite" && page.state_ !== "joined" && page.state_ !== "invited"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                //% "Only invited people can join. Ask someone inside for an invitation."
                text: qsTrId("rustrx-la-preview-invite-only")
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: page.waiting
                visible: running
                size: BusyIndicatorSize.Small
            }
        }
    }
}
