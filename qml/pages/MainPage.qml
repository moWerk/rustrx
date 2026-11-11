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

// Two panels, Rooms and People, in the tab view Jolla uses for Settings.
// Each panel is a list with its own pulley menu holding the one item,
// Settings. Invitations and favourites sit on top as sections.
Page {
    id: page
    objectName: "main"

    function tabTitle(kind) {
        //% "Rooms"
        if (kind === "rooms") return qsTrId("rustrx-he-rooms")
        //% "People"
        if (kind === "people") return qsTrId("rustrx-he-people")
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
    onStatusChanged: if (debugOpenRoom.value.length || debugMarkRead.value.length || debugNavigate.value.length) debugTimer.start()
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

    TabView {
        id: tabs
        anchors.fill: parent
        model: tabModel
        currentIndex: 0

        header: TabBar {
            model: tabModel
            delegate: TabButton {
                title: tabTitle(model.kind)
            }
        }

        delegate: TabItem {
            id: tabItem
            flickable: list

            readonly property bool isRooms: model.kind === "rooms"
            readonly property bool isFavs: model.kind === "favs"

            SilicaListView {
                id: list
                anchors.fill: parent
                model: isRooms ? rustrx.rooms : isFavs ? rustrx.favourites : rustrx.people
                currentIndex: -1

                // back from a chat: keep the row that was open in view
                Connections {
                    target: page
                    onStatusChanged: {
                        if (page.status === PageStatus.Active && list.currentIndex >= 0)
                            list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                    }
                }

                section.property: isFavs ? "" : "section"
                section.delegate: SectionHeader {
                    text: section
                    visible: section.length > 0
                    height: visible ? implicitHeight : 0
                }

                PullDownMenu {
                    MenuItem {
                        //% "Add Room / Contact"
                        text: qsTrId("rustrx-me-add")
                        onClicked: pageStack.push(Qt.resolvedUrl("SearchPage.qml"))
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

                    // one line while this device cannot read encrypted rooms
                    BackgroundItem {
                        width: parent.width
                        height: visible ? Theme.itemSizeSmall : 0
                        visible: rustrx.verificationState === "unverified" || rustrx.recoveryState === "incomplete"
                        onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))

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
                Timer { id: firstSyncGrace; interval: 10000; running: true }
                BusyIndicator {
                    anchors.centerIn: parent
                    size: BusyIndicatorSize.Large
                    running: list.count === 0 && firstSyncGrace.running && rustrx.syncState !== "error"
                }

                ViewPlaceholder {
                    enabled: list.count === 0 && !firstSyncGrace.running
                    text: rustrx.syncState === "running" || rustrx.syncState === "idle"
                          //% "Nothing here yet"
                          ? qsTrId("rustrx-la-empty")
                          //% "Not connected"
                          : qsTrId("rustrx-la-not-connected")
                    hintText: isRooms
                              //% "Rooms you join appear here, sorted by activity."
                              ? qsTrId("rustrx-la-empty-rooms-hint")
                              : isFavs
                              //% "Long press a room or a person and choose Add to Favs."
                              ? qsTrId("rustrx-la-empty-favs-hint")
                              //% "Direct chats appear here."
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
