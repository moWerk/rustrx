// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
// Stand-in for Silica's SectionHeader: right-aligned, small, highlight colour.
Item {
    property alias text: label.text
    width: parent ? parent.width : 0
    height: Theme.itemSizeExtraSmall
    Label {
        id: label
        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; bottom: parent.bottom; bottomMargin: Theme.paddingMedium }
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.highlightColor
    }
}
