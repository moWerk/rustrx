// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
// Stand-in for Silica's TextSwitch: the round indicator, text and description.
BackgroundItem {
    id: sw
    property alias text: title.text
    property alias description: desc.text
    property bool checked
    property bool automaticCheck: true
    height: col.height + 2 * Theme.paddingMedium
    onClicked: if (automaticCheck) checked = !checked
    Rectangle {
        id: dot
        x: Theme.horizontalPageMargin
        y: Theme.paddingMedium + (title.height - height) / 2
        width: Theme.iconSizeSmall; height: width; radius: width / 2
        color: sw.checked ? Theme.highlightColor : "transparent"
        border.color: sw.checked ? Theme.highlightColor : Theme.secondaryColor
        border.width: Math.max(1, Theme.px(2))
    }
    Column {
        id: col
        y: Theme.paddingMedium
        anchors { left: dot.right; leftMargin: Theme.paddingLarge; right: parent.right; rightMargin: Theme.horizontalPageMargin }
        Label { id: title; width: parent.width; wrapMode: Text.Wrap; color: sw.highlighted ? Theme.highlightColor : Theme.primaryColor }
        Label { id: desc; visible: text.length > 0; width: parent.width; wrapMode: Text.Wrap; font.pixelSize: Theme.fontSizeExtraSmall; color: Theme.secondaryColor }
    }
}
