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

// One room: name and a second line (topic, or the invitation), the unread
// count at the right. Long press for favourite, mark read, leave.
ListItem {
    id: item
    width: ListView.view.width
    contentHeight: Theme.iconSizeLarge + 2 * rowMargin
    menu: contextMenu

    // Tunables: the air above and below a row (the stock itemSizeMedium gave
    // half of this), and the name a tad over the standard medium size.
    readonly property real rowMargin: (Theme.itemSizeMedium - Theme.iconSizeLarge) * 1.5
    readonly property real nameSize: Theme.fontSizeMedium * 1.1

    readonly property bool invited: model.state === "invited"
    readonly property bool hasUnread: model.notifications > 0 || model.unread > 0

    onClicked: {
        if (invited) return
        pageStack.push(Qt.resolvedUrl("../pages/RoomPage.qml"), { roomId: model.room_id, roomName: model.name })
    }

    // the room's picture, or its initial where it has none
    Rectangle {
        id: avatar
        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.iconSizeLarge
        height: width
        radius: width / 2
        color: picture.visible && !app.roundAvatars
               ? "transparent"
               : Theme.rgba(item.highlighted ? Theme.highlightColor : Theme.primaryColor, Theme.opacityFaint)

        Label {
            anchors.centerIn: parent
            visible: !picture.visible
            text: model.name ? model.name.charAt(0).toUpperCase() : "?"
            font.pixelSize: Theme.fontSizeLarge
            color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
        }

        Image {
            id: picture
            anchors.fill: parent
            visible: status === Image.Ready
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: width
            sourceSize.height: height
            source: model.avatar ? "file://" + model.avatar : ""
            // clip: true would cut a square; the circle is a mask
            layer.enabled: app.roundAvatars
            layer.effect: ShaderEffect {
                fragmentShader: "
                    varying highp vec2 qt_TexCoord0;
                    uniform sampler2D source;
                    uniform lowp float qt_Opacity;
                    void main() {
                        highp float d = length(qt_TexCoord0 - vec2(0.5));
                        lowp float a = 1.0 - smoothstep(0.49, 0.5, d);
                        gl_FragColor = texture2D(source, qt_TexCoord0) * a * qt_Opacity;
                    }"
            }
        }
    }

    Column {
        anchors {
            left: avatar.right
            right: counter.left
            leftMargin: Theme.paddingLarge
            rightMargin: Theme.paddingLarge
            verticalCenter: parent.verticalCenter
        }

        Label {
            width: parent.width
            text: model.name
            truncationMode: TruncationMode.Fade
            font.pixelSize: nameSize
            font.bold: hasUnread
            color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
        }

        Label {
            width: parent.width
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
            text: {
                //% "Invitation. Long press to accept or decline."
                if (invited) return qsTrId("rustrx-la-invitation")
                if (model.topic && model.topic.length) return model.topic.replace(/\s+/g, " ")
                if (model.ts) return rustrx.formatDate(model.ts) + " " + rustrx.formatTime(model.ts)
                return model.encrypted
                        //% "Encrypted"
                        ? qsTrId("rustrx-la-encrypted")
                        : ""
            }
        }
    }

    Label {
        id: counter
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        width: visible ? implicitWidth : 0
        visible: model.notifications > 0
        text: model.notifications
        color: model.highlights > 0 ? Theme.highlightColor : Theme.secondaryHighlightColor
        font.pixelSize: Theme.fontSizeMedium
    }

    Component {
        id: contextMenu
        ContextMenu {
            MenuItem {
                visible: invited
                //% "Accept invitation"
                text: qsTrId("rustrx-me-accept")
                onClicked: rustrx.command({ cmd: "join_room", room_id: model.room_id })
            }
            MenuItem {
                visible: invited
                //% "Decline"
                text: qsTrId("rustrx-me-decline")
                onClicked: rustrx.command({ cmd: "leave_room", room_id: model.room_id })
            }
            MenuItem {
                visible: !invited
                //% "Remove from Favs"
                text: model.is_fav ? qsTrId("rustrx-me-unfavourite")
                //% "Add to Favs"
                                   : qsTrId("rustrx-me-favourite")
                onClicked: rustrx.command({ cmd: "set_favourite", room_id: model.room_id, value: !model.is_fav })
            }
            MenuItem {
                visible: !invited && hasUnread
                //% "Mark as read"
                text: qsTrId("rustrx-me-mark-read")
                onClicked: rustrx.command({ cmd: "mark_read", room_id: model.room_id })
            }
            MenuItem {
                visible: !invited
                //% "Leave"
                text: qsTrId("rustrx-me-leave")
                onClicked: {
                    var id = model.room_id
                    //% "Leaving"
                    item.remorseAction(qsTrId("rustrx-re-leaving"), function () {
                        rustrx.command({ cmd: "leave_room", room_id: id })
                    })
                }
            }
        }
    }
}
