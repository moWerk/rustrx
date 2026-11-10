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
import "../components"

// Two panels, Rooms and People, in the tab view Jolla uses for Settings.
// Each panel is a list with its own pulley menu holding the one item,
// Settings. Invitations and favourites sit on top as sections.
Page {
    id: page
    objectName: "main"

    ListModel {
        id: tabModel
        ListElement { kind: "rooms" }
        ListElement { kind: "people" }
        ListElement { kind: "favs" }
    }

    function tabTitle(kind) {
        //% "Rooms"
        if (kind === "rooms") return qsTrId("rustrx-he-rooms")
        //% "People"
        if (kind === "people") return qsTrId("rustrx-he-people")
        //% "Favs"
        return qsTrId("rustrx-he-favs")
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
                count: model.kind === "rooms" ? 0 : 0
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
                section.delegate: SectionHeader { text: section }

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

                ViewPlaceholder {
                    enabled: list.count === 0
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
}
