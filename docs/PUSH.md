# UnifiedPush beside the stand-in

Status: built in 0.4.5 (2026-11-16), tested on two phones with Foghorn
0.7.1. The implementation has an own QtDBus connector
(no Leghorn), the connector name org.unifiedpush.Connector.rustrx with
its D-Bus service file (RUSTRX_PUSH_WAKE, sailjail's UnifiedPush
permission), the push server's own Matrix gateway or the UnifiedPush
common gateway, an HTTP pusher with event ids only, and the SDK's
notification client fetching the one message in the woken process. The
plan below is the original one.

Original plan: Written 2026-11-15 from public sources:
the UnifiedPush idea, Foghorn's OpenRepos page, and Xmatic's push work
(ProjectMoon's pull request 9 on JimKnopfIoT/harbour-xmatic, merged
2026-10-09, Apache-2.0, with its docs/PUSH.md), which uses the same
distributor and the same SDK. Foghorn's own repository (git.agnos.is)
was not read: it sits behind a bot check.

## Why

Rustrx's background mode is a stand-in: Rustrx without a window, synced
all the time, started with the phone. It notifies within seconds, at the
cost of a running process and its sync. UnifiedPush is the other way: one
distributor holds one connection for every app and wakes an app when
something arrives. On Sailfish OS the distributor is **Foghorn**
(OpenRepos), on ntfy or Mozilla's push service; its Rust crate
**Leghorn** does the app side, including finding a push gateway that
speaks Matrix (logic borrowed from Element X).

Both stay. Settings > Notifications gets a choice for when Rustrx is
closed: nothing (default, as now), the stand-in, or UnifiedPush (only
offered when a distributor is installed).

## How it would work

1. **Register.** Rustrx registers with the distributor (Leghorn), gets
   an endpoint (the push address, a bearer secret: never shown, never
   logged).
2. **Gateway.** The homeserver does not push to the endpoint itself but
   to a Matrix push gateway: the push service's own (ntfy has one,
   Mozilla's has not), the UnifiedPush project's public one
   (`matrix.gateway.unifiedpush.org`), or one the user enters. No default,
   https only, as Xmatic does it.
3. **Pusher.** The core sets an HTTP pusher on the homeserver through
   the SDK, `format: event_id_only`: a push carries a room and an event
   id, never text.
4. **Wake.** A push reaches Rustrx over D-Bus. It must not start the
   window: the connector gets its own D-Bus name with a service file that
   starts a systemd user unit running Rustrx as the stand-in for one job
   (sync once, notify, exit), under the same sailjail profile. If the app
   or the stand-in runs already, it takes the push itself.
5. **Notify.** The woken core fetches and decrypts the event and shows
   the notification as now, following the privacy settings (text or "New
   message") and the account's push rules (muted stays quiet).
   A classic call's invite (`m.call.invite`) rings in the woken process
   as it does in the stand-in: `push_handle` sends it to the UI as the
   sync handler would ("voip"), the room's line is held while it rings,
   and an unanswered ring leaves the missed call. The process stays while
   it rings (main.cpp: the 30 s and the 2 min wait for the ring's end) and
   hands the ringing call to the app on Answer or a tap. While it rings,
   each pushed event of that room is fetched first: the caller's hangup
   and candidates, encrypted, are pushed, but the push rules filter them
   out once decrypted. In a room without encryption the server does not
   push them at all, and our own devices' answers and declines are never
   pushed: the ring then lasts its lifetime and ends as a missed call.
6. **Off / logout.** Remove this installation's pusher, unregister from
   the distributor, forget the endpoint.

## What leaves the phone

Not messages. The gateway and the push service learn the push address
and, per push, the room and event id, the time and the unread count: an
activity profile, not content. With ntfy and its own gateway that is one
server (one's own, if self-hosted), otherwise two. The Settings text says
so before it is switched on.

## Open points

- Leghorn is not on crates.io: a git dependency pinned to a commit on
  git.agnos.is (Xmatic pins
  `853d6e35720e5e0eccfa9c1a3a68a0e128335b73`, features `matrix`, no
  default features). The e15 build would fetch from there; the author
  decides whether that host is acceptable for the build, or whether to
  vendor it.
- After a reboot the store key (sailfish-secrets, bound to the device
  lock) is not there until the phone is unlocked: a push then can only
  say "New message".
- Two processes on one store: the woken one must hand over to the app
  the way the stand-in does (the session bus name as the lock).
- Test with Foghorn from OpenRepos on a test phone, with ntfy.sh first.

## Who holds the names (audit 0.8.4)

- **The distributor (IPC3).** Foghorn by its exact name
  (`org.unifiedpush.Distributor.Foghorn`), else the first by name; a
  name that only ends in "Foghorn" no longer wins. The one chosen first is
  kept in the data folder's push.json (`distributor`, across logouts too).
  While it is installed push stays with it, another one besides it
  included. When it is gone and another is there, push pauses and the app
  asks once per start ("Use %1 for push?", `pages/DistributorDialog.qml`);
  the window-less processes never switch and never ask.
- **The connector's name (IPC2).** When another process holds
  `org.unifiedpush.Connector.rustrx`, it is asked to prove that it is a
  Rustrx (`src/NameProof.h`: an HMAC under the data folder's `bus-secret`,
  bound to the name, its unique name and a nonce). A Rustrx on its way out
  is waited for and the name taken after it. Anyone else: no registration
  with the distributor, the pusher removed from the homeserver, `blocked`
  in push.json (so `activeOnDisk` is false), the stand-in runs while push
  stays chosen, and Settings > App says so in red under "When Rustrx is
  closed". A start that finds the name free again clears it and sets the
  pusher anew. Nothing notices a squat while no Rustrx process runs.
- **Reply** in a notification always goes to the app's own name
  (`net.mowerk.harbour-rustrx`, sailjail's own rule), so with Rustrx closed
  it opens the app; Mark as read and the emoji go to the connector, as their
  content is in the arguments anyway, but not while the name is held by
  another app.
- **The app's name (IPC6).** A push-woken process that finds
  `net.mowerk.harbour-rustrx` held by a program that cannot prove it is a
  Rustrx posts "Rustrx could not start" once per boot, then leaves.
