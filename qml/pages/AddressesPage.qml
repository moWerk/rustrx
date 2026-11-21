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

// A room's addresses, for those who may change them: the main one (shown
// and shared as the room's address), the others, and a new one on one's
// own server. A new address is first made on its server, then listed in
// the room; a listed one can become the main one or be taken out, and the
// address itself deleted from its server.
Page {
    id: page

    property string roomId
    property var settings: ({})

    readonly property string main: settings.canonical_alias || ""
    readonly property var others: settings.alt_aliases || []
    readonly property var all: (main.length ? [main] : []).concat(others)
    // the alias being added, until its server has it
    property string adding

    function setAliases(canonical, alt) {
        var c = { cmd: "set_aliases", room_id: roomId, alt: alt }
        if (canonical.length) c.canonical = canonical
        rustrx.command(c)
    }
    function without(list, a) { return list.filter(function (x) { return x !== a }) }

    Connections {
        target: rustrx
        onRoomSettings: if (settings.room_id === page.roomId) page.settings = settings
        onAliasAdded: {
            if (roomId !== page.roomId || alias !== page.adding) return
            page.adding = ""
            aliasField.text = ""
            // the first address becomes the main one
            if (!page.main.length) page.setAliases(alias, page.others)
            else page.setAliases(page.main, page.others.concat([alias]))
        }
        onErrorOccurred: if (cmd === "add_alias") page.adding = ""
    }

    RemorsePopup { id: remorse }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                //% "Addresses"
                title: qsTrId("rustrx-he-addresses")
                description: rustrx.roomInfo(page.roomId).name || ""
            }

            Repeater {
                model: page.all
                ListItem {
                    id: row
                    readonly property bool isMain: modelData === page.main
                    contentHeight: Theme.itemSizeSmall
                    menu: ContextMenu {
                        MenuItem {
                            visible: !row.isMain
                            //% "Make it the main address"
                            text: qsTrId("rustrx-me-alias-main")
                            onClicked: page.setAliases(modelData, page.without(page.others, modelData).concat(page.main.length ? [page.main] : []))
                        }
                        MenuItem {
                            //% "Take it out of the room"
                            text: qsTrId("rustrx-me-alias-unlist")
                            onClicked: {
                                var a = modelData
                                if (row.isMain) page.setAliases("", page.others)
                                else page.setAliases(page.main, page.without(page.others, a))
                            }
                        }
                        MenuItem {
                            //% "Delete it from its server"
                            text: qsTrId("rustrx-me-alias-delete")
                            onClicked: {
                                var a = modelData, main = page.main, others = page.others, id = page.roomId, core = rustrx
                                //% "Deleting the address"
                                row.remorseAction(qsTrId("rustrx-re-alias-delete"), function () {
                                    var c = { cmd: "set_aliases", room_id: id, alt: others.filter(function (x) { return x !== a }) }
                                    if (main.length && main !== a) c.canonical = main
                                    core.command(c)
                                    core.command({ cmd: "remove_alias", room_id: id, alias: a })
                                })
                            }
                        }
                    }
                    Label {
                        anchors { left: parent.left; leftMargin: Theme.horizontalPageMargin; right: mark.left; rightMargin: Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                        truncationMode: TruncationMode.Fade
                        text: modelData
                        color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        id: mark
                        visible: row.isMain
                        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        //% "main"
                        text: qsTrId("rustrx-la-alias-main")
                    }
                }
            }
            Note {
                kind: "status"
                visible: page.all.length === 0
                //% "No address yet. Without one, the room is found only by invitation or by its ID."
                text: qsTrId("rustrx-la-no-alias")
            }

            //% "New address"
            SectionHeader { text: qsTrId("rustrx-se-new-alias") }
            TextField {
                id: aliasField
                width: parent.width
                //: %1 the own server
                //% "Name, like the-room (on %1)"
                label: qsTrId("rustrx-la-new-alias").arg(page.settings.own_server || "")
                placeholderText: label
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                readonly property string alias: {
                    var t = text.trim().replace(/^#/, "")
                    if (!t.length) return ""
                    return "#" + (t.indexOf(":") > 0 ? t : t + ":" + (page.settings.own_server || ""))
                }
                readonly property bool ready: alias.length > 2 && page.adding.length === 0
                function add() {
                    page.adding = alias
                    rustrx.command({ cmd: "add_alias", room_id: page.roomId, alias: alias })
                }
                EnterKey.enabled: ready
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: add()
                // what will be added, said as not done yet: the bare address
                // under the field read as added, and the author left the page
                // with nothing sent (the only way was the Enter key)
                //: %1 the full address, like #room:server
                //% "To be added: %1"
                description: alias.length ? qsTrId("rustrx-la-alias-to-add").arg(alias) : ""
            }
            PageButton {
                visible: aliasField.alias.length > 2
                //% "Add"
                text: qsTrId("rustrx-bt-alias-add")
                button.enabled: aliasField.ready
                onClicked: aliasField.add()
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Small
                running: page.adding.length > 0
                visible: running
            }
            Note {
                kind: "footnote"
                //% "A long press on an address makes it the main one, takes it out, or deletes it from its server."
                text: qsTrId("rustrx-la-alias-hint")
            }
        }
        VerticalScrollDecorator { }
    }
}
