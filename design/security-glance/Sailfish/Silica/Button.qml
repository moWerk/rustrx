// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
// Stand-in for Silica's Button: a rounded, faint box with centred text.
MouseArea {
    id: b
    property alias text: label.text
    implicitWidth: Math.max(Theme.px(234), label.implicitWidth + 2 * Theme.paddingLarge)
    implicitHeight: Theme.itemSizeExtraSmall
    width: implicitWidth; height: implicitHeight
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: b.pressed ? Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
                         : Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
    }
    Label { id: label; anchors.centerIn: parent; color: b.pressed ? Theme.highlightColor : Theme.primaryColor }
}
