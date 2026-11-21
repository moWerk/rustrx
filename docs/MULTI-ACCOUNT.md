# Several accounts (0.7)

The author's picture (2026-11-18 and 2026-11-20): a small avatar of the account shown now, left
of the main page's tabs and the size of the filter icon, switches the whole app to another
account. The Account tab of Settings switches too, and its pulley alone holds "Log in to
another account" (a line for it in the avatar's list was tapped by mistake, the author's
find).
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

## All accounts in one list (the merged view)

dodoradio's idea; off by default, under Settings, App, with several accounts. One more
`RoomLists`, display only (`setDisplayOnly`: no notifications, no invitation bookkeeping), fed
by `Rustrx::refreshMerged` with every logged-in account's last `rooms`, each room tagged with
its `account` and a `uid` ("<account>/<room_id>"), in the core's order across accounts
(`RoomLists::mergeAccounts`). While it is on, the tabs, unread totals, filter and tab settings
come from it; what is looked up by a room's id (`roomInfo`, sharing's targets, the requests to
join) stays with the shown account. A row of another account opens in its account
(`app.openIn`: the account to the front, then the room, as a notification's tap does); its
actions go to its account (`commandTo`). A room in two accounts is two rows.

"Colour-coded accounts" (on while the merged view is): each account's colour by its key
(`Tunables.accountColours`), as a leaf in the Sailfish shape (`AccountChip`) on its rooms and in
the avatar's list. The debug value `debugChipPlacement` ("corner", "bar") tries a bar along the
row's edge instead.

## Not yet

- Ordering accounts, a name per account.
- The requests-to-join notification opens the shown account's list.
- In the merged view: the requests to join and sharing's targets are the shown account's; new
  chats and rooms are made in the shown account without saying which at the top.
