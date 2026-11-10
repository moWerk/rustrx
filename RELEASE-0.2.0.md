**Superseded by [0.2.1](https://github.com/moWerk/rustrx/releases/tag/v0.2.1)**, which fixes the session loss after a token rotation that this build has.

Rustrx 0.2.0, the first release: a native Matrix client for Sailfish OS on matrix-rust-sdk, the library behind Element X.

**Works**
- Log in with password; single sign-on offered where the server has it (untested so far). Everything on one page, errors inline.
- Rooms, People and Favs tabs; avatars; invitations on top, the rest by activity.
- Conversations: text (markdown), replies, reactions, removing own messages, images inline, files, stickers; images and files open through a copy in Downloads/Rustrx.
- Sending: text, images from the gallery, files, photos from the built-in camera (autofocus, preview before sending).
- Encryption: emoji verification with another client, restoring history with the recovery key, key backup. Verified with nheko.
- Drafts kept per room. Cover with unread counts and the connection state. Notifications while the app runs.
- Fast start from the cached session; native sliding sync where the server supports it, classic sync otherwise.

**Not yet**
- Notifications while the app is closed (background sync).
- QR code login (needs an OIDC server and Element as the showing device).
- Typing indicators, read receipts, room info and member pages, joining by alias, starting a chat from the app.
- Tested against one account on one homeserver only.

**Install**
`harbour-rustrx-0.2.0-1.aarch64.rpm`: `pkcon install-local` as root, or Storeman from a local file. Needs Sailfish OS 5.x on aarch64 (built against 5.1.0.11, used on 5.2.0.20).

**Authorship**
Written by an LLM (Claude), directed and tested by the author on a Jolla Phone 2026 over one evening. Each commit carries an LLMGD disclosure line (github.com/moWerk/llmgd-specs).
