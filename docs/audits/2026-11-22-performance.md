# Performance and memory, 2026-11-22 (0.8.2)

## Why

The author asked on 11-22 for startup speed and scroll speed, and for all
of the runtime behaviour to be investigated and optimised as far as
possible. The last profiling was many large changes old. The base was
0789a74 (build 0.8.1-15, on both phones).

## Scope

Everything the app does at run time: the start from the tap to a filled
room list, the room list (scrolling, updates, the favourite glow),
opening a room and scrolling a chat, the core and the bridge between the
core and C++, memory, pictures and media on disk, and idle cost.
Security was not in scope; what turned up along the way is listed below.

## Method

All agents were LLM agents working under the author's direction. This is
not an independent review.

- Five agents read the tree, none of them editing: startup, room list,
  timeline, core and bridge, memory and images. Each ranked its findings
  by what the user sees and marked its guesses.
- One agent measured on the Jolla Phone 2026 (jp26: hybris, Mali, 90 Hz,
  71 rooms on the shown account) and the Pro¹-X on mainline (the Prawn:
  SM6115, freedreno, 60 Hz, 8 rooms on the shown account). A script on
  the phone started Rustrx through D-Bus as a tap does, read
  `/proc/<pid>` (CPU ticks and context switches per thread, smaps), drove
  the UI with the dev hook and with touch events written into the
  touchscreen node, and read frame times from `RUSTRX_FRAMES=1` and
  `QSG_RENDER_TIMING=1`. Three cold and three warm starts per phone,
  three repetitions of each scroll, idle and memory phase.
- Before the fixes, timing markers went in (3b356b7): one clock from
  `main()`, `perf:` lines for each startup step, each room open and each
  rooms and timeline update. They stay for later audits
  (review-and-architecture-hints.md, "Timing markers").
- The findings were merged where several reports found the same cost
  (the table below) and fixed by agents that each owned a set of files,
  their patches merged by hand. That was 0.8.2-1 (65fd3d6).
- A second measurement round ran on 0.8.2-1, the same scripts and rooms.
  Its findings went into a second set of fixes, 0.8.2-2 (662ea8e).
- A third round ran on 0.8.2-4 (7cded38, with the regression fixes) in
  the night to 11-23, the same scripts again, the jp26 unlocked and in
  front throughout.

Limits. The jp26 sat behind its lock code for most of rounds one and
two, and no agent may enter it: there Rustrx is inactive, so the glow,
list scrolling and chat scrolling by touch could not be measured on it
then, and its round two startup and memory numbers were taken behind the
lock. The Prawn's 8 rooms fit its screen, so neither phone measured room
list scrolling with a long list. GPU memory does not show in the
process's RSS on either phone and was not measured.

Round three had its own limits. On the jp26 the room list was on the
People tab during the list scroll phases (changed by something outside
the scripts), so those rows are not like for like with round one's Rooms
tab; only the chat rows are. On the Prawn another session sent input
for a few minutes: the startups in that window were dropped, and the
memory run and further idle repetitions were not run. The jp26's room
open times of round three fall inside the C1 window (below) and are not
comparable.

Ids. The code reports' ids are kept (ST startup, RL room list, T
timeline, CB core and bridge, MI memory and images). The measurement
reports reused some of those letters, so their ids carry the round:
`m1.` for 0.8.1-15, `m2.` for 0.8.2-1, `m3.` for 0.8.2-4. Regressions
are REG, security finds SEC, the author's decisions DEC.

## What was found

The large costs, merged:

