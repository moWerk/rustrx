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
import "../emoji-data.js" as EmojiData

// The emoji picker (dodoradio's design): it takes the keyboard's place at
// the bottom, the five most used emoji on top, then the groups. In "insert"
// mode an emoji goes into the message at the cursor and the panel stays for
// the next one; in "react" mode it reacts to a message and closes.
DockedPanel {
    id: panel

    property string mode: "insert"
    // the message a reaction is for
    property string eventId
    property int group: 0

    signal picked(string emoji)
    // "React with text": the composer takes the reaction
    signal textReaction()

    width: parent.width
    // about the keyboard's height, so the conversation does not jump
    height: Math.round(Screen.height * (isPortrait ? 0.36 : 0.5))
    dock: Dock.Bottom
    modal: false

    function show(newMode, forEvent) {
        mode = newMode
        eventId = forEvent || ""
        open = true
    }

    readonly property var groupIcons: ["😀", "👋", "🐶", "🍕", "🚗", "⚽", "💡", "❤️", "🏁"]
    readonly property int columns: Math.max(6, Math.floor(width / Theme.itemSizeSmall))

    Rectangle {
        anchors.fill: parent
        color: Theme.rgba(Theme.highlightDimmerColor, 0.9)
    }

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
                    Label { anchors.centerIn: parent; text: modelData; font.pixelSize: Theme.fontSizeLarge }
                }
            }
        }

        // the groups, one emoji each
        Row {
            width: parent.width
            Repeater {
                model: panel.groupIcons.length
                BackgroundItem {
                    width: panel.width / panel.groupIcons.length
                    height: Theme.itemSizeExtraSmall
                    highlighted: down || panel.group === index
                    onClicked: { panel.group = index; grid.positionViewAtBeginning() }
                    Label {
                        anchors.centerIn: parent
                        text: panel.groupIcons[index]
                        font.pixelSize: Theme.fontSizeMedium
                        opacity: panel.group === index ? 1.0 : Theme.opacityHigh
                    }
                }
            }
        }
    }

    SilicaGridView {
        id: grid
        anchors { top: top.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        clip: true
        cellWidth: width / panel.columns
        cellHeight: Theme.itemSizeSmall
        model: EmojiData.groups[panel.group].emoji
        delegate: BackgroundItem {
            width: grid.cellWidth
            height: grid.cellHeight
            onClicked: panel.choose(modelData)
            Label { anchors.centerIn: parent; text: modelData; font.pixelSize: Theme.fontSizeLarge }
        }
        VerticalScrollDecorator {}
    }

    function choose(emoji) {
        app.countEmoji(emoji)
        picked(emoji)
        if (mode === "react") open = false
    }
}
