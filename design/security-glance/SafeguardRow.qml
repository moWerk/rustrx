// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Rustrx 1.0

// One safeguard as one line (Settings, Account): its mark in front, its
// name, and when it is missing what that means and the arrow to its fix.
// In place or not known yet it is plain text in the highlight colours,
// which in Sailfish say "nothing to press here"; missing it is a link in
// the primary colour, like LinkRow. The mark is whatever the direction
// draws for a safeguard (a node on a rail, a small ring, an icon): it goes
// in as a child and is centred on the name's first line.
BackgroundItem {
    id: row

    // "ok", "risk" or "unknown"
    property string status: "unknown"
    // every safeguard in place: the names go green
    property bool complete
    property alias title: name.text
    // what the gap means and how to close it; shown only when missing
    property string detail
    // false: missing, but nothing leads to a fix (no arrow)
    property bool fixable: true
    // the mark's column; the texts start after it
    property real markWidth: Theme.iconSizeSmall
    // the mark fills the row's height instead of sitting on the first line
    property bool markFills
    default property alias mark: markBox.data

    readonly property bool missing: status === "risk"
    // the middle of the name's first line, for marks that draw on it
    readonly property real firstLineCenter: texts.y + firstLine.height / 2

    width: parent ? parent.width : 0
    // a line to read is as tall as its text; a line to press never
    // shorter than Silica's smallest touch target
    height: Math.max(missing && fixable ? Theme.itemSizeSmall : 0,
                     texts.height + 2 * (missing ? Theme.paddingMedium : Theme.paddingSmall))
    enabled: missing && fixable

    Item {
        id: markBox
        x: Theme.horizontalPageMargin
        y: row.markFills ? 0 : row.firstLineCenter - height / 2
        width: row.markWidth
        height: row.markFills ? row.height : row.markWidth
    }

    // one line of the name's size, to find the first line's middle
    Text { id: firstLine; visible: false; text: "X"; font: name.font }

    Column {
        id: texts
        anchors {
            left: markBox.right
            leftMargin: Theme.paddingMedium
            right: parent.right
            rightMargin: Theme.horizontalPageMargin + (arrow.visible ? arrow.width - Theme.paddingMedium : 0)
            verticalCenter: parent.verticalCenter
        }
        Label {
            id: name
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: row.highlighted ? Theme.highlightColor
                 : row.missing ? Theme.primaryColor
                 : row.complete ? Tunables.securityComplete
                 : row.status === "ok" ? Theme.highlightColor
                 : Theme.secondaryHighlightColor
            Behavior on color { ColorAnimation { duration: 200 } }
        }
        Label {
            visible: row.missing && text.length > 0
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
            text: row.detail
        }
    }

    ThemeIcon {
        id: arrow
        visible: row.enabled
        anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium; verticalCenter: parent.verticalCenter }
        name: "icon-m-right"
        color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
    }
}
