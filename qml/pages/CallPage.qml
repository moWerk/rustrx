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
import Sailfish.WebView 1.0
import Sailfish.WebEngine 1.0

// A call: Element Call (a web app) in a WebView, its Matrix requests
// answered by the SDK's widget driver in the core (call-bridge.js carries
// the messages). Spike: started from the room's page, ends when the page
// closes or Element Call hangs up.
WebViewPage {
    id: page

    property string roomId
    property string url

    allowedOrientations: Orientation.Portrait

    Component.onCompleted: {
        // per-participant encryption needs RTCRtpScriptTransform; Gecko 115 has it behind a pref
        WebEngineSettings.setPreference("media.peerconnection.scripttransform.enabled", true)
    }
    Component.onDestruction: rustrx.command({ cmd: "call_end" })

    WebView {
        id: web
        anchors.fill: parent
        url: page.url
        onViewInitialized: {
            web.loadFrameScript(Qt.resolvedUrl("../call-bridge.js"))
            web.addMessageListener("rustrx:fromWidget")
            web.addMessageListener("rustrx:close")
        }
        onRecvAsyncMessage: {
            if (message === "rustrx:fromWidget")
                rustrx.command({ cmd: "call_message", data: data.data })
            else if (message === "rustrx:close" && page.status === PageStatus.Active)
                pageStack.pop()
        }
    }

    Connections {
        target: rustrx
        onCallMessage: web.sendAsyncMessage("rustrx:toWidget", { data: data })
        onCallEnded: if (roomId === page.roomId && page.status === PageStatus.Active) pageStack.pop()
    }
}
