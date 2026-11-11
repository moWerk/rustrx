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
        if (invited) { openMenu(); return }
        ListView.view.currentIndex = index   // the list keeps this row in view on return
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

        // unread badge, top right of the picture
        Rectangle {
            id: badge
            anchors {
                right: parent.right
                top: parent.top
                rightMargin: -Theme.paddingSmall / 2
                topMargin: -Theme.paddingSmall / 2
            }
            visible: !invited && (model.notifications > 0 || model.unread > 0)
            // one digit: a circle; every further digit adds one digit's width to
            // the straight middle only, so the ends stay half circles and the
            // pill grows to the left (it is anchored at the right)
            width: height + Math.max(0, badgeLabel.text.length - 1) * digitWidth.advanceWidth
            TextMetrics { id: digitWidth; font: badgeLabel.font; text: "0" }
            // the circle keeps this size; the digits grew into its padding
            height: Theme.iconSizeSmall
            radius: height / 2
            color: model.highlights > 0 ? Theme.highlightColor : Theme.secondaryHighlightColor

            // a soft shadow under the digits, one label behind the other
            Label {
                anchors.centerIn: badgeLabel
                anchors.horizontalCenterOffset: 1
                anchors.verticalCenterOffset: 2
                text: badgeLabel.text
                font: badgeLabel.font
                color: Theme.rgba("black", 0.5)
            }

            Label {
                id: badgeLabel
                anchors.centerIn: parent
                // digits have no descender; the text box's share for it pushes them low
                anchors.verticalCenterOffset: -Math.round(font.pixelSize * 0.04)
                text: model.notifications > 0 ? model.notifications : model.unread
                font.pixelSize: Theme.fontSizeExtraSmall
                font.bold: true
                color: "white"
            }
        }
    }

    Column {
        anchors {
            left: avatar.right
            right: age.left
            leftMargin: Theme.paddingLarge
            rightMargin: Theme.paddingMedium
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

        // the last message, a lock in front of it in encrypted rooms; the
        // topic or the state where there is no message yet
        Row {
            width: parent.width
            spacing: Theme.paddingSmall

            Image {
                id: lock
                anchors.verticalCenter: parent.verticalCenter
                visible: model.encrypted && !invited
                source: "image://theme/icon-s-secure?" + (item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor)
                sourceSize.width: Theme.fontSizeSmall * 0.8
                sourceSize.height: Theme.fontSizeSmall * 0.8
            }

            Label {
            width: parent.width - (lock.visible ? lock.width + parent.spacing : 0)
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall * 0.8
            color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
            textFormat: Text.StyledText
            text: {
                //% "Invitation. Long press to accept or decline."
                if (invited) return qsTrId("rustrx-la-invitation")
                if (model.preview && model.preview.length) {
                    var who = model.preview_sender
                              ? "<font color=\"" + (item.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor)
                                + "\">" + model.preview_sender.replace(/&/g, "&amp;").replace(/</g, "&lt;") + ":</font> " : ""
                    return who + model.preview.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                }
                if (model.topic && model.topic.length) return model.topic.replace(/\s+/g, " ")
                return model.encrypted
                        //% "Encrypted"
                        ? qsTrId("rustrx-la-encrypted")
                        : ""
            }
            }
        }
    }

    // favourite: a yellow star under the age
    Image {
        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; top: age.bottom; topMargin: Theme.paddingSmall }
        visible: model.is_fav && !invited
        source: "image://theme/icon-s-favorite?#ffd54a"
        sourceSize.width: Theme.iconSizeExtraSmall
        sourceSize.height: Theme.iconSizeExtraSmall
    }

    // how long ago the last activity was, coarser the older: 1m..59m, 1h..24h, 1d..365d, then years
    Label {
        id: age
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            top: parent.top
            topMargin: rowMargin + Theme.paddingSmall / 2
        }
        width: visible ? implicitWidth : 0
        visible: text.length > 0
        font.pixelSize: Theme.fontSizeExtraSmall
        color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        text: {
            if (!model.ts || invited) return ""
            var s = (app.now - model.ts) / 1000
            //: age of the last message, keep it to one short word
            //% "now"
            if (s < 60) return qsTrId("rustrx-la-age-now")
            //: minutes, keep the suffix to one or two characters
            //% "%1m"
            if (s < 3600) return qsTrId("rustrx-la-age-minutes").arg(Math.floor(s / 60))
            //: hours, keep the suffix to one or two characters
            //% "%1h"
            if (s < 86400) return qsTrId("rustrx-la-age-hours").arg(Math.floor(s / 3600))
            //: days, keep the suffix to one or two characters
            //% "%1d"
            if (s < 365 * 86400) return qsTrId("rustrx-la-age-days").arg(Math.floor(s / 86400))
            //: years, keep the suffix to one or two characters
            //% "%1y"
            return qsTrId("rustrx-la-age-years").arg(Math.floor(s / (365 * 86400)))
        }
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
                onClicked: app.markRead(model.room_id)
            }
            MenuItem {
                visible: !invited
                //% "Leave"
                text: qsTrId("rustrx-me-leave")
                onClicked: {
                    // the callback runs after the menu is gone; keep what it needs
                    var id = model.room_id, core = rustrx
                    //% "Leaving"
                    item.remorseAction(qsTrId("rustrx-re-leaving"), function () {
                        core.command({ cmd: "leave_room", room_id: id })
                    })
                }
            }
        }
    }
}
