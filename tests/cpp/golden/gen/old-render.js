// SPDX-License-Identifier: GPL-3.0-or-later
// The JavaScript that rendered plain texts before the C++ did, copied
// unchanged from the QML (only the functions and the expressions of the
// bindings that used them). gen.cpp runs it in QJSEngine with
// rustrx.emojify standing in for the old Rustrx::emojify (Twemoji::inHtml
// of 84311d4^) and Tunables for the singleton.
//
//   escapeText, linkify, reactionText: qml/components/MessageDelegate.qml
//       at 84311d4^, lines 86-101 (reactionText until ba68435)
//   body, caption, thread: the same file, lines 379, 381, 476, 636-638
//   label: the same file at ba68435^, lines 534 and 557
//   roomName, preview: qml/components/RoomDelegate.qml at 2754eef^,
//       lines 212 and 291-294

var Tunables = { reactionKeyMax: 24 }

// --- MessageDelegate.qml at 84311d4^, as it was ---

    // a reaction's key as shown: text reactions can be long
    function reactionText(key) {
        return key.length > Tunables.reactionKeyMax ? key.substring(0, Tunables.reactionKeyMax - 1) + "…" : key
    }

    // text for a StyledText, as it is
    function escapeText(t) {
        return t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    // plain bodies: escape, link the addresses, keep the line breaks
    function linkify(t) {
        return t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                .replace(/(https?:\/\/[^\s<]+|matrix:[^\s<]+)/g, "<a href=\"$1\">$1</a>")
                .replace(/\n/g, "<br>")
    }

// --- the bindings' expressions, "model." and the sizes made arguments ---

function body(model, emoteSize) {
    var msgType = model.type
    if (msgType === "emote") return rustrx.emojify(escapeText((model.sender_name || model.sender) + " " + (model.body || "")), emoteSize)
    if (msgType === "text" || msgType === "notice") return rustrx.emojify(linkify(model.body || ""), emoteSize)
    return ""
}

function caption(model) {
    return linkify(model.caption || "")
}

// the part after " · ", only when there is a sender
function thread(threadSummary, size) {
    return escapeText(threadSummary.latest_sender) + ": "
           + rustrx.emojify(escapeText((threadSummary.latest_body || "").replace(/\s+/g, " ")), size)
}

function label(modelData, size) {
    return rustrx.emojify(escapeText(reactionText(modelData.key)), size)
}

// --- RoomDelegate.qml at 2754eef^ ---

function roomName(model, size) {
    return rustrx.emojify((model.name || "").replace(/&/g, "&amp;").replace(/</g, "&lt;"), size)
}

function preview(model, multiLine, size) {
    var b = model.preview.replace(/&/g, "&amp;").replace(/</g, "&lt;")
    // one line: the breaks become spaces; more: kept
    b = multiLine ? b.replace(/\n/g, "<br>") : b.replace(/\s+/g, " ")
    return rustrx.emojify(b, size)
}
