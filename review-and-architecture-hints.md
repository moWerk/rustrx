# Review and architecture hints: Rustrx

For anyone reviewing Rustrx: where the code comes from, how it is laid out, what is worth reading and what is boilerplate.

## Where the code comes from

Rustrx is not a port. It was written from scratch, starting with `1a4b274` (2026-11-09), on one branch, `main`. All of it is new work, so the reliable view is the tree itself, or `git log -p` for how it grew. Each commit message says what changed, why and what was not checked, and ends with an LLMGD line grading it. `docs/audits/` holds the audits (a dated narrative and a findings ledger each): every finding, its commit, and what is still open.

The app was written by an LLM (Claude), directed and tested by the author, who has not read the code. Everything here is a prototype until a reviewer owns it. That is the point of this file.

## Architecture

- `core/` is a Rust crate on matrix-rust-sdk 0.19 (the SDK of Element X): Client, sync, crypto, store, timelines, sending. It is cross-compiled outside the Sailfish SDK (`core/build-aarch64.sh`, cargo-zigbuild, `--locked`), because Jolla's Rust is too old for the SDK, and bundled as `lib/librustrx.so`.
- The boundary is three C functions (`core/src/lib.rs`, `src/rustrx_core.h`): commands go in as JSON with a `"cmd"`, everything comes back as JSON events with an `"ev"` on one callback, on tokio threads. There are no return values. `core/src/protocol.rs` is the contract (one enum), and `docs/PROTOCOL.md` is written from it.
- `core/src/core/*.rs` is `impl Core`, split by domain: `commands.rs` (dispatch only), `session.rs` (login, restore, logout), `sync.rs` (sync, room list), `timeline.rs` (the open room, sending, the send queue), `threads.rs`, `rooms.rs`, `spaces.rs`, `knock.rs`, `moderation.rs` (what the room's power levels allow, and the owner, admin and moderator commands: roles, permissions, history, guests, encryption, the directory, addresses, the server access list, pins, upgrades, reports), `account.rs` (recovery, keys, devices), `verification.rs`, `packs.rs` (custom emoji), `push.rs`, `call.rs`. Shared state is one async mutex in `core.rs`. `serialize.rs` makes the JSON the UI gets.
- `design/` holds standalone mockups for design work, not built into the app: `security-glance/` is the Account tab's security glance, runnable on a desktop with `qmlscene -I . main.qml` (stand-ins for the few Silica types it uses).
- `src/` is the C++ bridge. `Rustrx.cpp` re-posts each event to the GUI thread and is `rustrx` in QML. Beside it: the models (`RoomLists`, `RoomModel`, `TimelineModel`, which formats each row's text once in C++), `Twemoji`, `Media`, `VoiceRecorder`, `ShareAdaptor` (share sheet target), `UnifiedPush` (D-Bus connector, `docs/PUSH.md`), `Handover`, `SecretStore` (a plugin of its own in `secrets/`, loaded at run time), `DebugLog`, `FrameStats`, `LocalOnly` (the QML engines get no network: every picture they show is a cached file), `Exif` (a photo's metadata out before sending, the orientation kept; tested in tests/cpp/exif), `KeyboardSlide` (a slide-out keyboard's state from MCE, so Lock to portrait gives way while it is open), `Voip` (classic 1:1 calls: the m.call.* state machine, media through GStreamer's webrtcbin, the microphone through Speex's echo canceller and noise suppression (appsink, appsrc) with the far end's audio as reference; the core only carries the events, `voip_send` and `voip`, and the TURN credentials; tested in tests/cpp/voip with two engines calling each other), `CallRoute` (during a call, through libpulse: the card's communication mode and the earpiece, the loudspeaker on the speaker button, all restored afterwards; the call streams join the policy's aliencall group by rpm/harbour-rustrx-xpolicy.conf), `Mp4` (a video's size and length from its header, sent with a frame from Sailfish's `thumbnaild-video`; tested in tests/cpp/mp4). Names and limits are in `Keys.h`.
- `qml/`: `harbour-rustrx.qml` is the window, `Service.qml` is notifications and background work in an engine without Silica (its notifications use the lipstick categories in `notificationcategories/`, because the QML Notification type cannot set the feedback hint: a message vibrates, a call rings), `pages/` has one file per page (Settings is a tab view, `SettingsPage`, whose four tabs and two sub-pages, Encryption and Devices, are in `pages/settings/`), and `components/` holds the shared parts (`Composer`, `MessageDelegate`, `RoomDelegate`, `EmojiDock`/`EmojiPanel`, `ChatAnchor` with its debug trace `ChatTrace` (reading is the room page's: the read receipt follows the newest message whose end was on screen, `TimelineModel::seenEventId`, tested in tests/cpp/readpoint; the unread line moves only when the room is left or the app goes to the cover), `DeviceList`, `BannerItem`, `OwnAvatar` (one's own picture in Settings), `HoldComboBox` (Silica's ComboBox with press-and-drag selection, as list item menus have it; every choice box uses it), `HaloRing` (the account's safeguards as a ring of glass and light, Settings' Account tab; small, with one arc, it leads that safeguard's line), `SafeguardRow` (one safeguard as one line: its mark, its name, and when missing the gap and the arrow to its fix), `PageButton` (every button among texts, with a paragraph's room around it; the margin is set there only), `ReasonDialog` (a moderator's confirmation with an optional reason), `LevelComboBox` (who may do one thing: members, moderators, admins, or a level), `LinkRow` (a line with an arrow that leads somewhere: every link on the Account tab), `StateIcon` (a safety state as Silica's check or cross icon, green or red: the Encryption page through `StateLine`, the devices list), `StateLine` (that icon in front of a state's text), the avatar mask `RoundMask` (a rounded square, its radius the avatar corners setting), `ActionLine` (one line on a page of actions: a message's "More…" page, `MessageActionsPage`, which works from a copy of the message so its row may scroll away, and a space's "Add…" page, `SpaceAddPage`), `DebugValue` for the debug switches in dconf, `IncomingCallSwipe` (a ringing call answered by a swipe sideways, declined by a swipe up, as in the phone app; VoipPage)). The message field's slash commands are parsed in `commands.js`, and `transcript.js` turns the loaded chat into the plain text of "Select text" (`TranscriptPage`, the system's selection handles over one read-only TextArea); both are tested under node. Text from other people goes into StyledText only through `markup.js` (`escaped`); Labels default to plain text (`_defaultLabelFormat`), and every link leaving the app passes `openExternal` in `harbour-rustrx.qml` (web, mail and map links only). The tuned numbers are in `singletons/Tunables.qml`, and the settings are dconf under the path in `names.js` (`components/Settings.qml`).
- One binary, three modes (`src/main.cpp`), locked against each other by the bus name `net.mowerk.harbour-rustrx`, all in the same sailjail profile:
  - the app;
  - the stand-in (`RUSTRX_BACKGROUND=1`, `systemd/harbour-rustrx.service`), never with a window;
  - push wake (`RUSTRX_PUSH_WAKE=1`, D-Bus activation by the UnifiedPush distributor, one message, then exit).
