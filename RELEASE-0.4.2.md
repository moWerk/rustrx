Rustrx 0.4.2: single sign-on works again, no more logouts when the network drops, and notifications for invitations. Updates 0.4.x in place.

**Fixed**
- **Single sign-on opens the browser again.** Since 0.4.0 the sign-on page was prepared but never handed to the browser (two things in the app shared a name). While Rustrx waits for the browser login, the login page now also shows the sign-on link with "Open again" and "Copy link", in case the browser does not come up by itself. Thanks to raketti and dopi04 in the forum.
- **Single sign-on shows up as soon as a full Matrix ID is typed** (@name:server.org), without leaving the field first.
- **No more logout when the network is gone for a moment.** Servers with short-lived tokens (tchncs.de, servers with an account service) need a token refresh every few minutes; a refresh that failed because the phone had no network logged you out. Now only a refusal from the server ends the session.

**New**
- **Invitations get a notification**, also while Rustrx is open or running in the background. A tap opens the list with the invitation on top.
- **Encryption:** the "not verified" line opens the settings right at the encryption section, with "Verify this device" and the recovery key field together. For an account with no other device and no recovery key, "Reset encryption" makes new keys and a new backup (older encrypted messages stay unreadable); servers with an account service ask you to approve it in the browser.
- On servers that manage logins in the browser, a hint says to use single sign-on if the password is refused (tchncs.de refused a correct password).
- More space between buttons and texts on the login and settings pages.

**Tested** by the author on a Jolla Phone 2026 and a Pro¹-X (SFOS 5.2) with accounts on matrix.org and tchncs.de: single sign-on, restoring the keys with the recovery key, the settings position. Not yet tested: the invitation notification, "Reset encryption", and the logout fix with the network actually gone.

**Install**
`harbour-rustrx-0.4.2-1.aarch64.rpm`: `pkcon install-local` as root, or Storeman from a local file. Sailfish OS 5.1 or later on aarch64. Upgrading keeps the session. No new permissions.

**Authorship**
Written by an LLM (Claude), directed and tested by the author. Each commit carries an LLMGD disclosure line (github.com/moWerk/llmgd-specs).
