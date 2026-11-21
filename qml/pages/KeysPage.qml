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
import Rustrx 1.0
import Sailfish.Silica 1.0
import "../components"
import Sailfish.Pickers 1.0

// Room keys as a passphrase-protected file, the format Element and nheko
// use: export for another client, import keys from one.
Page {
    id: page

    property string result
    // a failure apart from the result, in the error colour (0.8 audit)
    property string error
    property bool working

    Connections {
        target: rustrx
        onKeysExported: {
            page.working = false
            //: %1 the file path
            //% "Keys written to %1"
            page.result = qsTrId("rustrx-la-keys-exported").arg(path)
        }
        onKeysImported: {
            page.working = false
            //: %1 keys taken over, %2 keys in the file
            //% "%1 of %2 keys imported"
            page.result = qsTrId("rustrx-la-keys-imported").arg(imported).arg(total)
        }
        onErrorOccurred: {
            if (cmd !== "export_keys" && cmd !== "import_keys") return
            page.working = false
            page.result = ""
            page.error = app.friendlyError(cmd, message)
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                //% "Room keys"
                title: qsTrId("rustrx-he-keys")
            }

            Note {
                kind: "intro"
                //% "The file holds the keys to your encrypted messages. It is protected by the passphrase; keep both apart."
                text: qsTrId("rustrx-la-keys-hint")
            }

            PasswordField {
                id: pass
                width: parent.width
                //% "Passphrase"
                label: qsTrId("rustrx-la-passphrase")
                placeholderText: label
            }

            PageButton {
                enabled: pass.text.length >= Tunables.minPassphrase && !page.working
                //% "Export to Documents"
                text: qsTrId("rustrx-bt-export-keys")
                onClicked: {
                    page.working = true; page.result = ""; page.error = ""
                    var stamp = new Date().toISOString().slice(0, 10)
                    rustrx.command({ cmd: "export_keys", passphrase: pass.text,
                                     path: StandardPaths.documents + "/rustrx-keys-" + stamp + ".txt" })
                }
            }

            // the other direction, apart from Export (0.8 audit)
            Item { width: 1; height: Theme.paddingLarge }
            PageButton {
                enabled: pass.text.length > 0 && !page.working
                //% "Import from a file"
                text: qsTrId("rustrx-bt-import-keys")
                onClicked: {
                    var picker = pageStack.push(filePicker)
                    picker.selectedContentPropertiesChanged.connect(function () {
                        var path = picker.selectedContentProperties.filePath
                        if (!path) return
                        page.working = true; page.result = ""; page.error = ""
                        rustrx.command({ cmd: "import_keys", passphrase: pass.text, path: path })
                    })
                }
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: page.working
                visible: running
                size: BusyIndicatorSize.Medium
            }

            Note {
                kind: "status"
                text: page.result
            }
            Note {
                kind: "error"
                text: page.error
            }
        }
    }

    Component {
        id: filePicker
        FilePickerPage { }
    }
}
