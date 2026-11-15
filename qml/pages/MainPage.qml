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
import Sailfish.Silica.private 1.0
import Nemo.Configuration 1.0
import "../components"

// Three panels, Rooms, People and Favs, in the tab view Jolla uses for Settings.
// Each panel is a list with its own pulley menu holding the one item,
// Settings. Invitations and favourites sit on top as sections.
Page {
    id: page
    objectName: "main"

    function tabTitle(kind) {
        //% "Chats"
        if (kind === "all") return qsTrId("rustrx-he-chats")
        //% "Rooms"
        if (kind === "rooms") return qsTrId("rustrx-he-rooms")
        //% "People"
        if (kind === "people") return qsTrId("rustrx-he-people")
        //% "Spaces"
        if (kind === "spaces") return qsTrId("rustrx-he-spaces")
        //% "Favs"
        return qsTrId("rustrx-he-favs")
    }

    // debug: open this room when the page first becomes active, and go on to
    // its attached page when debugNavigate is "info" (unattended screenshots)
    ConfigurationValue { id: debugOpenRoom; key: "/apps/harbour-rustrx/debugOpenRoom"; defaultValue: "" }
    ConfigurationValue { id: debugNavigate; key: "/apps/harbour-rustrx/debugNavigate"; defaultValue: "" }
    ConfigurationValue { id: debugMarkRead; key: "/apps/harbour-rustrx/debugMarkRead"; defaultValue: "" }
    property bool debugDone
    // the key may hold a room id or a room name; names are looked up in the list
    Instantiator {
        id: debugRooms
        model: debugOpenRoom.value.length || debugMarkRead.value.length ? rustrx.rooms : null
        delegate: QtObject { property string rid: model.room_id; property string rname: model.name }
    }
    function debugResolve(key) {
        if (key.charAt(0) === "!") return key
        for (var i = 0; i < debugRooms.count; i++)
            if (debugRooms.objectAt(i).rname === key) return debugRooms.objectAt(i).rid
        return ""
    }
    onStatusChanged: {
        // back on the lists: no chat to reopen at the next start
        if (status === PageStatus.Active) app.lastRoom = ""
        // a room (or another page) opened: the menu closes, and a search
        // that was not kept goes with it
        if (status === PageStatus.Deactivating) {
            dropLooseFilter()
            sortOpen = false
        }
        if (debugOpenRoom.value.length || debugMarkRead.value.length || debugNavigate.value.length) debugTimer.start()
    }
    Timer { id: debugTimer; interval: 0; onTriggered: page.debugTry() }
    // after the room has attached its page
    Timer { id: debugForward; interval: 300; onTriggered: { pageStack.navigateForward(); debugNav.target = null } }
    function debugTry() {
        if (status !== PageStatus.Active || debugDone) return
        // "page:KeysPage" pushes that page from here
        if (debugNavigate.value.indexOf("page:") === 0 && !debugOpenRoom.value.length) {
            debugDone = true
            pageStack.push(Qt.resolvedUrl(debugNavigate.value.slice(5) + ".qml"))
            return
        }
        if (debugMarkRead.value.length) {
            var mid = debugResolve(debugMarkRead.value)
            if (mid.length) { debugDone = true; rustrx.command({ cmd: "mark_read", room_id: mid }) }
            return
        }
        if (!debugOpenRoom.value.length) return
        var id = debugResolve(debugOpenRoom.value)
        if (!id.length) return   // rooms not in yet; the next activation tries again
        debugDone = true
        var room = pageStack.push(Qt.resolvedUrl("RoomPage.qml"), { roomId: id, roomName: rustrx.roomInfo(id).name || "" })
        if (debugNavigate.value === "info") debugNav.target = room
    }
    // rooms arriving after the page is active
    Connections {
        target: rustrx.rooms
        onCountChanged: if (debugOpenRoom.value.length || debugMarkRead.value.length) debugTimer.start()
    }
    Connections {
        id: debugNav
        target: null
        onStatusChanged: if (target.status === PageStatus.Active) debugForward.start()
    }

    // the tab with this kind ("rooms", "people", "favs", "all"), or the first
    function tabIndex(kind) {
        for (var i = 0; i < tabModel.count; i++)
            if (tabModel.get(i).kind === kind) return i
        return 0
    }
    // an invitation's list: people for a direct chat, else rooms (or the one list)
    function showTab(i) { tabs.currentIndex = tabIndex(i === 1 ? "people" : "rooms") }

    // the tabs as the two settings say: rooms and people apart or as one
    // list ("Chats"), favourites as a tab of their own or not
    function buildTabs() {
        // read before the list changes: changing it moves the current tab,
        // which would store another one
        var wanted = lastTab.value
        var kinds = app.separateLists ? ["rooms", "people"] : ["all"]
        if (app.separateFavs) kinds.push("favs")
        if (app.separateSpaces) kinds.push("spaces")
        var same = tabModel.count === kinds.length
        for (var i = 0; same && i < kinds.length; i++) same = tabModel.get(i).kind === kinds[i]
        if (!same) {
            tabModel.clear()
            for (var j = 0; j < kinds.length; j++) tabModel.append({ kind: kinds[j] })
        }
        tabs.currentIndex = tabIndex(wanted)
        lastTab.value = wanted
    }
    Connections {
        target: app
        onSeparateListsChanged: page.buildTabs()
        onSeparateFavsChanged: page.buildTabs()
        onSeparateSpacesChanged: page.buildTabs()
    }
    Component.onCompleted: buildTabs()

    // the filter menu under the tabs; it closes whenever this page is left
    property bool sortOpen

    // the room search of the filter menu (see the search row there)
    property string filterText
    property bool filterLocked
    onFilterTextChanged: rustrx.roomFilter = filterText.trim().length >= 2 ? filterText : ""
    function lockFilter() {
        if (filterText.trim().length < 2) return
        filterLocked = true
        Qt.inputMethod.hide()
        focus = true
    }
    function clearFilter() {
        filterLocked = false
        filterText = ""
    }
    // a search that was not kept goes with the menu or when a room opens
    function dropLooseFilter() { if (!filterLocked) filterText = "" }

    // the app opens on the tab it was left on
    // the kind of tab ("rooms", "people", "favs", "all"), not its place: the
    // places change with the settings
    ConfigurationValue { id: lastTab; key: "/apps/harbour-rustrx/lastTabKind"; defaultValue: "rooms" }

    TabView {
        id: tabs
        anchors.fill: parent
        model: tabModel
        currentIndex: 0
        onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex < tabModel.count) lastTab.value = tabModel.get(currentIndex).kind

        // the tabs, and at their right a filter icon that opens the sort
        // order below them
        header: Column {
            id: sortHeader
            width: parent ? parent.width : page.width

            Item {
                width: parent.width
                height: tabBar.height
                // the tabs centred on the whole page, as Silica centres them;
                // the filter icon lies over its right edge (and gets its taps,
                // being declared after the bar)
                TabBar {
                    id: tabBar
                    width: parent.width
                    model: tabModel
                    delegate: TabButton {
                        title: tabTitle(model.kind)
                        // four tabs and the filter icon in one row: a size
                        // smaller, so narrow phones fit them
                        titleFontSize: tabModel.count >= 4 ? Math.round(Theme.fontSizeMedium * 0.9) : Theme.fontSizeLarge
                    }
                }
                MouseArea {
                    id: filterButton
                    // centred on the tabs' row: the bar adds a top margin for a
                    // camera cutout (Jolla Phone 2026) above it
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter; verticalCenterOffset: tabBar.topMargin / 2 }
                    width: Theme.itemSizeSmall
                    height: Theme.itemSizeSmall
                    onClicked: {
                        if (page.sortOpen) page.dropLooseFilter()
                        page.sortOpen = !page.sortOpen
                    }
                    // three bars, each narrower than the one above
                    Column {
                        anchors.centerIn: parent
                        // half again as tall and wide as at first, the bars as thin
                        spacing: Math.round(Theme.paddingSmall * 1.1)
                        Repeater {
                            model: 3
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Theme.iconSizeExtraSmall * 1.5 * (1 - index / 3)
                                height: Math.max(2, Math.round(Theme.paddingSmall / 2))
                                radius: height / 2
                                color: filterButton.pressed || page.sortOpen
                                       ? Theme.highlightColor
                                       : rustrx.sortOrder !== "activity" || page.filterLocked || rustrx.hideMuted ? Theme.secondaryHighlightColor
                                                                          : Theme.secondaryColor
                            }
                        }
                    }
                }
            }

            // one of three orders (a radio: the current one filled), then two
            // switches that combine with any order; the menu stays open, a
            // tap on the filter icon closes it
            Repeater {
                model: page.sortOpen ? [
                    //: room list order
                    //% "By activity"
                    { key: "activity", text: qsTrId("rustrx-me-sort-activity") },
                    //% "Unread first"
                    { key: "unread", text: qsTrId("rustrx-me-sort-unread") },
                    //% "Alphabetical"
                    { key: "name", text: qsTrId("rustrx-me-sort-name") }
                ] : []
                BackgroundItem {
                    id: orderRow
                    readonly property bool chosen: rustrx.sortOrder === modelData.key
                    width: page.width
                    height: Theme.itemSizeExtraSmall
                    onClicked: rustrx.sortOrder = modelData.key
                    Label {
                        anchors { right: radio.left; rightMargin: Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                        text: modelData.text
                        font.pixelSize: Theme.fontSizeSmall
                        color: orderRow.chosen || orderRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    // Silica's radio look: a dot in a ring, lit when chosen
                    Image {
                        id: radio
                        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                        source: "image://theme/icon-m-dot?" + (orderRow.chosen ? Theme.highlightColor : Theme.secondaryColor)
                        sourceSize.width: Theme.iconSizeSmall
                        sourceSize.height: Theme.iconSizeSmall
                        opacity: orderRow.chosen ? 1.0 : Theme.opacityLow
                    }
                }
            }
            Separator {
                visible: page.sortOpen
                x: Theme.horizontalPageMargin
                width: page.width - 2 * x
                color: Theme.secondaryColor
                horizontalAlignment: Qt.AlignRight
            }
            Repeater {
                model: page.sortOpen ? [
                    //: room list option, with any order
                    //% "Favourites on top"
                    { key: "favsOnTop", text: qsTrId("rustrx-me-sort-favs-on-top") },
                    //% "Low priority at the bottom"
                    { key: "lowLast", text: qsTrId("rustrx-me-sort-low-last") },
                    //% "Hide muted"
                    { key: "hideMuted", text: qsTrId("rustrx-me-sort-hide-muted") }
                ] : []
                BackgroundItem {
                    id: optionRow
                    readonly property bool on: rustrx[modelData.key] === true
                    width: page.width
                    height: Theme.itemSizeExtraSmall
                    onClicked: rustrx[modelData.key] = !on
                    Label {
                        anchors { right: box.left; rightMargin: Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                        text: modelData.text
                        font.pixelSize: Theme.fontSizeSmall
                        color: optionRow.on || optionRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    // on or off: Silica's check in a circle (as at the search),
                    // lit while on
                    Image {
                        id: box
                        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                        source: "image://theme/icon-m-acknowledge?" + (optionRow.on ? Theme.highlightColor : Theme.secondaryColor)
                        sourceSize.width: Theme.iconSizeSmall
                        sourceSize.height: Theme.iconSizeSmall
                        opacity: optionRow.on ? 1.0 : Theme.opacityLow
                    }
                }
            }
            Separator {
                visible: page.sortOpen
                x: Theme.horizontalPageMargin
                width: page.width - 2 * x
                color: Theme.secondaryColor
                horizontalAlignment: Qt.AlignRight
            }
            // a search: from the second letter the lists narrow while typing;
            // the check mark keeps it (then a cross removes it), else opening a
            // room or closing the menu drops it
            Item {
                visible: page.sortOpen
                width: page.width
                height: visible ? searchField.height : 0
                TextField {
                    id: searchField
                    anchors { left: parent.left; right: lockButton.left }
                    //% "Search"
                    placeholderText: qsTrId("rustrx-ph-room-search")
                    label: ""
                    readOnly: page.filterLocked
                    inputMethodHints: Qt.ImhNoPredictiveText
                    text: page.filterText
                    onTextChanged: page.filterText = text
                    EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                    EnterKey.enabled: text.trim().length >= 2
                    EnterKey.onClicked: page.lockFilter()
                    // a hidden field keeps its focus in QML and took the typing
                    // with the menu closed: with the menu it lets go, keyboard too
                    onVisibleChanged: if (!visible && activeFocus) { focus = false; Qt.inputMethod.hide() }
                }
                IconButton {
                    id: lockButton
                    anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium; verticalCenter: searchField.verticalCenter; verticalCenterOffset: -Theme.paddingSmall }
                    enabled: page.filterLocked || searchField.text.trim().length >= 2
                    icon.source: page.filterLocked ? "image://theme/icon-m-clear" : "image://theme/icon-m-acknowledge"
                    onClicked: page.filterLocked ? page.clearFilter() : page.lockFilter()
                }
            }
        }

        delegate: TabItem {
            id: tabItem
            flickable: list

            readonly property bool isRooms: model.kind === "rooms"
            readonly property bool isFavs: model.kind === "favs"
            readonly property bool isAll: model.kind === "all"
            readonly property bool isSpaces: model.kind === "spaces"

            SilicaListView {
                id: list
                anchors.fill: parent
                model: isAll ? rustrx.all : isRooms ? rustrx.rooms : isFavs ? rustrx.favourites
                     : isSpaces ? rustrx.spaces : rustrx.people
                currentIndex: -1

                // A list at its top stays at its top when rows arrive or move
                // there (the first sync, a new message, the sort order); the
                // view would keep the old first row and push the new one up
                // under the tabs. Once scrolled, the place is left alone.
                property bool pinnedTop: true
                onMovementEnded: pinnedTop = atYBeginning
                // after the view has applied the change: inside the model's
                // signal it still has the old rows (Qt 5.6 has no callLater)
                function keepTop() { if (pinnedTop) keepTopLater.restart() }
                Timer {
                    id: keepTopLater
                    interval: 0
                    onTriggered: if (list.pinnedTop && !list.moving) list.positionViewAtBeginning()
                }
                // at the start the tab bar is laid out after the list: the
                // header's spacer grows by its height and the first rows
                // ended up under the tabs
                Connections {
                    target: tabs
                    onTabBarHeightChanged: list.keepTop()
                }
                Connections {
                    target: list.model
                    onRowsInserted: list.keepTop()
                    onRowsMoved: list.keepTop()
                    onModelReset: list.keepTop()
                }

                // back from a chat: keep the row that was open in view
                Connections {
                    target: page
                    onStatusChanged: {
                        if (page.status === PageStatus.Active && list.currentIndex >= 0)
                            list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                    }
                }

                section.property: isFavs || isSpaces ? "" : "section"
                section.delegate: SectionHeader {
                    text: section
                    visible: section.length > 0
                    height: visible ? implicitHeight : 0
                }

                PullDownMenu {
                    MenuItem {
                        visible: !isSpaces
                        //% "Add Room / Contact"
                        text: qsTrId("rustrx-me-add")
                        onClicked: pageStack.push(Qt.resolvedUrl("SearchPage.qml"))
                    }
                    MenuItem {
                        visible: isSpaces
                        //% "Join a space or room"
                        text: qsTrId("rustrx-me-join-address")
                        onClicked: pageStack.push(Qt.resolvedUrl("JoinPage.qml"))
                    }
                    MenuItem {
                        visible: isSpaces
                        //% "New space"
                        text: qsTrId("rustrx-me-new-space")
                        onClicked: pageStack.push(Qt.resolvedUrl("CreateRoomPage.qml"), { space: true })
                    }
                    MenuItem {
                        //% "Settings"
                        text: qsTrId("rustrx-me-settings")
                        onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                    }
                }

                // the tab bar is drawn over the list; content starts below it
                header: Column {
                    width: list.width

                    Item { width: 1; height: tabs.tabBarHeight + Theme.paddingMedium }

                    // people asking to join a room or space we may invite to
                    BackgroundItem {
                        width: parent.width
                        height: visible ? Theme.itemSizeSmall : 0
                        visible: rustrx.knockRequests.length > 0
                        onClicked: pageStack.push(Qt.resolvedUrl("KnockPage.qml"))

                        Label {
                            x: Theme.horizontalPageMargin
                            width: parent.width - 2 * x
                            anchors.verticalCenter: parent.verticalCenter
                            truncationMode: TruncationMode.Fade
                            color: Theme.highlightColor
                            font.pixelSize: Theme.fontSizeSmall
                            //: %n open requests to join rooms or spaces
                            //% "%n requests to join. Tap to answer."
                            text: qsTrId("rustrx-la-banner-knocks", rustrx.knockRequests.length)
                        }
                    }

                    // one line while this device cannot read encrypted rooms
                    BackgroundItem {
                        width: parent.width
                        height: visible ? Theme.itemSizeSmall : 0
                        visible: rustrx.verificationState === "unverified" || rustrx.recoveryState === "incomplete"
                        // the settings, scrolled to the encryption section, where
                        // verifying from another device and the recovery key sit
                        // together: without another device only the key helps
                        onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"), { showEncryption: true })

                        Label {
                            x: Theme.horizontalPageMargin
                            width: parent.width - 2 * x
                            anchors.verticalCenter: parent.verticalCenter
                            truncationMode: TruncationMode.Fade
                            color: Theme.highlightColor
                            font.pixelSize: Theme.fontSizeSmall
                            text: rustrx.verificationState === "unverified"
                                  //% "This device is not verified. Tap to verify it."
                                  ? qsTrId("rustrx-la-banner-unverified")
                                  //% "Older encrypted messages need the recovery key. Tap to enter it."
                                  : qsTrId("rustrx-la-banner-recovery")
                        }
                    }
                }

                delegate: RoomDelegate { }

                // the first sync takes a moment: a spinner first, the text after
                // a spinner only until the first room list is in; an empty tab
                // then says so at once
                BusyIndicator {
                    anchors.centerIn: parent
                    size: BusyIndicatorSize.Large
                    running: list.count === 0 && !rustrx.roomsLoaded && rustrx.syncState !== "error"
                }

                ViewPlaceholder {
                    enabled: list.count === 0 && (rustrx.roomsLoaded || rustrx.syncState === "error")
                    text: rustrx.roomFilter.length
                          //: %1 the search text
                          //% "No rooms match “%1”"
                          ? qsTrId("rustrx-la-no-match").arg(rustrx.roomFilter)
                          : rustrx.syncState === "running" || rustrx.syncState === "idle"
                          //% "Nothing here yet"
                          ? qsTrId("rustrx-la-empty")
                          //% "Not connected"
                          : qsTrId("rustrx-la-not-connected")
                    hintText: rustrx.roomFilter.length ? ""
                              : !(rustrx.syncState === "running" || rustrx.syncState === "idle")
                              //% "Rustrx connects again by itself once the network is back."
                              ? qsTrId("rustrx-la-offline-hint")
                              : isRooms || isAll
                              //% "Join or create a room with Add Room / Contact in the pulley menu."
                              ? qsTrId("rustrx-la-empty-rooms-hint")
                              : isFavs
                              //% "Long press a room or a person and choose Add to Favs."
                              ? qsTrId("rustrx-la-empty-favs-hint")
                              : isSpaces
                              //% "A space groups rooms. Make one with New space, or join one with its address in the pulley menu."
                              ? qsTrId("rustrx-la-empty-spaces-hint")
                              //% "Start a chat with someone with Add Room / Contact in the pulley menu."
                              : qsTrId("rustrx-la-empty-people-hint")
                }

                VerticalScrollDecorator { }
            }
        }
    }

    // after the TabView: children go in the order they were made, and the
    // TabView must go before its model (else it reaches into a dead context
    // when the window is deleted in background mode)
    ListModel {
        id: tabModel
        ListElement { kind: "rooms" }
        ListElement { kind: "people" }
        ListElement { kind: "favs" }
    }
}
