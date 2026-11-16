// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Text from other people (names, topics, message bodies) going into a
// StyledText label: escaped, so it shows as written and cannot add a tag,
// a link or a remote picture.
function escaped(text) {
    return String(text || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
}
