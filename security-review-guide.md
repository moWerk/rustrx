# Security review guide: Rustrx

For a reviewer who looks at Rustrx for security and maintainability: what it relies on, where the trust boundaries are, where secrets live, how untrusted input travels, what earlier audits covered, and where to start. It assumes no knowledge of Sailfish OS. The general map of the code is [review-and-architecture-hints.md](review-and-architecture-hints.md); read its Architecture section first.

Every claim here points at the code; line numbers are those of commit `71fe101`, except where a section names a later change. Where this guide is unsure, it says so. If the code and this file disagree, the code is right and this file has a bug.

## What Rustrx is

Rustrx is a native Matrix client for Sailfish OS, a Linux phone system with a Qt 5.6 and QML user interface (Silica is its widget set). Three layers:

- `core/`: a Rust crate on matrix-rust-sdk 0.19.1 (`core/Cargo.toml`, `core/Cargo.lock`), built as `librustrx.so`.
- `src/`: a C++ bridge (Qt 5.6) from the core's callback to the GUI thread, list models, media handling, D-Bus, calls.
- `qml/`: the user interface.

### What the SDK does, and what Rustrx does on top

matrix-rust-sdk does all of the Matrix cryptography: Olm and Megolm, end-to-end encryption and decryption, device and user verification (SAS), cross-signing, key backup and recovery, the encrypted SQLite stores (`matrix-sdk-sqlite`, `matrix-sdk-store-encryption`), OAuth 2.0 login, the QR login and grant flows (MSC4108), token refresh. Rustrx calls these APIs. It implements no cryptographic primitives of its own; it does make the store key and seals `session.json` with the SDK's `StoreCipher` (below).

Rustrx adds, and this is where a review pays off most:

- where the store key comes from and how it reaches the SDK (Sailfish Secrets, below);
- sealing `session.json` with the SDK's `StoreCipher`;
- every path from other people's content to the screen: the HTML rewriting in C++, links, media files on disk, notifications;
- every way other apps on the phone reach Rustrx: D-Bus methods, the share target, URL handlers, push;
- the metadata stripping of pictures and videos before sending, with its own parsers;
- classic 1:1 calls over GStreamer's webrtcbin (`src/Voip.cpp`);
- what is logged, and what logout deletes.

### Status

