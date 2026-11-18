// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
// A control beside the phone frame (not part of the design).
Rectangle {
    property alias text: label.text
    property string swatch
    signal clicked()
    width: parent.width; height: 28; radius: 4
    color: area.pressed ? "#555" : "#3a3a3a"
    Rectangle { visible: swatch.length > 0; x: 6; width: 16; height: 16; radius: 8; anchors.verticalCenter: parent.verticalCenter; color: swatch || "transparent" }
    Text { id: label; x: swatch.length ? 28 : 8; anchors.verticalCenter: parent.verticalCenter; color: "white"; font.pixelSize: 13 }
    MouseArea { id: area; anchors.fill: parent; onClicked: parent.clicked() }
}
