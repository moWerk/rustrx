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
import Sailfish.Silica 1.0
import "../components"
import "../names.js" as Names

// Find rooms in the server's public directory and people in the user
// directory; a typed address (#room:server, @user:server) is offered as is.
// Tapping a joined room opens it, another room its join page, and a person
// asks first, then opens the direct chat. While the field is empty, what
// was opened or joined from here before is offered again.
Page {
    id: page
    objectName: "search"

    property string lastTerm
    // what is typed: the field lives in the list's header, out of reach of
    // ids from here (the timer's field.text was a ReferenceError, so typing
    // alone never searched; only the enter key did)
    property string typed
    property bool searching
    // why the last search failed, in words; "" when it did not
    property string searchError
    property string pending   // name of the chat being started, for the busy label
    property string pendingId
    // a room handed to the join page: kept once it is joined
    property string joinTerm
    property string joinName

    // what was opened or joined from here before, newest first, for each
    // account its own (the author's wish, 0.8): a JSON list of
    // { term, name, account } in one dconf key
    readonly property int historyMax: Tunables.searchHistoryMax
    ConfigurationValue {
        id: historyValue
        key: Names.dconfPath + "/searchHistory"
        defaultValue: "[]"
    }
    function allHistory() {
        try {
            var all = JSON.parse(historyValue.value || "[]")
            return Array.isArray(all) ? all : []
        } catch (e) {
            return []
        }
    }
    readonly property var history: {
        var all = allHistory(), mine = []
        for (var i = 0; i < all.length; i++)
            if (all[i].account === rustrx.userId && all[i].term) mine.push(all[i])
        return mine
    }
    function remember(term, name) {
        if (!term || !term.length) return
        var all = allHistory(), kept = [], count = 1
        for (var i = 0; i < all.length; i++) {
            var mine = all[i].account === rustrx.userId
            if (mine && all[i].term === term) continue
            if (mine && ++count > historyMax) continue
            kept.push(all[i])
        }
        kept.unshift({ term: term, name: name || term, account: rustrx.userId })
        historyValue.value = JSON.stringify(kept)
    }
    function forget(term) {
        var all = allHistory(), kept = []
        for (var i = 0; i < all.length; i++)
            if (!(all[i].account === rustrx.userId && all[i].term === term)) kept.push(all[i])
        historyValue.value = JSON.stringify(kept)
    }
    function forgetAll() {
        var all = allHistory(), kept = [], me = rustrx.userId
        for (var i = 0; i < all.length; i++)
            if (all[i].account !== me) kept.push(all[i])
        historyValue.value = JSON.stringify(kept)
    }

    function search(term) {
        term = term.trim()
        hits.clear()
        page.searchError = ""
        if (term.length < Tunables.minSearchChars) return
        page.lastTerm = term
        if (/^[@#!][^\s]+:[^\s]+$/.test(term)) {
            hits.append({ kind: term.charAt(0) === "@" ? "user" : "room",
                             id: term, name: term, info: "", joined: false })
        }
        page.searching = true
        rustrx.command({ cmd: "search", term: term })
    }

    function open(roomId, name) {
        // an existing chat comes back at once, while the question still
        // slides away: the stack takes no page during a transition
        if (pageStack.busy) {
            openLater.roomId = roomId
            openLater.name = name
            openLater.restart()
            return
        }
        page.pending = ""
        page.pendingId = ""
        pageStack.replace(Qt.resolvedUrl("RoomPage.qml"), { roomId: roomId, roomName: name })
    }
    Timer {
        id: openLater
        property string roomId
        property string name
        interval: Tunables.retryIntervalMs
        onTriggered: page.open(roomId, name)
    }

    // a person: ask first, as a link to them does (starting a chat invites
    // them and shows them one's address; a tap did it at once, 0.8 audit)
    function askChat(userId, name) {
        var dialog = pageStack.push(Qt.resolvedUrl("StartChatDialog.qml"), { userId: userId, name: name })
        dialog.accepted.connect(function () {
            page.pending = name
            page.pendingId = userId
            rustrx.command({ cmd: "create_dm", user_id: userId })
        })
    }

    // back here without joining: the join page's room is not kept
    onStatusChanged: if (status === PageStatus.Active) { joinTerm = ""; joinName = "" }

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
                              info: [r.alias, qsTrId("rustrx-la-n-members", r.member_count || 0), r.topic]
                                    .filter(function (s) { return s && s.length }).join(" · "),
                              joined: r.joined })
            }
        }
        onJoined: {
            // joined on the join page this page opened
            if (page.joinTerm.length && page.status !== PageStatus.Active) {
                page.remember(page.joinTerm, page.joinName)
                page.joinTerm = ""
            }
        }
        onDmReady: {
            if (!page.pending.length) return
            page.remember(page.pendingId, page.pending)
            page.open(roomId, page.pending)
        }
        onErrorOccurred: {
            if (cmd === "create_dm") { page.pending = ""; page.pendingId = "" }
            if (cmd === "search") {
                page.searching = false
                // not "Nothing found": the search did not get through
                page.searchError = app.friendlyError(cmd, message)
            }
        }
    }

    ListModel { id: hits }

    Timer {
        id: debounce
        interval: Tunables.searchDebounceMs
        onTriggered: page.search(page.typed)
    }

    RemorsePopup { id: remorse }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: hits
        // no current row: the view gave the focus to its current item when
        // the first results came, and the search field lost it mid-word
        currentIndex: -1
        onCurrentIndexChanged: if (currentIndex !== -1) currentIndex = -1

        header: Column {
            width: parent.width

            PageHeader {
                //% "Add room or contact"
                title: qsTrId("rustrx-he-add")
            }

            // a room of one's own, named after what was typed if anything
            LinkRow {
                //% "Create a new room"
                text: qsTrId("rustrx-la-create-room")
                onClicked: pageStack.push(Qt.resolvedUrl("CreateRoomPage.qml"), { name: field.text.charAt(0) === "#" ? field.text.slice(1).split(":")[0] : field.text })
            }

            SearchField {
                id: field
                width: parent.width
                //% "Room, person or address"
                placeholderText: qsTrId("rustrx-la-search-placeholder")
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: { debounce.stop(); page.search(text) }
                onTextChanged: { page.typed = text; debounce.restart() }
                Component.onCompleted: forceActiveFocus()
            }

            Note {
                kind: "status"
                //% "Searching…"
                text: page.searching ? qsTrId("rustrx-la-searching") : ""
            }

            // what was opened or joined before: a tap searches it again, the
            // cross forgets it
            Column {
                width: parent.width
                visible: page.typed.length === 0 && page.history.length > 0

                SectionHeader {
                    //: the rooms and people opened from this page before
                    //% "Recent"
                    text: qsTrId("rustrx-se-recent")
                }
                Repeater {
                    model: page.typed.length === 0 ? page.history : []
                    BackgroundItem {
                        id: recentRow
                        width: parent.width
                        height: Theme.itemSizeMedium
                        onClicked: {
                            field.text = modelData.term
                            debounce.stop()
                            page.search(modelData.term)
                        }
                        RowTexts {
                            anchors {
                                left: parent.left
                                leftMargin: Theme.horizontalPageMargin
                                right: forgetButton.left
                                verticalCenter: parent.verticalCenter
                            }
                            highlighted: recentRow.highlighted
                            text: modelData.name
                            description: modelData.name !== modelData.term ? modelData.term : ""
                        }
                        IconButton {
                            id: forgetButton
                            anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                            icon.source: "image://theme/icon-m-clear"
                            onClicked: page.forget(modelData.term)
                        }
                    }
                }
                ActionLine {
                    //% "Clear all"
                    text: qsTrId("rustrx-me-clear-recent")
                    //% "Clearing"
                    onClicked: remorse.execute(qsTrId("rustrx-re-clearing-recent"), function () { page.forgetAll() })
                }
            }
        }

        delegate: ListItem {
            id: row
            width: list.width
            contentHeight: Theme.itemSizeMedium
            enabled: !page.pending.length

            onClicked: {
                if (model.kind === "user") {
                    page.askChat(model.id, model.name)
                } else if (model.joined) {
                    page.remember(model.id, model.name)
                    page.open(model.id, model.name)
                } else {
                    // the join page: what the room is, joining or asking to
                    // join as the room wants, and why it failed in words (a
                    // join from here failed silently, and asked nobody)
                    page.joinTerm = model.id
                    page.joinName = model.name
                    pageStack.push(Qt.resolvedUrl("JoinPage.qml"), { address: model.id })
                }
            }

            RowTexts {
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                highlighted: row.highlighted
                text: {
                    var t = model.name
                    //% "joined"
                    if (model.joined) t += " · " + qsTrId("rustrx-la-joined")
                    //% "opening…"
                    if (page.pending.length && page.pendingId === model.id) t += " · " + qsTrId("rustrx-la-opening")
                    return t
                }
                description: model.kind === "user"
                             //% "Person"
                             ? (model.info.length ? model.info : qsTrId("rustrx-la-person"))
                             : model.info
            }
        }

        ViewPlaceholder {
            enabled: hits.count === 0 && page.lastTerm.length > 0 && !page.searching
            text: page.searchError.length
                  //% "The search did not work"
                  ? qsTrId("rustrx-la-search-failed")
                  //% "Nothing found"
                  : qsTrId("rustrx-la-nothing-found")
            hintText: page.searchError.length ? page.searchError
                      //% "Try an address like #room:server or @name:server."
                      : qsTrId("rustrx-la-nothing-found-hint")
        }

        VerticalScrollDecorator { }
    }
}
