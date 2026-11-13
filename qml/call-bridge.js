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
