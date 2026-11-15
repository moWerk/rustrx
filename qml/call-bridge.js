/*
 * Frame script for the call page's WebView (runs in the content process).
 * Element Call talks the Matrix widget API by postMessage to its "parent",
 * which in a dedicated WebView is its own window: its requests and its
 * answers go to the app (and on to the SDK's widget driver), the driver's
 * requests and answers are posted into the page.
 */
const ORIGIN = "https://call.element.io";

// What Element Call needs and Gecko 115 lacks, defined in the page before
// its own scripts run. Intl.Segmenter came with Firefox 125; this one cuts
// graphemes by code point and words at spaces, which is what a call page
// needs it for (initials, trimming names).
const POLYFILL = "(" + function () {
    if (typeof Intl === "undefined" || Intl.Segmenter) return;
    function Segmenter(locale, options) {
        this.granularity = (options && options.granularity) || "grapheme";
        this.locale = Array.isArray(locale) ? locale[0] : (locale || "en");
    }
    Segmenter.prototype.resolvedOptions = function () {
        return { locale: this.locale, granularity: this.granularity };
    };
    Segmenter.prototype.segment = function (input) {
        input = String(input);
        var parts = [], i = 0;
        if (this.granularity === "word" || this.granularity === "sentence") {
            var re = this.granularity === "word" ? /(\s+|[^\s\w]|\w+)/gu : /[^.!?]+[.!?]*\s*/g, m;
            while ((m = re.exec(input)) !== null) {
                if (m[0].length === 0) { re.lastIndex++; continue; }
                parts.push({ segment: m[0], index: m.index, input: input, isWordLike: /\w/u.test(m[0]) });
            }
        } else {
            Array.from(input).forEach(function (ch) {
                parts.push({ segment: ch, index: i, input: input });
                i += ch.length;
            });
        }
        var segments = {
            containing: function (index) {
                for (var k = 0; k < parts.length; k++)
                    if (index >= parts[k].index && index < parts[k].index + parts[k].segment.length) return parts[k];
                return undefined;
            }
        };
        segments[Symbol.iterator] = function () { return parts[Symbol.iterator](); };
        return segments;
    };
    Segmenter.supportedLocalesOf = function (l) { return Array.isArray(l) ? l : (l ? [l] : []); };
    Object.defineProperty(Intl, "Segmenter", { value: Segmenter, writable: true, configurable: true });
} + ")();";

addEventListener("DOMWindowCreated", function (e) {
    var win = e.target && e.target.defaultView;
    if (!win || !win.location || win.location.origin !== ORIGIN) return;
    try {
        win.wrappedJSObject.eval(POLYFILL);
    } catch (err) {
        try { sendAsyncMessage("rustrx:console", { level: "bridge", text: "polyfill: " + err }); } catch (e2) {}
    }
}, true);

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
