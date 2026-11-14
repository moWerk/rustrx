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
- An empty tab (no rooms, no favourites yet) says so and how to add one, instead of a spinner.

**Room list**
- **Search:** in the filter menu, below the switches. From the second letter the lists narrow to matching rooms while you type; the check mark keeps the search (a cross removes it again), otherwise opening a room or closing the menu drops it.
- **At a glance**, in front of the last message: a stop sign for muted rooms, the lock for encrypted ones, and a check when your own last message was read.
- **Message preview:** 1, 2 or 3 lines of the last message (Settings, Appearance).
- **Your layout:** "Separate people from rooms" and "Separate favourites" (both on by default). Turned off, everything shares one "Chats" list, with or without the Favs tab.
- An upgraded room says so, and a tap takes you to the new one.
- **Double tap a message to reply.**

**Also new**
- **Room pictures:** set one from the room page (swipe left in a chat, pulley, "Change the room's picture"; for moderators). The room page shows it large, the chat header small beside the name. Pictures changed by others now update in the list too (before, a picture was fetched once and never again).
- **View source:** long press a message for its JSON as received, and its latest edit's.
- "Unread first" sorts by the number of unread messages.
- Matrix ID fields (login, invite) start with the "@" already.
- Copy text and Copy link confirm; "Save to Downloads" in a message's menu saves without opening another app; a reply or an edit can be cancelled with a visible button; error messages name the action instead of an internal command.
- Settings in a new order: Privacy first, devices listed in place, and an About section with the app's LLMGD grade.
- Declining an invitation has a remorse timer; a room's header says "Connecting" while it connects.

**Polish**
- Avatars 10 % smaller, names and badges where they were. The unread badge digits are in Source Sans 3 Semibold (bundled; the system font has no heavier cut installed, so "bold" never did anything), on a slightly darker shade of the ambience colour, with a soft shadow under the pill. The badge still grows to show the full count.
- The filter icon is larger and sits centred on the tabs, also on phones with a camera cutout. Its menu has the three orders as a choice of one (by activity, unread first, alphabetical) and two switches that work with any of them: **Favourites on top** (off by default) and **Low priority at the bottom** (on, as before). The list re-sorts while the menu is open.
- A list no longer starts with its first row hidden under the tabs.
- The cover: the app icon is larger, its bottom lined up with the cover buttons, its fibres behind the lines of text.

**Tested** by the author on a Jolla Phone 2026 and a Pro¹-X (SFOS 5.2) with accounts on matrix.org and tchncs.de: single sign-on, restoring the keys with the recovery key, the invitation notification, everything in the room list. Not yet tested: "Reset encryption".

**Known:** on servers that rotate the login token (tchncs.de and others with an account service), a refresh whose answer gets lost on a bad connection can still end the session; Rustrx then also clears its local data, so you log in again and enter the recovery key. Keeping the data and the device for that case comes first in 0.4.3.

**Install**
`harbour-rustrx-0.4.2-1.aarch64.rpm`: `pkcon install-local` as root, or Storeman from a local file. Sailfish OS 5.1 or later on aarch64. Upgrading keeps the session. No new permissions.

**Authorship**
Written by an LLM (Claude), directed and tested by the author. Each commit carries an LLMGD disclosure line (github.com/moWerk/llmgd-specs).
