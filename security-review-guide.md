# Security review guide: Rustrx

For a reviewer who looks at Rustrx for security and maintainability: what it relies on, where the trust boundaries are, where secrets live, how untrusted input travels, what earlier audits covered, and where to start. It assumes no knowledge of Sailfish OS. The general map of the code is [review-and-architecture-hints.md](review-and-architecture-hints.md); read its Architecture section first.

Every claim here points at the code by file and function. Line numbers are left out, because they drifted after every audit (0.8.4 audit, FFI-13); grep for the name. Where this guide is unsure, it says so. If the code and this file disagree, the code is right and this file has a bug. The state described is that of `b0d9102` (0.8.4).

## What Rustrx is

Rustrx is a native Matrix client for Sailfish OS, a Linux phone system with a Qt 5.6 and QML user interface (Silica is its widget set). Three layers:

- `core/`: a Rust crate on matrix-rust-sdk 0.19.1 (`core/Cargo.toml`, `core/Cargo.lock`), built as `librustrx.so`.
- `src/`: a C++ bridge (Qt 5.6) from the core's callback to the GUI thread, list models, media handling, D-Bus, calls.
- `qml/`: the user interface.

### What the SDK does, and what Rustrx does on top

matrix-rust-sdk does all of the Matrix cryptography: Olm and Megolm, end-to-end encryption and decryption, device and user verification (SAS), cross-signing, key backup and recovery, the encrypted SQLite stores (`matrix-sdk-sqlite`, `matrix-sdk-store-encryption`), OAuth 2.0 login, the QR login and grant flows (MSC4108), token refresh. Rustrx calls these APIs. It implements no cryptographic primitives of its own; it does make the store key, seals `session.json` with the SDK's `StoreCipher` (below), and computes one HMAC of its own for the bus-name proof (`src/NameProof.h`, Qt's `QMessageAuthenticationCode`).

Rustrx adds, and this is where a review pays off most:

- where the store key comes from and how it reaches the SDK (Sailfish Secrets, below);
- sealing `session.json` with the SDK's `StoreCipher`;
- every path from other people's content to the screen: the HTML rewriting in C++, links, pictures and media files on disk, notifications;
- every way other apps on the phone reach Rustrx: D-Bus methods, notification actions, the share target, URL handlers, push;
- the checks the 0.8.4 audit added before a login, a device grant and a ring (`core/src/core/checks.rs`, `call.rs`);
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
- TLS is rustls with aws-lc-rs through reqwest (`core/Cargo.toml`, feature `rustls-aws-lc-rs`), the system's CA bundle through rustls-platform-verifier, TLS 1.2 at least. Rustrx sets no TLS options of its own (`build_client_with` in `session.rs`), never disables certificate checks and makes no HTTP requests outside the SDK. Not settled: whether the platform verifier checks revocation on Linux (probably not), whether an `HTTP(S)_PROXY` in the environment would be used, and whether libnice checks a `turns:` server's certificate (audit 0.8.4 NET-12).
- **Plain http** is refused for every login unless the server is on the phone or the home network: localhost and IP literals in 127/8, `::1`, 10/8, 172.16/12, 192.168/16 and fc00::/7; a name such as `server.lan` counts as remote. That holds whether the address was typed, came from `.well-known` or from a QR code (`checks::insecure_http`, `refuse_insecure`, called in `build_client_with`, `probe_server` and the QR paths; NET-3). A saved http session goes on, with a log line.
- Server-supplied URLs that leave the app (SSO and OAuth pages, identity reset approval, device management pages) go through `openExternal` (below).

### Other Matrix users and their content

Everything another user or their server sends is untrusted: message bodies and `formatted_body` HTML, links, media files and their claimed MIME types, names and sizes, room state (names, topics, avatars, power levels), display names, custom emoji and image packs (MSC2545, `core/src/core/packs.rs`), reactions, call events (`m.call.*`), verification requests, push payloads, event ids from old room versions. See "Untrusted input" below.

### Other apps on the phone

This guide assumes that any other process of the user may call Rustrx's D-Bus methods, and that an unsandboxed one can also read Rustrx's files; for the latter, D-Bus checks are only defence in depth.

Rustrx is reachable in these ways:

- **The app's D-Bus name** `net.mowerk.harbour-rustrx`, object `/net/mowerk/harbour_rustrx`, interface `net.mowerk.harbour_rustrx` (`src/Keys.h`). The exported methods are the `Q_SCRIPTABLE` slots in `src/Rustrx.h`, registered with `ExportScriptableSlots`: opening rooms, `openUrl`, `open`, `activate` and `activateWith`, `quit`, the notification actions (reply, react, mark read, answer and decline a call), and `dev`.
- **The notification actions are gated** (`src/NotifyGate.h`, audit 0.8.4 IPC1). Reply, emoji, Mark as read, Answer, Decline and the stand-in's Quit are taken only from a caller running as the same Unix user (the bus's `GetConnectionUnixUser` against `getuid()`, asked at every call; a failed lookup refuses), and only for a notification Rustrx has up and the action matching it: Reply and Mark as read for a room with a line, an emoji for that line's event and one of its emoji, Answer for a room that rings, Decline for that ring's event. What is up is listed in `notified.json` in the data folder, written by `Service.qml` as it posts and closes notifications. An Answer for a ring that is over only opens the room (`NotifyGate::answerFor`). An earlier rule, the owner of `org.freedesktop.Notifications` only, failed on the phone: notification actions arrive from a short-lived helper process of the user, not from the notification server (c8de0de).
  - **The residual risk**, plainly: any app of the user can take an action of a notification that is listed as up, for example answer a ringing call or reply into a room that has a line. A line swiped away while no Rustrx process ran stays listed until the room's next line or its close. Up to 0.8.3 there was no check at all.
  - **Quit** is taken only in the stand-in, and only while its background line is up or within `kQuitGraceMs` (5 s) after it closed, because lipstick closes the line as Quit is tapped, before the helper's call arrives (`NotifyGate::mayQuit`, `StandInLine`; 4833d40). With "Show the background line" off there is no line, and no app can end the stand-in. From a shell, `quit` needs the dev flag.
  - **Still open to any caller:** the open actions (`openRoomFromNotification`, `openAccountRoom`, `openKnocks`, `openVerification`, `open`) and `openUrl`, as the launcher and links need them. `activate()` only shows the window.
