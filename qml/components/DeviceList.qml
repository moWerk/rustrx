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

// The account's sessions, listed in place (Settings). Removing one asks
// for the password, as the server requires; a server whose logins an
// account service keeps (matrix.org) takes none, and its own page for the
// device opens in the browser instead (0.8 audit: the prompt was a dead end).
Column {
    id: devicesColumn

    property bool loading: true
    property string loadError
    // the account service's device list, "" without one
    property string accountUrl
    // back from the account service's page: the list again, without the removed one
    property bool awayToRemove

    Component.onCompleted: rustrx.command({ cmd: "devices" })
    Connections {
        target: Qt.application
        onStateChanged: if (Qt.application.state === Qt.ApplicationActive && devicesColumn.awayToRemove) {
            devicesColumn.awayToRemove = false
            rustrx.command({ cmd: "devices" })
        }
    }

    ListModel { id: deviceModel }

    BusyIndicator {
        anchors.horizontalCenter: parent.horizontalCenter
        size: BusyIndicatorSize.Medium
        running: devicesColumn.loading
        visible: running
    }

    Note {
        kind: "error"
        visible: !devicesColumn.loading && deviceModel.count === 0
        //% "The devices could not be loaded"
        text: qsTrId("rustrx-la-devices-failed") + (devicesColumn.loadError.length ? ": " + devicesColumn.loadError : ".")
    }
    Note {
        kind: "footnote"
        visible: devicesColumn.accountUrl.length > 0 && deviceModel.count > 0
        //% "Your account's devices are removed on your server's account page, which opens in the browser."
        text: qsTrId("rustrx-la-devices-account-page")
    }

    Repeater {
        model: deviceModel
        ListItem {
            id: item
            width: devicesColumn.width
            // a third line for an unverified device
            contentHeight: Math.max(Theme.itemSizeMedium, info.height + 2 * Theme.paddingSmall)
            menu: model.current ? null : removeMenu

            // verified or not at a glance; a login without encryption keys
            // has nothing to verify and gets no icon. Not verified is a
            // warning, not StateIcon's cross: a cross at a row's right edge
            // read as a delete button (0.8 audit)
            StateIcon {
                id: verifiedIcon
                anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                level: model.keys === true && model.verified === true ? "good" : ""
            }
            Image {
                visible: model.unverified === true
                anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                sourceSize.width: Theme.iconSizeMedium
                sourceSize.height: Theme.iconSizeMedium
                source: visible ? "image://theme/icon-m-warning?" + Theme.errorColor : ""
            }

            Column {
                id: info
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin + Theme.iconSizeMedium + Theme.paddingMedium
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    // the user's own phone is "this phone" everywhere (0.8 audit)
                    //% "%1 (this phone)"
                    text: model.current ? qsTrId("rustrx-la-this-phone-device").arg(model.name || model.device_id) : (model.name || model.device_id)
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: model.device_id + (model.last_seen_ts ? "  " + app.dayLabel(model.last_seen_ts) + " " + rustrx.formatTime(model.last_seen_ts) : "")
                }
                Label {
                    visible: model.unverified
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.errorColor
                    //% "Not verified"
                    text: qsTrId("rustrx-la-device-not-verified")
                }
            }

            Component {
                id: removeMenu
                ContextMenu {
                    MenuItem {
                        //% "Remove"
                        text: qsTrId("rustrx-me-remove")
                        onClicked: {
                            // the account service removes it on its own page
                            if (model.manage_url) {
                                devicesColumn.awayToRemove = app.openExternal(model.manage_url)
                                return
                            }
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
            devicesColumn.loading = false
            devicesColumn.accountUrl = accountUrl
            deviceModel.clear()
            for (var i = 0; i < devices.length; i++) {
                // only a device with encryption keys can be verified
                var d = devices[i]
                d.unverified = d.keys === true && d.verified === false
                // a ListModel role keeps its first type: no nulls
                d.manage_url = d.manage_url || ""
                deviceModel.append(d)
            }
        }
        onDeviceDeleted: rustrx.command({ cmd: "devices" })
        onErrorOccurred: if (cmd === "devices") { devicesColumn.loading = false; devicesColumn.loadError = message }
    }
}
