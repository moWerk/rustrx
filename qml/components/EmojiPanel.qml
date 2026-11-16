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
import "../emoji-data.js" as EmojiData

// The emoji picker (dodoradio's design): it takes the keyboard's place at
// the bottom, the five most used emoji on top, then the groups and the
// custom emoji packs (the user's own, the enabled ones, the room's). In
// "insert" mode an emoji goes into the message at the cursor and the panel
// stays for the next one; with an empty message ("stickers" on) a pack's
// sticker is sent at once. In "react" mode it reacts to a message and closes.
// The room page places it and moves it, sharing the place with the keyboard.
Item {
    id: panel

    // shown (the page slides it in and out)
    property bool open

    property string mode: "insert"
    // the message a reaction is for
    property string eventId
    property int group: 0
    // the image packs of the room (from the core's "image_packs")
    property var packs: []
    // the message is empty: stickers are offered too
    property bool stickers

    signal picked(string emoji)
    // a custom emoji or sticker from a pack: { shortcode, url, body, info, emoticon, sticker }
    signal pickedImage(var image)
    // "React with text": the composer takes the reaction
    signal textReaction()

    width: parent.width

    function show(newMode, forEvent) {
        mode = newMode
        eventId = forEvent || ""
        open = true
    }

    readonly property var groupIcons: ["😀", "👋", "🐶", "🍕", "🚗", "⚽", "💡", "❤️", "🏁"]
    readonly property int unicodeGroups: groupIcons.length
    // the packs with something to offer here: custom emoji, or stickers when
    // a sticker can be sent
    readonly property var shownPacks: {
        var out = []
        for (var i = 0; i < packs.length; i++) {
            var imgs = imagesFor(packs[i])
            if (imgs.length) out.push({ name: packs[i].name, avatar: packs[i].avatar, images: imgs })
        }
        return out
    }
    readonly property int columns: Math.max(Tunables.emojiMinColumns, Math.floor(width / Theme.itemSizeSmall))
    readonly property bool onPack: group >= unicodeGroups && group - unicodeGroups < shownPacks.length

    function imagesFor(pack) {
        var out = []
        for (var i = 0; i < pack.images.length; i++) {
            var img = pack.images[i]
            if (img.emoticon || (img.sticker && mode === "insert" && stickers)) out.push(img)
        }
        return out
    }

    // the cached picture of a custom emoji; "" until it is fetched
    property var emoteFiles: ({})
    function picture(url) {
        if (emoteFiles[url]) return emoteFiles[url]
        return rustrx.emotePath(url)
    }
    Connections {
        target: rustrx
        onEmoteReady: {
            var f = panel.emoteFiles
            f[mxc] = path
            panel.emoteFiles = f
            // the same object again: the bindings are told by hand
            panel.emoteFilesChanged()
        }
    }

    onShownPacksChanged: if (group >= unicodeGroups + shownPacks.length) group = 0

    Rectangle {
        anchors.fill: parent
        color: Theme.rgba(Theme.highlightDimmerColor, Tunables.panelOpacity)
    }

    // A swipe down closes the picker, as it closes the keyboard: pulled past
    // the top of the emoji (or anywhere on the rows above them) by more than
    // a row's height, it slides away on release; less springs back.
    readonly property real pull: Math.max(0, topDrag.dragging ? -topDrag.contentY : 0,
                                          grid.dragging ? grid.originY - grid.contentY : 0)
    property bool pulledFar
    onPullChanged: if (pull > Theme.itemSizeSmall) pulledFar = true
    function releasePull() {
        if (pulledFar && !topDrag.dragging && !grid.dragging) open = false
        if (!topDrag.dragging && !grid.dragging) pulledFar = false
    }

    // the rows above the emoji: their buttons take taps, this a pull down
    Flickable {
        id: topDrag
        width: parent.width
        height: top.height
        contentHeight: height
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.DragOverBounds
        onDraggingChanged: panel.releasePull()

        Column {
            id: top
            width: parent.width

            // reacting: any text can be a reaction too (as in nheko and SchildiChat)
            BackgroundItem {
                visible: panel.mode === "react"
                width: parent.width
                height: visible ? Theme.itemSizeExtraSmall : 0
                onClicked: { panel.open = false; panel.textReaction() }
                Label {
                    anchors.centerIn: parent
                    //% "React with text"
                    text: qsTrId("rustrx-bt-react-text")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }

            // the five most used, the same as in a message's reactions
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                Repeater {
                    model: app.favouriteEmoji
                    BackgroundItem {
                        width: panel.width / panel.columns
                        height: Theme.itemSizeSmall
                        onClicked: panel.choose(modelData)
                        Image {
                            anchors.centerIn: parent
                            width: Theme.iconSizeMedium * Tunables.emojiGlyphScale
                            height: width
                            sourceSize.width: width
                            source: rustrx.emojiUrl(modelData)
                        }
                        Label { anchors.centerIn: parent; visible: !rustrx.emojiUrl(modelData); text: modelData; font.pixelSize: Theme.fontSizeLarge }
                    }
                }
            }

            // the groups, one emoji each, then the packs, their picture each;
            // they scroll sideways when there are many
            SilicaFlickable {
                width: parent.width
                height: Theme.itemSizeExtraSmall
                contentWidth: tabs.width
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                Row {
                    id: tabs
                    readonly property real tabWidth: Math.max(Theme.itemSizeExtraSmall,
                        panel.width / (panel.unicodeGroups + panel.shownPacks.length))
                    Repeater {
                        model: panel.unicodeGroups + panel.shownPacks.length
                        BackgroundItem {
                            readonly property bool isPack: index >= panel.unicodeGroups
                            width: tabs.tabWidth
                            height: Theme.itemSizeExtraSmall
                            highlighted: down || panel.group === index
                            onClicked: { panel.group = index; grid.positionViewAtBeginning() }
                            readonly property string twemoji: isPack ? "" : rustrx.emojiUrl(panel.groupIcons[index])
                            Label {
                                visible: !isPack && !twemoji
                                anchors.centerIn: parent
                                text: isPack ? "" : panel.groupIcons[index]
                                font.pixelSize: Theme.fontSizeMedium
                                opacity: panel.group === index ? 1.0 : Theme.opacityHigh
                            }
                            Image {
                                visible: isPack || !!twemoji
                                anchors.centerIn: parent
                                width: Theme.iconSizeSmall
                                height: width
                                fillMode: Image.PreserveAspectFit
                                sourceSize.height: height
                                opacity: panel.group === index ? 1.0 : Theme.opacityHigh
                                source: isPack ? panel.picture(panel.shownPacks[index - panel.unicodeGroups].avatar || "") : twemoji
                            }
                        }
                    }
                }
            }

            Label {
                visible: panel.onPack
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: visible ? implicitHeight : 0
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: panel.onPack ? panel.shownPacks[panel.group - panel.unicodeGroups].name : ""
            }
        }
    }

    SilicaGridView {
        id: grid
        anchors { top: topDrag.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        onDraggingChanged: panel.releasePull()
        // past its top: the pull that closes the picker (Silica stops at
        // the bounds when there is no pulley menu)
        boundsBehavior: Flickable.DragOverBounds
        clip: true
        cellWidth: width / panel.columns
        cellHeight: Theme.itemSizeSmall
        model: panel.onPack ? panel.shownPacks[panel.group - panel.unicodeGroups].images
                            : EmojiData.groups[Math.min(panel.group, panel.unicodeGroups - 1)].emoji
        delegate: BackgroundItem {
            readonly property bool isImage: typeof modelData !== "string"
            width: grid.cellWidth
            height: grid.cellHeight
            onClicked: isImage ? panel.chooseImage(modelData) : panel.choose(modelData)
            readonly property string twemoji: isImage ? "" : rustrx.emojiUrl(modelData)
            Label {
                visible: !isImage && !twemoji
                anchors.centerIn: parent
                text: isImage ? "" : modelData
                font.pixelSize: Theme.fontSizeLarge
            }
            Image {
                visible: isImage || !!twemoji
                anchors.centerIn: parent
                // a Unicode emoji a little smaller than a pack's picture
                width: isImage ? Theme.iconSizeMedium : Theme.iconSizeMedium * Tunables.emojiGlyphScale
                height: width
                fillMode: Image.PreserveAspectFit
                sourceSize.height: height
                asynchronous: true
                source: isImage ? panel.picture(modelData.url) : twemoji
            }
        }
        VerticalScrollDecorator {}
    }

    function choose(emoji) {
        app.countEmoji(emoji)
        picked(emoji)
        if (mode === "react") open = false
    }
    function chooseImage(image) {
        pickedImage(image)
        if (mode === "react") open = false
    }
}
