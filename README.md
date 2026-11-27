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
  inline, nothing thrown away. A server over plain http is taken only on
  the phone itself or the home network, and a browser login that ends in
  another account than the one typed is undone. "Create an account" makes one on the
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
- "Add room or contact" in the pulley menu searches users and public rooms,
  takes a #alias or @user directly, keeps the ones you opened as Recent, and
  creates rooms and direct chats. A room opens on its join page first (what
  it is, Join or Ask to join), a person is asked about before a chat starts.
- A line under the tabs reminds you while this phone is unverified or has
  no recovery key, or when notifications are off for the account on its
  server.
- Conversations with text in markdown, replies that quote what they answer,
  spoilers (covered until tapped; `||text||` sends one), reactions, images, videos, audio, files, stickers, polls, locations and
  state events. A long press shows reactions and the four usual actions,
  the rest on a "More…" page: your own messages can be edited or removed;
  messages can be forwarded to another room, reported, copied as a link or
  shown as source, and a running upload can be cancelled. A message that
  did not go out says so in red; a tap sends it again. A double tap on a
  text replies. GIFs move while on screen (Settings > App: play, loop). Messages sit in bubbles, without avatars:
  the sender's name is inside. The header shows the room's picture and who
  is typing, "read by ..." shows read receipts. Drafts are kept per room
  and per thread, and an unsaved edit comes back when the room is opened
  again.
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
  screen dark at the ear. A ringing call opens the call page full screen,
  as in the phone app: with Rustrx closed, behind other apps and over the
  lock screen, where it is answered without unlocking. It is answered with
  a swipe, from its notification (also with Rustrx closed, and with push)
  or from the cover; Settings > App > Behaviour > "Force Call UI in front
  on incoming calls" turns the full-screen page off. Calls ring only in
  rooms of two; in a group a call is a quiet notification with Answer and
  Decline. A call nobody answered leaves one "Missed call" notification. A
  second call is told you are busy. By default only through the server's
  relay, so the other side never sees the phone's address. How the
  earpiece works on Sailfish OS: docs/CALLS.md.
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
  apps, with a look at where it goes and what is sent before it goes.
  `matrix:` and matrix.to links open in Rustrx; a room link from another
  app waits for "Look up" before the server is asked.
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
  Settings (tested with nheko); another person's request comes as a
  notification first and opens on a tap; restoring encrypted history with the
  recovery key; key backup; room key export and import.
- Settings in four tabs. Account: one's own picture, a security ring that
  turns green once the store is encrypted, the device verified, a recovery
  key set and the other devices verified, with each gap named; the
  logged-in devices, storage and the debug log. Privacy: read receipts,
  typing, showing as online (off by default), what pictures and videos
  leave without (nothing, the location, or all metadata but which way up
  they are, the default; JPEG, PNG and MP4), calls only through a relay
  (on by default), message text and chat names (and who calls) outside
  Rustrx, invitations from strangers, ignored users; these live in
  Rustrx's own settings file. App: appearance (avatar corners from square
  to round, preview lines, the tabs), behaviour (Force Call UI in front on
  incoming calls), notifications.
  About. Portrait and landscape, or locked to portrait.
- The local store (messages and keys) is encrypted with a key kept in
  sailfish-secrets where that works. On community ports without it Rustrx
  still runs, unencrypted, and says so once after login, with the command
  that installs Secrets. New installs get a Secrets collection of their
  own; if Secrets refuses Rustrx, the Account tab says so, and a missing
  key asks before starting over. If a server ends the session, the store
  and the device are kept, so logging in again brings back keys and
  history; only a sign-out the server makes final (a classic server
  without refresh tokens) wipes the messages and media on the phone.
- A debug log, on until 1.0 and off with a switch: if something goes wrong,
  Settings > Debug log hands it to the share sheet, so nobody has to make
  it happen again; no copy goes to Downloads. It holds room and user IDs,
  never message texts or passwords, and URL queries, tokens and server
  bodies are cut from every line. During a call it also holds Element
  Call's own warnings, with query strings and long ids cut out.