- Rustrx was written by an LLM (Claude) under the author's direction. The author decided the design and tested builds on phones.
- **Not yet reviewed by others.** The earlier audits (below) were made by LLM agents directed by the author, not by independent reviewers.
- Every commit message ends with two disclosure lines, `Disclosure:` and `LLMGD:`, which grade how it was made and checked (scheme: [moWerk/llmgd-specs](https://github.com/moWerk/llmgd-specs)). They say "not yet reviewed by others" where that holds. `git log` shows them.
- Commit messages also say what was not checked. Read them for the files you review.

## Threat model and trust boundaries

What Rustrx must protect: message contents, the account's tokens, the encryption keys (device keys, room keys, the recovery key), metadata about who talks to whom, and the phone itself from content sent by strangers.

### The homeserver and the network

- The homeserver is trusted with what Matrix gives it (metadata, unencrypted rooms, media it stores). It is not trusted with encrypted content; the SDK does that.
- TLS is rustls with aws-lc-rs (`core/Cargo.toml`, feature `rustls-aws-lc-rs`). Certificate checks are the SDK's and reqwest's defaults; Rustrx sets no options of its own there (`build_client_with`, `core/src/core/session.rs:222`).
- A homeserver URL typed with `http://` is taken as given (`session.rs`, in `build_client_with`). See the open questions.
- Server-supplied URLs that leave the app (SSO and OAuth pages, identity reset approval, device management pages) go through `openExternal` (below).

### Other Matrix users and their content

Everything another user or their server sends is untrusted: message bodies and `formatted_body` HTML, links, media files and their claimed MIME types and names, room state (names, topics, avatars, power levels), display names, custom emoji and image packs (MSC2545, `core/src/core/packs.rs`), reactions, call events (`m.call.*`), push payloads, event ids from old room versions. See "Untrusted input" below.

### Other apps on the phone

Rustrx is reachable by other processes in these ways:

- **The app's D-Bus name** `net.mowerk.harbour-rustrx`, object `/net/mowerk/harbour_rustrx`, interface `net.mowerk.harbour_rustrx` (`src/Keys.h`). The exported methods are the `Q_SCRIPTABLE` slots in `src/Rustrx.h:565-613`, registered with `ExportScriptableSlots` (`src/Rustrx.cpp:267`): opening rooms, `openUrl`, `activate`, `quit`, the notification actions (reply, react, mark read, answer and decline a call), and `dev`. Lipstick (the home screen) calls these for notification buttons. The slots do not check who called them; see the open questions.
- **The dev hook** `dev(json)` (`src/Rustrx.cpp:773`): answered only while a file `dev-commands` exists in the app's data folder (no package installs it), and then only the requests in the allowlist of `src/DevHook.h` (`refusal()`), tested in `tests/cpp/devhook` and `tests/test_static.py` (`test_dev_hook_takes_only_the_test_commands`). The allowlist includes a password login (`{"login": "password", ...}`) for tests against a local server, and, TEST ONLY, `{"cmd": "dev_ring", "room_id"}`: the core makes up an `m.call.invite` from the room's other member and passes it on as a real one (`dev_ring` in `core/src/core/call.rs`, which checks for the `dev-commands` file itself too), so that a ring can be tested in every process with nobody to call.
- **The share target** (`X-Share-Methods` in `harbour-rustrx.desktop`; `src/ShareAdaptor.cpp`, `src/ShareCopy.h`): files and text from other apps' share sheets. A path is opened with `O_NOFOLLOW`, must be a regular file, must not resolve into Rustrx's own folders (`ShareAdaptor::mayShare`, line 48), and is copied into the cache at share time; the copy is sent. Descriptors are copied the same way. The share goes to `SharePage` (pick a room), then `ConfirmSendPage`, which sends from its button (`send()`, `rustrx.shareTo`).
- **URL handlers**: `x-scheme-handler/matrix`, `x-url-handler/matrix.to` and `www.matrix.to` (`harbour-rustrx.desktop`), delivered over D-Bus to `openUrl` (`src/Rustrx.cpp:434`), parsed in `openMatrixUri` (`qml/harbour-rustrx.qml:630`). A user link shows a dialog before a chat starts; a room link shows the join page first (below).
- **UnifiedPush**: the distributor (for example Foghorn) talks to the connector name `org.unifiedpush.Connector.rustrx` (`src/UnifiedPush.cpp`, `docs/PUSH.md`). Calls carry a token Rustrx made (`registerApp`, line 296, a random UUID) and are ignored without it (`fromDistributor`, line 395). An endpoint that is not `https://` is ignored. A push names only a room and an event; the content is fetched from the homeserver.
- **Push wake**: `dbus/org.unifiedpush.Connector.rustrx.service` starts the binary with `RUSTRX_PUSH_WAKE=1` inside sailjail when a push arrives and nothing holds the connector name.

### The local filesystem and backups

- Data lives in the app's `QStandardPaths` folders, organisation `net.mowerk`, application `harbour-rustrx` (`src/main.cpp:102-103`): the data folder (session, store, media, logs; extra accounts in `accounts/<n>/`), the config folder (QSettings, the `background` flag, which the stand-in's unit looks for at `%h/.config/net.mowerk/harbour-rustrx/background`), and the cache folder (prepared uploads, shared copies). Under sailjail the data folder should be `~/.local/share/net.mowerk/harbour-rustrx/`; the comment at `src/main.cpp:185` names an older path. Check on a phone.
- Files written to Downloads (exported media, `Downloads/Rustrx/`; collected logs) are readable by every app with the Downloads permission.
- Backups: this guide has not checked what Sailfish's backup tool includes from these folders. The store's values are encrypted with a key that stays in Sailfish Secrets, but `session.json` is plain on phones without Secrets (below).

### The Sailfish sandbox

- Apps run under sailjail (firejail underneath). The profile is the `[X-Sailjail]` section of `harbour-rustrx.desktop`: `Permissions=Internet;Pictures;Videos;Music;Documents;Downloads;RemovableMedia;MediaIndexing;Camera;Audio;Microphone;AppLaunch;Secrets;WebView;UnifiedPush`. What each permission opens (paths, D-Bus names) is defined by Sailfish OS's permission files, not in this repository.
- All three processes run in this one profile: the app through invoker, the stand-in through `sailjail -p harbour-rustrx.desktop` (`systemd/harbour-rustrx.service`), push wake likewise (`dbus/org.unifiedpush.Connector.rustrx.service`).

### Three processes and the bus-name lock

- One binary, three modes (`src/main.cpp`): the app; the stand-in (`RUSTRX_BACKGROUND=1`, a systemd user unit, no window, syncs and notifies); push wake (one push, then exit).
- The session bus name is the only lock on the stores (`src/main.cpp:139-181`). The SDK's cross-process lock is off (`CrossProcessLockConfig::SingleProcess`, `session.rs`, `build_client_with`), so two processes on one store would be a real fault. The core is stopped before the name is released (`aboutToQuit`, `src/main.cpp:190-195`).
- The stand-in and push wake never ask Sailfish Secrets interactively (`src/SecretStore.cpp:45-57`); before the first unlock after boot they get no key and fail, and the unit retries a minute later.

## Secrets and sensitive data

### Access and refresh tokens: session.json

- `core/src/core/session_file.rs`. The file holds the homeserver, the SDK's `MatrixSession` (user id, device id, access and refresh tokens), the sync flavour, and for OAuth logins the client id (`SavedSession`, `core/src/core.rs:66`).
- With a store key, the file is sealed: a fresh `StoreCipher` per write, its export wrapped with `export_with_key` (HKDF-SHA256 in `matrix-sdk-store-encryption`), the JSON encrypted with it (XChaCha20-Poly1305) (`write_session_file`, line 84).
- Without a key (no Sailfish Secrets, for example on community ports) the file is plain JSON. Either way it is written owner-only (`0o600`) through a temporary file and a rename.
- A plain file is sealed at its first read once a key exists (`load_session_file`, line 113). A sealed file without a key is a failed restore, tried again, never a logout. A key that no longer fits drops the file and asks for a new login (`load_or_drop_session_file`, line 127).
- Tests in the same file, each with its planted bug.

### The store key

- Made and kept by `src/SecretStore.cpp`, built as a separate plugin `librustrx-secrets.so` (`secrets/secrets.pro`) so Rustrx still runs where Sailfish Secrets is missing. Loaded at run time with `QLibrary` by `Rustrx::handStoreKeyOn` (`src/Rustrx.cpp:74`).
- The key is 32 bytes from `/dev/urandom`, stored as 64 hex characters in a Secrets collection `rustrx` of its own: `DeviceLock` lock type, `DeviceLockKeepUnlocked` (open after the first unlock until reboot), `OwnerOnlyMode` (`createCollection`, line 85). How Secrets identifies the owning app under sailjail is Sailfish's code, not checked here.
- The key goes from the plugin's buffer to `rustrx_set_store_key` (`core/src/lib.rs:143`), and the C++ copies are wiped (`src/Wipe.h`; audit finding B13). The core keeps it in a process-wide `OnceLock<String>` (`lib.rs:133`) for the life of the process, not wiped. One key serves every account of the phone.
- A new key is made only when Secrets answers twice the same way that there is none, no other Secrets error came before in the same process, and making the collection does not find one that the first answer said was missing (a failed metadata query in Secrets comes back as "no collection"; audit SEC3, `src/StoreKeyGuard.h`). Otherwise the restore fails and Retry asks again. Before a new key is stored, every account's store with `rustrx-encrypted` goes with its `session.json` to `store-lost-<UTC time>/<account>/` in the data folder, outside the folders the start's account sweep and a logout remove; they are put back if Secrets does not keep the new key. Nothing removes `store-lost-*` yet.

### The local stores

- matrix-sdk's SQLite stores (state, crypto, event cache, media) in `store/` per account. **Without a store key** (no Sailfish Secrets, or a store made before Rustrx used it) all of them, the crypto store with the device's keys included, are unencrypted, and so is `session.json`. `store_passphrase` (`session.rs`) reads the key. Until 0.8.2 it went to `SqliteStoreConfig::passphrase`, the passphrase path: the store's cipher exported under PBKDF2-HMAC-SHA256 with 200 000 rounds, which for a random 256-bit key adds cost at every open and no strength.
- **Since 0.8.2:** the 64 hex characters are also valid unpadded base64, so every store is opened through the SDK's high-entropy path (`high_entropy_passphrase(.., Base64Variant::Unpadded)`, `store_config` in `session.rs`): the store cipher is wrapped with HKDF-SHA256 instead of PBKDF2. The SDK tries the key first and falls back to PBKDF2 once, then rewraps the cipher row in one statement, per database, so a crash between the four databases leaves a mix that the next open finishes (tested). The data keys inside each store are unchanged; only their wrapping changed, and all four databases (and every account) are now wrapped under one key, each export with its own random nonce. A build before 0.8.2 cannot open migrated stores: it shows the restore error with Retry and deletes nothing. Marker files `rustrx-key-based-<db>` only record the migration.
- What the SDK encrypts: values are encrypted and keys are hashed with the store cipher; it is not whole-file encryption. Table layout and sizes are visible. Verify for each store which columns stay plain.
- A marker file `store/rustrx-encrypted` says the store is encrypted; a store without it is an old unencrypted one and stays so until a new login. Since 0.8.2 a stable fingerprint (FNV-1a) is kept in `rustrx-key-fingerprint`, and a store is deleted only when opening it proves the key wrong (`prove_store_key`, an AEAD refusal), never because of a fingerprint alone (before, an unstable `DefaultHasher` fingerprint decided it). `store_state` reports the state to the Account tab.
- Drafts are kept in the SDK store (audit I12), not in QSettings.

### Recovery key, room keys, devices

- `core/src/core/account.rs`: `recovery_recover`, `recovery_enable`, `recovery_reset`, `identity_reset` (line 9 onwards). Each new recovery key goes to the UI as a `recovery_key` event; `Rustrx::handleEvent` reads its `recovery_key` field (`src/Rustrx.cpp:1217-1218`), and `qml/pages/settings/RecoveryKeyDialog.qml` shows it once and closes only after "I have saved it".
- Copying it uses `app.copySecret` (`qml/harbour-rustrx.qml:310`): the clipboard is cleared after `Tunables.clipboardKeyMs` (2 minutes) unless something else was copied since. Other apps can read the clipboard meanwhile.
- `export_keys` and `import_keys` (line 59) are the SDK's passphrase-protected room key export, at a path the UI gives.
- An identity reset on an account service (MAS) opens the approval page in the browser. One that asks for a password is refused (`account.rs`, `CrossSigningResetAuthType::Uiaa`).

### Logins

All in `core/src/core/session.rs`. No WebView is used for logins.

- **OAuth 2.0 / account service** (`login_oauth`, line 809): dynamic client registration as a native app, the account service's page in the system browser, the answer on a loopback server on 127.0.0.1 from `matrix_sdk::utils::local_server`. The redirect URI is registered without its port (RFC 8252 section 7.3). State and PKCE are handled by the SDK's `oauth().login()`; confirm that there. The password never passes through Rustrx. The client id is kept in `session.json`.
- **Password** (`login_password`, line 574): `m.login.password` through the SDK. The password travels as a JSON string from QML through the C ABI and is not wiped anywhere.
- **Classic SSO** (`login_sso`, line 610): the SDK's `login_sso` (feature `sso-login`) waits for the browser's redirect with a login token; a 10 minute limit (`SSO_TIMEOUT`) and a cancel. How the SDK receives the redirect was not checked in the SDK's source for this guide.
- **QR login** (`login_qr`, line 725; MSC4108): this device shows a code, a logged-in device scans it, the user types two digits (`qr_check_code`). The other device hands over the secrets through the SDK's secure channel.
- **Link a new device** (`core/src/core/qrlink.rs`, `grant_scanned` line 204, `grant_qr` line 220, `run_grant` line 258): this device lets a new one in and hands over its keys (cross-signing, backup). The account service's consent page opens in the browser; `run_grant` confirms the SDK's `WaitingForAuth` step right after opening it. The camera's frames are decoded by `rustrx_decode_qr` (`lib.rs`) with `rqrr`.
- A device kept from a session the server ended (`relogin.json`, `kept_device` line 443) is reused only for the same user id; otherwise the old store is wiped first.

### Memory

`src/Wipe.h` clears key buffers in C++ with writes the compiler cannot drop. Copies inside Sailfish Secrets, inside the core (the `OnceLock`, the SDK), in QML strings (passwords, recovery keys) and in the JSON passed across the C ABI are not wiped.

### What is logged

- The debug log is on by default until 1.0 (`kDebugLogDefault`, `src/Keys.h:33`); the first start says so. `src/DebugLog.cpp` writes every Qt message (C++ `qInfo` and friends, QML `console.log`) to one file per process in the app's data folder, about 1 MB each plus one older file, and passes it on to the previous handler (the system journal). Settings can collect the logs into a file in Downloads (`DebugLog::collect`, line 125) and clear them.
- The core's tracing goes into the same log as `log` events (`lib.rs`, `LogWriter`). Default filter `warn,rustrx=info,matrix_sdk_crypto::backups=error` (`lib.rs:175`); `RUSTRX_LOG` overrides it.
- **Must never be logged:** access and refresh tokens, the store key, the recovery key, passwords, room key export passphrases, TURN credentials, the push endpoint and connector token, message bodies, file contents.
- What this guide found, from the 144 `tracing::` calls in `core/src`, every `qInfo`, `qWarning`, `qDebug` in `src/` and every `console.log` in `qml/` (read by their arguments, not traced through every error type): commands are logged by name and room id only (`dispatch`, `core/src/core/commands.rs:13`); none of the items above is logged by these lines (the endpoint by its length, `UnifiedPush.cpp:420`; the gateway by its host). Room ids, user ids, event ids, call ids, mxc addresses and verification flow ids are logged.
- Not covered by that reading: every failed command logs its error text (`Core::error`, `core/src/core.rs:273`; `Rustrx.cpp:1402`; `console.log("error:", ...)` in `harbour-rustrx.qml`), which can carry server or SDK text; the SDK's own WARN lines; the panic hook (`lib.rs:195`), whose message is the panic's text; Element Call's console lines, forwarded capped at 600 characters with query strings and long tokens masked (`forward` in `qml/call-bridge.js`, `CallPage.qml`); and the last 400 bytes of `gst-launch-1.0`'s error output when a re-encode fails (`Media.cpp`, in `shrinkVideo`), which can name a file path. The lifecycle audit noted the SDK lines as not read (I11).

### Notifications

- Built in `qml/Service.qml` (`notify`, line 255). By default the room name, the sender and the message text are shown (`chatDetails` and `notifyText`, `src/PrivateSettings.cpp`, in the app's own settings file, not dconf: every app may read and write dconf); switching either off shows "Rustrx" or "N new messages".
- Whether a preview shows on the lock screen is the system's choice. The notification server keeps notifications on disk; this guide has not checked where or how.
- A push wake fetches and decrypts the one event (`core/src/core/push.rs`) to fill the notification.

### Media on disk

- Decrypted copies of received media are in `media/` per account, named after the event id with only ASCII letters and digits kept (`get_media`, `core/src/core/timeline.rs:987`), written through a temporary file and a rename. A sweep once per process keeps the folder under 300 MB, oldest first (`core/src/core/media_cache.rs`). Custom emoji are in `media/emotes/`, named after the mxc address (`fetch_emote`, `timeline.rs:1121`). The SDK keeps its own media cache in the store (encrypted when the store is).
- "Save" and opening a file export a copy into `Downloads/Rustrx/`, named after the message's body with `/`, `\` and control characters replaced, never over an existing file (`create_new`), and refused when the folder is a link (`timeline.rs`, in `get_media`).
- Opening an exported file hands it to the system's handler, except for installer and script extensions (`openFile`, `qml/components/MessageDelegate.qml:280`).

### Metadata on send

- Settings > Privacy has three levels, `metadataLevel` (`src/Keys.h:41-42`, default 2): 0 keeps everything, 1 removes the place, 2 removes all metadata. The orientation is kept.
- Pictures (`Media::sendFile`, `src/Media.cpp:230`): JPEG through `src/Exif.h`, PNG through `src/Png.h`, without re-encoding; anything that does not parse, and every other format, is re-encoded. A picture scaled down is re-encoded anyway. Tests in `tests/cpp/exif` and `tests/cpp/png`.
- Videos: `src/Mp4.h` reads the size and length and blanks metadata boxes in a copy, in place (`VideoStrip`, `Media.cpp:307-322`). A video that cannot be stripped is not sent at all. Tests in `tests/cpp/mp4`.
- "Send smaller videos" (`videoTier`, default off, `Keys.h:38`) re-encodes to WebM with `gst-launch-1.0` as a child process (`Media::shrinkVideo`). The arguments are a `QStringList` given to `QProcess::start`, so no shell is involved. `gst-launch-1.0` joins its arguments and parses them again as a pipeline description, so no file name from outside reaches it: the video goes in through a link whose name Rustrx makes, and both paths must be plain letters, digits, `/`, `.`, `_` and `-` (commit 239ada1; before it, a crafted file name could add to the pipeline).
- The thumbnail of a video comes from Sailfish's `thumbnaild-video` as a new JPEG (`prepareVideo`).

### What Clear media, logout and account removal delete

- **Clear media** (`clear_media`, `account.rs`): `media/` and the SDK's media cache.
- **Logout** (`logout`, `session.rs:897`): ends a call, logs out on the server (15 s limit), removes `session.json` and its temporary file, then `wipe_local_data` (line 488): `store/`, `device_id`, `relogin.json`, `media/`, `avatars/`. The push connector unregisters from the distributor and clears its state (`UnifiedPush::loggedOut`).
- **A session the server ended** (`session_expired`, line 466): only `session.json` goes; the store, media and device stay for a login as the same user.
- **An extra account** whose folder has neither `session.json` nor `relogin.json` is removed at the next start (`src/Rustrx.cpp:240-246`).
- Not removed by any of these: the debug logs (cleared from Settings), exported files in Downloads, the QSettings file, prepared uploads in the cache (swept by age, `Media.cpp`, `sweepOutgoing`), notifications already posted, and the store key in Sailfish Secrets.

## Untrusted input

### Message HTML

- `TimelineModel::renderHtml` (`src/TimelineModel.cpp:283`) turns a message's `formatted_body` into Qt's `Text.StyledText`, a small HTML subset. Tags are matched by a regular expression; only a fixed set passes, without attributes; text between tags has numeric entities decoded and `<` `>` escaped again; links keep only `https`, `http`, `ftp`, `mailto`, `magnet` and `matrix` (`hrefSafe` in `src/Twemoji.cpp:326` escapes quotes); `<img>` becomes a cached custom emoji for `mxc://` addresses and its alt text otherwise, so that a message cannot make the phone fetch a URL; `<mx-reply>` is dropped. Tests: `tests/cpp/renderhtml` (hostile input) and `tests/cpp/golden`.
- `<img>` alt text is escaped for `<` only; check that against StyledText's parser.
- Plain bodies are escaped and linkified in C++ (`StyledText::linkified`, `Twemoji.cpp`).

### Other text from other people

- Silica Labels default to plain text for the whole window (`_defaultLabelFormat: Text.PlainText`, `qml/harbour-rustrx.qml:45`; audit I1; `test_labels_default_to_plain_text`).
- Where QML builds StyledText from remote strings, it is meant to go through one helper, `escaped` in `qml/markup.js` (`test_one_escaping_helper`), or the C++ `StyledText::escaped` (`RoomModel::nameHtml`, `TimelineModel::renderReactions`). The StyledText Labels are in `RoomDelegate.qml`, `MessageDelegate.qml`, `ThreadsPage.qml`, `MembersPage.qml` and `AboutTab.qml`; this guide spot-checked them, not every binding.

### Links

- Every link that leaves the app goes through `openExternal` (`qml/harbour-rustrx.qml:713`): `https`, `http`, `mailto` and `geo` only. `openLink` (line 703) keeps Matrix links in the app. The one other `Qt.openUrlExternally` is the exported file in `openFile`. Both are pinned by `test_external_links_go_through_one_gate`.

### QML engines without network

- Both QML engines (the window's and `Service.qml`'s) get a network access manager that cannot reach the network (`src/LocalOnly.h`; `src/main.cpp:209` and `:260`). Every picture they show is meant to be a local file, so that a markup bug does not turn into a fetch from QML.
- The exception is Element Call: `qml/pages/CallPage.qml` is a Sailfish WebView (Gecko) that loads `https://call.element.io` (`ELEMENT_CALL_URL`, `core/src/consts.rs:22`) with network access. `qml/call-bridge.js` passes widget API messages only for that origin. The widget gets the capabilities it asks for; that was decided and documented (`docs/CALLS.md`, audit I17).

### Pictures and media

- Received pictures are decoded by Qt 5.6's image plugins on the phone, and received audio and video by GStreamer through QtMultimedia. These are system libraries; Rustrx does not control their versions.
- The file's extension on disk comes from sniffing its first bytes (`sniff_extension`, `timeline.rs:1187`: jpg, png, gif, webp, mp4, webm, ogg), else from the sender's MIME type, else `bin`. Qt picks its reader by that extension.
- Rustrx's own parsers read the user's own files before sending, not received ones: `src/Exif.h`, `src/Png.h`, `src/Mp4.h`, with bounds checks and tests. `Mp4::read` also reads a received or shared video's header before a re-encode.
- The room's avatar provider (`src/AvatarProvider.h`, `src/RoundAvatar.h`) reads local files only, by the path in the image id that QML gives it.

### JSON between core and UI

- Commands are one Rust enum, `core/src/protocol.rs` (`parse`, line 348): an object with `"cmd"`, its fields typed by serde. Unknown commands, missing fields and ill-typed fields are refused before anything runs; extra fields are accepted on purpose (`lenient_where_documented`). Tests in the same file (`hostile_commands_are_refused`, `arrays_are_refused`, `required_fields_are_enforced`). `docs/PROTOCOL.md` is written from it; `tests/test_static.py` checks that every sent command and field exists and is documented.
- Events go the other way as JSON with `"ev"`; `Rustrx::handleEvent` reads them on the GUI thread.

### The FFI boundary

- The C functions are in `src/rustrx_core.h` and `core/src/lib.rs`. Commands return at once; replies are events on a callback that fires on tokio threads, and `Rustrx.cpp` hops to the GUI thread before touching Qt state.
- `panic = "unwind"` is required (`core/Cargo.toml:45`): command handlers run under `catch_unwind` (`commands.rs`, `dispatch`) and answer a panic with an error event, and `rustrx_shutdown` catches its own (`lib.rs:78-92`). The other exported functions have no `catch_unwind`; a panic escaping an `extern "C"` function aborts the process.
- `rustrx_decode_qr` trusts the caller's width, height and stride to describe the buffer.

### Calls

- Classic 1:1 calls: `src/Voip.cpp` drives webrtcbin; the core only carries `m.call.*` events (`core/src/core/call.rs`) and the TURN credentials (`turn_server`, line 162), which go into webrtcbin's TURN URL percent-encoded (`Voip::turnUrl`, line 52). Remote SDP is parsed by GStreamer.
- Answers, candidates and hangups are taken only from the party that the call is bound to, and a ring's lifetime is capped (audit findings S1, S5, S5b). A relay-only switch in Settings > Privacy keeps the phone's IP addresses from the peer (S6).
- Tests: `tests/cpp/voip` (two engines calling each other).

### Emoji packs and custom emoji

- `core/src/core/packs.rs`: packs from room state and account data; an image is taken only with an `mxc://` URL, fetched through the homeserver's media API and cached by address (`fetch_emote`). Pack names and shortcodes reach Labels, which default to plain text; this guide has not traced every place they are shown. Design: `docs/EMOJI.md`.

### Room links and matrix.to

- `openMatrixUri` (`qml/harbour-rustrx.qml:630`) takes `matrix:r/`, `matrix:roomid/`, `matrix:u/` and `https://matrix.to/#/` links, with `via` parameters. A user link opens `StartChatDialog`; a room link opens `JoinPage`, which asks the homeserver for a preview at once (`room_preview`) and joins or knocks only from its buttons.

### Slash commands

- `qml/commands.js` only parses the user's own text (`/me`, `/shrug`, `/plain`, `/topic`, `/nick`, `/invite`, `/join`; unknown ones are not sent). Tested under node (`tests/test_commands.py`). Markdown in the user's own messages is rendered by the SDK; HTML the user types passes into `formatted_body`, which is the receiving client's problem, as in other clients.

## Earlier audits

`docs/audits/` (index in `docs/audits/README.md`; `docs/AUDIT-2026-11-16.md` only points there). Each audit has a narrative and a findings ledger (`.findings.json`) with each finding's status and commit. By their method sections, the code reading was done by LLM agents under the author's direction, and the memory audit was a measurement on the phone; none was an independent review.

- **2026-11-16, structure** (0.4.6): layout and double work. All 25 findings fixed or decided.
- **2026-11-16, lifecycle and untrusted input** (0.4.7): the most relevant one for security. Two agents read every spawn, lock and timeout, and every path from servers and other apps into StyledText, links, shares, D-Bus and the log. Findings I1 to I17 cover Label AutoText, a `< img` bypass of the HTML filter, href quote injection, unescaped names, links starting chats, `geo:` and server URLs opened unchecked, shares reading Rustrx's own files, the dev hook, the push token, the log's contents and drafts in plain QSettings. All are marked fixed except I17 (Element Call's capabilities, decided). I9 says session commands are refused through the dev hook; the current allowlist (`src/DevHook.h`) does allow a password login.
- **2026-11-18, memory** (0.4.9): glibc arenas and store pools. Open: M3 (a native crash not reached), M4, M5.
- **2026-11-19, calls and lifecycle** (0.6 work): includes security findings S1 to S10 (call spoofing, planted links into private folders, log copy through a link, the dev hook, IP exposure in calls, share time-of-check, opening received files, metadata coverage, plain `session.json`), all fixed. Open: V9, D1, DOC, BG; K9 partly fixed.

None of the method sections names the dependency tree, the SDK's use of the store cipher or the sandbox profile as in scope.

## Open questions to start with

These are questions, not findings. Each one is a place where this guide could not settle the answer from the code.

1. **D-Bus callers.** The methods on `net.mowerk.harbour_rustrx` (`src/Rustrx.h:565-613`) include notification actions that send a reply or a reaction as the user, and `quit`, `activate`, `openUrl`. No slot checks the caller. Which processes can reach this name under sailjail: only lipstick, every unsandboxed process of the user, other sandboxed apps? Is a caller check needed for the actions that send?
2. **The bus name as lock.** Can another process take `net.mowerk.harbour-rustrx` or `org.unifiedpush.Connector.rustrx` first, and what follows (Rustrx not starting, pushes going elsewhere)?
3. **gst-launch-1.0 arguments.** `gst-launch-1.0` reads its arguments as a pipeline description (above); the paths are now made by Rustrx. Would building the pipeline through the GStreamer API be the better design?
4. **openFile's denylist.** `MessageDelegate.qml:279` refuses installer and script extensions by the last extension of a name the sender chose. Is an allowlist by sniffed type safer? What does the system's handler do with a name such as `x.rpm ` or a type it sniffs from content?
5. **Store fingerprint.** Settled in 0.8.2: a stable fingerprint, and a store is removed only when the open proves the key wrong (above). Left: builds before 0.8.2 still decide by the old `DefaultHasher` fingerprint, which 0.8.2 leaves in place for them.
6. **The KDF.** Since 0.8.2 the stores use the high-entropy path (above). Check the migration and that one key wrapping all four databases (and every account) is acceptable; `session.json` is sealed with an HKDF of the hex text, so its wrapping key differs from the stores'.
7. **Plain http.** `build_client_with` accepts an `http://` homeserver as typed. Should a password login over plain HTTP be refused or warned about?
8. **Error texts in the log.** `Core::error` logs every failed command's error with `{err:#}`. Can an SDK or server error string carry a secret (a URL with a login token, a request body)? Can a serde error for a hostile command echo a field's value? Can a panic message carry message text (string slicing panics quote the string)?
9. **The journal.** The debug log goes to the system journal as well (`DebugLog.cpp`, `s_previous`). Who can read that journal on Sailfish OS?
10. **Cache name collisions.** Media cache names keep only ASCII letters and digits of the event id (`timeline.rs`, `safe` in `get_media`); emote cache names map every other character to `_` (`fetch_emote`). In room versions where a server chooses event ids, can two events, or two mxc addresses, map to one file and show another's content? `find_existing` checks the size where the event gives one.
11. **Share data branch.** In `ShareAdaptor::share`, data that arrives inline (not as a path or descriptor) is written with a plain `QFile` open under the sender's file name, without `O_EXCL | O_NOFOLLOW` and without the size cap the other branches have. The folder is Rustrx's own cache; is that enough?
12. **Link a new device.** `run_grant` (`qrlink.rs:258`) confirms the SDK's `WaitingForAuth` step as soon as the consent page opens. Check in the SDK what that confirmation allows before the account service's consent is given, and when the keys are sent.
13. **The FFI functions without catch_unwind** (`rustrx_start`, `rustrx_command`, `rustrx_command_to`, `rustrx_decode_qr`, `rustrx_set_store_key`): can any of them panic on input (camera frames into `rqrr`, for one)?
14. **Notifications at rest.** Where does the notification server keep message previews, and for how long? Should `notifyText` default to off?
15. **Backups.** What does Sailfish's backup take from the app's folders, and is a plain `session.json` (phones without Secrets) in it?
16. **Element Call.** The WebView loads remote code with network access and gets every widget capability it asks for (I17). Is pinning or limiting those capabilities feasible?

## Build, tests, dependencies, CI

### Build

- The core: `core/build-aarch64.sh`, `cargo zigbuild --locked --release` for aarch64 glibc 2.38, with rustc 1.99.1 and zig 0.17.1 for the releases. `Cargo.lock` is in the tree; `--locked` fails a build that would change it.
- The app: qmake and `rpm/harbour-rustrx.spec` in the Sailfish Platform SDK; `secrets/` is built as its own plugin. The RPM installs the core at `/usr/share/harbour-rustrx/lib/librustrx.so`.
- A host build for reading and tests needs no phone: the test suites below build against the host's Qt 5.

### Tests

- `python3 -m pytest -q tests`: `tests/test_static.py` (wiring between Rust, C++ and QML, the link gate, the escaping helper, the dev hook allowlist, the audits' ledgers), `tests/test_commands.py` and `tests/test_transcript.py` (`qml/commands.js` and `qml/transcript.js` under node; skipped without node).
- `tests/cpp/run.sh`: QtTest suites against the host's Qt 5 (renderhtml, golden, exif, png, mp4, devhook, sharecopy, handover, voip and more). Needs Qt 5 qmake and GStreamer development packages (see `ci.yml`).
- `cargo test --locked` in `core/`: the protocol, the session file, the media cache, the QR reader, shutdown. Slow, because it compiles matrix-sdk.
- Every test names the bug planted to prove it can fail (`Planted-bug:` in its comment).

### Dependencies

- `core/Cargo.lock` pins about 445 crates, most of them matrix-sdk's tree. Direct dependencies are in `core/Cargo.toml`; `matrix-sdk-store-encryption` and `matrix-sdk-base` are pinned to `=0.19.1`.
- No `cargo audit`, `cargo deny`, `deny.toml`, Dependabot or `SECURITY.md` is set up yet. Running `cargo audit` and `cargo deny check` against `Cargo.lock` is a good first step.
- On the phone, Rustrx uses the system's Qt 5.6, GStreamer (with webrtcbin, libnice), speexdsp, PulseAudio and Sailfish Secrets (`rpm/harbour-rustrx.spec`, `Requires:`). Their versions are Sailfish OS's.

### CI

- `.github/workflows/ci.yml`: the pytest suite and the C++ suites on every push and pull request (ubuntu-24.04).
- `.github/workflows/nightly.yml`: `cargo test --locked` nightly with rustc 1.99.1.
- Actions are referenced by tag or branch, not by commit hash (`actions/checkout@v5`, `Swatinem/rust-cache@v2`, `dtolnay/rust-toolchain@master`).
- No workflow builds the core or the RPM for a release.

## Reporting and credit

- **Findings:** open a GitHub issue on the repository, one per finding, with file and line, what you saw and what you expect.
- **Anything sensitive** (a way to read someone's messages or keys, run code, or act as the user): use GitHub's private vulnerability reporting on the repository ("Report a vulnerability" under the Security tab) instead of a public issue.
- **Credit:** the author's stance is "I will credit everyone who reviews as a co-author who took ownership of the code by reviewing it." Reviewers are named in the README's Credits section and on Settings > About.
