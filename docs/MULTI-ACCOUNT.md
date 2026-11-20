# Several accounts (0.7)

The author's picture (2026-11-18 and 2026-11-20): a small avatar of the account shown now, left
of the main page's tabs and the size of the filter icon, switches the whole app to another
account. The Account tab of Settings switches too, and holds "Log in to another account".
Notifications and unread counts come for every account at once; a tap on one shows its
account. As many accounts as anyone wants.

## How it is built

- **One core per account, all running.** librustrx keeps a list of cores (`rustrx_start`
  returns each one's number, `rustrx_command_to` addresses one). Every core has its own
  data folder, store, session and sync. One Secrets key serves every store.
- **Folders, no migration.** Account 0 is the data folder as it always was; nothing of an
  existing login moves. Further accounts live in `accounts/<n>/` inside it (n = 1, 2, ...,
  never reused while the folder exists). An account folder without a session at start is
  left over from a logout or a login that was given up, and goes.
- **The app shows one account.** `Rustrx` keeps an `Account` per core (its room lists,
  login and encryption state, drafts) and the properties QML reads answer for the shown
  one. Events of the others update their own lists and state and notify, nothing else.
  Switching closes the open room and tells QML that everything changed.
- **Notifications** carry the account: their actions call `openAccountRoom`,
  `markAccountRoomRead`, `replyFromAccountNotification`, `reactFromAccountNotification`
  with the account's number first. The older actions stay for account 0 (lines posted by an
  older Rustrx). The handover from the stand-in carries the account with the room.
- **Push** (UnifiedPush): every account registers with the distributor under its own token
  and sets its own pusher; `push.json` lives in its folder. The one connector hands each
  call to the instance whose token it carries, and the woken process fetches the message
  with that account's core. The switch ("when Rustrx is closed") is one for all accounts;
  the stand-in stays away only while every account's push works.
- **Calls** (classic): the account a call rings on owns the call; the app switches to it
  when the call is answered. No switching while a call runs.

## Not yet

- Ordering accounts, a name per account.
- The requests-to-join notification opens the shown account's list.
