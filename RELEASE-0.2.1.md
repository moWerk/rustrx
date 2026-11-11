Rustrx 0.2.1: the first day of real use on a Jolla Phone 2026, with the fixes and the features that came out of it. Supersedes [0.2.0](https://github.com/moWerk/rustrx/releases/tag/v0.2.0).

**Fixed**
- Sessions survive the homeserver's refresh-token rotation (matrix.org). 0.2.0 saved the session only at login, so the first restart after a rotation came back with "Invalid refresh token" on every request. The session is saved on every token change; when the server declares it dead the app returns to the login page and says why.
- Sync resumes by itself after a network loss (the phone's Wi-Fi drops while the screen is off); the cover no longer sits on "Connecting" while the rooms are updating.
- Direct chats show the other person's avatar instead of initials on a grey circle.
- Unread counts match other clients: the sync fetches the last 20 events per room, and the cover counts what the badges count.
- Leaving a room or removing a message works from the context menu again (the remorse callback ran after the menu was gone), and the list keeps its place: no jump to the top after leaving a room or coming back from a chat.
- "Today" and "Yesterday" are translated on the device.

**New**
- Rows tell more: the last message as a second line (sender, "Photo", "File: name", "Sticker", "Encrypted message"), a short age at the right edge (now, 1m to 59m, 1h to 23h, 1d to 364d, 1y), the unread count as a badge on the avatar, a yellow star for favourites, a lock in front of the preview in encrypted rooms. More air between rows, a slightly larger room name.
- Read receipts: "read by Alice, Bob" under the last message each person has read. Switchable in Settings > Appearance.
- Round avatars, switchable in Settings > Appearance.
- Add Room / Contact in the pulley menu: a search page over the user directory and the public room directory; a typed #alias or @user is offered as is; tapping joins or opens the direct chat.
- Replies show the quoted message above the reply (sender and up to three lines).
- A tappable line under the tabs while the device is unverified or has no recovery key; it opens Settings.
- Login page: a note that only a session token stays on the phone.

**Not yet**
- Notifications while the app is closed (background sync).
- QR code login: matrix.org does not advertise MSC4108 and its rendezvous endpoint answers 404, so there is nothing to log in against yet.
- Typing indicators, room info and member pages, filtering the room list by space.
- Tested against one account on one homeserver only.

**Install**
`harbour-rustrx-0.2.1-1.aarch64.rpm`: `pkcon install-local` as root, or Storeman from a local file. Needs Sailfish OS 5.1 or later on aarch64 (the core links against glibc 2.34; built against 5.1.0.11, used on 5.2.0.22). Upgrading from 0.2.0 keeps the session.

**Authorship**
Written by an LLM (Claude), directed and tested by the author on a Jolla Phone 2026. Each commit carries an LLMGD disclosure line (github.com/moWerk/llmgd-specs).