| Merged | Ids | Where it went |
|---|---|---|
| Whole timeline resent per change, compared in depth | CB1, T6 | 65fd3d6 |
| PBKDF2 at every store open | CB3, ST5 | 662ea8e, after the author's yes |
| Extra crypto store open at every start | CB7 | 65fd3d6 |
| First room list read one room at a time | CB4, ST9 | 65fd3d6 |
| Whole room list every batch, rebuilt after every quiet minute, six model diffs | CB5, CB6, ST12, RL2, m2.RL2 | 65fd3d6, deltas in 662ea8e |
| Every role of a row on every new message | RL3 | 65fd3d6 |
| Rows inserted and moved one by one | ST13, RL6, RL7, ST11 | 65fd3d6, partly |
| Invitation scan, latest-event listeners, avatar downloads in the list task | ST10, ST15, CB9 | 65fd3d6 |
| Pictures rewritten, videos fetched again | CB8, MI3, T9 | 65fd3d6 |
| Media stored twice, no limit | MI4, m1.D1, m2.D1b | 65fd3d6 and 662ea8e, partly |
| Emoji packs: the room's whole state on every open | CB2, m2.P2 | 65fd3d6, kept on disk in 662ea8e |
| Info page and member list on every room open | MI1, T1 | 65fd3d6 |
| Emoji panel built hidden with every room page | MI2, T2 | 65fd3d6 |
| An offscreen layer per rounded avatar and bare picture | RL1, MI7, T10 | open |
| The favourite glow drawing every frame | RL4, m1.I1, m2.I1b | 65fd3d6, 662ea8e, 3d7eb04 |
| Message row weight, state rows build the bubble | T5 | 65fd3d6 |
| Older messages only at the very top | T3 | 65fd3d6 |
| Unread seek, first snapshot waits on receipts, relations per open | T4, T7, T8 | 65fd3d6 |
| Full-screen photo and GIFs at full size | MI6, T15, MI8 | 65fd3d6, partly |
| A media player per audio row | MI5 | 65fd3d6 |
| Main page builds hidden parts, only after logged_in | ST2, ST3, ST4, m1.T1 | 65fd3d6 |
| gst_init, proximity sensor, D-Bus and file work before the window | ST6, ST7, ST8, ST14, MI10 | 65fd3d6 |
| Startup not attributable | ST1, m1.T2 | 3b356b7 |
| Release profile | CB10 | open |

Found only by measuring:

- **The favourite glow (m1.I1).** With a starred unread room on screen
  and Rustrx in front, the window rendered at 90 fps without pause: 55 %
  of one core, 910 wakeups per second, on the jp26 from the first frame
  on. It also hid in every startup and scroll number there.
- **Mesa's shader cache (m1.P1).** On the Prawn the sandbox's `~/.cache`
  is a tmpfs, so the cache was empty at every start and the first content
  frame took 520 to 610 ms to render. A test with the cache in the app's
  own cache folder took 222 to 289 ms.
- **The room list after a change (m2.RL2).** On 0.8.2-1 one changed room
  still sent the whole list, 61.7 KB, 6 to 25 ms of GUI thread on the
  jp26.
- **Emoji packs after a restart (m2.P2).** The busy room fetched its
  room state again (about 0.9 MB) at its first open after every start:
  up to 8.4 s on the Prawn.
- **A runaway after a reopen (m3.C1, new in round three).** At one start
  on the jp26 Rustrx reopened a long encrypted DM as the last room; its
  timeline grew from 50 to 798 items in 10 s without script input, the
  core thread ran at about 100 % of one core for about 5.5 minutes, and
  the run's peak RSS was 428 MB (other runs 296 to 345 MB). It did not
  happen at about 20 other starts of the round. Unexplained so far.

## What was done

- **3b356b7**: the timing markers (ST1).
- **65fd3d6** (0.8.2-1), in six parts by file owner. Room list: the
  first list built several rooms at once, nothing sent when no row
  changed, a full rebuild only every 10 minutes or after a lagged
  stream, avatars beside the list, ranges and one insert for the first
  fill. Timeline: protocol 3, the order plus only new or changed items,
  the first snapshot before reader names, the first page sized by the
  unread count, an event-to-row index. Start: the main page first when a
  session is saved, GStreamer with the first call, the proximity sensor
  only in a call, emoji names and the push distributor off the GUI
  thread, the to-device token check once per store, Mesa's shader cache
  in the app's cache folder. Chats: the member list on the info page's
  first swipe, the emoji panel when first opened, lighter message rows,
  older messages two screens before the top, a player on the first tap,
  a full-screen photo decoded to the screen. Room list rows: the glow
  only for rows on screen in the shown tab, the badge only with something
  unread, the binding loop at start gone, a switch for the avatar A/B.
  Media: files already on disk served without the SDK or a rewrite,
  `media/` kept under 300 MB, packs from the local store, Clear media
  brings pictures back, room permissions per account, frequent commands
  logged only when they fail.
- **662ea8e** (0.8.2-2): the room list as its order plus the changed
  rooms (`rooms_resync` when the UI lost track); emoji packs kept in the
  account's store; the stores opened with the store key on the SDK's
  high-entropy path instead of PBKDF2; the SDK media cache at 100 MB and
  Clear media emptying it, uploads kept; the glow resting after its
  breaths; GIFs paused while a chat moves; two binding loops on the room
  page gone.

## Measured, before and after

