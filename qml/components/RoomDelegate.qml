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
// count at the right. Long press for Favs, mark read, leave.
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
    // from the page, measured once for all rows: the height of the
    // preview's first line
    property real previewLineHeight
    // how the picture is rounded (Tunables.avatarRounding, the page's debug
    // override): "layer" a layer with RoundMask, "texture" RoundMaskTexture
    // over the hidden Image, "provider" pre-rounded by image://rxavatar,
    // "off" square
    property string avatarRounding: Tunables.avatarRounding

    // the room's account in the merged view (all accounts in one list), else
    // -1: the shown account's own list
    readonly property int account: model.account !== undefined && model.account !== null ? model.account : -1
    readonly property bool otherAccount: account >= 0 && account !== rustrx.accountKey
    readonly property string accountMark: rustrx.mergedView && !app.accountChips && account >= 0
                                          ? (rustrx.accountMarks[account] || "") : ""
    // a command for the room's own account
    function send(c) {
        if (account >= 0) rustrx.commandTo(account, c)
        else rustrx.command(c)
    }
    readonly property bool invited: model.state === "invited"
    readonly property bool hasUnread: model.notifications > 0 || model.unread > 0
    // a favourite with something unread glows (Settings): a band across the
    // row breathes with the app's one breath (app.favGlow), so all glowing
    // rows breathe together; a glowing row on screen counts itself in, and
    // one coming into sight starts a few breaths (then the glow holds still):
    // its list shown (the page active, its tab current: the view's
    // glowShown) and the row within the view's visible part, not only in its
    // cache buffer. Only glowing rows read the view's position (the && stops
    // the others first).
    readonly property bool favGlow: app.highlightFavs && model.is_fav === true && hasUnread && !invited
    readonly property bool glowing: favGlow && ListView.view !== null && ListView.view.glowShown !== false
                                    && y + height > ListView.view.contentY && y < ListView.view.contentY + ListView.view.height
    property bool counted
    onGlowingChanged: {
        if (glowing === counted) return
        counted = glowing
        app.favGlowRows += glowing ? 1 : -1
    }
    Component.onCompleted: {
        if (glowing && !counted) { counted = true; app.favGlowRows += 1 }
        unreadSumBefore = unreadSum
        if (!app.perfDelegateSeen) { app.perfDelegateSeen = true; rustrx.perf("delegate") }
    }
    Component.onDestruction: if (counted) app.favGlowRows -= 1
    // a glowing row on screen with something new breathes again
    readonly property int unreadSum: favGlow ? model.notifications + model.unread : 0
    property int unreadSumBefore
    onUnreadSumChanged: {
        if (counted && unreadSum > unreadSumBefore) app.breatheFavs()
        unreadSumBefore = unreadSum
    }
    // the band: the page's whole width, brightest in the row's middle
    // (3 % at a breath's low, 12 % at its height), fading to nothing
    // above and below. Made for favourites only; the gradient is fixed at
    // the peak and the breath scales its opacity (the same alpha, without
    // rebuilding the gradient every frame).
    Loader {
        anchors.fill: parent
        z: -1
        active: item.favGlow
        sourceComponent: Rectangle {
            opacity: (Tunables.favGlowBase + Tunables.favGlowAmp * app.favGlow) / (Tunables.favGlowBase + Tunables.favGlowAmp)
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Theme.rgba(Theme.primaryColor, Tunables.favGlowBase + Tunables.favGlowAmp) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    // the account's colour (merged view, "Colour-coded accounts"): a bar
    // along the row's edge, cleaner on every row than a leaf at the picture
    // (the author's pick on the phone; the leaf stays on the account menu's
    // pictures)
    Rectangle {
        visible: app.accountChips && item.account >= 0
        x: 0
        width: Math.round(Theme.paddingSmall * 0.75)
        height: parent.height
        color: app.accountColour(item.account)
    }

    onClicked: {
        if (invited) { openMenu(); return }
        // a room of another account (merged view): that account comes to the
        // front, then the room (or its successor) opens there
        if (otherAccount) {
            ListView.view.currentIndex = index
            app.openIn(account, model.replaced_by && model.replaced_by.length ? model.replaced_by : model.room_id)
            return
        }
        // an upgraded room: on to its successor (joined if need be)
        if (model.replaced_by && model.replaced_by.length) {
            if (rustrx.roomInfo(model.replaced_by).name) app.showRoom(model.replaced_by)
            else app.openMatrixUri("matrix:roomid/" + model.replaced_by.slice(1))
            return
        }
        ListView.view.currentIndex = index   // the list keeps this row in view on return
        if (model.is_space)
            pageStack.push(Qt.resolvedUrl("../pages/SpacePage.qml"), { spaceId: model.room_id, spaceName: model.name })
        else {
            rustrx.perf("room_push")
            pageStack.push(Qt.resolvedUrl("../pages/RoomPage.qml"), { roomId: model.room_id, roomName: model.name })
        }
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
        color: picture.ready
               ? "transparent"
               : Theme.rgba(item.highlighted ? Theme.highlightColor : Theme.primaryColor, Theme.opacityFaint)

        Label {
            anchors.centerIn: parent
            visible: !picture.ready
            text: model.name ? model.name.charAt(0).toUpperCase() : "?"
            font.pixelSize: Theme.fontSizeLarge
            color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
        }

        Image {
            id: picture
            readonly property bool ready: status === Image.Ready
            readonly property bool rounded: app.avatarRadius > 0 && item.avatarRounding !== "off"
            anchors.fill: parent
            // under RoundMaskTexture the Image only lends its texture
            visible: ready && !(rounded && item.avatarRounding === "texture")
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: avatarSize
            sourceSize.height: avatarSize
            // the version makes a changed picture load past Qt's image cache
            // a stranger's invitation without its picture, if so set
            source: model.avatar_path && !(invited && model.inviter_known !== true && !app.strangerInvites)
                    ? (rounded && item.avatarRounding === "provider"
                       ? "image://rxavatar/" + model.avatar_path + "?v=" + (model.avatar_v || 0) + "&r=" + Math.round(app.avatarRadius * 1000)
                       : "file://" + model.avatar_path + "?v=" + (model.avatar_v || 0))
                    : ""
            // clip: true would cut a square; the rounding is a mask
            layer.enabled: rounded && item.avatarRounding === "layer"
            layer.effect: RoundMask { radius: app.avatarRadius }
        }
        Loader {
            anchors.fill: picture
            active: picture.ready && picture.rounded && item.avatarRounding === "texture"
            sourceComponent: RoundMaskTexture { sourceImage: picture; radius: app.avatarRadius }
        }

        // the unread badge with its shadow, made only for rows with something
        // unread
        Loader {
            anchors.fill: parent
            active: !invited && hasUnread
            sourceComponent: badgeComponent
        }
        // a mention: an "@" in the same bubble at the top left, in the
        // badge's one tone (the author's idea), while one is unread
        Loader {
            readonly property bool mention: true
            anchors.fill: parent
            active: !invited && model.highlights > 0
            sourceComponent: badgeComponent
        }
    }
    Component {
        id: badgeComponent
        Item {
            // set by the mention Loader (its parent); the unread one has none
            readonly property bool mention: parent && parent.mention === true

            // ---- the bubbles' layout, all in one place (the author nudges these) ----
            // the unread pill: how far it reaches out of the picture's top right
            readonly property real unreadOutRight: Theme.paddingSmall / 2
            readonly property real unreadOutTop: Theme.paddingSmall / 2
            // the mention "@": how far it reaches out of the picture's top left
            readonly property real mentionOutLeft: Theme.paddingSmall / 2
            readonly property real mentionOutTop: Theme.paddingSmall / 2
            // both bubbles' height (the "@" is a circle of it)
            readonly property real bubbleSize: Theme.iconSizeSmall
            // the text inside: size, and nudges right (x) and down (y)
            readonly property real unreadFontSize: Math.round(Theme.fontSizeExtraSmall * Tunables.badgeTextScale)
            readonly property real unreadTextX: 0
            readonly property real unreadTextY: 1 - Math.round(unreadFontSize * 0.04)
            readonly property real mentionFontSize: unreadFontSize
            readonly property real mentionTextX: 0
            readonly property real mentionTextY: unreadTextY
            // --------------------------------------------------------------------------
            // the badge's shadow, offset a little further than the digits' shadow,
            // soft at its edge: three pills, each a little larger, stacked to
            // about 42 % black in the middle (no blur in plain Qt Quick 2.6)
            Repeater {
                model: 3
                Rectangle {
                    readonly property real spread: index * 0.96
                    x: badge.x + 1.5 - spread
                    y: badge.y + 3 - spread
                    width: badge.width + 2 * spread
                    height: badge.height + 2 * spread
                    radius: height / 2
                    color: Theme.rgba("black", 0.17)
                }
            }

            // unread badge, top right of the picture; the mention's top left
            Rectangle {
                id: badge
                x: mention ? -mentionOutLeft : parent.width - width + unreadOutRight
                y: mention ? -mentionOutTop : -unreadOutTop
                // one digit: a circle; every further digit adds one digit's width to
                // the straight middle only, so the ends stay half circles and the
                // pill grows to the left (it is anchored at the right). The digit
                // is measured (by the page) at the size the digits had, so the
                // pill keeps its geometry while the digits shrink inside it
                // (measured here: inside the Loader's component "item" is the
                // Loader's own item, not the row, and the pill stayed a circle)
                width: height + Math.max(0, badgeLabel.text.length - 1) * digitWidth.advanceWidth
                TextMetrics {
                    id: digitWidth
                    font.family: app.semiboldFontFamily
                    font.pixelSize: Theme.fontSizeExtraSmall
                    text: "0"
                }
                // the circle keeps this size; the digits grew into its padding
                height: bubbleSize
                radius: height / 2
                // a shade (10 %) darker than the ambience's secondary highlight
                // colour, so the white digits stand out; one tone for every row,
                // a mention included (the "@" bubble says it, the author's spec)
                color: Qt.darker(Theme.secondaryHighlightColor, 1.1)

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
                    anchors.horizontalCenterOffset: mention ? mentionTextX : unreadTextX
                    anchors.verticalCenterOffset: mention ? mentionTextY : unreadTextY
                    text: mention ? "@" : model.notifications > 0 ? model.notifications : model.unread
                    // a tenth smaller than the size the pill is measured for: more
                    // room around the digits inside it
                    font.pixelSize: mention ? mentionFontSize : unreadFontSize
                    // the file is the Semibold cut: asking for bold on top of it
                    // would make Qt embolden it again
                    font.family: app.semiboldFontFamily
                    color: "white"
                }
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
        // leads its first line: a handset while a call runs, a crossed-out
        // speaker when muted, a lock when
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
                y: (item.previewLineHeight - height) / 2
                // a call is running in the room: the plain handset, as the
                // room page shows it (the incoming-call arrow read as "someone
                // is calling you", 0.8 audit)
                Image {
                    visible: model.in_call === true && !invited
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-call?" + Theme.highlightColor
                    sourceSize.width: parent.parent.iconSize
                    sourceSize.height: parent.parent.iconSize
                }
                // muted: the crossed-out speaker; the stop sign read as
                // "blocked", next to Ignored users (0.8 audit). Its glyph
                // fills more of its canvas than the lock: a tenth smaller
                Image {
                    visible: model.muted === true && !invited
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-speaker-mute?" + parent.parent.iconColor
                    sourceSize.width: Math.round(parent.parent.iconSize * 0.9)
                    sourceSize.height: Math.round(parent.parent.iconSize * 0.9)
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

            Label {
                id: preview
                // what the first line leaves free for the icons
                readonly property real indent: statusIcons.width > 0 ? statusIcons.width + Theme.paddingSmall : 0
                width: parent.width
                wrapMode: Text.Wrap
                maximumLineCount: Math.max(1, Math.min(Tunables.previewLinesMax, app.previewLines))
                truncationMode: TruncationMode.Fade
                // what Silica's Fade does here (the last of several lines
                // elided, one line cut), but not decided from lineCount:
                // that read the layout its own choice changes, a binding
                // loop on _elideText in every row at start
                elide: maximumLineCount > 1 ? Text.ElideRight : Text.ElideNone
                font.pixelSize: Theme.fontSizeSmall * Tunables.metaTextScale
                color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                textFormat: Text.StyledText
                onLineLaidOut: if (line.number === 0) { line.x += indent; line.width -= indent }
                text: {
                    // a new indent needs a new layout (Qt 5.6 has no forceLayout)
                    var unused = indent
                    //% "Invitation. Tap to accept or decline."
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
    // it, shown only in the breath's last quarter (glowing favourites only)
    Loader {
        anchors.fill: favStar
        active: item.favGlow && favStar.visible
        sourceComponent: Image {
            opacity: Math.max(0, (app.favGlow - Tunables.favSparkleFrom) / (1 - Tunables.favSparkleFrom))
            source: "image://theme/icon-s-favorite?" + Tunables.favSparkleColor
            sourceSize.width: Theme.iconSizeExtraSmall
            sourceSize.height: Theme.iconSizeExtraSmall
        }
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
        // the merged view without colours: the account's short name before
        // the age, so the accounts can still be told apart (0.8 audit)
        text: (item.accountMark.length ? item.accountMark + (ageText.length ? " · " : "") : "") + ageText
        readonly property string ageText: {
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
                onClicked: item.send({ cmd: "join_room", room_id: model.room_id })
            }
            MenuItem {
                visible: invited
                //% "Decline"
                text: qsTrId("rustrx-me-decline")
                onClicked: {
                    var id = model.room_id, core = rustrx, acc = item.account
                    //% "Declining"
                    item.remorseAction(qsTrId("rustrx-re-declining"), function () {
                        if (acc >= 0) core.commandTo(acc, { cmd: "leave_room", room_id: id })
                        else core.command({ cmd: "leave_room", room_id: id })
                    })
                }
            }
            MenuItem {
                visible: !invited && !model.is_space
                //% "Remove from Favs"
                text: model.is_fav ? qsTrId("rustrx-me-unfavourite")
                //% "Add to Favs"
                                   : qsTrId("rustrx-me-favourite")
                onClicked: item.send({ cmd: "set_favourite", room_id: model.room_id, value: !model.is_fav })
            }
            MenuItem {
                visible: !invited && hasUnread
                //% "Mark as read"
                text: qsTrId("rustrx-me-mark-read")
                onClicked: app.markRead(model.room_id, undefined, undefined, item.account >= 0 ? item.account : undefined)
            }
            MenuItem {
                visible: !invited
                //% "Leave"
                text: qsTrId("rustrx-me-leave")
                onClicked: {
                    // the callback runs after the menu is gone; keep what it needs
                    var id = model.room_id, core = rustrx, acc = item.account
                    //% "Leaving"
                    item.remorseAction(qsTrId("rustrx-re-leaving"), function () {
                        if (acc >= 0) core.commandTo(acc, { cmd: "leave_room", room_id: id })
                        else core.command({ cmd: "leave_room", room_id: id })
                    })
                }
            }
        }
    }
}
