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
// joining and asking are offered all the same.
Page {
    id: page

    // the address (#alias:server or !id), and the servers from a link
    property string address
    property var via: []

    property var preview: ({})
    property bool loading
    property bool waiting
    property bool asked

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
        onErrorOccurred: if (cmd === "join_by_id_or_alias" || cmd === "knock" || cmd === "room_preview") { page.waiting = false; page.loading = false }
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
                //% "Address, like #room:server"
                label: qsTrId("rustrx-la-join-address")
                placeholderText: label
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                EnterKey.iconSource: "image://theme/icon-m-search"
                EnterKey.enabled: text.trim().length >= Tunables.minJoinChars
                EnterKey.onClicked: { page.via = []; page.look() }
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: page.loading
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
