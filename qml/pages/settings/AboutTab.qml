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

// Settings, About: the icon, the versions, the links and the attributions.
SilicaFlickable {
    id: tab

    property Item settings

    // a short tab: centred on the screen rather than hugging the top
    contentHeight: Math.max(column.height, height)

    Column {
        id: column
        width: parent.width
        y: Math.max(0, (tab.height - height) / 2)
        spacing: Theme.paddingLarge

        // the tab bar is drawn over a tab with a pulley menu; content starts below it
        Item { width: 1; height: tab.pullDownMenu ? settings.tabBarHeight : 0 }

        // the largest size the package installs; the theme does not know
        // the icon by name
        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.itemSizeHuge
            height: width
            sourceSize.width: width
            sourceSize.height: height
            smooth: true
            source: "file:///usr/share/icons/hicolor/172x172/apps/harbour-rustrx.png"
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.secondaryHighlightColor
            linkColor: Theme.highlightColor
            textFormat: Text.StyledText
            onLinkActivated: app.openExternal(link)
            //: %1 app version, %2 core version; lines separated by <br>, keep the link
            //% "Rustrx %1, core %2<br>A Matrix client built on <a href='https://github.com/matrix-org/matrix-rust-sdk'>matrix-rust-sdk</a><br>GPLv3+, from moWerk with love<br><a href='https://github.com/moWerk/rustrx'>github.com/moWerk/rustrx</a>"
            text: qsTrId("rustrx-la-about-lines").arg(appVersion).arg(rustrx.coreVersion)
        }

        // everyone whose report or idea went into Rustrx, as the changelog
        // and the release notes credit them (README's Credits keeps the same list)
        Label {
            readonly property var names: ["doctorflynt", "dodoradio", "finceptum", "Fuzzillogic", "Luna-Terra-CG",
                                          "MrHappy", "poetaster", "projectmoon", "Tealk"]
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
            //: %1 the names of people who reported bugs or brought ideas, comma separated
            //% "Thanks for reports and ideas to %1"
            text: qsTrId("rustrx-la-about-thanks").arg(names.join(", "))
        }

        // the emoji pictures' attribution (CC-BY 4.0 asks for it)
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            linkColor: Theme.highlightColor
            textFormat: Text.StyledText
            onLinkActivated: app.openExternal(link)
            //: attribution of the emoji pictures; keep the links and names as they are
            //% "Emoji: <a href='https://github.com/jdecked/twemoji'>Twemoji</a> by Twitter, Inc. and other contributors and Jason Sofonia &amp; Justine De Caires, <a href='https://creativecommons.org/licenses/by/4.0/'>CC-BY 4.0</a>"
            text: qsTrId("rustrx-la-about-twemoji")
        }

        // how it was made, graded by LLMGD (SPEC section 3: understood and
        // tested by the author, not read line by line, no outside review: A3;
        // the author directs and decides the design, the LLM writes: origin O1)
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            linkColor: Theme.highlightColor
            textFormat: Text.StyledText
            onLinkActivated: app.openExternal(link)
            //: the LLM disclosure grade; keep the link and the grade as they are
            //% "<a href='https://github.com/moWerk/llmgd-specs'>LLMGD-3</a> · origin O1: written by an LLM, directed, understood and tested on the phone by the author; not yet reviewed by others"
            text: qsTrId("rustrx-la-about-llmgd")
        }

        Item { width: 1; height: Theme.paddingLarge }
    }

    VerticalScrollDecorator { }
}