Only measured numbers, medians, by build and phone: 0.8.1-15 is round
one, 0.8.2-1 round two, 0.8.2-4 round three. On the jp26, round one ran
in front with the glow running, round two behind the lock (no glow, no
pictures drawn), round three in front. The startup rows are round
three's own table, which recomputed all three builds the same way (on
the Prawn the clean runs of the QSG set, cold n=3, warm n=2), so a few
earlier figures differ slightly from round two's text.

Startup, ms from the tap:

| | 0.8.1-15 | 0.8.2-1 | 0.8.2-4 |
|---|---|---|---|
| jp26, rooms shown, cold / warm | 2510 / 1581 | 2003 / 1247 | 2042 / 1164 |
| jp26, first content frame, cold / warm | 2466 / 1635 | 2190 / 1448 | 2124 / 1398 |
| jp26, `client built` per account, cold / warm | 324 / 221 | 228 / 172 | 115 / 67 |
| jp26, last frame after the tap | never stops | 3 s (locked) | 18.4 to 19.9 s |
| Prawn, rooms shown, cold / warm | 4102 / 3830 | 3851 / 3504 | 3693 / 3366 |
| Prawn, first content frame, cold / warm | 4776 / 4512 | 4173 / 3937 | 4129 / 3733 |
| Prawn, render time of that frame, cold / warm | 530 / 558 | 237 / 259 | 312 / 284 |
| Prawn, `client built` per account, cold / warm | 511 / 479 | 399 / 377 | 151 / 164 |
| Prawn, CPU in the first 10 s, cold (ticks) | 685 | 540 | 477 |
| Prawn, of it the core thread, cold | 209 | 193 | 113 |

- The key-based stores (CB3) build the client two to three times faster
  on both phones. On the Prawn the core uses 41 % less CPU in the first
  10 s; on the jp26 (71 rooms) the core's share is unchanged.
- The page build on the Prawn (about 600 ms from `qml_completed` to
  `mainpage`) is unchanged and is still the largest block.

The favourite glow, jp26, starred unread rows on the first screen:

| | 0.8.1-15 | 0.8.2-1 | 0.8.2-4 |
|---|---|---|---|
| list in front, steady state | 55 % of one core, 90 fps, 910 wakeups/s | 42 %, 90 fps, 930 wakeups/s | 0.6 %, 1 frame a minute, 8 wakeups/s |
| after a start, or the tab shown again | never stops | never stops | 16.3 to 16.4 s at 90 fps, then rest |

Each return to the list starts a breath again: about 350 frames, roughly
6 core-seconds on the jp26 (by design; m3.G2).

Room list updates, jp26 (71 rooms), one changed room:

| | 0.8.2-1 | 0.8.2-4 |
|---|---|---|
| bytes per event | 61.7 KB (the whole list) | 3.4 KB |
| parse on the core thread | 4 to 4.5 ms | 0.6 ms |
| GUI thread, lists plus merged view | about 6 to 25 ms | about 12 ms; 4 of 32 events at 66 to 73 ms |

In 10 minutes on the list, 33 events: one full (the 10-minute sweep),
the rest one room each. A check of the app's list against a full refresh
found no difference on either phone.

Emoji packs, the busy room's first open after a restart:

| | 0.8.2-1 | 0.8.2-4 |
|---|---|---|
| jp26 | 1 to 3 requests, about 0.9 MB, 0.4 to 2.6 s | from the store, 1 request of 76 bytes, 54 ms |
| Prawn | 0.8 s and 8.4 s | from the store, 2 requests of 0 bytes, 163 ms |

Scrolling. The Prawn's chat (three builds, phase CPU and frame rate):

| phase | 0.8.1-15 | 0.8.2-1 | 0.8.2-4 |
|---|---|---|---|
| 12 flicks older | 34.9 %, 19 fps | 72.1 %, 34 fps | 73.8 %, 34 fps |
| 8 flicks newer | 100.4 %, 35 fps | 57.0 %, 34 fps | 60.3 %, 34 fps |
| slow drags | 42.9 %, 32 fps | 30.3 %, 33 fps | 33.5 %, 32 fps |

- "12 flicks older" is not like for like with round one: then the test
  account's history ended at its join after a few flicks and the list
  stood still; since 0.8.2-1 older history loads and the list moves the
  whole phase. "8 flicks newer" is: the core thread went from 390 ticks
  to 1, and stays near 0 on 0.8.2-4.
- The jp26's chat, the first touch run since round one: 12 flicks older
  at 87 fps (was 42, with more history loading now), frames over 20 ms
  from about 3 % to 0.4 %, none over 34 ms. 8 flicks newer: core thread
  from 200 ticks to about 0.

