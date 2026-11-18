# Design brief: the security glance on Rustrx's Account tab

Rustrx is a native Matrix chat client for Sailfish OS, built with Qt 5.6 / QML and Sailfish Silica, running on the Jolla Phone 2026 (1080 px wide portrait, ambience-themed, mostly dark). Settings have four tabs; the first, Account, opens with a glance at how well the user's messages are protected.

## What it has to say

One verdict, then four safeguards behind it:

- **Verdict** (most important, read first): "Everything that protects your messages is active." / "Not everything that protects your messages is active." / "Checking…"
- **The four safeguards**, each in place, missing, or not known yet:
  1. Encrypted on the phone (the local store; weighs most, it is the only data at rest). Missing: stored unencrypted (old store, or no Sailfish Secrets on the phone).
  2. The phone is verified (cross-signed by the user's other devices).
  3. Key backup exists (with this phone holding the recovery key). Missing: no backup, or the recovery key is missing.
  4. Other devices verified (all of the user's other logins).
- **Gaps**: for each missing safeguard, a sentence on the risk and a tap that leads to its fix (Encryption page, Devices page, start a verification).
- **Notes** (no risk, worth knowing): debug log on, read receipts sent, push gateway sees timing, cached media unencrypted. Each a link.

## What it is now (0.4.9)

From top: avatar, display name field, user ID and server; the verdict line (green when complete); a ring of four arcs with gaps between them (thin, Silica's ProgressCircleBase, store arc twice as long), a lock icon in the middle (a warning sign when the store is not encrypted), small numbers 1 to 4 inside the arcs; a numbered legend of the four safeguards below, green or red; then gaps and notes as rows with an arrow to the right; then Encryption and Devices rows, the debug log section.

Colours: all green (#3ecf6e) when everything is in place; otherwise the arcs in place take the ambience highlight colour, missing ones the theme's error red, unknown ones dimmed.

## What the author thinks of it

All the features he asked for are there, but it looks neither in line with Sailfish OS design nor striking as eye candy on its own. The verdict above the ring is settled. Fresh ideas are wanted for the glance itself: the ring, its legend, how a safeguard and its state pair up visually.

## Constraints

- Sailfish Silica look: thin lines, the ambience's highlight colours (Theme.highlightColor and friends, which change with the user's ambience), no hard boxes, pulley menus, the arrow-row links. Theme icons available include the outlined check in a circle (icon-m-acknowledge), the cross in a filled circle (icon-m-clear), icon-m-device-lock, icon-m-warning, icon-s-filled-warning.
- Green is the one colour outside the ambience: it means "all good", at a glance, without reading.
- Must work on light and dark ambiences, and in landscape.
- Qt 5.6 QML only: shapes from Rectangle, Silica's progress circles, Canvas, or a ShaderEffect (Rustrx already uses one for rounded avatar masks). No Qt Quick Shapes.
- Four safeguards today; the design should not break if one is added later.
- Text is translatable: allow for longer words.
