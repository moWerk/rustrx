// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
// Stand-in for Silica's SilicaFlickable.
Flickable {
    property Item pullDownMenu: null
    clip: true
    boundsBehavior: Flickable.StopAtBounds
}