- **The bus-name proof** (`src/NameProof.h`, IPC2, IPC6). Every Rustrx process answers at `/net/mowerk/harbour_rustrx/proof` from the moment it holds a name, with an HMAC under the data folder's `bus-secret` over the name, its own unique name and the asker's nonce, and only for a name it holds itself, so a squatter can neither answer nor relay. A holder with no right answer within `kProofWaitMs` counts as another app. A second launch makes the stand-in leave only with proof of the secret, bound to the stand-in's unique name (`activateWith`, `NameProof::leaveToken`), so the secret never goes to whoever holds the name.
- **The dev hook** `dev(json)` (`Rustrx::dev`): answered only while a file `dev-commands` exists in the app's data folder (no package installs it), and then only the requests in the allowlist of `src/DevHook.h` (`refusal()`), tested in `tests/cpp/devhook` and `tests/test_static.py` (`test_dev_hook_takes_only_the_test_commands`). The allowlist includes a password login (`{"login": "password", ...}`) and the add-account page, for tests against a local server: with the flag file present, any unsandboxed caller can add an account (IPC8, open). TEST ONLY: `{"cmd": "dev_ring", "room_id"}` makes the core feed a made-up `m.call.invite` from the room's other member through the real path (`dev_ring` in `core/src/core/call.rs`, which checks for the `dev-commands` file itself too). The debug switches in dconf (`DebugValue`) act only while the same file exists (`rustrx.devMode`).
- **The share target** (`X-Share-Methods` in `harbour-rustrx.desktop`; `src/ShareAdaptor.cpp`, `src/ShareCopy.h`): files and text from other apps' share sheets. A path is opened with `O_NOFOLLOW`, must be a regular file, and must not resolve into Rustrx's own folders (`ShareAdaptor::mayShare`). Every branch (path, descriptor, inline data) is copied at share time into a 0700 folder of its own under `cache/shared/`, made new (`O_EXCL|O_NOFOLLOW`, 0600), under one size cap (`kShareMaxBytes`, 512 MiB), pruned by folder after a day (IPC5). The copy is sent. A path is read with Rustrx's rights (Documents, Downloads and the other media folders), so a sharing app can name a file it could not read itself; it goes only to a room the user picks, after `ConfirmSendPage` shows its name (IPC11, accepted).
- **URL handlers**: `x-scheme-handler/matrix`, `x-url-handler/matrix.to` and `www.matrix.to` (`harbour-rustrx.desktop`), delivered over D-Bus to `openUrl`, parsed in `openMatrixUri` (`qml/harbour-rustrx.qml`). A user link shows a dialog before a chat starts; a room link shows the join page first (below).
- **UnifiedPush**: the distributor (Foghorn) talks to the connector name `org.unifiedpush.Connector.rustrx` (`src/UnifiedPush.cpp`, `docs/PUSH.md`). Calls carry a token Rustrx made (`registerApp`, a random UUID) and are ignored without it (`fromDistributor`). An endpoint that is not `https://` is ignored. A push names only a room and an event; the content is fetched from the homeserver. Since 0.8.4:
  - the distributor is matched by its exact name `org.unifiedpush.Distributor.Foghorn` (it was a suffix), the first one is kept in `push.json`, and another one needs the user's yes (`pages/DistributorDialog.qml`, IPC3);
  - a connector name held by a program that cannot give the proof above pauses push: no registration, the pusher dropped, `blocked` in `push.json` so that the stand-in runs, and a warning in red in Settings (IPC2). Only a name that has an owner can be squatted: a free name that the sandbox may not own is no squat (b0d9102);
  - Reply in a notification always goes to the app's own name, so the typed text reaches no other app; Mark as read and emoji go to the connector only while push is on, a distributor is there and the name is ours (IPC2).
- **Push wake**: `dbus/org.unifiedpush.Connector.rustrx.service` starts the binary with `RUSTRX_PUSH_WAKE=1` inside sailjail when a push arrives and nothing holds the connector name. A push-woken process that finds the app's bus name held by a program that fails the proof posts "Rustrx could not start" once per boot (`qml/NameTakenNote.qml`) and leaves (IPC6).

### The local filesystem and backups

- Data lives in the app's `QStandardPaths` folders, organisation `net.mowerk`, application `harbour-rustrx` (`src/main.cpp`): the data folder (session, store, media, logs, `bus-secret`, `notified.json`, `secrets-collection`; extra accounts in `accounts/<n>/`), the config folder (QSettings with the private settings, and the `background` flag, which the stand-in's unit looks for at `%h/.config/net.mowerk/harbour-rustrx/background`), and the cache folder (prepared uploads, shared copies, voice recordings, notification icons, the collected log). Under sailjail the data folder should be `~/.local/share/net.mowerk/harbour-rustrx/`. Check on a phone.
- Files written to Downloads (exported media, `Downloads/Rustrx/`) and Documents (key exports) are readable by every app with that permission. Collected logs stay in the app's cache and go to the share sheet (below).
- Only `session.json`, `push.json`, share copies and stripped videos are 0600; the rest gets the process umask. That separates nothing on Sailfish OS, where all apps run as one user and sailjail's whitelists are the boundary (SEC14).
- Backups: on a 5.1 port image no jolla-vault unit reads Rustrx's data, config or cache folders, so neither a plain `session.json` nor the store is in a backup (audit 0.8.4, Q15). Jolla's own images may ship more units: check `ls /usr/share/jolla-vault/units/` on a phone.

### The Sailfish sandbox

- Apps run under sailjail (firejail underneath). The profile is the `[X-Sailjail]` section of `harbour-rustrx.desktop`: `Permissions=Internet;Pictures;Videos;Music;Documents;Downloads;RemovableMedia;MediaIndexing;Camera;Audio;Microphone;AppLaunch;Secrets;WebView;UnifiedPush`. What each permission opens (paths, D-Bus names) is defined by Sailfish OS's permission files, not in this repository.
- All three processes run in this one profile: the app through invoker, the stand-in through `sailjail -p harbour-rustrx.desktop` (`systemd/harbour-rustrx.service`), push wake likewise (`dbus/org.unifiedpush.Connector.rustrx.service`).
- **Private settings**: the settings another app could misuse (receipts, typing, presence, notification text and details, stranger invites, closed mode, the last room, search history, the ask-once notices) live in Rustrx's own settings file instead (`src/PrivateSettings.cpp`, moved out of dconf once by `DconfOld`; SEC1). The look, `forceCallUi` and the debug switches stay in dconf.

### Three processes and the bus-name lock

- One binary, three modes (`src/main.cpp`): the app; the stand-in (`RUSTRX_BACKGROUND=1`, a systemd user unit, no window, syncs and notifies; not started without a saved login, and gone at once with exit 0 when another Rustrx holds the name, b0d9102); push wake (one push, then exit).
- The session bus name is the only lock on the stores. The SDK's cross-process lock is off (`CrossProcessLockConfig::SingleProcess` in `build_client_with`), so two processes on one store would be a real fault. The core is stopped before the name is released (`aboutToQuit` in `src/main.cpp`).
- The stand-in and push wake never ask Sailfish Secrets interactively (`interaction()` in `src/SecretStore.cpp`) and never make a store key; before the first unlock after boot they get no key and fail, and the unit retries a minute later.
- A ring in a window-less process starts the app with the call handed over (`src/Handover.cpp`, each field checked before it is taken); the handover file is the app's alone.

## Secrets and sensitive data

### Access and refresh tokens: session.json

