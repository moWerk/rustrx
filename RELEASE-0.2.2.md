Rustrx 0.2.2: the room's own page, and a round of cleanup from a code review.

**New**
- A page for each room, one swipe to the left in a conversation (or a tap on its header): name and topic (editable for moderators, saved on Enter and with "Save topic", never on their own), address, encryption, Favourite and Low priority, the members with Direct chat and Ignore in their menu, Invite and Leave in the pulley.
- Reactions toggle with a tap. Messages that mention you get a bar and the highlight colour. Addresses in plain messages are links.

**Fixed**
- The room's header stays at the top; with a long history it used to scroll out of view.
- "Mark as read" from the room list works (it did nothing for rooms that were not open).
- A spinner during the first seconds instead of "Nothing here yet" or "No messages yet"; "loading older" while history loads, and no endless reloading at the beginning of a room.
- A tap on an invitation opens its menu. The age labels can be translated. The time line under a message no longer runs off the screen. Errors on the settings page are not shown twice.
- The session file (it holds the login tokens) is readable only by its owner and written in one piece. The app shuts down cleanly. Room names no longer go into the system log.
- The build is reproducible: the core's lock file is in the repository.

**Install**
`harbour-rustrx-0.2.2-1.aarch64.rpm`: `pkcon install-local` as root, or Storeman from a local file. Needs Sailfish OS 5.1 or later on aarch64 (the core links against glibc 2.34). Upgrading keeps the session.

**Authorship**
Written by an LLM (Claude) after a review by five LLM reviewers, directed and tested by the author on a Jolla Phone 2026. Each commit carries an LLMGD disclosure line (github.com/moWerk/llmgd-specs).
