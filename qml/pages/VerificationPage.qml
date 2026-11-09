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

// One verification, incoming or started here: the request, then the seven
// emoji, then done. Going back cancels it.
Page {
    id: page

    property string flowId
    property var info: ({})
    property bool waiting      // we asked for a self-verification and wait for its request
    property string phase: waiting ? "waiting" : (info.state || "")
    property var emojis: []
    property var decimals: null
    property string reason
    property bool finished: phase === "done" || phase === "cancelled"

    onStatusChanged: {
        if (status === PageStatus.Deactivating && !finished && flowId.length)
            rustrx.command({ cmd: "verification_cancel", flow_id: flowId })
    }

    function stateText() {
        switch (phase) {
        //% "Waiting for your other device to pick up the request…"
        case "waiting": return qsTrId("rustrx-la-ver-waiting")
        case "created":
        //% "Request sent. Accept it on the other device."
        case "ready": return qsTrId("rustrx-la-ver-sent")
        //: %1 the other device's name or id
        //% "%1 wants to verify this device. Accept to compare emoji."
        case "requested": return qsTrId("rustrx-la-ver-requested").arg(info.other_device_name || info.other_device || info.other_user || "")
        //% "Starting the emoji comparison…"
        case "sas": return qsTrId("rustrx-la-ver-starting")
        //% "Compare the emoji with the other device. Same order, same symbols?"
        case "keys_exchanged": return qsTrId("rustrx-la-ver-compare")
        //% "Waiting for the other device to confirm…"
        case "confirmed": return qsTrId("rustrx-la-ver-confirming")
        //% "Verified. Encrypted messages from your other devices will now reach this one."
        case "done": return qsTrId("rustrx-la-ver-done")
        //: %1 the reason
        //% "Cancelled: %1"
        case "cancelled": return qsTrId("rustrx-la-ver-cancelled").arg(reason)
        //% "The other device chose a method this app does not support."
        case "unsupported": return qsTrId("rustrx-la-ver-unsupported")
        default: return phase
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            //% "Verify device"
            PageHeader { title: qsTrId("rustrx-he-verify") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: stateText()
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Large
                running: phase === "waiting" || phase === "created" || phase === "ready" || phase === "sas" || phase === "confirmed"
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: phase === "requested"
                //% "Accept"
                text: qsTrId("rustrx-bt-accept")
                onClicked: rustrx.command({ cmd: "verification_accept", flow_id: flowId })
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: phase === "ready" && !info.we_started
                //% "Start emoji comparison"
                text: qsTrId("rustrx-bt-start-sas")
                onClicked: rustrx.command({ cmd: "verification_start_sas", flow_id: flowId })
            }

            Grid {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: phase === "keys_exchanged" && emojis.length > 0
                columns: 4
                spacing: Theme.paddingLarge
                Repeater {
                    model: emojis
                    Column {
                        width: Theme.itemSizeLarge
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.symbol
                            font.pixelSize: Theme.fontSizeHuge
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.description
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                        }
                    }
                }
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: phase === "keys_exchanged" && emojis.length === 0 && decimals
                text: decimals ? decimals.join("  ") : ""
                font.pixelSize: Theme.fontSizeHuge
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: phase === "keys_exchanged"
                spacing: Theme.paddingLarge
                Button {
                    //% "They match"
                    text: qsTrId("rustrx-bt-match")
                    onClicked: rustrx.command({ cmd: "verification_confirm", flow_id: flowId })
                }
                Button {
                    //% "They differ"
                    text: qsTrId("rustrx-bt-mismatch")
                    onClicked: rustrx.command({ cmd: "verification_mismatch", flow_id: flowId })
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: finished
                //% "Done"
                text: qsTrId("rustrx-bt-done")
                onClicked: pageStack.pop()
            }
        }
    }

    Connections {
        target: rustrx
        onVerification: {
            // the page waiting for a self-verification adopts the first request that appears
            if (waiting && flowId.length === 0 && data.we_started) {
                flowId = data.flow_id
                waiting = false
            }
            if (data.flow_id !== flowId) return
            info = data
            phase = data.state
            if (data.state === "cancelled") reason = data.reason || ""
        }
        onSas: {
            if (data.flow_id !== flowId) return
            phase = data.state
            if (data.emojis) emojis = data.emojis
            if (data.decimals) decimals = data.decimals
            if (data.state === "cancelled") reason = data.reason || ""
        }
    }
}
