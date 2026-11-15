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
        // a call plays the others' audio without a tap first
        WebEngineSettings.setPreference("media.autoplay.default", 0)
        WebEngineSettings.setPreference("media.autoplay.blocking_policy", 0)
    }
    Component.onDestruction: rustrx.command({ cmd: "call_end" })

    // Element Call's requests for the client itself rather than the Matrix
    // side (Element X answers them in its UI): answered here, the SDK's
    // driver does not know them and answered with an error
    readonly property var clientActions: ["io.element.join", "io.element.device_mute", "set_always_on_screen",
        "io.element.tile_layout", "io.element.spotlight", "io.element.close", "im.vector.hangup"]
    function answerLocally(json) {
        var m
        try { m = JSON.parse(json) } catch (e) { return false }
        if (m.api !== "fromWidget" || m.response !== undefined || clientActions.indexOf(m.action) < 0)
            return false
        m.response = {}
        web.sendAsyncMessage("rustrx:toWidget", { data: JSON.stringify(m) })
        return true
    }

    // the widget API's flow for the debug log: who sent which action, a
    // request or an answer; never the contents (keys and tokens travel there)
    function trace(from, json) {
        try {
            var m = JSON.parse(json)
            console.log("call widget", from + ":", m.api, m.action, m.response !== undefined ? "answer" : "request",
                        m.response && m.response.error ? "error " + JSON.stringify(m.response.error).slice(0, 200) : "")
        } catch (e) {
            console.log("call widget", from + ": unreadable message")
        }
    }

    WebView {
        id: web
        anchors.fill: parent
        onLoadingChanged: console.log("call page loading", loading, "progress", loadProgress, url.toString().split("?")[0])
        onViewInitialized: {
            web.loadFrameScript(Qt.resolvedUrl("../call-bridge.js"))
            web.addMessageListener("rustrx:fromWidget")
            web.addMessageListener("rustrx:close")
            web.addMessageListener("rustrx:console")
            // only now: the frame script puts its polyfill into the page
            // before Element Call's scripts run
            web.url = page.url
        }
        onRecvAsyncMessage: {
            if (message === "rustrx:fromWidget") {
                page.trace("page", data.data)
                if (!page.answerLocally(data.data))
                    rustrx.command({ cmd: "call_message", data: data.data })
            }
            else if (message === "rustrx:console")
                console.log("call page", data.level + ":", data.text)
            else if (message === "rustrx:close" && page.status === PageStatus.Active)
                pageStack.pop()
        }
    }

    Connections {
        target: rustrx
        onCallMessage: {
            page.trace("driver", data)
            web.sendAsyncMessage("rustrx:toWidget", { data: data })
        }
        onCallEnded: if (roomId === page.roomId && page.status === PageStatus.Active) pageStack.pop()
    }
}