- Login (`session.rs`), with no WebView and no Amber.Web.Authorization:
  - Where the server has an account service (MAS, matrix.org), OAuth 2.0 through the SDK (`login_oauth`). Rustrx registers itself as a native app, opens the service's page in the system browser and takes the code on a loopback port. The password never passes through Rustrx, and the session file keeps the client id.
  - Elsewhere, a password (`m.login.password`) or classic `m.login.sso` (`login_sso`): the sign-on page in the browser, and back with a login token.
  - With a QR code (`login_qr`, MSC4108, where the server has the rendezvous): this device shows the code, a logged-in one scans it, and the user types its two digits (`qr_check_code`). The other device hands over the keys.
- Calls: Element Call in a WebView with `qml/call-bridge.js` injected, and the SDK's widget driver behind it (`docs/CALLS.md`).

## Read these first

1. `core/src/lib.rs` and `src/Rustrx.cpp`: the thread hop. Nothing may touch Qt state from a tokio thread.
2. `core/src/core.rs` and `timeline.rs`: the state lock (the audit found it held across a network wait) and the send queue, including how a refused event is dropped at start.
3. `src/main.cpp`, `Handover.cpp` and the two systemd units: the three processes and who opens the store when.
4. `session.rs` and `SecretStore.cpp`: the session file and the store key.
5. `src/TimelineModel.cpp`: the HTML rewriting of message bodies, which comes from other people's servers.

## Skim

`qml/twemoji/` (Twemoji PNGs), `qml/emoji-data.js` (generated), `icons/`, `translations/`, most of `qml/pages/`, `Tunables.qml`, `Keys.h`, `consts.rs`, packaging.

## Worth questioning

- The tests are young (0.4.7). They hold the wiring between the three languages, the message HTML and the plain texts, the room order, and the core's command parsing; nothing yet exercises a live server, the three processes or Qt 5.6 itself. The audits' ledgers list every finding with its commit or the decision behind it.
- Without sailfish-secrets (community ports), the store is unencrypted.
- The debug log is on by default until 1.0. It holds room and user ids, never message texts or passwords. Check that claim in `DebugLog.cpp` and the core's tracing calls.
- The stand-in never gets a window, because lipstick 0.37.6 crashed when a process exited after hiding one. It is a workaround, not a fix.
- `MainPage.qml` and `SettingsPage.qml` import `Sailfish.Silica.private` for the tab view (one reason it is no Jolla Store app, besides the bundled core).
- Other people's text reaches StyledText only escaped (`markup.js`, `TimelineModel`), Labels default to plain text, the QML engines have no network, and links leave the app through one gate (`openExternal`). Try to break that.

## How it was tested

By the author, as a daily client on the Jolla Phone 2026 (SailfishOS 5.2) and on the hybris Pro¹-X port, with two accounts (matrix.org, and tchncs.de with single sign-on); verification was tested against nheko. Before each handover, the LLM checked builds, `qmllint`, package contents and start logs on those phones.

Since 0.4.7 (see `docs/audits/2026-11-16-lifecycle-and-input.md`):

- `python3 -m pytest -q tests`: static and wiring checks, on every push (`.github/workflows/ci.yml`).
- `tests/cpp/run.sh`: QtTest suites against the host's Qt 5, on every push.
- `cargo test --locked` in `core/`: nightly (`.github/workflows/nightly.yml`), because matrix-sdk takes minutes to compile.

Every test says which planted bug it was proven against, and each commit names its plants.

**Proof gate** for a change to count as done: the three suites pass, the RPM builds in the Sailfish SDK (which also catches what Qt 5.15 accepts and Qt 5.6 does not), and the change was seen working on a phone. Commit messages say which of these did not happen.