Memory, MB (median of 3):

| | 0.8.1-15 | 0.8.2-1 | 0.8.2-4 |
|---|---|---|---|
| jp26, RSS after 15 rooms | 271 | 246 | 247 |
| jp26, RSS back on the list | 278 | 247 | 249 |
| jp26, peak (VmHWM) | 362 | 322 | 329 |
| Prawn, RSS after 7 rooms | 226 | 205 | not run |
| Prawn, peak | 251 | 238 | not run |

On the jp26 round three draws pictures in front and still matches round
two within 2 to 7 MB. Per room opened, jp26: 2.2 MB (0.8.1-15), 1.7 MB
(0.8.2-1).

Disk, jp26 (MiB, as round three measured all three):

| | 0.8.1-15 | 0.8.2-1 | 0.8.2-4 |
|---|---|---|---|
| data folder | 856 | 796 | 577 |
| SDK media store | 332.4 | 332.4 | 98.0 |
| `media/` | 308 | 247 | 247 |
| SDK event cache | 151.8 | 156.5 | 171.1 |

- `get_media`: every call after a restart came from disk in rounds two
  and three (`cached=true`), 0 to 4 ms in round two.
- Packs: a second open in the same process asks the server nothing
  (`requests=0`), both phones, both rounds.
- Warnings on 0.8.2-4: the badge TypeError and the footer height loop
  are gone (0); the `_contentDeficit` loop is still there, about once
  per room open. No panics, no resyncs.
- Decryption after the store migration works on both phones: on the
  Prawn the pictures that arrived in an encrypted DM after the install
  show decrypted, and on the jp26 a new encrypted text message showed as
  text. No decryption warning in either log. No test message was sent.

## Decided by the author

- **DEC1, the glow.** It breathes five times when a glowing favourite
  comes into sight and then rests in the lowest 5 % of a breath, until
  something new starts it (3d7eb04).
- **DEC2, GIFs.** They pause while a chat moves (662ea8e).
- **DEC3, the store key.** Approved "if no negative security
  implications": security and going back to an older build were checked
  first, then it ran on the Prawn's test account before the jp26. Each
  database's cipher is rewrapped once; the data keys stay. A build before
  0.8.2 cannot open migrated stores (Retry, nothing deleted), so the
  release notes must say that going back needs a new login. Decryption
  after the migration was checked in round three and works on both
  phones.
- **DEC4, the SDK media cache.** Capped at 100 MB (662ea8e).

## Regressions found and fixed

- **REG1.** The badge's width binding read a row that was already gone:
  a TypeError each time a room was read (found by round two; 71fe101;
  none on 0.8.2-4).
