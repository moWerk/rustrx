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
import "../components"

// The conversation. The list runs bottom to top, so its header, the text
// field, sits at the bottom, and the push-up menu with the attachments
// appears when the newest message is in view.
Page {
    id: page

    property string roomId
    property string roomName
    property string replyToId
    property string replyToText
    property bool paginating

    Component.onCompleted: rustrx.openRoom(roomId)
    Component.onDestruction: if (rustrx.openRoomId === roomId) rustrx.closeRoom()

    onStatusChanged: {
        if (status === PageStatus.Active) markRead()
    }

    function markRead() {
        if (rustrx.timeline.count > 0 && Qt.application.state === Qt.ApplicationActive)
            rustrx.command({ cmd: "mark_read", room_id: roomId })
    }

    function send() {
        var text = input.text.trim()
        if (text.length === 0) return
        rustrx.sendText(roomId, text, replyToId)
        input.text = ""
        replyToId = ""
        replyToText = ""
        typingTimer.stop()
        rustrx.command({ cmd: "typing", room_id: roomId, typing: false })
    }

    Timer {
        id: typingTimer
        interval: 4000
        onTriggered: rustrx.command({ cmd: "typing", room_id: roomId, typing: false })
    }

    Connections {
        target: rustrx
        onPaginated: if (roomId === page.roomId) paginating = false
    }

    Connections {
        target: rustrx.timeline
        onCountChanged: if (list.atYBeginning) markRead()
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        verticalLayoutDirection: ListView.BottomToTop
        model: rustrx.timeline
        spacing: Theme.paddingSmall
        clip: true

        // the oldest end is at the top in this layout
        onAtYEndChanged: {
            if (atYEnd && count > 0 && !paginating) {
                paginating = true
                rustrx.command({ cmd: "paginate_back", room_id: roomId, count: 40 })
            }
        }

        PushUpMenu {
            MenuItem {
                //% "Take a photo"
                text: qsTrId("rustrx-me-take-photo")
                onClicked: {
                    var cam = pageStack.push(Qt.resolvedUrl("CameraPage.qml"))
                    cam.captured.connect(function (path) { rustrx.sendFile(roomId, path) })
                }
            }
            MenuItem {
                //% "Send an image"
                text: qsTrId("rustrx-me-send-image")
                onClicked: {
                    var picker = pageStack.push(multiImagePicker)
                    picker.accepted.connect(function () {
                        for (var i = 0; i < picker.selectedContent.count; i++)
                            rustrx.sendFile(roomId, picker.selectedContent.get(i).filePath)
                    })
                }
            }
            MenuItem {
                //% "Send a file"
                text: qsTrId("rustrx-me-send-file")
                onClicked: {
                    var picker = pageStack.push(filePicker)
                    picker.selectedContentPropertiesChanged.connect(function () {
                        if (picker.selectedContentProperties.filePath)
                            rustrx.sendFile(roomId, picker.selectedContentProperties.filePath)
                    })
                }
            }
        }

        // in a bottom-to-top list the footer is drawn at the top
        footer: PageHeader {
            title: roomName
            description: rustrx.syncState === "running" ? "" :
                         //% "Not connected"
                         qsTrId("rustrx-la-not-connected")
        }

        header: Column {
            width: list.width

            Item { width: 1; height: Theme.paddingMedium }

            Label {
                visible: replyToId.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                //: %1 the quoted text
                //% "Replying to: %1"
                text: qsTrId("rustrx-la-replying-to").arg(replyToText)
                MouseArea { anchors.fill: parent; onClicked: { replyToId = ""; replyToText = "" } }
            }

            Row {
                width: parent.width

                TextArea {
                    id: input
                    width: parent.width - sendButton.width
                    //% "Message"
                    placeholderText: qsTrId("rustrx-ph-message")
                    label: roomName
                    onTextChanged: {
                        if (text.length === 0) return
                        if (!typingTimer.running)
                            rustrx.command({ cmd: "typing", room_id: roomId, typing: true })
                        typingTimer.restart()
                    }
                }

                // a text button, as in Jolla's Messages
                BackgroundItem {
                    id: sendButton
                    width: sendLabel.implicitWidth + 2 * Theme.paddingLarge
                    height: input.height
                    enabled: input.text.trim().length > 0
                    onClicked: send()
                    Label {
                        id: sendLabel
                        anchors.centerIn: parent
                        //% "Send"
                        text: qsTrId("rustrx-bt-send")
                        color: !sendButton.enabled ? Theme.secondaryColor
                             : sendButton.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }
        }

        delegate: MessageDelegate {
            roomId: page.roomId
            onReply: {
                replyToId = eventId
                replyToText = text
                input.focus = true
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            //% "No messages yet"
            text: qsTrId("rustrx-la-no-messages")
        }

        VerticalScrollDecorator { }
    }

    Component {
        id: multiImagePicker
        MultiImagePickerDialog { }
    }

    Component {
        id: filePicker
        FilePickerPage { }
    }
}
