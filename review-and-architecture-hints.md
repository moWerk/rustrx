# Review and architecture hints: Rustrx

For anyone reviewing Rustrx: where the code comes from, how it is laid out, what is worth reading and what is boilerplate.

## Where the code comes from

Rustrx is not a port. It was written from scratch, starting with `1a4b274` (2026-11-09), on one branch, `main`. All of it is new work, so the reliable view is the tree itself, or `git log -p` for how it grew. Each commit message says what changed, why and what was not checked, and ends with an LLMGD line grading it. `docs/audits/` holds the audits (a dated narrative and a findings ledger each): every finding, its commit, and what is still open.

The app was written by an LLM (Claude), directed and tested by the author, who has not read the code. Everything here is a prototype until a reviewer owns it. That is the point of this file.

## Architecture

- `core/` is a Rust crate on matrix-rust-sdk 0.19 (the SDK of Element X): Client, sync, crypto, store, timelines, sending. It is cross-compiled outside the Sailfish SDK (`core/build-aarch64.sh`, cargo-zigbuild, `--locked`), because Jolla's Rust is too old for the SDK, and bundled as `lib/librustrx.so`.
- The boundary is three C functions (`core/src/lib.rs`, `src/rustrx_core.h`): commands go in as JSON with a `"cmd"`, everything comes back as JSON events with an `"ev"` on one callback, on tokio threads. There are no return values. `core/src/protocol.rs` is the contract (one enum), and `docs/PROTOCOL.md` is written from it.
- `core/src/core/*.rs` is `impl Core`, split by domain: `commands.rs` (dispatch only), `session.rs` (login, restore, logout), `sync.rs` (sync, room list), `timeline.rs` (the open room, sending, the send queue), `threads.rs`, `rooms.rs`, `spaces.rs`, `knock.rs`, `account.rs` (recovery, keys, devices), `verification.rs`, `packs.rs` (custom emoji), `push.rs`, `call.rs`. Shared state is one async mutex in `core.rs`. `serialize.rs` makes the JSON the UI gets.
- `src/` is the C++ bridge. `Rustrx.cpp` re-posts each event to the GUI thread and is `rustrx` in QML. Beside it: the models (`RoomLists`, `RoomModel`, `TimelineModel`, which formats each row's text once in C++), `Twemoji`, `Media`, `VoiceRecorder`, `ShareAdaptor` (share sheet target), `UnifiedPush` (D-Bus connector, `docs/PUSH.md`), `Handover`, `SecretStore` (a plugin of its own in `secrets/`, loaded at run time), `DebugLog`, `FrameStats`. Names and limits are in `Keys.h`.
- `qml/`: `harbour-rustrx.qml` is the window, `Service.qml` is notifications and background work in an engine without Silica, `pages/` has one file per page, and `components/` holds the shared parts (`Composer`, `MessageDelegate`, `RoomDelegate`, `EmojiDock`/`EmojiPanel`, `ChatAnchor` with its debug trace `ChatTrace`, `DeviceList`, `BannerItem`, the avatar mask `RoundMask`). Text from other people goes into StyledText only through `markup.js` (`escaped`); Labels default to plain text (`_defaultLabelFormat`), and every link leaving the app passes `openExternal` in `harbour-rustrx.qml` (web, mail and map links only). The tuned numbers are in `singletons/Tunables.qml`, and the settings are dconf under `/apps/harbour-rustrx` (`components/Settings.qml`).
- One binary, three modes (`src/main.cpp`), locked against each other by the bus name `net.mowerk.harbour-rustrx`, all in the same sailjail profile:
  - the app;
  - the stand-in (`RUSTRX_BACKGROUND=1`, `systemd/harbour-rustrx.service`), never with a window;
  - push wake (`RUSTRX_PUSH_WAKE=1`, D-Bus activation by the UnifiedPush distributor, one message, then exit).
- Login is a password, or classic `m.login.sso` through the SDK's `sso-login` feature (`session.rs`, `login_sso`): a listener on localhost, the sign-on page in the system browser, and back with a login token. There is no WebView and no Amber.Web.Authorization. OIDC (MAS) is not built.
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

- No automated tests and no CI. The audit checked its two rewrites against the old code (formatting on about 20,000 messages, sorting on 3000 random lists), but those checks are not in the tree.
- Without sailfish-secrets (community ports), the store is unencrypted.
- The debug log is on by default until 1.0. It holds room and user ids, never message texts or passwords. Check that claim in `DebugLog.cpp` and the core's tracing calls.
- The stand-in never gets a window, because lipstick 0.37.6 crashed when a process exited after hiding one. It is a workaround, not a fix.
- `MainPage.qml` imports `Sailfish.Silica.private` (one reason it is no Jolla Store app, besides the bundled core).
- Send queue recovery after a network error is not verified (next for 0.4.7).

## How it was tested

By the author, as a daily client on the Jolla Phone 2026 (SailfishOS 5.2) and on the hybris Pro¹-X port, with two accounts (matrix.org, and tchncs.de with single sign-on); verification was tested against nheko. Before each handover, the LLM checked builds, `qmllint`, package contents and start logs on those phones.

There are no automated tests; the on-device checks are listed in the commit messages.
