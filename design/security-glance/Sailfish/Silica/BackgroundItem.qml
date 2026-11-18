// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
// Stand-in for Silica's BackgroundItem: a press lights it in the highlight colour.
MouseArea {
    id: item
    property bool highlighted: pressed && containsMouse
    property color highlightedColor: Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
    default property alias content: holder.data
    width: parent ? parent.width : 0
    height: Theme.itemSizeSmall
    Rectangle { anchors.fill: parent; color: item.highlighted ? item.highlightedColor : "transparent" }
    Item { id: holder; anchors.fill: parent }
}
