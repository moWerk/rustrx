// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
// Stand-in for Silica's Label: the theme's font and colour.
Text {
    color: Theme.primaryColor
    linkColor: Theme.highlightColor
    font.pixelSize: Theme.fontSizeMedium
    font.family: Theme.fontFamily
    textFormat: Text.PlainText
}
