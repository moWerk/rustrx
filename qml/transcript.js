// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// The loaded chat as plain text, for selecting across messages and pasting
// elsewhere (TranscriptPage). build() takes the timeline's items oldest
// first and gives the text and where each item's line starts in it:
//   [14:02] Adam Pigg: maybe plumbing separately?
//   > Vivia: I think it's a relatively small PR
//   [14:03] moWerk: sure (👍 2)

function two(n) { return (n < 10 ? "0" : "") + n }
function clock(ts) {
    var d = new Date(ts)
    return two(d.getHours()) + ":" + two(d.getMinutes())
}
function day(ts) {
    var d = new Date(ts)
    return d.getFullYear() + "-" + two(d.getMonth() + 1) + "-" + two(d.getDate())
}
function nameOf(m) { return m.sender_name || m.sender || "" }

// the first line of a quoted message, kept short
var quoteMax = 80
function quote(r) {
    var b = String(r.body || "").split("\n")[0]
    if (b.length > quoteMax) b = b.slice(0, quoteMax) + "…"
    return "> " + nameOf(r) + ": " + b
}

function reactions(m) {
    if (!m.reactions || !m.reactions.length) return ""
    return " (" + m.reactions.map(function (r) { return r.key + " " + r.count }).join(", ") + ")"
}

// one item as text; "" for what has none (the read marker, the start)
function itemText(m) {
    if (m.kind === "date") return "--- " + day(m.ts) + " ---"
    if (m.kind !== "event") return ""
    var head = "[" + clock(m.ts) + "] "
    var body = String(m.body || "")
    var t
    switch (m.type) {
    case "emote":
        t = head + "* " + nameOf(m) + " " + body
        break
    case "membership": case "state": case "call":
        t = head + "* " + body
        break
    case "redacted":
        t = head + nameOf(m) + ": [removed]"
        break
    case "utd":
        t = head + nameOf(m) + ": [not decrypted]"
        break
    case "image": case "video": case "audio": case "file": case "sticker":
        var name = m.filename || body
        t = head + nameOf(m) + ": [" + m.type + ": " + name + "]"
        // a caption: the body when it is not just the file's name
        if (body.length && body !== name) t += " " + body
        break
    default:
        t = head + nameOf(m) + ": " + body
    }
    if (m.reply_to && !m.reply_to.missing && !m.reply_to.pending)
        t = quote(m.reply_to) + "\n" + t
    return t + reactions(m)
}

// items oldest first: { text, starts } with starts[i] the offset of item
// i's text, or -1 when it has none
function build(items) {
    var parts = []
    var starts = []
    var at = 0
    for (var i = 0; i < items.length; i++) {
        var t = itemText(items[i])
        if (!t.length) { starts.push(-1); continue }
        if (parts.length) at += 1   // the newline before it
        starts.push(at)
        parts.push(t)
        at += t.length
    }
    return { text: parts.join("\n"), starts: starts }
}
