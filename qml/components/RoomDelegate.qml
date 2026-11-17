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
import "../markup.js" as Markup

// One room: name and a second line (topic, or the invitation), the unread
// count at the right. Long press for favourite, mark read, leave.
ListItem {
    id: item
    width: ListView.view.width
    // the same air above and below; more preview lines make the row taller
    contentHeight: Math.max(Theme.iconSizeLarge, textColumn.height) + 2 * rowMargin
    menu: contextMenu

    // Tunables: the air above and below a row (the stock itemSizeMedium gave
    // half of this), and the name a tad over the standard medium size.
    readonly property real rowMargin: (Theme.itemSizeMedium - Theme.iconSizeLarge) * Tunables.roomRowMarginFactor
    readonly property real nameSize: Theme.fontSizeMedium * Tunables.roomNameScale
    // the picture: 10 % smaller than the large icon size
    readonly property int avatarSize: Math.round(Theme.iconSizeLarge * Tunables.avatarScale)

    readonly property bool invited: model.state === "invited"
    readonly property bool hasUnread: model.notifications > 0 || model.unread > 0
    // a favourite with something unread glows (Settings): a band across the
    // row breathes with the app's one breath (app.favGlow), so all glowing
    // rows breathe together; a shown glowing row counts itself in, which
    // keeps the breath running only while one is on screen
    readonly property bool favGlow: app.highlightFavs && model.is_fav === true && hasUnread && !invited
    readonly property bool glowing: favGlow && visible
    property bool counted
    onGlowingChanged: {
        if (glowing === counted) return
        counted = glowing
        app.favGlowRows += glowing ? 1 : -1
    }
    Component.onCompleted: if (glowing && !counted) { counted = true; app.favGlowRows += 1 }
    Component.onDestruction: if (counted) app.favGlowRows -= 1
    // the band: the page's whole width, brightest in the row's middle
    // (3 % at rest, 12 % at the height of a breath), fading to nothing
    // above and below
    Rectangle {
        anchors.fill: parent
        visible: item.favGlow
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.rgba(Theme.primaryColor, Tunables.favGlowBase + Tunables.favGlowAmp * app.favGlow) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    onClicked: {
        if (invited) { openMenu(); return }
        // an upgraded room: on to its successor (joined if need be)
        if (model.replaced_by && model.replaced_by.length) {
            if (rustrx.roomInfo(model.replaced_by).name) app.showRoom(model.replaced_by)
            else app.openMatrixUri("matrix:roomid/" + model.replaced_by.slice(1))
            return
        }
        ListView.view.currentIndex = index   // the list keeps this row in view on return
        if (model.is_space)
            pageStack.push(Qt.resolvedUrl("../pages/SpacePage.qml"), { spaceId: model.room_id, spaceName: model.name })
        else
            pageStack.push(Qt.resolvedUrl("../pages/RoomPage.qml"), { roomId: model.room_id, roomName: model.name })
    }

    // the room's picture, or its initial where it has none
    Rectangle {
        id: avatar
        // 10 % smaller than the large icon size, centred in that slot, so the
        // texts beside it stay where they were
        x: Theme.horizontalPageMargin + (Theme.iconSizeLarge - width) / 2
        // at the row's top, where a one-line row has it: rows with more
        // preview lines grow downwards and the pictures stay in line
        y: rowMargin + (Theme.iconSizeLarge - height) / 2
        width: avatarSize
        height: width
        radius: width * app.avatarRadius
        // behind a picture nothing: one with transparency (dodoradio's, the
        // AsteroidOS room) shows the list through it, not a circle
        color: picture.visible
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
            sourceSize.width: avatarSize
            sourceSize.height: avatarSize
            // the version makes a changed picture load past Qt's image cache
            source: model.avatar_path ? "file://" + model.avatar_path + "?v=" + (model.avatar_v || 0) : ""
            // clip: true would cut a square; the rounding is a mask
            layer.enabled: app.avatarRadius > 0
            layer.effect: RoundMask { radius: app.avatarRadius }
        }

        // the badge's shadow, offset a little further than the digits' shadow,
        // soft at its edge: three pills, each a little larger, stacked to
        // about 42 % black in the middle (no blur in plain Qt Quick 2.6)
        Repeater {
            model: 3
            Rectangle {
                readonly property real spread: index * 0.96
                visible: badge.visible
                x: badge.x + 1.5 - spread
                y: badge.y + 3 - spread
                width: badge.width + 2 * spread
                height: badge.height + 2 * spread
                radius: height / 2
                color: Theme.rgba("black", 0.17)
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
            // measured at the size the digits had, so the pill keeps its
            // geometry while the digits shrink inside it
            TextMetrics {
                id: digitWidth
                font.family: app.semiboldFontFamily
                font.pixelSize: Theme.fontSizeExtraSmall
                text: "0"
            }
            // the circle keeps this size; the digits grew into its padding
            height: Theme.iconSizeSmall
            radius: height / 2
            // a shade (10 %) darker than the ambience's highlight colours, so the
            // white digits stand out; a mention gets the brighter one
            color: Qt.darker(model.highlights > 0 ? Theme.highlightColor : Theme.secondaryHighlightColor, 1.1)

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
                // digits have no descender; the text box's share for it pushed
                // them low in Sail Sans. Source Sans 3 sits higher, and the
                // shadow below the digits adds visual weight: 1 px down
                // centres digits and shadow together (the author's eye)
                anchors.verticalCenterOffset: 1 - Math.round(font.pixelSize * 0.04)
                text: model.notifications > 0 ? model.notifications : model.unread
                // a tenth smaller than the size the pill is measured for: more
                // room around the digits inside it
                font.pixelSize: Math.round(Theme.fontSizeExtraSmall * Tunables.badgeTextScale)
                // the file is the Semibold cut: asking for bold on top of it
                // would make Qt embolden it again
                font.family: app.semiboldFontFamily
                color: "white"
            }
        }
    }

    Column {
        id: textColumn
        anchors {
            left: avatar.right
            right: age.left
            leftMargin: Theme.paddingLarge + (Theme.iconSizeLarge - avatar.width) / 2
            rightMargin: Theme.paddingMedium
        }
        // centred beside the picture in a one-line row, from the top when taller
        y: rowMargin + Math.max(0, (Theme.iconSizeLarge - height) / 2)

        Label {
            id: nameLabel
            width: parent.width
            // emoji in the name as Twemoji pictures, rendered by the model
            textFormat: Text.StyledText
            text: model.name_html
            truncationMode: TruncationMode.Fade
            font.pixelSize: nameSize
            font.bold: hasUnread
            color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
        }

        // the last message (1 to 3 lines, the last faded). The room's state
        // leads its first line: a stop sign when muted, a lock when
        // encrypted, a check when one's own last message was read. Only the
        // first line makes room for them (lineLaidOut), so the lines below
        // start at the left edge. The topic or the state where there is no
        // message yet.
        Item {
            width: parent.width
            height: preview.height
            readonly property color iconColor: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
            readonly property real iconSize: Theme.fontSizeSmall * Tunables.metaTextScale

            Row {
                id: statusIcons
                spacing: Theme.paddingSmall
                // centred on the preview's first line
                y: (firstLine.height - height) / 2
                // a call is running in the room
                Image {
                    visible: model.in_call === true && !invited
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-s-incoming-call?" + Theme.highlightColor
                    sourceSize.width: parent.parent.iconSize
                    sourceSize.height: parent.parent.iconSize
                }
                // muted: Silica's stop sign; its canvas is 42 px where the
                // lock's is 48, so it is drawn at that ratio
                Image {
                    visible: model.muted === true && !invited
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-s-blocked?" + parent.parent.iconColor
                    sourceSize.width: Math.round(parent.parent.iconSize * 42 / 48)
                    sourceSize.height: Math.round(parent.parent.iconSize * 42 / 48)
                }
                Image {
                    visible: model.encrypted && !invited
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-s-secure?" + parent.parent.iconColor
                    sourceSize.width: parent.parent.iconSize
                    sourceSize.height: parent.parent.iconSize
                }
                // one's own last message was read
                Label {
                    visible: model.own_read === true && !invited
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\u2713"
                    font.pixelSize: Theme.fontSizeSmall * Tunables.readCheckScale
                    color: item.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }

            // the height of one line of the preview, to centre the icons on it
            FontMetrics { id: firstLine; font: preview.font }

            Label {
                id: preview
                // what the first line leaves free for the icons
                readonly property real indent: statusIcons.width > 0 ? statusIcons.width + Theme.paddingSmall : 0
                width: parent.width
                wrapMode: Text.Wrap
                maximumLineCount: Math.max(1, Math.min(Tunables.previewLinesMax, app.previewLines))
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall * Tunables.metaTextScale
                color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                textFormat: Text.StyledText
                onLineLaidOut: if (line.number === 0) { line.x += indent; line.width -= indent }
                text: {
                    // a new indent needs a new layout (Qt 5.6 has no forceLayout)
                    var unused = indent
                    //% "Invitation. Long press to accept or decline."
                    if (invited) return qsTrId("rustrx-la-invitation")
                    // a space: its topic, else how many rooms it holds
                    //: %n the rooms in a space
                    //% "%n rooms"
                    if (model.is_space) return model.topic && model.topic.length ? Markup.escaped(model.topic).replace(/\s+/g, " ")
                                                                                   : qsTrId("rustrx-la-space-rooms", model.children || 0)
                    //% "Replaced by a new room. Tap to go there."
                    if (model.replaced_by && model.replaced_by.length) return qsTrId("rustrx-la-replaced")
                    if (model.preview && model.preview.length) {
                        var who = model.preview_sender
                                  ? "<font color=\"" + (item.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor)
                                    + "\">" + Markup.escaped(model.preview_sender) + ":</font> " : ""
                        // the message with its emoji as pictures, rendered by the
                        // model: a press (the highlight) only redoes the sender's colour
                        return who + model.preview_html
                    }
                    if (model.topic && model.topic.length) return Markup.escaped(model.topic).replace(/\s+/g, " ")
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
        id: favStar
        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; top: age.bottom; topMargin: Theme.paddingSmall }
        visible: model.is_fav && !invited
        source: "image://theme/icon-s-favorite?" + Tunables.favStarColor
        sourceSize.width: Theme.iconSizeExtraSmall
        sourceSize.height: Theme.iconSizeExtraSmall
    }
    // its sparkle at the top of each breath of the glow: a lighter star over
    // it, shown only in the breath's last quarter
    Image {
        anchors.fill: favStar
        visible: item.favGlow && favStar.visible
        opacity: Math.max(0, (app.favGlow - Tunables.favSparkleFrom) / (1 - Tunables.favSparkleFrom))
        source: "image://theme/icon-s-favorite?" + Tunables.favSparkleColor
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

    // what the model renders the name and the preview with, from the labels'
    // own sizes and the preview's lines (one line: the breaks become spaces)
    Binding {
        target: item.ListView.view ? item.ListView.view.model : null
        property: "nameEmojiSize"
        value: Math.round(nameLabel.font.pixelSize * Tunables.emojiScaleList)
    }
    Binding {
        target: item.ListView.view ? item.ListView.view.model : null
        property: "previewEmojiSize"
        value: Math.round(preview.font.pixelSize * Tunables.emojiScaleList)
    }
    Binding {
        target: item.ListView.view ? item.ListView.view.model : null
        property: "previewMultiLine"
        value: preview.maximumLineCount > 1
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
                onClicked: {
                    var id = model.room_id, core = rustrx
                    //% "Declining"
                    item.remorseAction(qsTrId("rustrx-re-declining"), function () {
                        core.command({ cmd: "leave_room", room_id: id })
                    })
                }
            }
            MenuItem {
                visible: !invited && !model.is_space
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
