# Calls, lifecycle and the night's work, 2026-11-19 (after 0.5.0)

## Why

0.5.0 was released in the evening, and the same night classic 1:1 calls
(m.call.* over GStreamer's webrtcbin, the earpiece through PulseAudio)
were written and tested on two phones. The author asked for a deep audit
of the whole tree after that jump, with the abort seen at every quit as
the first thing to find.

## Method

Six agents read the tree at the same time, each one area, none of them
editing: the Rust core, the C++ bridge, QML and UX, security and privacy,
tests, docs and build, and the abort on quit. The last one reproduced the
abort on the Jolla Phone 2026 under gdb. The findings were triaged with
an advisor into batches; each batch was built, installed on the Jolla
Phone 2026 and the Pro¹-X and tried there where a phone could show it.
The ledger beside this file lists every finding with its status.

## What was found

**The abort (A1).** Every quit since 0.4.7 ended in abort(): the core's
shutdown made its tokio timeout on Qt's main thread, outside the runtime,
and the panic could not leave the extern "C" function. Captured under gdb:

    panicked at src/core/session.rs:565:37: there is no reactor running
    panic in a function that cannot unwind

So nothing of the shutdown ever ran: the session was not saved at the
end, the stores were not closed, and in background mode the stand-in was
not started after closing the app. Lipstick crashed in the same second
several times during the night's test installs. Fixed, with a test that
fails on the old line; on the phone a quit now ends with exit(0).

**The night's own regressions.** A Connections block for the call engine
went into the middle of the one for rustrx (A2): push notifications,
knocks and the quit request did nothing in the test builds 0.5.0-2 to
-13. The spoiler sender panicked on any non-ASCII text (K1). The CI's
C++ job lacked GStreamer, so every push since the call work was red and
the whole C++ suite skipped (C1). The call test sent the same tone from
both sides, which the echo cancellers learned away; it passed only by
reading early (T2). All fixed, each with a test.

**Calls.** Anyone in any room could ring every member, with a lifetime of
their choosing (S1); anyone who saw a call's id could answer or end it
(S5); a busy invite was dropped silently (V7); quitting during a call
sent no hangup (V6); late GStreamer callbacks could act on the next call
(B1); and the call page could vanish, be pushed twice, never show, or
leave Rustrx on its start spinner (V1-V5). All fixed; the device checks
are in the ledger.

**Moderation.** The mute switch demoted moderators (P2); choice boxes
stopped following the room after the first tap (P1); the own server was
matched with its port in access lists (M1). Fixed.

**Files.** A copy into Downloads could be led through a link into
Rustrx's own folders, including the stand-in's handover file (S2); the
link is refused, the copy is created new, and the handover file counts
only for two minutes. Validating its fields is left open.

## Left open, for the author

Design questions and measurements went to the queue rather than into the
night: a write lock for power levels (K2), the shutdown's task tracking
(K8), PulseAudio calls off the GUI thread (B7), a relay-only call option
(S6), stripping videos and PNGs (S9), tokens in the secret store (S10),
the dev hook as an allowlist (S4), the cover during a call (V10),
proximity blanking (V11), and two performance items to measure first
(R1, R2). README and CALLS.md need the classic calls before release.

## To try on the phones

- Background mode: close Rustrx, the stand-in should start (the code
  after app->exec() runs for the first time since 0.4.7).
- With Rustrx closed, answer a classic call from the notification: the
  call page over the room list, the list after hanging up.
- The stand-in's ring stops once the call is answered in the app (V9).
- A push handled by the stand-in decrypts (K5's new setup).
