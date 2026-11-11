Rustrx 0.4.0: notifications while the app is closed, voice messages, the share sheet, and most of what was missing for daily use. Supersedes [0.2.1](https://github.com/moWerk/rustrx/releases/tag/v0.2.1); 0.2.2 and 0.3 were never published on their own, their changes are in here.

**Background mode**
- Settings > Notifications > "Keep running in the background", off by default. With it on, closing Rustrx ends the app as usual, and a stand-in without a window takes over: it stays connected, notifies, starts with the phone, and shows a quiet line in the events view, "Rustrx matrix client is running in the background". Tap it (or the app icon) to open Rustrx, or quit it there.
- Notifications arrive within seconds even while the phone sleeps: the open connection wakes it. As a fallback the stand-in also wakes every five minutes in the system's shared slot.
- Switched off, nothing runs once Rustrx is closed.

**New**
- Notifications: one per room, updated in place, with the last message (or only a count, see Settings > Privacy). A tap opens the room; opening the room closes its notification.
- Voice messages: "Record a voice message" in the room's push-up menu (Ogg/Opus). Audio messages play in place, videos open on their own page.
- Polls show their answers and votes; a tap votes. Shared locations open in a maps app (Pure Maps takes them).
- The share sheet: "Send to a room" from the gallery, the browser and other apps. matrix: links and matrix.to links open in Rustrx: a room you are in opens, others are joined, a person gets a direct chat.
- Cover actions: search, and the most recent room with unread messages.
- Messages: edit your own, report, send again or discard a failed one, the six reactions at the top of the menu, copy a link. The header shows who is typing. A note under messages from unverified senders.
- Pictures go out upright, with a preview for other clients, and scaled down above 2048 px unless "Send smaller pictures" is off. The text in the composer becomes the caption of a single picture; captions show under pictures, videos and files. Pictures open in a viewer (pinch, double tap, save to Downloads).
- Rooms: a page for each room (swipe left in a conversation) with name, topic, address, encryption, favourite and low priority, members (direct chat, ignore), invite and leave; notifications per room (default, all, mentions, muted); create a room from Add Room / Contact.
- Settings: Privacy (read receipts, typing, message text in notifications), Storage (sizes, clear pictures and files), Room keys (export and import to a passphrase file).
- Login page: "Create an account" opens the server's sign-up page in the browser.

**Fixed**
- Reading back through history keeps its place; a new message or receipt no longer jumps to the bottom.
- Logout clears everything that belonged to the account.
- The unread badge stays a circle for one digit and grows to the left.
- "Mark as read" from the room list works; the room's header stays at the top; the session file is readable only by its owner; room names no longer go into the system log.
- The RPM has a changelog.

**Not yet**
- QR code login and login through the server's account page (OIDC).
- Spaces, threads, pinned messages, forwarding, mention completion, QR verification.
- Push through UnifiedPush (Foghorn) as a lighter alternative to the stand-in.
- Tested against one account on one homeserver only; voice messages and pictures have not been checked from a second account yet.

**Install**
`harbour-rustrx-0.4.0-1.aarch64.rpm`: `pkcon install-local` as root, or Storeman from a local file. Needs Sailfish OS 5.1 or later on aarch64 (the core links against glibc 2.34; used on 5.2.0.22). Upgrading keeps the session. Rustrx asks for new permissions (Audio, Microphone, Videos, Music, User services) once on the first start: User services lets it start its stand-in.

**Authorship**
Written by an LLM (Claude), directed and tested by the author on a Jolla Phone 2026. Each commit carries an LLMGD disclosure line (github.com/moWerk/llmgd-specs).
