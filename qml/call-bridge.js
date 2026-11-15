/*
 * Frame script for the call page's WebView (runs in the content process).
 * Element Call talks the Matrix widget API by postMessage to its "parent",
 * which in a dedicated WebView is its own window: its requests and its
 * answers go to the app (and on to the SDK's widget driver), the driver's
 * requests and answers are posted into the page.
 */
const ORIGIN = "https://call.element.io";

addEventListener("message", function (e) {
    if (!content || e.origin !== ORIGIN) return;
    var m;
    try {
        m = JSON.parse(JSON.stringify(Components.utils.waiveXrays(e.data)));
    } catch (err) {
        return;
    }
    if (!m || typeof m.api !== "string") return;
    // what the page sends: its own requests, and its answers to ours
    var fromPage = (m.api === "fromWidget" && m.response === undefined)
            || (m.api === "toWidget" && m.response !== undefined);
    if (!fromPage) return;   // our own messages, echoed back by postMessage
    if (m.api === "fromWidget" && (m.action === "io.element.close" || m.action === "im.vector.hangup"))
        sendAsyncMessage("rustrx:close", {});
    sendAsyncMessage("rustrx:fromWidget", { data: JSON.stringify(m) });
}, true, true);

addMessageListener("rustrx:toWidget", function (msg) {
    if (!content) return;
    var m = JSON.parse(msg.data.data);
    content.postMessage(Components.utils.cloneInto(m, content), ORIGIN);
});

// Element Call's errors and warnings, and the engine's script errors, for
// the debug log: what a call that fails says about it
(function () {
    var sent = 0;
    function forward(level, text) {
        if (sent++ > 2000) return;   // a flood would push everything else out of the log
        try { sendAsyncMessage("rustrx:console", { level: level, text: String(text).slice(0, 600) }); } catch (e) {}
    }
    try {
        Services.obs.addObserver({
            observe: function (subject) {
                var m = subject.wrappedJSObject;
                if (!m || (m.level !== "error" && m.level !== "warn")) return;
                var args = [];
                for (var i = 0; m.arguments && i < m.arguments.length; i++) {
                    try { args.push(typeof m.arguments[i] === "object" ? JSON.stringify(m.arguments[i]) : String(m.arguments[i])); }
                    catch (e) { args.push(String(m.arguments[i])); }
                }
                forward(m.level, args.join(" "));
            }
        }, "console-api-log-event");
    } catch (e) { forward("bridge", "no console observer: " + e); }
    try {
        Services.console.registerListener({
            observe: function (m) {
                if (m instanceof Components.interfaces.nsIScriptError)
                    forward("script", m.errorMessage + " (" + m.sourceName + ":" + m.lineNumber + ")");
            }
        });
    } catch (e) { forward("bridge", "no console listener: " + e); }
    forward("bridge", "loaded");
})();
