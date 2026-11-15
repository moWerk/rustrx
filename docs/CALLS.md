# Calls in Rustrx: plan and spike

Status: spike on branch `calls-spike`. First call on 2026-11-15, 01:58, on the
Jolla Phone 2026: Element Call joined a room, camera and microphone working.

## How Matrix calls work today

Element X and current Element call through **Element Call**: MatrixRTC
(MSC4143) for who is in the call, a LiveKit server (SFU) for the media,
WebRTC in between. The older 1:1 VoIP (`m.call.*` events) is legacy and
not what the other side's Element X answers. Both servers we use advertise
a LiveKit service in `.well-known` (`org.matrix.msc4143.rtc_foci`):
matrix.org (`livekit-jwt.call.matrix.org`) and tchncs.de
(`rtc.matrix.tchncs.de/livekit/jwt`).

Element X does not implement the media itself: it shows Element Call, a
web app, in a WebView, and matrix-rust-sdk's **widget driver** answers
Element Call's Matrix requests over the widget postMessage API (MSC2762,
MSC3819 to-device, MSC4157 delayed events, MSC4515 transports).
matrix-sdk 0.19.1 has all of it behind the `experimental-widgets` feature:
`WidgetSettings::new_virtual_element_call_widget` builds the URL,
`WidgetDriver::run` answers the widget, `WidgetDriverHandle` carries the
strings.

## The Sailfish side

On the Jolla Phone 2026 (SFOS 5.2.0.22): Gecko ESR 115
(`xulrunner-qt5-115.40`), Sailfish WebView 1.7.4 with frame scripts
(`loadFrameScript`, `sendAsyncMessage`, `addMessageListener`), its own
WebRTC permission dialog (`embed:webrtcrequest` in its popups),
`gecko-camera` with the droid plugin, and the sailjail permissions
WebView, Camera, Microphone. There is no native WebRTC library to link, so
the WebView route is the realistic one.

## What the spike does

- **Core** (`core/src/core/call.rs`): `call_start {room_id, voice}` picks
  the intent (start or join, DM or room, voice in a DM), per-participant
  encryption in encrypted rooms, `controlled_audio_devices` so the phone
  keeps the audio routing, no screen sharing; builds the URL for the hosted
  Element Call (`https://call.element.io`), starts the driver (every
  capability Element Call asks for is granted) and emits `call_url`.
  `call_message {data}` passes a page message to the driver; the driver's
  messages come back as `call_message` events. `call_end` stops it; logout
  stops it too.
- **UI**: "Call (test)" in the room page's pulley (swipe left from a chat).
  `qml/pages/CallPage.qml` is a `WebViewPage` with the URL;
  `qml/call-bridge.js` is a frame script that forwards the page's widget
  API requests and answers to the app and posts the driver's into the page
  (origin checked). Element Call's hang up (`io.element.close`,
  `im.vector.hangup`) closes the page; closing the page ends the call.
- **Packaging**: the WebView permission, `sailfish-components-webview-qt5`.
- The page sets `media.peerconnection.scripttransform.enabled`, see below.

## Open questions the spike answers, in this order

1. **Does Element Call load and connect?** Lobby shown, the widget
   handshake done (capabilities, `content_loaded`), the call joined in an
   unencrypted room first. Watch the journal for the driver's lines.
2. **Microphone and camera** in a sailjailed app's WebView: does the WebRTC
   permission dialog come up, does `getUserMedia` get the mic
   (Microphone permission) and the camera (gecko-camera, Camera)?
3. **Encryption.** Per-participant E2EE in LiveKit needs
   `RTCRtpScriptTransform` (Firefox shipped it in 117; ESR 115 may have it
   behind the pref the page sets) or insertable streams. Without it Element
   Call refuses encrypted rooms and DMs, which are most calls. If the pref
   does not exist in 115, this is the blocker until Jolla moves Gecko on;
   a shared-secret call would not interoperate with Element X.
4. **Audio routing.** Earpiece vs speaker, the proximity sensor, the call
   volume keys: Gecko's PulseAudio streams need the phone role
   (`media.role=phone`) for the droid modules to route like a call. Check
   with `pactl list sink-inputs` during a call.
5. **Lifecycle.** A call while the screen locks or the app goes to the
   cover; the stand-in (background mode) must not tear the window down
   during a call (see the lipstick crash notes).

## First run on the Jolla Phone 2026 (2026-11-15)