- **REG2.** Inside the badge's Loader `item` named the Loader's own item,
  so a badge with two or more digits stayed a circle instead of a pill
  (the author's find; 34ce7e2).
- **REG3.** In 662ea8e the glow rested halfway up a breath and looked
  frozen (the author's find; 3d7eb04).
- **REG4.** The new room list test needs a QML engine; the CI runner had
  only qtbase, so every push since 65fd3d6 failed there (qtdeclarative5-dev
  added, 7cded38).

## Security, found while writing security-review-guide.md

- **SEC1.** A recovery key reset and an identity reset sent the new key
  as `key`, a name from before protocol 2: the dialog showed an empty key
  and could be closed as saved. Fixed in 239ada1, pinned by a test in
  18ecd5e.
- **SEC2.** "Send smaller videos" passed the video's path into a
  gst-launch-1.0 pipeline description, which is parsed again: a file name
  chosen elsewhere could add to the pipeline. The video now goes in by a
  link with a name Rustrx makes (239ada1). The feature was not released
  yet.
- **SEC3.** A store was deleted when a fingerprint of the key from Rust's
  `DefaultHasher` did not match, and that hash is not stable across
  toolchains. Now a stable fingerprint is kept and a store is deleted only
  when opening it proves the key wrong (662ea8e).

## Open

- **m3.C1**, the runaway after a reopen (above): being investigated. To
  reproduce, reopen a long encrypted DM as the last room with perf-log on
  and the full log kept, and watch the core thread.
- **m3.RL3**, the room list on a delta: the bytes went from 61.7 KB to
  3.4 KB, but the GUI thread still pays about 12 ms per one-room change
  on the jp26, with spikes of 66 to 73 ms, because RoomLists regroups all
  lists. Being worked on.
- **m3.P3**, packs after a restart: 1 to 2 small requests are left (an
  account data kind the server does not have is asked again at every
  start, and a global pack whose kept state is not fresh).
- **m3.G2**, the glow on every return to the list: a new breath of about
  16 s each time. By design; skipping it when the same rows breathed a
  moment ago is an idea, not a finding.
- **m1.L2**, the `_contentDeficit` binding loop on the room page: still
  there on 0.8.2-4, about once per room open (the footer loop, m2.L3, is
  gone).
- RL1, MI7, T10: the offscreen layer per rounded avatar and picture. The
  A/B switch is in; the run on the Prawn's 8 rows showed nothing either
  way. It needs a phone with many picture avatars and touch.
- MI4: the 100 MB cap works (the jp26's SDK media store 332 to 98 MiB),
  but below the cap new pictures still land in both caches (the Prawn,
  0.8.2-4).
- m2.T4: a room's first snapshot is a full compare in one GUI-thread
  block, unchanged on 0.8.2-4: 25 / 60 / 88 ms (median / p90 / max) on
  the jp26 and 33 / 125 / 174 ms on the Prawn.
- m1.T3: the core's startup CPU on the jp26 with 71 rooms, about 5 to 6 s
  in the first 10 s of a cold start on every build (on the Prawn it fell
  by 41 % with the key-based stores).
- m1.M1: memory per room opened, now 1.7 MB, still kept (the 11-18 M4 and
  M5 questions).
- m1.N1: on every start the stored token is already expired (401, then a
  refresh); matrix.org also refreshes every 5 minutes.
- m2.T5: the core's timeline diff grows with history (about 10 ms at 500
  items, 15 ms at 900, on the core thread).
- ST11: the emoji-size Bindings still sit in every room row.
- CB10 (release profile, to be tried alone and measured), CB11 (one
  runtime for all accounts), CB12, CB15, CB16, CB17, RL5, RL9, RL10, RL12,
  T12, MI9 (malloc_trim, measure first), RL6 (resets, measure first).
- From the guide: the D-Bus caller check for the notification actions.

## Measured or checked, not findings

So the next audit does not measure them again:

- Scrolling on the jp26 is smooth: almost every frame 11 ms at 90 Hz,
  render 1 to 2 ms, polish at most 3 ms. The few longer gaps are GUI
  thread stalls outside polish, and some are the pauses between gestures.
- Chat scrolling on the Prawn is bound by the GPU and the compositor:
  swap 17 to 24 ms of a 24 to 29 ms frame, render 2 to 3 ms, before and
  after 0.8.2.
- Pausing GIFs while the chat moves showed no measurable effect on the
  Prawn's chat (the room has few GIFs).
- Room list live updates: the app's list matched a full refresh on both
  phones (rounds two and three).
- `cacheBuffer` is already `Theme.itemSizeMedium * 8` in SilicaListView,
  about six room rows above and below the screen.
- The JSON of each core event is parsed on the core's thread; the GUI
  thread gets a QVariantMap.
- Qt 5.6 has no QML disk cache (it came in 5.8): QML compiles from source
  at every start, and the `QML_DISABLE_DISK_CACHE=1` that firejail sets
  changes nothing.
- No booster is in Rustrx's start: the tap to the process is 0.4 to 0.7 s
  on the jp26 and 1.5 to 1.7 s on the Prawn, before any Rustrx code runs.
- The GStreamer registry is kept between starts (the folder is
  whitelisted).
- Idle with nothing animating: 0.3 to 0.5 % of one core, 4 to 7 wakeups
  per second, the core's sync. The core has no interval timers.
- The 11-18 fixes hold: two glibc arenas, the low-memory SQLite pools.
- Room subscriptions are replaced on each open, not added; the SDK event
  cache shrinks closed rooms.
- Pictures in the list and the chat are asynchronous with `sourceSize`;
  a GIF keeps one frame (`cache: false`); the pixmap cache keeps 2 MB.

## Dead ends

- **Hardware video encoders.** For "Send smaller videos" (4220011) the
  phones' H.264 encoders could not be used: droidvenc on hybris takes only
  camera frames, the V4L2 encoder on the mainline Prawn ignores the
  bitrate. Smaller videos are WebM from software encoders.
- **GIF size.** `AnimatedImage` ignores `sourceSize` in Qt 5.6, so a GIF
  cannot be decoded smaller; pausing it while the chat moves is what was
  left (DEC2).
- **Avatar rounding A/B.** Four modes on the Prawn, n=1 each: all within
  run-to-run noise, and the edges look the same. No evidence yet, for or
  against.
