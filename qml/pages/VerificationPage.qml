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
import "../components"

// One verification, incoming or started here: the request, then the seven
// emoji, then done. Going back cancels it.
ShortPage {
    id: page
    objectName: "verification"

    property string flowId
    property var info: ({})
    property bool waiting      // we asked for a self-verification and wait for its request
    property string phase: waiting ? "waiting" : (info.state || "")
    property var emojis: []
    property var decimals: null
    property string reason
    // another person's name, from their request (later events do not repeat it)
    property string otherName
    Component.onCompleted: otherName = info.other_user_name || ""
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
        case "requested":
            // another person: named by their name and always their user ID,
            // not by a device name they chose (NET-6)
            if (info.self_verification === false) {
                //: %1 the person's name, %2 their user ID
                //% "%1 (%2) wants to verify you. Accept to compare emoji."
                if (otherName.length) return qsTrId("rustrx-la-ver-requested-other").arg(otherName).arg(info.other_user || "")
                //: %1 the person's user ID (no name known)
                //% "%1 wants to verify you. Accept to compare emoji."
                return qsTrId("rustrx-la-ver-requested-other-id").arg(info.other_user || "")
            }
            //: %1 the other device's name or id
            //% "%1 wants to verify this phone. Accept to compare emoji."
            return qsTrId("rustrx-la-ver-requested").arg(info.other_device_name || info.other_device || info.other_user || "")
        //% "Starting the emoji comparison…"
        case "sas": return qsTrId("rustrx-la-ver-starting")
        //% "Compare the emoji with the other device. Same order, same symbols?"
        case "keys_exchanged": return qsTrId("rustrx-la-ver-compare")
        //% "Waiting for the other device to confirm…"
        case "confirmed": return qsTrId("rustrx-la-ver-confirming")
        case "done":
            //: %1 the person's name or user ID
            //% "Verified. %1 is now shown as verified here."
            if (info.self_verification === false) return qsTrId("rustrx-la-ver-done-other").arg(otherName || info.other_user || "")
            //% "Verified. Encrypted messages from your other devices will now reach this one."
            return qsTrId("rustrx-la-ver-done")
        //: %1 the reason
        //% "Cancelled: %1"
        case "cancelled": return qsTrId("rustrx-la-ver-cancelled").arg(reason)
        //% "The other device chose a method this app does not support."
        case "unsupported": return qsTrId("rustrx-la-ver-unsupported")
        default: return phase
        }
    }

    // the self-verification is about this phone; another person's is
    // about their device (0.8 audit: "this phone" for the user's own)
    title: waiting || info.self_verification === true
           ? qsTrId("rustrx-he-verify-phone")
           //% "Verify device"
           : qsTrId("rustrx-he-verify")

    body: [
        // done ends in the large green check, as the QR login does
        FlowStatus {
            text: page.stateText()
            done: page.phase === "done"
            busy: page.phase === "waiting" || page.phase === "created" || page.phase === "ready"
                  || page.phase === "sas" || page.phase === "confirmed"
        },
        // four to a row inside the margins; a long name wraps in its cell
        // instead of running into the next (0.8 audit)
        Grid {
            id: grid
            readonly property real cell: (parent.width - 2 * Theme.horizontalPageMargin - 3 * Theme.paddingMedium) / 4
            anchors.horizontalCenter: parent.horizontalCenter
            visible: page.phase === "keys_exchanged" && page.emojis.length > 0
            columns: 4
            spacing: Theme.paddingMedium
            Repeater {
                model: page.emojis
                Column {
                    width: grid.cell
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.symbol
                        font.pixelSize: Theme.fontSizeHuge
                    }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: modelData.description
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                }
            }
        },
        BigCode {
            text: page.phase === "keys_exchanged" && page.emojis.length === 0 && page.decimals
                  ? page.decimals.join("  ") : ""
        }
    ]

    buttons: [
        PageButton {
            visible: page.phase === "requested"
            //% "Accept"
            text: qsTrId("rustrx-bt-accept")
            onClicked: rustrx.command({ cmd: "verification_accept", flow_id: page.flowId })
        },
        PageButton {
            visible: page.phase === "ready" && !page.info.we_started
            //% "Start emoji comparison"
            text: qsTrId("rustrx-bt-start-sas")
            onClicked: rustrx.command({ cmd: "verification_start_sas", flow_id: page.flowId })
        },
        // the two answers stacked, far apart: side by side a slip on
        // "They differ" could hit "They match", which trusts the other
        // device (0.8 audit)
        Column {
            width: parent.width
            visible: page.phase === "keys_exchanged"
            spacing: Theme.itemSizeMedium
            PageButton {
                //% "They match"
                text: qsTrId("rustrx-bt-match")
                onClicked: rustrx.command({ cmd: "verification_confirm", flow_id: page.flowId })
            }
            PageButton {
                //% "They differ"
                text: qsTrId("rustrx-bt-mismatch")
                onClicked: rustrx.command({ cmd: "verification_mismatch", flow_id: page.flowId })
            }
        },
        // waiting for another device of one's own: without one, only the
        // recovery key helps, and the page now leads there (0.8 audit)
        PageButton {
            visible: page.waiting || (page.info.self_verification === true
                                      && (page.phase === "created" || page.phase === "requested"))
            //% "No other device? Use the recovery key"
            text: qsTrId("rustrx-bt-ver-use-key")
            onClicked: {
                if (page.flowId.length) rustrx.command({ cmd: "verification_cancel", flow_id: page.flowId })
                pageStack.replace(Qt.resolvedUrl("SettingsPage.qml"), { showEncryption: true })
            }
        },
        PageButton {
            visible: page.finished
            //% "Done"
            text: qsTrId("rustrx-bt-done")
            onClicked: pageStack.pop()
        }
    ]

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
            if (data.other_user_name) otherName = data.other_user_name
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
