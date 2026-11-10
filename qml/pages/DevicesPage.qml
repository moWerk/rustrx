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

// The account's sessions. Removing one asks for the password, as the
// server requires.
Page {
    id: page

    Component.onCompleted: rustrx.command({ cmd: "devices" })

    ListModel { id: deviceModel }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: deviceModel

        //% "Devices"
        header: PageHeader { title: qsTrId("rustrx-he-devices") }

        delegate: ListItem {
            id: item
            width: parent.width
            contentHeight: Theme.itemSizeMedium
            menu: model.current ? null : removeMenu

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    //% "%1 (this device)"
                    text: model.current ? qsTrId("rustrx-la-this-device").arg(model.name || model.device_id) : (model.name || model.device_id)
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: model.device_id + (model.last_seen_ts ? "  " + app.dayLabel(model.last_seen_ts) + " " + rustrx.formatTime(model.last_seen_ts) : "")
                }
            }

            Component {
                id: removeMenu
                ContextMenu {
                    MenuItem {
                        //% "Remove"
                        text: qsTrId("rustrx-me-remove")
                        onClicked: {
                            var dialog = pageStack.push(passwordDialog, { deviceName: model.name || model.device_id })
                            var id = model.device_id
                            dialog.accepted.connect(function () {
                                rustrx.command({ cmd: "delete_device", device_id: id, password: dialog.password })
                            })
                        }
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            //% "Loading devices…"
            text: qsTrId("rustrx-la-loading-devices")
        }

        VerticalScrollDecorator { }
    }

    Component {
        id: passwordDialog
        Dialog {
            property string deviceName
            property alias password: field.text
            canAccept: field.text.length > 0
            Column {
                width: parent.width
                DialogHeader {
                    //% "Remove device"
                    title: qsTrId("rustrx-he-remove-device")
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    wrapMode: Text.Wrap
                    color: Theme.highlightColor
                    //: %1 device name
                    //% "The server asks for your password before %1 is logged out."
                    text: qsTrId("rustrx-la-remove-device-note").arg(deviceName)
                }
                PasswordField {
                    id: field
                    width: parent.width
                    //% "Password"
                    placeholderText: qsTrId("rustrx-ph-password")
                    label: placeholderText
                    focus: true
                }
            }
        }
    }

    Connections {
        target: rustrx
        onDevices: {
            deviceModel.clear()
            for (var i = 0; i < devices.length; i++) deviceModel.append(devices[i])
        }
        onDeviceDeleted: rustrx.command({ cmd: "devices" })
    }
}
