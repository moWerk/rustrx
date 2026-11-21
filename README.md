# Rustrx

<img src="icons/rustrx-512.png" alt="Rustrx icon: a rainbow flag of connecting fibres on the dark Sailfish app shape" width="256" align="right">

A native Matrix client for Sailfish OS: a Silica interface on top of
[matrix-rust-sdk](https://github.com/matrix-org/matrix-rust-sdk), the library
behind Element X. The trustworthy and well maintained SDK does the heavy
lifting (sync, end-to-end encryption, key backup, device verification);
Rustrx is the Sailfish part around it.

The name is Rust and Matrix mushed together. The icon is a rainbow flag of
connecting fibres, because Rust.

## Why another one

It started the evening a Jolla Phone 2026 was unboxed, and Matrix was the
first thing that did not work. Sailtrix could not show the rooms properly,
and its project does not take LLM-written contributions. Hydrogen is a web
wrapper, and Element in App Support is not what you want on a Sailfish
phone. So Rustrx was built that evening.

What the author did not find that evening was Xmatic, a native Matrix client
that already existed and does more. Had it turned up, Rustrx would not
have been started. That says something about how scattered the places are where
Sailfish apps live. It also turned out to be lucky: Rustrx became a project
worth finishing, and its differences grew on their own, from following one
person's needs instead of looking at what other clients do. It is lean and
text first, made for texting and sharing some pictures and files. Rooms and
people sit apart by default, timestamps are short (5m, 3h, 2d), the room
list can be sorted, filtered and searched, and it runs on community ports
without sailfish-secrets. Since 0.6 it makes classic 1:1 calls with Element and
nheko, end-to-end encrypted, on the earpiece (and Element Call since 0.4.4), threads, spaces with requests to join, and Twemoji with custom emoji and
stickers. Since 0.7 it holds as many accounts as you like, each notifying on
its own, and shows them apart or all in one list.

![Rustrx 0.7.0 on the Pro¹-X: the account menu with three accounts above all accounts' rooms in one list, each row with its account's colour as a bar at its edge; Link a new device showing its QR code with "Valid for 56 s"; the account service's "Access granted" page in the browser after the new phone was let in](screenshots/rustrx-0.7.0-accounts-qr.jpg)

![Rustrx 0.7.0 Settings: the Account tab's pulley with "Log in to another account" and the other accounts above the security ring, all green; the App tab with "All accounts in one list" and "Colour-coded accounts"; the Account tab with Classic sync and Link a new device](screenshots/rustrx-0.7.0-settings.jpg)

## What it does

- Log in on the server's own page in the browser where the server has an
  account service (OAuth 2.0 / MAS, matrix.org among them): the password
  never passes through Rustrx, which only gets a token. Elsewhere with a
  password or single sign-on. Everything on one page, errors shown
  inline, nothing thrown away. "Create an account" makes one on the
  account service's page and logs in, or opens the sign-up page. The session is kept: Rustrx starts in about a second, and if
  a server really ends the session, it goes back to the login page and says
  so.
- Several accounts, as many as you like: the shown account's picture left
  of the tabs switches between them, and Settings > Account adds one more.
  Every account syncs and notifies at once, in the background and through
  push too; a notification's tap shows its account. Each keeps its own
  store and keys. "All accounts in one list" (Settings > App, off by
  default) shows every account's rooms together, each marked with its
  account's colour, and opens a room in its own account.
- Logging in with a QR code, both ways: Settings > Account > Link a new
  device shows a code for a new phone or scans the new phone's code, and
  the login page scans the code of a device that is logged in. The new
  device comes in verified, with the keys. It works with Element on the
  other side too, on servers that offer it (an account service with the
  QR rendezvous).
- Native sliding sync where the server has it (matrix.org does), classic
  sync otherwise, and a switch per account for servers whose sliding sync
  leaves rooms out. Sync comes back by itself after a Wi-Fi drop.
- Rooms, People, Favs and Spaces as tabs, paged the way Jolla's Settings
  are, or, if you prefer, one "Chats" list with or without the Favs and
  Spaces tabs.
  Invitations sit on top. Rows show the avatar with the unread count as a
  badge that grows to the full count, one to three lines of the last
  message, its age, a star for favourites, and in front of the message a
  stop sign for muted rooms, a lock for encrypted ones and a check when
  your own last message was read. Long press to favourite, mark read or
  leave.
- A filter icon at the tabs: order by activity, unread first (most on top)
  or alphabetically; favourites on top and low priority at the bottom as
  switches; and a search that narrows the lists while you type, kept with
  a check mark if you want it to stay.
- "Add Room / Contact" in the pulley menu searches users and public rooms,
  takes a #alias or @user directly, and creates rooms and direct chats.
- A line under the tabs reminds you while the device is unverified or has
  no recovery key.
- Conversations with text in markdown, replies that quote what they answer,
  spoilers (covered until tapped; `||text||` sends one), reactions, images, videos, audio, files, stickers, polls, locations and
  state events. A long press shows reactions and the four usual actions,
  the rest on a "More…" page: your own messages can be edited or removed;
  messages can be reported, copied as a link, shown as source, or sent
  again when they failed, and a running upload can be cancelled. A double
  tap replies. Messages sit in bubbles, without avatars:
  the sender's name is inside. The header shows the room's picture and who
  is typing, "read by ..." shows read receipts. Drafts are kept per room.
  Pinned messages: a pill under the header steps through them; pinning and
  unpinning in a message's menu, where the room allows it.
- Threads: replies in a thread are marked in the room and open their
  thread; a thread's page shows it alone with its own text field; "Reply
  in thread" in a message's menu; the room's threads, with the new
  replies marked, in its settings.
- Spaces: a tab with your spaces, each with its rooms and subspaces,
  joined or not; making spaces and rooms in them, adding and removing
  rooms, leaving a space alone or with its rooms. Who can join a room or
  space (invited only, ask to join, members of the space, anyone);
  requests to join with a notification and a list to accept, decline or
  ban; asking to join yourself; a preview before joining by address or
  link.
- Classic 1:1 calls in direct chats, the ones Element Web and Android and
  nheko make (`m.call.*`): end-to-end encrypted in an encrypted chat, audio
  through GStreamer's WebRTC with Opus and an echo canceller, on the
  earpiece like a phone call (the speaker button for the loudspeaker), the
  screen dark at the ear. A ringing call is answered with a swipe as in the
  phone app, from its notification (also with Rustrx closed) or from the
  cover; a second call is told you are busy. Optionally only through the
  server's relay, so the other side never sees the phone's address. How
  the earpiece works on Sailfish OS: docs/CALLS.md.
- Calls through Element Call in a WebView, with the SDK's widget driver
  behind it: incoming calls ring (VoIP ringtone, Answer, Decline), a
  running call shows in the room and the list. Unencrypted rooms only
  for now: Sailfish OS 5.2's Gecko 115 cannot do Element Call's
  encryption.
- Emoji as Twemoji pictures; an emoji picker in the keyboard's place
  behind a smiley button (your five most used on top, a swipe down closes
  it); custom emoji and stickers from Matrix image packs, "Add to my
  emoji" for one you see; reactions with any text.
- A push-up menu to take a photo (autofocus, a preview before sending), send
  a picture or file, or record a voice message (Ogg/Opus). Pictures go out
  upright and scaled down, with the composer text as caption, and show in
  the room at once with their upload progress. Pictures open in a viewer
  with pinch zoom and saving to Downloads.
- The share sheet: "Send to a room" from the gallery, the browser and other
  apps. `matrix:` and matrix.to links open in Rustrx.
- A page for each room: its picture (moderators can change it), name,
  topic, address, encryption, favourite, low priority, members, invite,
  leave, and its own notification level. Moderation as the room's power
  levels allow: roles, muting, removing, banning (from a whole space too),
  revoking invitations, removing someone's recent messages, who may do
  what, who can read the history, guests, the room directory, server
  access lists, upgrading a room.
- Notifications per room, updated in place, opening the room on a tap, and
  one for each new invitation; also while Rustrx is open, except for the
  room in front.
- Notifications with Rustrx closed, one choice in Settings > Notifications:
  push through UnifiedPush (Foghorn from OpenRepos as the distributor; the
  homeserver pushes through a Matrix gateway, D-Bus activation wakes Rustrx
  for one message, nothing keeps running), or, without a distributor, the
  background mode: a stand-in without a window stays connected, notifies
  within seconds even while the phone sleeps, and starts with the phone. A
  line in the events view shows it runs; tap it to open Rustrx, or quit it
  there.
- Device verification by emoji comparison, incoming or started from
  Settings (tested with nheko); restoring encrypted history with the
  recovery key; key backup; room key export and import.
- Settings in four tabs. Account: one's own picture, a security ring that
  turns green once the store is encrypted, the device verified, a recovery
  key set and the other devices verified, with each gap named; the
  logged-in devices, storage and the debug log. Privacy: read receipts,
  typing, showing as online (off by default), what pictures and videos
  leave without (nothing, the location, or all metadata but which way up
  they are, the default; JPEG, PNG and MP4), calls only through a relay,
  message text and chat names (and who calls) outside Rustrx,
  invitations from strangers, ignored users. App: appearance (avatar
  corners from square to round, preview lines, the tabs), notifications.
  About. Portrait and landscape, or locked to portrait.
- The local store (messages and keys) is encrypted with a key kept in
  sailfish-secrets where that works. On community ports without it Rustrx
  still runs, unencrypted, and says so once after login, with the command
  that installs Secrets. If a server ends the session, the store and the
  device are kept, so logging in again brings back keys and history.
- A debug log, on until 1.0 and off with a switch: if something goes wrong,
  Settings > Debug log shares it, so nobody has to make it happen again. It
  holds room and user IDs, never message texts or passwords. During a call
  it also holds Element Call's own warnings, with query strings and long
  ids cut out.
- The cover shows unread counts and the connection state, with actions for
  search and the latest room with unread messages; during a call, the call.

## How it is built

[review-and-architecture-hints.md](review-and-architecture-hints.md) is the map for reviewers.

`core/` is a Rust crate that wraps matrix-rust-sdk behind six C functions
(`core/src/lib.rs`). Commands go in as JSON, events come back as JSON on one
callback. It is cross-compiled for aarch64 with `cargo-zigbuild`
(`core/build-aarch64.sh`), because the Rust shipped in the Sailfish SDK is too
old for the SDK, and placed at `lib/librustrx.so`.

The rest is an ordinary Sailfish application (qmake, `rpm/`, `qml/`,
`src/`). The C++ in `src/` is the bridge from the callback to the GUI thread,
plus the list models. It is built with the Sailfish Platform SDK and installs
the core next to its data in `/usr/share/harbour-rustrx/lib`.

Background mode is the same binary started by a systemd user unit
(`systemd/harbour-rustrx.service`) without a display connection: it loads
only `qml/Service.qml` (sync, notifications, the events view line). The
session bus name is the lock between the two. When the app opens, the
stand-in hands over and quits; when the app closes, it starts the unit
again. The stand-in never has a window, because lipstick 0.37.6 crashes
when a process exits after hiding or recreating its window.

## Status

0.7.0. From "let's build a client" to 0.2.0 in one evening, used daily on
the Jolla Phone 2026 since, and developed and maintained for the long run.
Tested with several accounts at once, on matrix.org, tchncs.de and a server
of our own with an account service (OAuth 2.0 login, QR login and linking).
Not yet there: video calls, forwarding, QR verification. Push through
UnifiedPush works with [Foghorn](https://openrepos.net/content/projectmoon/foghorn)
as the distributor; the stand-in remains the fallback without one.

The road from here: 0.8 audits the code in depth and polishes the UI, 0.9
fixes only what users report, and 1.0 comes after some quiet weeks of 0.9.

## Install

RPMs are attached to the GitHub releases, for Sailfish OS 5.1 or later on
aarch64 (the core links against glibc 2.34; 5.0 and 4.6 ship older ones).
Rustrx bundles its core library, so it is not a Jolla Store app, and not a
Chum one yet: Chum builds from source and Jolla's Rust is 1.75,
matrix-rust-sdk 0.19 needs 1.96. `core/Cargo.lock` is in the tree and the core
is built with `--locked` (rustc 1.99.1, zig 0.17.1, cargo-zigbuild).

On the first start Sailfish asks once for the permissions Rustrx uses
(internet, pictures, videos, music, documents, downloads, camera, audio,
microphone, user services, secrets). User services is what lets it start
its background stand-in; secrets is where the store key is kept.

[Harpoon](https://github.com/JuhaniLehtimaeki/Harpoon) can install Rustrx
from this repository and keep it updated.

## Credits

Thanks for bug reports and ideas to doctorflynt, dodoradio, finceptum,
Fuzzillogic, JimKnopfIoT, Luna-Terra-CG, MrHappy, poetaster, projectmoon and
Tealk; dodoradio also for testing nearly every build and designing much of
Rustrx with the author, and JimKnopfIoT, whose Xmatic made the classic
WebRTC calls first and so brought them to Rustrx. The same list is on
Settings > About.

## Authorship

The program was written by an LLM (Claude). The author directed it, decided
the design and tested every build on the phone. Every commit carries an LLMGD disclosure line; the
grading scheme is described at [moWerk/llmgd-specs](https://github.com/moWerk/llmgd-specs).

## Licence

GPL-3.0-or-later. matrix-rust-sdk is Apache-2.0. The bundled Source Sans 3 Semibold
(`qml/fonts/`, by Adobe, unmodified) is under the SIL Open Font License 1.1. The emoji list
(`qml/emoji-data.js`) is generated from Unicode's emoji-test.txt, under the
[Unicode licence](https://www.unicode.org/terms_of_use.html). The emoji pictures
(`qml/twemoji/`) are [Twemoji](https://github.com/jdecked/twemoji) 17.0.3, unmodified:
graphics copyright Twitter, Inc. and other contributors and Jason Sofonia & Justine De
Caires and other contributors, licensed under
[CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/) (`qml/twemoji/LICENSE-GRAPHICS`).
