# Second audit for 0.4.7: lifecycle and untrusted input

The 0.4.6 audit (`2026-11-16-structure.md`) looked at structure and double
work. It left out what this one covers. The author asked for it after
comparing Rustrx with his other project's discipline
(asteroid-docking-bay): Rustrx had no tests, no CI and no audit ledger,
because it began as an evening's idea and grew without a plan.

## What came first: a test floor

Before the audit, the tests the project lacked. Each test was trusted only
after it failed against a bug planted on purpose (its `Planted-bug:` line
says which), and each commit names its plants.

| Suite | Runs | What it holds |
|---|---|---|
| `tests/test_static.py` (pytest) | every push | the wiring between Rust, C++ and QML: commands, events, `rustrx.x`, Tunables, Keys, the bus names, the translation ids, Qt 5.6 traps, licence lines, the review hints, the audits' ledgers, the D-Bus test hook's refused list, one gate for links, one source for names |
| `tests/cpp/` (QtTest, host Qt 5) | every push | Twemoji, message HTML with hostile input, the plain texts against the old JavaScript (4320 cases), the room order against the old comparator (3000 random lists) |
| `cargo test` in `core/` | nightly | the command list and fields pinned, hostile JSON, dispatch answering every command, previews and the room order |

The two equivalence checks of the 0.4.6 audit, which had been thrown
away, are in `tests/cpp/golden` and `tests/cpp/roomsort` now.

The tests alone found six things (`found_by_tests` in the ledger), from a
`docs/PROTOCOL.md` that lacked 14 commands and had a spliced table, to a
core that ran `["logout"]` sent as a JSON array.

## Method

Two LLM agents read the code without changing it, with the first audit's
findings as known:
- **Lifecycle:** every spawn, lock, abort, loop and timeout in the core; the
  app, the stand-in and push wake as three processes of one binary; checked
  against the matrix-sdk 0.19.1 sources.
- **Untrusted input, privacy, names:** every path from other people's
  servers and other apps into StyledText, links, shares, D-Bus and the log;
  Qt 5.6's StyledText and Silica's Label read from source.

Each finding is CONFIRMED (traced end to end) or SUSPECTED, with file and
line. Then the fixes went in one concern per commit, with a test where one
can reach the bug.

## What it found

30 findings: 3 high, 8 medium, 18 low, 1 trivial. The
ledger has each with its status and commit; the main ones:

| Id | What | Status |
|---|---|---|
| C1 | After any send error a room's send queue stayed off until a restart; "Send again" sent into it | fixed `760903c`: back on after 30 s and when the sync runs; `retry_send` resends as it was |
| I1 | Silica Labels render AutoText: a display name `Bob <img src=...>` made every phone fetch that address | fixed `4a5290d` (plain text by default), `634beaa` (QML engines have no network) |
| I2 | `< img ...>` passed the HTML rewrite and StyledText drew it | fixed `d54a44c`, tests written failing first |
| I3 | A quote in a link's address started a second href; a link showing https could open `file:` | fixed `d54a44c`, `c735bd7` (one gate for links leaving the app) |
| I5 | A user link created a direct chat and invited them, unasked | fixed `c735bd7`: a dialog asks first |
| C2 | A slow logout could delete a newer login's store | fixed `cb14789`: a login generation counter |
| C4 | Single sign-on waited for the browser without end | fixed `cb14789`: 10 minutes, and Cancel |
| I8 | A share could name Rustrx's own session file | fixed `8409035` |
| I10 | After a logout any bus client could set the push endpoint | fixed `c721905` |

## Open, for the next release

- **The three processes (C3, C9, C12, C13):**
  - The bus name is taken before the D-Bus object exists.
  - Nothing stops the core before the name is released at exit.
  - The stand-in does not retry a restore without the store key.

  Each needs the start paths tested on a phone, so none was changed
  unattended.
- **Drafts (I12)** are kept in the plain settings file, outside the
  encrypted store.
- **Counts the UI keeps itself (I13, I14):** the "new" counts of threads, and
  the notification's message count.
- **Protocol 2 (I15):** one wire name per concept. `id`, `url`, `key` and
  `members` each mean two things today.
- **Names in two places (I16):** the dconf path in nine places, and Element
  Call's origin in Rust and JavaScript (now cross-referenced).

## What was not the gap

The measured non-findings are in the ledger, so the next audit need not
measure them again. The main ones:
- One tokio mutex, never held across the network.
- The thread hop is correct.
- Every SDK request is bounded.
- No reachable panic.
- The 0.4.6 logout teardown is complete.
- Rustrx's own log lines hold no message texts.

## Dead ends and what was not checked

Also in the ledger. Not checked on any phone yet:
- Qt 5.6's StyledText itself.
- What matrix-sdk logs at WARN.
- Whether the browser hands `matrix:` links over without asking.
- What `file://` opens inside sailjail.