- The cover shows unread counts and the connection state, with actions for
  search and the latest room with unread messages; during a call, the call.

## What 0.8.4 tackled

Calls:

- An incoming call opens the call page full screen with Answer and
  Decline, as the phone app does: with Rustrx closed, behind other apps,
  and over the lock screen, in a window of its own that is answered
  without unlocking and shows nothing else of Rustrx.
- Settings > App > Behaviour > "Force Call UI in front on incoming calls"
  (on by default) turns that off: a ring is then its notification only.
- Calls ring with Rustrx closed on phones with a push distributor
  (Foghorn). Before, such a call came only as a message line. The caller's
  hangup ends the ring in encrypted rooms.
- A call nobody answered leaves one "Missed call" notification, as the
  phone app's, whose tap opens the room, and no message line for it.
- The ring lasts as long as the invite is still valid, not a flat minute,
  and the caller waits 10 s past the invite's lifetime, so an answer that
  arrives late on two slow servers still connects.
- Calls ring only in rooms of two. In a group, a call is a quiet
  notification with Answer and Decline, with no ringtone and no call
  page.
- Calls go through the server's relay by default (Settings > Privacy).
- An Answer on a ring that is already over opens the room and starts
  nothing.
- "Call again" shows only for a call placed on this phone, never over the
  lock screen, and a call placed here that failed keeps "Call again" and
  "Close".
- A call no longer hangs in "Connecting" when the audio could not start
  after the wait for the relay's credentials.
- On phones whose proximity sensor reads "near" at once (the C2), the
  call page goes dark at the ear only after the sensor has said "far"
  once in that call.
- Only the invitee's answer and candidates count before a call is
  answered; the other side's SDP is capped at 64 KB, its candidates at
  200 a call, and only its first Opus audio stream is played.
- The log names the ICE candidate types and the pair in use, never an
  address.

Background and push:

- The background stand-in no longer starts without a saved login, and a
  tap on the icon while it leaves opens the app instead of nothing.
- Answer and Decline on a call notification work with Rustrx closed.
- Reply, emoji and Mark as read on a notification work with push on but
  no distributor installed.
- A ring the stand-in posted and the app took over ends with the call,
  ringtone included.
- A stand-in that finds Rustrx already running leaves at once instead of
  waiting 10 s.
- `systemctl --user stop` and the session's end run the normal quit, so
  the session is saved and the stores are closed.
- The stand-in's Quit is taken only while its background line is up.

Security, from our own audit (the record is in
[docs/audits](docs/audits/README.md), and the
[security review guide](security-review-guide.md) is updated from it):

- Notification actions (Reply, emoji, Mark as read, Answer, Decline) are
  taken only from a process of the same user and only for a notification
  Rustrx has up.
- Reply always goes to Rustrx's own name. Another program holding
  Rustrx's push name must prove it is Rustrx; if it cannot, push pauses,
  the background stand-in takes over, and Settings shows it in red.
- A second launch hands the stand-in over with a proof of a secret, not
  a plain call; a push-woken Rustrx that finds its name held by another
  program says "Rustrx could not start", once per boot.
- The push distributor is matched by its exact name, and a new one needs
  your yes once ("Use %1 for push?").
- Plain http servers are taken only on the phone itself and the home
  network, whether typed, found through `.well-known` or in a QR code.
- Link a new device opens the new device's consent page only on your
  account service's own host.
- A browser login (single sign-on, OAuth) that ends in another account
  than the one typed is logged out and refused.
- Another person's verification request comes as a notification first,
  naming their user ID, and opens on a tap.
- New installs keep the store key in a Sailfish Secrets collection with
  a name of their own; a refusal by Secrets has its own state on the
  Account tab.
- No new store key is made while Secrets' answers contradict each other;
  with an encrypted store and no key, Rustrx asks before starting over,
  and the old stores are moved aside, never deleted.
