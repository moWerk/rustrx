Rustrx 0.4.1: message bubbles, upload progress, a sort order for the room lists, and sharing from the gallery fixed. Updates 0.4.0 in place.

**New**
- **Message bubbles.** Own messages on the right, others on the left with the sender's name inside the bubble. No avatars on messages: Rustrx stays text first.
- **Upload progress.** A picture or file shows in the room as soon as you send it, with a bar and "uploading N %" until the server has it. Before, nothing showed until the whole upload was done.
- **Sort order.** A small filter icon at the right of the tab bar opens three orders: by activity, unread first, by name. Invitations stay on top, low priority rooms go to the bottom.
- Rustrx opens on the tab you left it on (Rooms, People, Favs). With Settings > Appearance > "Reopen the last chat" it opens the chat that was open when you closed it (off by default).
- The cover reads "Connected" over "to matrix" and its icon sits higher; the background line in the events view reads "Matrix client is running", "Tap to open Rustrx".

**Fixed**
- Sharing a picture from the gallery (or any sandboxed app) to Rustrx: the share sheet's call did not reach Rustrx, and from the background stand-in the "Send to" page was lost while the app opened.

**Not yet**
- "Reopen the last chat" is new and has had the least testing.
- Calls are being looked into on the `calls-spike` branch (Element Call in a WebView), not in this release.
- Tested by the author on a Jolla Phone 2026 (5.2) with one account; tests with a second account and homeserver are next.

**Install**
`harbour-rustrx-0.4.1-1.aarch64.rpm`: `pkcon install-local` as root, or Storeman from a local file. Sailfish OS 5.1 or later on aarch64. Upgrading keeps the session. No new permissions.

**Authorship**
Written by an LLM (Claude), directed and tested by the author on a Jolla Phone 2026. Each commit carries an LLMGD disclosure line (github.com/moWerk/llmgd-specs).