1. **Element Call loads and joins**, after three fixes:
   - Gecko 115 has no `Intl.Segmenter` (Firefox 125): Element Call died
     at start. The frame script defines a small one in the page before
     its scripts run (`call-bridge.js`).
   - The hosted Element Call must be opened at `/room`: at its bare
     address it shows its own start page ("Name of call").
   - `io.element.join`, `io.element.device_mute`, `set_always_on_screen`
     and the other client actions are answered by the page, not the
     SDK's driver (which answered them with an error), as Element X does.
   The handshake then runs through (versions, capabilities, OpenID, the
   call membership event, LiveKit, a WebRTC peer connection).
2. **Microphone and camera**: Jolla's WebRTC permission dialog comes up
   in the sandboxed app; the microphone is listed, the camera row is
   empty (no device name), but the camera works: the author saw himself
   in the call. "Remember for this site" spares the next prompts.
3. **Encryption: blocked by Gecko 115.** In an encrypted room Element
   Call stops with `E2EE_NOT_SUPPORTED` ("Your web browser does not
   support encrypted calls. Supported browsers include Chrome, Safari,
   and Firefox 117+"). The page's feature probe:
   `RTCRtpScriptTransform=undefined`, no `transform` on RTCRtpSender, no
   `createEncodedStreams`; the API came with Firefox 117, so the
   preference the page sets has nothing to turn on. Calls in unencrypted
   rooms work; encrypted rooms and most DMs wait for a newer Gecko in
   Sailfish OS, unless another media path is found (a native WebRTC
   stack outside the WebView, or LiveKit's E2EE in a worker without
   insertable streams, which does not exist).
4. Audio: autoplay was blocked ("AudioContext was prevented from
   starting"); the page now allows it (`media.autoplay.*`). Routing still
   open.
5. Lifecycle: still open. Seen at night: Rustrx started by D-Bus while
   the phone is locked closes itself right after its first page change
   ("cannot pop while transition is in progress") and the stand-in
   takes over; tests need the display on.

**A way past Gecko 115:** the community port sailfish-browser-next153
(OpenRepos, aviarus, 3.0.0-7 of 2026-08-29, tested on the Jolla Phone
2026) installs Gecko 153 beside the system's, with its own WebView
module `SailfishNext153.WebView` / `SailfishNext153.WebEngine`
(package `sailfish-components-webview-qt5-next153`). The package now
carries the call page twice: `CallPage.qml` on the system WebView and
`CallPageNext153.qml`, generated from it at build time with the two
imports swapped. Rustrx uses the second where that module is installed
(`app.callPage()`, logged as "calls: web engine"). Untested: install
the next153 WebView on a test phone, then an encrypted call.

Server notes: matrix.org's LiveKit service refuses one optional
cross-origin request (`delegate_delayed_leave`); Element Call goes on.

Test tooling: `Rustrx::dev()` over D-Bus (see `src/Rustrx.h`), enabled
by a file `dev-commands` in the data folder; `call_start` takes
`skip_lobby`. The page logs the widget API's flow (actions only) and
Element Call's errors to the debug log.

## After the spike, if 1-3 work

- A call button where it fits the text-first UI (room header or the room
  page), voice by default in DMs.
- Incoming calls: MatrixRTC notification events (`m.rtc.notification`,
  `org.matrix.msc4075.call.notify`) as a ringing notification with answer
  and decline, also from the stand-in.
- Ship the embedded Element Call package with the app instead of the hosted
  one (no remote code loaded at call time, works with any server), as
  Element X does; check its licence (AGPL-3.0) against bundling.
- Native call UI over the WebView where Element Call allows it
  (`header=none`, `controlledAudioDevices`), Sailfish-style hang up and
  mute.

## The phone's own call screen (research, 2026-11-15)

Goal: a Matrix call shows up like a phone call, in Jolla's call screen
(voicecall-ui-jolla) with its answer and decline, lock screen, ringtone,
call history, earpiece and proximity handling, while the media stays with
Element Call in Rustrx's WebView.

What the Jolla Phone 2026 (5.2.0.22) has: voicecall-qt5 0.9.0 with the
Telepathy provider plugin, telepathy-mission-control 5.16, telepathy-qt5
0.9.8 including `libtelepathy-qt5-service` (the library for writing a
connection manager), ring (cellular) and gabble as managers.

How voicecall takes calls (sailfishos/voicecall,
plugins/providers/telepathy/src):

- It registers as the Telepathy client handler `voicecall` for audio
  call channels (`ChannelClassSpec::audioCall()`, Call1, and the old
  StreamedMedia), `telepathyproviderplugin.cpp:74-86`.
- It makes a provider for every account whose protocol is `tel` or `sip`
  and ignores all others, `telepathyproviderplugin.cpp:293`. A provider
  counts while its account is enabled, online and connected,
  `telepathyprovider.cpp:241`.
- Dialing from the phone app on a `sip` account requests a Call1 audio
  channel for the address, `telepathyprovider.cpp:135`.
- For a Call1 channel it sets up Farstream media only if the channel asks
  the handler to stream (`handlerStreamingRequired()`, the inverse of the
  channel's `HardwareStreaming`), `callchannelhandler.cpp:308`. With
  `HardwareStreaming = true`, as ring does for cellular calls, voicecall
  only drives the call's state and the screen.

Checked on the phone: mission-control accepts an account for a
third-party manager. With a test manager file
(`/usr/share/telepathy/managers/<name>.manager`), `mc-tool add` made the
account in the default keyfile storage (`accounts.cfg`), no libaccounts
provider needed. Test account and file removed again.

So the route is a small Telepathy connection manager for Rustrx:

- A separate binary (telepathy-qt service classes), started by D-Bus
  activation, outside the app's sandbox: `rustrx.manager` with a `sip`
  protocol (voicecall takes no other name), a D-Bus service file, and one
  account made on first use.
- Incoming: when a call notification (`m.rtc.notification`) arrives for a
  DM, Rustrx (app or stand-in) tells the manager, which announces an
  incoming Call1 channel (`HardwareStreaming = true`, the caller as the
  target). voicecall shows its incoming call screen and rings.
- Answer in the call screen = `Accept` on our channel: the manager tells
  Rustrx, which joins the call in the WebView. Hang up or decline =
  `Hangup`: Rustrx leaves or declines. A hang up in Element Call closes
  the channel.
- Outgoing from Rustrx: the manager announces an outgoing channel, so
  the call screen shows the running call too.
- Call history: voicecall's commhistory plugin logs these calls like
  phone calls (the Matrix ID as the "number").

Open points to test, in this order:

1. Does the WebView keep sending and playing audio while voicecall's
   screen is in front and Rustrx is in the background?
2. Audio routing: voicecall and ohm switch the audio to the call route
   (earpiece) for the channel; does Gecko's stream follow, or does it
   need `media.role=phone`?
3. The `sip` name: the call screen and history show the address as a
   SIP one; can the address be the Matrix ID, or a display name?
4. A Harbour app could not ship this (own D-Bus service names, a
   Telepathy manager); Rustrx is an OpenRepos app, so it can.

## Incoming calls (queued for 0.4.4, 2026-11-15)

First two-phone test: the jp26 started a call in the plain room, the
Prawn showed nothing. Rustrx does not see calls it did not start; the
other side has to open the room and start the call too (Element Call
then joins the running one).

1. **A running call is visible.** Element Call announces each participant
   in the room's state (`m.call.member`, one per device, empty content
   when it leaves). The core watches these for the rooms in the list and
   reports "call in progress" per room; the room shows "Join call", the
   list a mark.
2. **Ringing.** Whoever starts a call sends a notification event
   (`m.rtc.notification`, MSC4075; `notification_type` "ring" in DMs,
   "notification" in groups; older clients `org.matrix.msc4075.call.notify`).
   Rustrx answers a ring with a notification that rings, with Answer
   (opens the call page and joins) and Decline (MSC4310 decline, or just
   dismissing it). Later the native call screen (above) takes it over.

Built 2026-11-15 (0.4.3-19, not yet tested): the core rings on
`org.matrix.msc4075.rtc.notification`, `m.rtc.notification` and the older
`org.matrix.msc4075.call.notify` while they are within their lifetime
(the server's `unsigned.age`); a notification with lipstick's
`voip_ringtone` feedback, Answer (opens the room and joins), Decline
(MSC4310 decline). A new call sends `sendNotificationType=ring` to
Element Call in a DM or a two-person room, `notification` in a group.
The room shows "Call in progress, tap to join" and the list a mark
while `m.call.member` says someone is in a call.

Limits: in background mode the stand-in's sync is frozen while the phone
sleeps (a short window every five minutes), so a ring in between is
missed; push (Foghorn, docs/PUSH.md) is the way out. Whether lipstick
plays `x-nemo-feedback` for an app's notification on a locked phone is
to be seen; the fallback is a notification category file of our own.