- `session.json` is removed only when the key really cannot open it.
- A sign-out that the server makes final wipes the messages and media on
  the phone; every other kind keeps them for a login again.
- Element Call gets only the capabilities a call needs, not everything it
  asks for.
- Pictures are decoded only as JPEG, PNG, GIF or WebP, by their bytes,
  each by its own decoder; anything else is a file.
- A picture larger than 40 megapixels is not decoded at all, and
  notification icons render at 256 px at most.
- Received audio and video that is a playlist fetches nothing.
- A message that would draw more than 200 000 characters (deeply nested
  lists did) shows "Message too long to show".
- Message HTML renders about twenty times faster on long input.
- Custom emoji: at most 64 per message, four downloads at a time, none
  above 256 KiB.
- A picture larger than 20 MB by its message shows "Large file, tap to
  load", and the media folder is swept again after every 50 MB written.
- Files open only when name and content agree on a safe kind; the rest is
  saved, with its name cleaned of bidi and zero-width characters.
- A link whose text names another address than it opens asks first.
- A room link from another app waits for "Look up".
- People who share a display name show their user ID beside it, in the
  chat, quotes, the room list and notifications.
- Forwarding a file from an encrypted room into an unencrypted one
  uploads a fresh copy, so the file's key stays out.
- Collected logs go to the share sheet from Rustrx's own cache, with no
  copy in Downloads.
- Key exports are written privately and copied without following a link;
  a second export on the same day gets a new name.
- Privacy settings (receipts, typing, presence, notification text,
  invitations from strangers, the last room, search history) moved out of
  dconf into Rustrx's own settings file; the debug switches act only on
  a test setup.
- URL queries, tokens, server error bodies and command values are cut
  from every log line, core and app.
- Shared files each go into a folder of their own, written new, under one
  size cap.
- The QR scanner no longer crashes the app on some noisy camera frames.
- Cache files are named by a hash of their ids, so two rooms or emoji can
  never share one.
- A file saved to Downloads cannot be redirected through a planted link.
- Sent voice messages and prepared uploads are swept from the cache.
- The password field is emptied after a login.
- `url` 2.5.9.

Chats:

- Quick reactions show only emoji and short text, and nothing runs over
  the buttons beside them (thanks dodoradio).
- Closing the pulled-up menu in a chat no longer makes the chat bounce,
  and reaching the bottom no longer loads older history (thanks
  projectmoon).
- Jumping to the first unread message shows the small loading indicator
  at the top of the chat, not a large one over the messages (thanks
  projectmoon).

## How it is built

[review-and-architecture-hints.md](review-and-architecture-hints.md) is the map for reviewers.
[security-review-guide.md](security-review-guide.md) is where a security review starts.

`core/` is a Rust crate that wraps matrix-rust-sdk behind eight C functions
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

0.8.4. From "let's build a client" to 0.2.0 in one evening, used daily on
the Jolla Phone 2026 since, and developed and maintained for the long run.
Tested with several accounts at once, on matrix.org, tchncs.de and a server
of our own with an account service (OAuth 2.0 login, QR login and linking).
Not yet there: video calls, QR verification. Push through
UnifiedPush works with [Foghorn](https://openrepos.net/content/projectmoon/foghorn)
as the distributor; the stand-in remains the fallback without one.

The road from here: the 0.8.x releases carry the audits (the first UI
audit in 0.8.0, performance in 0.8.2, security in 0.8.4), the polish and
fixes for what users report. 0.9 then fixes only what users report, and 1.0 comes after
some quiet weeks of 0.9.

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

Thanks for bug reports and ideas to davodego, doctorflynt, dodoradio,
finceptum, Fuzzillogic, JimKnopfIoT, lowxorx, Luna-Terra-CG, MrHappy, poetaster,
projectmoon and Tealk; dodoradio also for testing nearly every build and designing
much of Rustrx with the author, JimKnopfIoT, whose Xmatic made the classic
WebRTC calls first and so brought them to Rustrx, and lowxorx, whose questions
led to the security review guide. The same list is on
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
