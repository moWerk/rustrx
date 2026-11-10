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

// Find rooms in the server's public directory and people in the user
// directory; a typed address (#room:server, @user:server) is offered as is.
// Tapping a room joins and opens it, tapping a person opens the direct chat.
Page {
    id: page

    property string lastTerm
    property bool searching
    property string pending   // id being joined or started, for the busy label

    function search(term) {
        term = term.trim()
        hits.clear()
        if (term.length < 2) return
        page.lastTerm = term
        if (/^[@#!][^\s]+:[^\s]+$/.test(term)) {
            hits.append({ kind: term.charAt(0) === "@" ? "user" : "room",
                             id: term, name: term, info: "", joined: false })
        }
        page.searching = true
        rustrx.command({ cmd: "search", term: term })
    }

    function open(roomId, name) {
        page.pending = ""
        pageStack.replace(Qt.resolvedUrl("RoomPage.qml"), { roomId: roomId, roomName: name })
    }

    Connections {
        target: rustrx
        onSearchResults: {
            if (results.term !== page.lastTerm) return   // an older search
            page.searching = false
            var i, r
            for (i = 0; i < results.users.length; i++) {
                r = results.users[i]
                hits.append({ kind: "user", id: r.user_id, name: r.name || r.user_id,
                              info: r.name ? r.user_id : "", joined: false })
            }
            for (i = 0; i < results.rooms.length; i++) {
                r = results.rooms[i]
                hits.append({ kind: "room", id: r.alias || r.room_id, name: r.name || r.alias || r.room_id,
                              //: %1 the number of people in a room
                              //% "%1 members"
                              info: [r.alias, qsTrId("rustrx-la-members").arg(r.members), r.topic]
                                    .filter(function (s) { return s && s.length }).join(" · "),
                              joined: r.joined })
            }
        }
        onJoined: if (page.pending.length) open(roomId, page.pending)
        onDmReady: if (page.pending.length) open(roomId, page.pending)
        onErrorOccurred: page.pending = ""
    }

    ListModel { id: hits }

    Timer {
        id: debounce
        interval: 400
        onTriggered: page.search(field.text)
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: hits

        header: Column {
            width: parent.width

            PageHeader {
                //% "Add Room / Contact"
                title: qsTrId("rustrx-he-add")
            }

            SearchField {
                id: field
                width: parent.width
                //% "Room, person or address"
                placeholderText: qsTrId("rustrx-la-search-placeholder")
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: { debounce.stop(); page.search(text) }
                onTextChanged: debounce.restart()
                Component.onCompleted: forceActiveFocus()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.searching
                //% "Searching…"
                text: qsTrId("rustrx-la-searching")
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        delegate: ListItem {
            id: row
            width: list.width
            contentHeight: Theme.itemSizeMedium
            enabled: !page.pending.length

            onClicked: {
                if (model.kind === "user") {
                    page.pending = model.name
                    rustrx.command({ cmd: "create_dm", user_id: model.id })
                } else if (model.joined) {
                    open(model.id, model.name)
                } else {
                    page.pending = model.name
                    rustrx.command({ cmd: "join_by_id_or_alias", id: model.id })
                }
            }

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                    text: {
                        var t = model.name
                        //% "joined"
                        if (model.joined) t += " · " + qsTrId("rustrx-la-joined")
                        if (page.pending === model.name)
                            t += " · " + (model.kind === "user"
                                          //% "opening…"
                                          ? qsTrId("rustrx-la-opening")
                                          //% "joining…"
                                          : qsTrId("rustrx-la-joining"))
                        return t
                    }
                }

                Label {
                    width: parent.width
                    visible: text.length > 0
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: model.kind === "user"
                          //% "Person"
                          ? (model.info.length ? model.info : qsTrId("rustrx-la-person"))
                          : model.info
                }
            }
        }

        ViewPlaceholder {
            enabled: hits.count === 0 && page.lastTerm.length > 0 && !page.searching
            //% "Nothing found"
            text: qsTrId("rustrx-la-nothing-found")
            //% "Try an address like #room:server or @user:server."
            hintText: qsTrId("rustrx-la-nothing-found-hint")
        }

        VerticalScrollDecorator { }
    }
}
