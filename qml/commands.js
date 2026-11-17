// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Slash commands in the message field, as Element and others know them.
// parse() only reads the text and says what to do; the composer does it.
//   { send: body, markdown, emote }   a message (the plain text if none)
//   { topic: text } { nick: name } { invite: userId } { join: address }
//   { unknown: "/name" }              not sent: the user decides
// "//text" sends "/text" as a message. The rest of a command's line, its
// argument, may hold spaces and new lines.
var shrug = "¯\\_(ツ)_/¯"

function parse(text) {
    var t = String(text || "")
    if (t.indexOf("//") === 0) return { send: t.slice(1), markdown: true, emote: false }
    var m = /^\/([a-z]+)(?:\s+([\s\S]*))?$/i.exec(t)
    if (!m) return { send: t, markdown: true, emote: false }
    var name = m[1].toLowerCase()
    var arg = (m[2] || "").trim()
    switch (name) {
    case "me":
        if (!arg.length) break
        return { send: arg, markdown: true, emote: true }
    case "shrug":
        // plain: markdown would take the backslash and the underscores
        return { send: arg.length ? arg + " " + shrug : shrug, markdown: false, emote: false }
    case "plain":
        if (!arg.length) break
        return { send: arg, markdown: false, emote: false }
    case "topic":
        return { topic: arg }
    case "nick":
        if (!arg.length) break
        return { nick: arg }
    case "invite":
        if (!/^@[^:\s]+:\S+$/.test(arg)) break
        return { invite: arg }
    case "join":
        if (!/^[#!][^:\s]+:\S+$/.test(arg)) break
        return { join: arg }
    }
    return { unknown: "/" + name }
}