- `core/src/core/session_file.rs`. The file holds the homeserver, the SDK's `MatrixSession` (user id, device id, access and refresh tokens), the sync flavour, and for OAuth logins the client id (`SavedSession`, `core/src/core.rs`).
- With a store key, the file is sealed: a fresh `StoreCipher` per write, its export wrapped with `export_with_key` (HKDF-SHA256 in `matrix-sdk-store-encryption`, over the 64 hex characters, so its wrapping key differs from the stores'), the JSON encrypted with it (XChaCha20-Poly1305) (`write_session_file`).
- Without a key (no Sailfish Secrets, for example on community ports) the file is plain JSON. Either way it is written owner-only (`0o600`) through a temporary file and a rename.
- A plain file is sealed at its first read once a key exists (`load_session_file`). A sealed file without a key is a failed restore, tried again, never a logout. Only a cipher the key cannot unwrap (an AEAD refusal: the secret was made anew) removes the file; a damaged file stays and is a failed restore (`read_session_file`, `load_or_drop_session_file`; SEC9).
- A sealed file is not bound to its account: moved into another account's folder it opens (same key). That needs write access to the data folder.
- Tests in the same file, each with its planted bug.

### The store key

- Made and kept by `src/SecretStore.cpp`, built as a separate plugin `librustrx-secrets.so` (`secrets/secrets.pro`) so Rustrx still runs where Sailfish Secrets is missing. Loaded at run time with `QLibrary` by `Rustrx::handStoreKeyOn`; its entry is `rustrx_secrets_store_key2` (key, flags, state).
- The key is 32 bytes from `/dev/urandom`, stored as 64 hex characters in a Secrets collection of its own: `DeviceLock` lock type, `DeviceLockKeepUnlocked` (open after the first unlock until reboot), access for its owner only.
- **The collection's name** is drawn once at random for a new install (`rustrx` plus 16 letters or digits, kept in `secrets-collection` in the data folder, written only by the app); an install from before keeps `rustrx`, the name its key lies under (`StoreKeyGuard::collectionName`; SEC2). Collection names are global in Secrets, so another app could have made one of that name first: Secrets then refuses Rustrx (PermissionsError), which is its own state, `secrets_refused` on the Account tab, not "no Secrets". The next login draws a fresh name, never while a store still needs the key under the old one.
- The key goes from the plugin's buffer to `rustrx_set_store_key` (`core/src/lib.rs`), and the C++ copies are wiped (`src/Wipe.h`; audit B13). The core keeps it in a process-wide `OnceLock<String>` for the life of the process, not wiped; its other copies are `Zeroizing` (`StoreSecret`, `store_passphrase`; SEC13). One key serves every account of the phone.
- **A new key** is made only by the app, only when Secrets answers twice the same way that there is none, no other Secrets error came before in the same process, and making the collection does not find one that the first answer said was missing (a failed metadata query in Secrets comes back as "no collection"; `src/StoreKeyGuard.h`). With an encrypted store on the phone and no key, the app asks first (`StoreKeyLostDialog`, SEC3): "Start over" makes a key and moves every account's store with `rustrx-encrypted` and its `session.json` to `store-lost-<UTC time>/<account>/` in the data folder, outside the folders the account sweep and a logout remove; they are put back if Secrets does not keep the new key. "Not now" keeps every account at the start page, and the next start asks again. Nothing removes `store-lost-*` yet.

### The local stores

- matrix-sdk's SQLite stores (state, crypto, event cache, media) in `store/` per account. **Without a store key** (no Sailfish Secrets, or a store made before Rustrx used it) all of them, the crypto store with the device's keys included, are unencrypted, and so is `session.json`. `store_passphrase` (`session.rs`) reads the key. Until 0.8.2 it went to `SqliteStoreConfig::passphrase`, the passphrase path: the store's cipher exported under PBKDF2-HMAC-SHA256 with 200 000 rounds, which for a random 256-bit key adds cost at every open and no strength.
- **Since 0.8.2:** the 64 hex characters are also valid unpadded base64, so every store is opened through the SDK's high-entropy path (`high_entropy_passphrase(.., Base64Variant::Unpadded)`, `store_config` in `session.rs`): the store cipher is wrapped with HKDF-SHA256 instead of PBKDF2. The SDK tries the key first and falls back to PBKDF2 once, then rewraps the cipher row in one statement, per database, so a crash between the four databases leaves a mix that the next open finishes (tested). The data keys inside each store are unchanged; only their wrapping changed, and all four databases (and every account) are now wrapped under one key, each export with its own random nonce. The 0.8.4 audit found this fine: each database keeps its own random data key and MAC seed (Q6). A build before 0.8.2 cannot open migrated stores: it shows the restore error with Retry and deletes nothing. Marker files `rustrx-key-based-<db>` only record the migration.
- What the SDK encrypts: values are encrypted and keys are hashed with the store cipher; it is not whole-file encryption. What stays plain (matrix-sdk-sqlite 0.19.1, SEC12): table layout, row counts, value sizes (the ciphertext length follows the plaintext's), the event cache's relation type per event, chunk positions and event counts per room, the media store's access times and sizes, the send queue's transaction ids, priorities and times, thread subscription states, and a few crypto flags. Someone with the files but not the key learns the shape and times of activity, not content. The receipt and state-event inserts were not read.
- A marker file `store/rustrx-encrypted` says the store is encrypted; a store without it is an old unencrypted one and stays so until a new login. Since 0.8.2 a stable fingerprint (FNV-1a) is kept in `rustrx-key-fingerprint`, and a store is deleted only when opening it proves the key wrong (`prove_store_key`, an AEAD refusal), never because of a fingerprint alone. `store_state` reports the state to the Account tab.
- Drafts are kept in the SDK store (audit I12), not in QSettings.

### Recovery key, room keys, devices

- `core/src/core/account.rs`: `recovery_recover`, `recovery_enable`, `recovery_reset`, `identity_reset`. Each new recovery key goes to the UI as a `recovery_key` event, which `Rustrx::handleEvent` passes on, and `qml/pages/settings/RecoveryKeyDialog.qml` shows it once and closes only after "I have saved it".
- Copying it uses `app.copySecret` (`qml/harbour-rustrx.qml`): the clipboard is cleared after `Tunables.clipboardKeyMs` (2 minutes) unless something else was copied since. Other apps can read the clipboard meanwhile.
- `export_keys` and `import_keys` are the SDK's passphrase-protected room key export. The export is written by the SDK into the data folder, then copied into Documents new only and never through a link (`export_new_file` in `timeline.rs`); a second export on a day gets `-2`, `-3` before the extension, and the private copy is removed (FFI-4).
- An identity reset on an account service (MAS) opens the approval page in the browser. One that asks for a password is refused (`account.rs`, `CrossSigningResetAuthType::Uiaa`).
- The SDK enables a key backup only when the recovery key's public key matches the server's backup version; a backup the server made with its own key is not adopted (checked in the 0.8.4 audit).

### Logins

All in `core/src/core/session.rs`, with the refusals in `checks.rs`. No WebView is used for logins.

- **OAuth 2.0 / account service** (`login_oauth`): dynamic client registration as a native app, the account service's page in the system browser, the answer on a loopback server on 127.0.0.1 from `matrix_sdk::utils::local_server`. The redirect URI is registered without its port (RFC 8252 section 7.3). State and PKCE are made and checked by the SDK (`oauth().login()`), so a code cannot be injected; a local process can still answer the loopback first with an error, whose text the login page then shows (NET-10, no fix needed). The password never passes through Rustrx. The client id is kept in `session.json`.
- **Password** (`login_password`): `m.login.password` through the SDK. The password travels as a JSON string from QML through the C ABI and is not wiped anywhere; the login field is emptied after a login (NET-13).
- **Classic SSO** (`login_sso`): the SDK's `login_sso` (feature `sso-login`) waits on the same `local_server` (127.0.0.1, a port in 20000 to 29999) for the browser's redirect with a login token. It has no state, and the first request wins, so another app on the phone could answer with its own account's token (NET-7). A 10 minute limit (`SSO_TIMEOUT`) and a cancel.
- **A browser login that ends in another account** than the full Matrix ID typed (SSO and OAuth) is logged out at once and refused (`typed_account_or_undo`, `checks::other_account`; the `other_account` reason of `login_error`). Its store goes, unless it was the typed user's kept one. With only a server typed there is nothing to compare.
- **QR login** (`login_qr`; MSC4108): this device shows a code, a logged-in device scans it, the user types two digits (`qr_check_code`). The other device hands over the secrets through the SDK's secure channel. Not read in the 0.8.4 audit: what this side checks about the homeserver the other device names, and what it does with the secrets bundle.
- **Link a new device** (`core/src/core/qrlink.rs`, `grant_scanned`, `grant_qr`, `run_grant`): this device lets a new one in and hands over its keys (cross-signing, backup). The consent page's address is supplied by the new device, so it is opened only when it is https on the host of the account service's issuer or device endpoint; anything else cancels the grant (`checks::foreign_consent_page`, `qr_grant` `error` with reason `foreign_page`; NET-2). `run_grant` confirms the SDK's `WaitingForAuth` step right after opening it; the 0.8.4 audit found that this is not a problem as asked (Q12). The camera's frames are decoded by `rustrx_decode_qr` (`lib.rs`) with `rqrr`.
- A device kept from a session the server ended (`relogin.json`, `kept_device`) is reused only for the same user id; otherwise the old store is wiped first.

### Memory

`src/Wipe.h` clears key buffers in C++ with writes the compiler cannot drop; the core's store key copies are `Zeroizing`. Not wiped: the copies inside Sailfish Secrets and its D-Bus reply buffers, the core's process-wide `OnceLock`, QML strings (passwords, recovery keys, export passphrases) and the JSON passed across the C ABI. Reading them needs code running as the user or root (SEC13).

### What is logged

- The debug log is on by default until 1.0 (`kDebugLogDefault`, `src/Keys.h`); the first start says so. `src/DebugLog.cpp` writes every Qt message (C++ `qInfo` and friends, QML `console.log`) to one file per process in the app's data folder, about 1 MB each plus one older file, and passes it on to the previous handler (the system journal). The switch gates only the file: with the debug log off the same lines still go to the journal (SEC11, open).
- **Redaction** (SEC7): every line from QML and C++ passes `DebugLog::redact` before the file and the journal: URL queries, a URL's `user:password@` part (TURN), `token` values and `Bearer` tokens are cut (tested in `tests/cpp/debuglog`). Every core line passes `core/src/redact.rs` in `LogWriter` first: URL queries and fragments, values after `access_token`, `refresh_token`, `token=`, `Bearer` and a UIAA `session`, server error bodies other than the errcode and message (cut to 120 characters), serde's echo of string values, and JSON `body` and `formatted_body` values. Status codes, errcodes and ids stay. Check both against the lines in the code, and against what the SDK's own WARN lines print (`http_client` prints a request error with its `Debug`).
- The core's tracing goes into the same log as `log` events (`lib.rs`, `LogWriter`). Default filter `warn,rustrx=info,matrix_sdk_crypto::backups=error`; `RUSTRX_LOG` overrides it.
- **Must never be logged:** access and refresh tokens, the store key, the recovery key, passwords, room key export passphrases, TURN credentials, the push endpoint and connector token, message bodies, file contents.
- Room ids, user ids, event ids, call ids, mxc addresses and verification flow ids are logged. Calls log the ICE candidate types and the selected pair's, never an address.
- Still able to carry text from outside, after redaction: the panic hook (`set_hook` in `lib.rs`) logs the panic's message whole (FFI-8, open; no panic that quotes message text was found); GStreamer's warnings (`Voip.cpp`); Element Call's console lines, capped at 600 characters with queries and runs of 40 or more token characters masked (`forward` in `qml/call-bridge.js`), so a shorter secret would pass; the last 400 bytes of `gst-launch-1.0`'s error output (`shrinkVideo`), whose paths are Rustrx's own.
- **Share the debug log** collects the logs into the app's own cache (`shared-log/`, the one before removed; `Rustrx::shareableLog`, `DebugLog::collect`) and hands that file to the share sheet; nothing goes to Downloads, and copies that earlier builds left there go at the next share (SEC8). sailfish-share runs outside the sandbox and passes the file on.
- **The journal** (Q9, 5.1 port image): volatile, about 1 MB in RAM, readable by root and the `systemd-journal`, `adm` and `wheel` groups; the device user is in none of them. Check `/etc/systemd/journald.conf` and `id` on a phone.

### Notifications

- Built in `qml/Service.qml` (`notify`). By default the room name, the sender and the message text are shown (`chatDetails` and `notifyText`, in `src/PrivateSettings.cpp`, not dconf); switching either off shows "Rustrx" or "N new messages".
- The home screen shows summary and body as plain text (5.1: `textFormat: Text.PlainText` in its notification QML, no `body-markup` announced), so names and bodies are not markup there.
- Whether a preview shows on the lock screen is the system's choice. The notification server keeps a notification until it is closed; Rustrx's message notifications have no expiry, and Rustrx closes a room's notifications when it is read.
- The action gate above binds each action to what Rustrx has up instead of trusting a token in the action's arguments, because those arguments are not secret.
- A logout does not close Rustrx's notifications: the author's decision, so that the notification history stays as the user left it (SEC5, DEC16).
- A push wake fetches and decrypts the one event (`core/src/core/push.rs`) to fill the notification.
- A message's line shows its sender's picture, rounded by Rustrx into a PNG of at most 256 px in the cache's `notification-icons/` (`RoundAvatar::cachedFile`); lipstick never parses a sender's bytes. The folder outlives a logout.

### Media on disk

- Decrypted copies of received media are in `media/` per account, outside the store's encryption by design (CB8; SEC4), named by a SHA-256 of room and event id (`cache_name` in `serialize.rs`; FFI-6), written through a temporary file and a rename (`get_media`, `core/src/core/timeline.rs`). A sweep keeps `media/`, `media/emotes/` included, under 300 MB, oldest first: before the first file of a run, and again after every 50 MB written, sparing what the run gave out (`core/src/core/media_cache.rs`; FFI-3). `avatars/` is not swept by age.
- Custom emoji are in `media/emotes/`, named by a SHA-256 of the whole mxc address (`fetch_emote`): at most 64 asked per message, four downloads at a time, none above 256 KiB kept, and only a JPEG, PNG, GIF or WebP (FFI-2). Over a cap an emoji stays its `:shortcode:`.
- A picture, sticker or GIF larger than 20 MB by the size its event states loads only at a tap, "Large file, tap to load" (`Tunables.mediaAutoLoadMaxBytes`; FFI-3). The size is the sender's claim; there is no hard byte limit on a download.
- The SDK keeps its own media cache in the store (encrypted when the store is), capped at 100 MiB.
- "Save" and opening a file export a copy into `Downloads/Rustrx/`, named after the message's `filename` without bidi or zero-width characters and trailing dots or spaces (`export_name`), never over an existing file: the folder is opened once without following a link and the file created in it with `openat` (`O_EXCL|O_NOFOLLOW`) (`export_new_file`; FFI-5).
- Opening an exported file hands it to the system's handler only for an allowed kind (pictures, audio and video without playlists, PDF, plain text, OpenDocument and MS Office documents) whose name and bytes agree, by the phone's shared-mime-info (`openFile` in `qml/components/MessageDelegate.qml`, `src/OpenCheck.h`; UC5). Anything else is only saved.
- Sent voice messages (`cache/voice/`) and prepared uploads (`cache/outgoing/`) are swept by age once nothing waits on them (`sweepOutgoing` in `Media.cpp`).

### Metadata on send

- Settings > Privacy has three levels, `metadataLevel` (`src/Keys.h`, default 2): 0 keeps everything, 1 removes the place, 2 removes all metadata. The orientation is kept.
- Pictures (`Media::sendFile`): JPEG through `src/Exif.h`, PNG through `src/Png.h`, without re-encoding; anything that does not parse, and every other format, is re-encoded. A picture scaled down is re-encoded anyway. Tests in `tests/cpp/exif` and `tests/cpp/png`.
- Videos: `src/Mp4.h` reads the size and length and blanks metadata boxes in a copy, in place (`VideoStrip` in `Media.cpp`). A video that cannot be stripped is not sent at all. Tests in `tests/cpp/mp4`; 3 million mutated inputs under ASan and UBSan found nothing (0.8.4 audit).
- "Send smaller videos" (`videoTier`, default off) re-encodes to WebM with `gst-launch-1.0` as a child process (`Media::shrinkVideo`). The arguments are a `QStringList` given to `QProcess::start`, so no shell is involved. `gst-launch-1.0` joins its arguments and parses them again as a pipeline description, so no file name from outside reaches it: the video goes in through a link whose name Rustrx makes, and both paths must be plain letters, digits, `/`, `.`, `_` and `-` (commit 239ada1).
- The thumbnail of a video comes from Sailfish's `thumbnaild-video` as a new JPEG (`prepareVideo`).
- A forward of a file from an encrypted room into an unencrypted one uploads a fresh unencrypted copy, so the original's key and address stay out (`forward_copy` in `timeline.rs`; UC15). Other forwards resend the event's content.

### What Clear media, logout and account removal delete

- **Clear media** (`clear_media`, `account.rs`): `media/` and the SDK's media cache.
- **Logout** (`logout`, `session.rs`): ends a call, logs out on the server (15 s limit), removes `session.json` and its temporary file, then `wipe_local_data`: `store/`, `device_id`, `relogin.json`, `media/`, `avatars/`. The push connector unregisters from the distributor and clears its state (`UnifiedPush::loggedOut`).
- **A session the server ended** (`session_expired`, `gone_for_good`): `session.json` goes, and the store, media and device stay for a login again as the same user (3aab50e), unless the session is gone for good (`checks::hard_logout`; SEC6, DEC17). Gone for good means: no refresh token, the 401 said `soft_logout` false, the server has no account service (asked: `server_metadata` answers "not supported"), and the session is a classic one, not OAuth. Then they go as at a logout here. A refused refresh never counts, because Synapse answers a `session.json` that missed saved rotations the same way as a removed device; on a server with an account service (matrix.org, tchncs.de) an expiry and a sign-out elsewhere look the same, so there the data always stays. A refresh that got no answer never ends a session (`session_gone`). The `logged_out` event says `wiped`.
- **An extra account** whose folder has neither `session.json` nor `relogin.json` is removed at the next start (`Rustrx.cpp`).
- Not removed by any of these: the debug logs (cleared from Settings), exported files in Downloads and Documents, the settings files, the notification icons, notifications already posted (DEC16), `store-lost-*`, the Element Call WebView's profile (not checked), and the store key in Sailfish Secrets (the next login reuses it).

## Untrusted input

### Message HTML

- `TimelineModel::renderHtml` (`src/TimelineModel.cpp`) turns a message's `formatted_body` into Qt's `Text.StyledText`, a small HTML subset. Tags are matched by a regular expression; only a fixed set passes, without attributes; text between tags has numeric entities decoded and `<` `>` escaped again; links keep only `https`, `http`, `ftp`, `mailto`, `magnet` and `matrix` (`hrefSafe` in `src/Twemoji.cpp` percent-encodes both quotes, which StyledText's value parser needs and which is enough); `<img>` becomes a cached custom emoji for `mxc://` addresses and its alt text otherwise, so that a message cannot make the phone fetch a URL; `<mx-reply>` is dropped. Tests: `tests/cpp/renderhtml` (hostile input) and `tests/cpp/golden`.
- **Size**: a message that would draw more than 200 000 characters shows "Message too long to show" instead (`TimelineModel::kMaxDrawn`; UC1). The count is what StyledText draws, not the message's length: Qt 5.6 indents each list item by six characters per open list, so 12.8 KB of nested lists drew 15 million characters and took 585 MB. Close tags are counted, and the attribute expressions are compiled once (UC9).
- Known and left (info, no injection): a bare `&` passes into StyledText text and `<img>` alt text, where Qt reads to the next `;` or space and a sender can only garble their own message (UC10); `&amp;` is not decoded in hrefs, so the address opened keeps it (UC11); `attribute()` takes the first textual match, even inside another attribute's value, and the `<mx-reply>` removal is greedy, so a sender can show Rustrx users another link or text than other clients show (UC12).
- Plain bodies are escaped and linkified in C++ (`StyledText::linkified`, `Twemoji.cpp`).

### Other text from other people

- Silica Labels default to plain text for the whole window (`_defaultLabelFormat: Text.PlainText` in `qml/harbour-rustrx.qml`; audit I1; `test_labels_default_to_plain_text`). Silica's Notices, headers, menu items and switches are Labels too.
- Where QML builds StyledText from remote strings, it is meant to go through one helper, `escaped` in `qml/markup.js` (`test_one_escaping_helper`), or the C++ `StyledText::escaped` (`RoomModel::nameHtml`, `TimelineModel::renderReactions`). The 0.8.4 audit read all sixteen StyledText sites in `qml/` and found each escaped or rendered.
- **Display names** another member of the room shares get the user ID beside them, in the bubble, the quote, the room list and notifications (`sender_ambiguous`, `shown_name`, from the SDK's `display_name_ambiguous`; UC7).

### Links

- Every link that leaves the app goes through `openExternal` (`qml/harbour-rustrx.qml`): `https`, `http`, `mailto` and `geo` only. `openLink` keeps Matrix links in the app. The one other `Qt.openUrlExternally` is the exported file in `openFile`. Both are pinned by `test_external_links_go_through_one_gate`.
- A link whose text reads as an address of another host than the link's asks first ("Open link?", `TimelineModel::linkHosts`, `pages/LinkCheckDialog.qml`; UC8). Other links open as before.

### QML engines without network

- Both QML engines (the window's and `Service.qml`'s) get a network access manager that cannot reach the network (`src/LocalOnly.h`, installed in `src/main.cpp`). Every picture they show is meant to be a local file, so that a markup bug does not turn into a fetch from QML.
- The exception is Element Call: `qml/pages/CallPage.qml` is a Sailfish WebView (Gecko) that loads `https://call.element.io` (`ELEMENT_CALL_URL`, `core/src/consts.rs`) with network access. `qml/call-bridge.js` passes widget API messages only for that origin. Element Call is remote code, unpinned, so the SDK's widget driver grants of what it asks only what a call needs (`CallCapabilities` in `call.rs`: Element X's set, the own call membership only, never the room's messages or other state); anything refused is logged by its type (`call: capabilities refused`; NET-1). The SDK answers OpenID requests itself, without the capabilities provider, and the widget parameters travel in the URL's fragment.

### Pictures and media

- Received pictures are decoded by Qt 5.6's image plugins on the phone, and received audio and video by GStreamer through QtMultimedia. These are system libraries; Rustrx does not control their versions. Which image plugins a phone has installed was not checked.
- **Before any decode** (`src/PixelGate.h`): a received picture's size is read from its header, a GIF's canvas included; past 40 MP or a side past 32767 it is not decoded and shows as a picture that failed to load (UC3). That holds for QML's sources (`rustrx.picture()`), avatars and notification icons (`RoundAvatar::render`) and custom emoji.
- **Only four formats are decoded**: JPEG, PNG, GIF and WebP, by their first bytes, each by its own decoder alone (`pictureFormat`, `onlyItsDecoder`; UC2). Up to 0.8.3 Qt chose any plugin, SVG and TIFF included, by the sender's type. The file's extension on disk comes from sniffing its bytes (`sniff_extension`: jpg, png, gif, webp, mp4, webm, ogg), else `bin`; what the chat draws is named by its bytes alone (`picture_extension`). A picture or sticker of another kind is a file row (`rustrx.isPicture`).
- **GStreamer's HLS, DASH and MSS demuxers** are at rank none in the process (`src/GstGuard.h`, through `GST_PLUGIN_FEATURE_RANK` first thing in `main()` and the registry once GStreamer starts): a received file that is a playlist fails as unplayable and fetches nothing (UC4).
- Rustrx's own parsers read the user's own files before sending, not received ones: `src/Exif.h`, `src/Png.h`, `src/Mp4.h`, with bounds checks and tests. `Mp4::read` also reads a received or shared video's header before a re-encode.
- The room's avatar provider (`src/AvatarProvider.h`, `src/RoundAvatar.h`) reads local files only, by the path in the image id that QML gives it.

### JSON between core and UI

- Commands are one Rust enum, `core/src/protocol.rs` (`parse`): an object with `"cmd"`, its fields typed by serde. Unknown commands, missing fields and ill-typed fields are refused before anything runs; extra fields are accepted on purpose (`lenient_where_documented`). serde_json's recursion limit stops deep nesting. Tests in the same file (`hostile_commands_are_refused`, `arrays_are_refused`, `required_fields_are_enforced`). `docs/PROTOCOL.md` is written from it; `tests/test_static.py` checks that every sent command and field exists and is documented.
- Events go the other way as JSON with `"ev"`; `Rustrx::handleEvent` reads them on the GUI thread. An event above Qt 5.6's JSON limit (128 MiB) is dropped without a log line (`eventCallback`; FFI-10, open).

### The FFI boundary

- Eight C functions, in `src/rustrx_core.h` and `core/src/lib.rs`. Commands return at once; replies are events on a callback that fires on tokio threads, and also synchronously on the calling thread (a command that does not parse is answered inside `dispatch`, and `dispatch`'s own log lines go through the callback; FFI-12). `Rustrx.cpp` hops to the GUI thread before touching Qt state, so nothing breaks, but a C++ caller must never hold `s_lock` around a command (`QMutex` is not recursive).
- The callback is C++ called from Rust frames, with no `try`/`catch` around its body: an exception there (`std::bad_alloc` on a huge event) would unwind through Rust, which is undefined behaviour (FFI-12, open).
- `panic = "unwind"` is required (`core/Cargo.toml`): command handlers run under `catch_unwind` (`dispatch` in `commands.rs`) and answer a panic with an error event; `rustrx_shutdown` catches its own; `rustrx_decode_qr` runs rqrr under `catch_unwind`, because rqrr 0.11 asserts on some noisy frames (FFI-1, a test with such a frame). The 0.8.4 audit found no panic on input in `rustrx_start`, `rustrx_command`, `rustrx_command_to` and `rustrx_set_store_key` (Q13).
- `rustrx_decode_qr` trusts the caller's width, height and stride to describe the buffer; its own bounds checks were read and found right.
- The `unsafe` blocks in `core/` are in `lib.rs` (the C ABI and the QR decoder's buffer), read in the 0.8.4 audit and found sound, and since 0.8.4 the `openat` in `export_new_file` (`timeline.rs`), which that audit did not read.

### Calls

- Classic 1:1 calls: `src/Voip.cpp` drives webrtcbin; the core only carries `m.call.*` events (`core/src/core/call.rs`) and the TURN credentials (`turn_server`), which go into webrtcbin's TURN URL percent-encoded (`Voip::turnUrl`). Remote SDP is parsed by GStreamer.
- Answers, candidates and hangups are taken only from the party that the call is bound to, and before the answer only the invitee's events count (S1, S5, S5b, NET-5). The far end's SDP is capped at 64 KB, its candidates at 200 a call and to an existing m-line, and only its first Opus audio stream is played, any other into a fakesink (NET-9).
- **Who can ring the phone**: an incoming invite rings only in a room of at most two joined members, for classic calls and Element Call rings alike (`rings_in` in `call.rs`, `joined_members` on `voip`; NET-4). In a bigger room a classic call is quiet: answerable from its notification, with no ringtone and no call page. A ring's lifetime is its invite's remaining lifetime.
- **Relay only** (Settings > Privacy, `kVoipRelayOnly`) keeps the phone's IP addresses from the peer and, in unencrypted rooms, out of the room's history; on by default since 0.8.4 (S6, NET-8). A call that cannot get a relay (a server without TURN) ends at once with its own reason instead of failing after a while (`docs/CALLS.md`).
- **The call page over the lock screen**: with the screen locked (MCE's tklock mode, `rustrx.screenLocked`), a ring opens `pages/CallWindow.qml`, a window of its own with only the call page in it, which `Rustrx::showOverLock` gives the lipstick category "call"; lipstick shows it over the lock screen and the device lock, as the phone app's call window, so answering needs no unlock. In that window a call that ended offers no "Call again" (fd1041b), a room open behind it is not marked read and its messages still notify (b84ed45), and an Element Call answered there runs in the app, behind the lock. "Force Call UI in front on incoming calls" (`forceCallUi`, in dconf, on by default) switches all of this off: then a ring is its notification only.
- Tests: `tests/cpp/voip` (two engines calling each other), `tests/cpp/notifygate`.

### Emoji packs and custom emoji

- `core/src/core/packs.rs`: packs from room state and account data; an image is taken only with an `mxc://` URL, fetched through the homeserver's media API and cached by a hash of its address (`fetch_emote`), with the caps above. Pack names and shortcodes reach Labels, which default to plain text; no StyledText site takes them. Design: `docs/EMOJI.md`.

### Room links and matrix.to

- `openMatrixUri` (`qml/harbour-rustrx.qml`) takes `matrix:r/`, `matrix:roomid/`, `matrix:u/` and `https://matrix.to/#/` links, with `via` parameters. A user link opens `StartChatDialog`; a room link opens `JoinPage`, which asks the homeserver for a preview (`room_preview`) at once for a link tapped in the app, and only at its Look up button for a link from another app (`lookFirst`; IPC7), and joins or knocks only from its buttons. The lookup goes through the user's own homeserver, which federates it to the server named in the link: that server learns the user's homeserver and the time, not the phone's address.
- A joined room opened from outside (a link from another app, a notification's open action) still sends its read receipt as the Privacy setting has it, without a touch (IPC7, open).

### Verification requests

- Another person's request is a notification first ("%1 (%2) wants to verify you", the user ID always named), and its page opens only on a tap (`verification.rs`, `openVerification`; NET-6). One's own devices open their page at once. The SAS emoji and their descriptions come from the SDK's fixed table, and "They match" sits well apart from "They differ".

### Slash commands

- `qml/commands.js` only parses the user's own text (`/me`, `/shrug`, `/plain`, `/topic`, `/nick`, `/invite`, `/join`; unknown ones are not sent). Tested under node (`tests/test_commands.py`). Markdown in the user's own messages is rendered by the SDK; HTML the user types passes into `formatted_body`, which is the receiving client's problem, as in other clients.

## Earlier audits

`docs/audits/` (index in `docs/audits/README.md`; `docs/AUDIT-2026-11-16.md` only points there). Each audit has a narrative and a findings ledger (`.findings.json`) with each finding's status and commit. By their method sections, the code reading was done by LLM agents under the author's direction, and the memory and performance audits measured on the phones; none was an independent review.

- **2026-11-16, structure** (0.4.6): layout and double work. All 25 findings fixed or decided.
- **2026-11-16, lifecycle and untrusted input** (0.4.7): two agents read every spawn, lock and timeout, and every path from servers and other apps into StyledText, links, shares, D-Bus and the log. Findings I1 to I17 cover Label AutoText, a `< img` bypass of the HTML filter, href quote injection, unescaped names, links starting chats, `geo:` and server URLs opened unchecked, shares reading Rustrx's own files, the dev hook, the push token, the log's contents and drafts in plain QSettings. All are marked fixed except I17 (Element Call's capabilities, decided then, an allowlist since 0.8.4). I9 says session commands are refused through the dev hook; the allowlist that came later (`src/DevHook.h`) does allow a password login (IPC8).
- **2026-11-18, memory** (0.4.9): glibc arenas and store pools. Open: M3 (a native crash not reached), M4, M5.
- **2026-11-19, calls and lifecycle** (0.6 work): includes security findings S1 to S10 (call spoofing, planted links into private folders, log copy through a link, the dev hook, IP exposure in calls, share time-of-check, opening received files, metadata coverage, plain `session.json`), all fixed. Open: V9, D1, DOC, BG; K9 partly fixed.
- **2026-11-22, performance** (0.8.2): measured on two phones; security was not in scope. Its security finds while writing this guide are in its ledger (SEC), and the store key's move to the high-entropy path was its DEC3.
- **2026-11-27, security** (0.8.4): five agents (IPC, network, secrets, FFI, content) at 3204fa2, its findings about Rustrx all answered with a fix, a decision or a reason; the author's decisions DEC1 to DEC25. Read this one first. The open items are listed in its "Left open".

The 0.8.4 audit named the sandbox profile and the SDK's store cipher as in scope, and ran `cargo audit` and `cargo deny` once; none of the audits read the whole dependency tree.

## Open questions to start with

These are questions, not findings. The first sixteen are the ones this guide asked before 0.8.4, kept under their numbers because the audit answers them by number; each says where it stands now. The rest are new.

1. **D-Bus callers.** Answered (IPC1): no slot checked its caller. The gate (above) takes an action only from the same Unix user and only for a notification Rustrx has up. Left: is the same-user residual risk acceptable, and should the open actions (`openVerification`, `openKnocks`, `openAccountRoom`) be gated too?
2. **The bus name as lock.** Answered (IPC2, IPC6): yes, a process can take either name first. Since 0.8.4 the holder must prove it is a Rustrx (`src/NameProof.h`): otherwise push pauses with a red warning in Settings and the stand-in runs, or a push-woken process says "Rustrx could not start" once per boot. The distributor is chosen by its exact name and kept; another one needs the user's yes (IPC3).
3. **gst-launch-1.0 arguments.** `gst-launch-1.0` reads its arguments as a pipeline description (above); the paths are now made by Rustrx. Would building the pipeline through the GStreamer API be the better design? (Not covered by the 0.8.4 audit.)
4. **openFile's allowlist.** Answered (UC5): the denylist was bypassed; since 0.8.4 `OpenCheck::mayOpen` opens only allowed kinds whose name and bytes agree. Left: does the phone's handler (libcontentaction) pick its app by the name or by the content, and can a file pass both checks and still reach an installer?
5. **Store fingerprint.** Not a problem (Q5): only an AEAD refusal of the cipher row removes a store. Builds before 0.8.2 still decide by the old `DefaultHasher` fingerprint, which 0.8.2 leaves in place for them; a downgrade deletes only if two toolchains' `DefaultHasher` differ.
6. **The KDF.** Not a problem (Q6): the high-entropy path, one wrapping key over databases that each keep their own data key.
7. **Plain http.** Answered (NET-3): refused away from the phone and the home network, wherever the address came from. Left: is the home-network exception right, given that a name like `server.lan` must be typed as its IP?
8. **Error texts in the log.** Answered (SEC7, FFI-8): server text, URLs with queries and serde's echo of values reached the log; no secret was found. Both logs are redacted since 0.8.4. Left: the panic hook's payload, and the SDK's WARN lines outside `http_client`, which nobody has read.
9. **The journal.** Answered on a 5.1 port image: root and the journal groups only. Left: check on Jolla's own image; the debug-log switch does not stop the journal copy (SEC11).
10. **Cache name collisions.** Answered (FFI-6, UC6): they could collide; since 0.8.4 every cache file named from remote ids is named by a SHA-256 of them (`cache_name`).
11. **Share data branch.** Answered (IPC5): the private folder was enough against links, not against size or overwriting. Since 0.8.4 every branch writes new files in a folder of its own, under one cap.
12. **Link a new device.** Not a problem as asked; but the consent page's address came from the new device and was opened unchecked (NET-2), now only on the account service's host.
13. **The FFI functions without catch_unwind.** Answered (FFI-1): `rustrx_decode_qr` could panic in rqrr, and now catches it. The others showed no panic on input.
14. **Notifications at rest.** Answered (SEC5): the notification server keeps them until they are closed. Rustrx leaves them at logout (DEC16). Left: should `notifyText` default to off? (The author's call; today on, with an off switch.)
15. **Backups.** Answered on a 5.1 port image: no backup unit reads Rustrx's folders. Left: check Jolla's own image.
16. **Element Call.** Answered (NET-1): the capabilities are an allowlist since 0.8.4. Left: pinning the remote code (large), and what call.element.io keeps in the WebView's profile.
17. **The checks that need a phone.** The 0.8.4 audit names these; none is recorded as done: a squatted connector name and app name (the red hint, the stand-in taking over, "Rustrx could not start"); a ring in a room of three; a call in an unencrypted room on a server without TURN; a link whose text names another host; a hard logout on a classic Synapse.
18. **The QR login's new-device side.** What this device checks about the homeserver the other device names in `m.login.protocols`, and what it does with the secrets bundle (the SDK's `qrcode/login.rs` against MSC4108). Not read.
19. **Sizes the sender states.** The 20 MB tap-to-load limit trusts the event's `size`; a hard byte limit would need Rustrx's own download path. Is the SDK's own handling of a huge response bounded?
20. **What 0.8.4 left open on purpose.** `store-lost-*` is never cleaned or restored; the dev hook's password login once the flag file exists (IPC8); the panic payload in the log (FFI-8); commands that delete the file they are given (FFI-9, paths made by C++ today); no catch around the callback (FFI-12); the shared runtime's two workers under remote load (FFI-7, not measured); the common push gateway after any probe failure (NET-11); a joined room opened from outside sends its receipt (IPC7). Each is in the 0.8.4 ledger with its reason.
21. **What stays in dconf.** The look, `forceCallUi` and the debug switches (which act only with the dev flag). Is any of it worth moving into the private settings file?

## Build, tests, dependencies, CI

### Build

- The core: `core/build-aarch64.sh`, `cargo zigbuild --locked --release` for aarch64 glibc 2.38, with rustc 1.99.1 and zig 0.17.1 for the releases. `Cargo.lock` is in the tree; `--locked` fails a build that would change it.
- The app: qmake and `rpm/harbour-rustrx.spec` in the Sailfish Platform SDK; `secrets/` is built as its own plugin. Build dependencies include `pkgconfig(mlite5)` (for `PrivateSettings`). The RPM installs the core at `/usr/share/harbour-rustrx/lib/librustrx.so`.
- A host build for reading and tests needs no phone: the test suites below build against the host's Qt 5.

### Tests

- `python3 -m pytest -q tests`: `tests/test_static.py` (wiring between Rust, C++ and QML, the link gate, the escaping helper, the dev hook allowlist, the audits' ledgers), `tests/test_commands.py` and `tests/test_transcript.py` (`qml/commands.js` and `qml/transcript.js` under node; skipped without node).
- `tests/cpp/run.sh`: QtTest suites against the host's Qt 5. The ones for the trust boundaries: renderhtml and golden (message HTML), pixelgate and roundavatar (decoding), opencheck (opening files), gstguard (playlists), sharecopy (shares), notifygate, nameproof, unifiedpush and busleave (D-Bus, on a private bus), handover, devhook, debuglog (redaction), privatesettings, storekeyguard, voip, exif, png, mp4. Needs Qt 5 qmake and GStreamer development packages (see `ci.yml`).
- `cargo test --locked` in `core/`: the protocol, the session file, the media cache, the refusals in `checks.rs`, the redaction in `redact.rs`, the QR reader with a frame that makes rqrr assert, shutdown. Slow, because it compiles matrix-sdk.
- Every test names the bug planted to prove it can fail (`Planted-bug:` in its comment).

### Dependencies

- `core/Cargo.lock` pins about 445 crates, most of them matrix-sdk's tree. Direct dependencies are in `core/Cargo.toml`; `matrix-sdk-store-encryption` and `matrix-sdk-base` are pinned to `=0.19.1`.
- `cargo audit` and `cargo deny check advisories bans sources` were run once for the 0.8.4 audit (FFI-11): an SQLite advisory in the bundled SQLite (needs a crafted database with its `-wal` file; Rustrx opens only its own stores; the fix comes with an SDK update), a `url` advisory (no caller of the affected function; `url` 2.5.9 since), four unmaintained crates, all crates from crates.io. No `deny.toml`, CI step, Dependabot or `SECURITY.md` is set up yet.
- On the phone, Rustrx uses the system's Qt 5.6, GStreamer (with webrtcbin, libnice), speexdsp, PulseAudio, mlite (`PrivateSettings`) and Sailfish Secrets (`rpm/harbour-rustrx.spec`, `Requires:`). Their versions are Sailfish OS's.

### CI

- `.github/workflows/ci.yml`: the pytest suite and the C++ suites on every push and pull request (ubuntu-24.04).
- `.github/workflows/nightly.yml`: `cargo test --locked` nightly with rustc 1.99.1.
- Actions are referenced by tag or branch, not by commit hash (`actions/checkout@v5`, `Swatinem/rust-cache@v2`, `dtolnay/rust-toolchain@master`).
- No workflow builds the core or the RPM for a release.

## What to check first, after 0.8.4

The audit's fixes are young: tested on the host, with a few corrections from tests on the phones (c8de0de, b0d9102), and read by nobody else. The checks on squatted names, rings in groups and calls without a relay have not been tried on a phone yet. Where a reviewer's time pays off most:

- `src/NotifyGate.h` with `qml/Service.qml`'s `notified.json`: can an action reach the core for something Rustrx does not have up, and does the list stay right across the three processes?
- `src/NameProof.h`: the HMAC's inputs, the nonce, and whether a squatter can relay a real Rustrx's answer.
- `core/src/core/checks.rs`: the home-network ranges, the consent page's host match, the typed account compare, and `hard_logout` against what servers really send.
- `src/PixelGate.h` and `src/OpenCheck.h`: what a crafted header or a file that is two things at once does.
- `src/DebugLog.cpp` and `core/src/redact.rs`: what still gets through.
- `src/StoreKeyGuard.h` and `SecretStore.cpp`: every way to a new key.
- `pages/CallWindow.qml` and `Rustrx::showOverLock`: what the window over the lock screen lets someone without the unlock code do.

## Reporting and credit

- **Findings:** open a GitHub issue on the repository, one per finding, with file and line, what you saw and what you expect.
- **Anything sensitive** (a way to read someone's messages or keys, run code, or act as the user): use GitHub's private vulnerability reporting on the repository ("Report a vulnerability" under the Security tab) instead of a public issue.
- **Credit:** the author's stance is "I will credit everyone who reviews as a co-author who took ownership of the code by reviewing it." Reviewers are named in the README's Credits section and on Settings > About.
