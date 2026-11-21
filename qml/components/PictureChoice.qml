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
import Sailfish.Pickers 1.0

// A picture that a tap changes: a photo taken now, or one from the gallery
// (the author's wish, 0.8: a selfie for one's own picture). Its content is
// the picture as it is shown; `picked` has the file either way, and the
// caller sets it as before (Media.cpp turns it upright and scales it).
ListItem {
    id: item

    // the gallery picker's header
    property string title
    // the camera starts on the front (one's own picture)
    property bool front

    signal picked(string path)

    function takePhoto() {
        var cam = pageStack.push(Qt.resolvedUrl("../pages/CameraPage.qml"), { front: item.front, mode: "picture" })
        cam.captured.connect(item.picked)
    }
    function pickPicture() { pageStack.push(picker) }

    contentHeight: Theme.itemSizeHuge
    onClicked: openMenu()

    menu: Component {
        ContextMenu {
            MenuItem {
                text: qsTrId("rustrx-me-take-photo")
                onClicked: item.takePhoto()
            }
            MenuItem {
                //% "Pick from the gallery"
                text: qsTrId("rustrx-me-pick-picture")
                onClicked: item.pickPicture()
            }
        }
    }

    property Component picker: Component {
        ImagePickerPage {
            title: item.title
            onSelectedContentPropertiesChanged: {
                if (selectedContentProperties.filePath) item.picked(selectedContentProperties.filePath)
            }
        }
    }
}
