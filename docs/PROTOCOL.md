# The core protocol

Rustrx is two halves: the Rust core (`core/`, matrix-rust-sdk) and the Qt/QML
UI. They talk through three C functions (`src/rustrx_core.h`) and JSON:

- `rustrx_command(json)`: one command object, `{"cmd": "...", ...}`.
- The callback given to `rustrx_start`: one event object per call,
  `{"ev": "...", ...}`, on a core thread; the bridge (`src/Rustrx.cpp`)
  hands it to the GUI thread.
- `rustrx_stop()`: no event reaches the callback afterwards.

**Version 1.** The core sends `"proto": 1` with `ready` and `logged_in`
(`PROTO` in `core/src/core.rs`); the bridge warns in the log when it expects
another (`kProto` in `src/Rustrx.cpp`). Both halves ship in one package, so a
mismatch means a build mix-up, not an upgrade path.

The command list below is the enum in `core/src/protocol.rs`, which is the
source of truth: a command whose fields do not match it fails before anything
runs, and the error event carries serde's message (`missing field "body"`).
Fields marked `?` may be absent or `null`; unknown extra fields are ignored.

## Commands

### Session
| cmd | fields | answered by |
|---|---|---|
| `probe_server` | `user?`, `homeserver?` | `server_info` |
| `login_password` | `user`, `password`, `homeserver?` | `logged_in` or `login_error` |
| `login_sso` | `user?`, `homeserver?`, `idp?` | `open_url`, then `logged_in` or `login_error` |
| `logout` | | `logged_out` |

### The open room
| cmd | fields | answered by |
|---|---|---|
| `open_room` | `room_id` | `timeline` (repeatedly), `typing` |
| `close_room` | | |
| `paginate_back` | `room_id`, `count?` (30) | `paginated` |
| `send_text` | `room_id`, `body`, `reply_to?`, `markdown?` (true) | `timeline` |
| `send_file` | `room_id`, `path`, `caption?`, `w?`, `h?`, `thumb_path?`, `thumb_w?`, `thumb_h?`, `voice?`, `duration_ms?` | `timeline` |
| `react` | `room_id`, `event_id`, `key` (toggles) | `timeline` |
| `redact` | `room_id`, `event_id?` or `txn_id?` (discards a local echo), `reason?` | `timeline` |
| `edit` | `room_id`, `event_id?` or `txn_id?`, `body` | `timeline` |
| `poll_vote` | `room_id`, `event_id`, `answers` (answer ids) | `timeline` |
| `report` | `room_id`, `event_id`, `reason?` | |
| `mark_read` | `room_id`, `private?` (a private receipt) | `rooms` |
| `typing` | `room_id`, `typing?` | |
| `get_media` | `room_id`, `event_id`, `thumb?`, `dest_dir?` (a copy for other apps) | `media` |

### Rooms
| cmd | fields | answered by |
|---|---|---|
| `get_notification_mode` | `room_id` | `notification_mode` |
| `set_notification_mode` | `room_id`, `mode` (`all`, `mentions`, `mute`, other: the account's default) | `notification_mode` |
| `set_favourite` / `set_low_priority` | `room_id`, `value?` (true) | `rooms` |
| `join_room` / `leave_room` | `room_id` | `rooms` |
| `join_by_id_or_alias` | `id` | `joined` |
| `create_dm` | `user_id` | `dm_ready` |
| `create_room` | `name?`, `topic?`, `public?`, `alias?`, `encrypted?` (not public) | `joined` |
| `room_members` | `room_id` | `members` |
| `set_room_name` / `set_room_topic` | `room_id`, `name` / `topic` | `rooms` |
| `invite` | `room_id`, `user_id` | |
| `search` | `term` | `search_results` |
| `refresh_rooms` | | `rooms` |

### Encryption
| cmd | fields | answered by |
|---|---|---|
| `verification_request_self` | | `verification` |
| `verification_accept`, `verification_start_sas`, `verification_cancel` | `flow_id` | `verification`, `sas` |
| `verification_confirm`, `verification_mismatch` | `flow_id` | `sas`, `verification_state` |
| `recovery_recover` | `key` | `recovery_done` |
| `recovery_enable`, `recovery_reset` | | `recovery_key` |
| `export_keys` / `import_keys` | `path`, `passphrase` | `keys_exported` / `keys_imported` |

### Account and devices
| cmd | fields | answered by |
|---|---|---|
| `devices` | | `devices` |
| `delete_device` | `device_id`, `password` | `device_deleted` |
| `profile` | | `profile` |
| `set_display_name` | `name` | |
| `ignore_user` / `unignore_user` | `user_id` | |
| `storage_info` | | `storage` |
| `clear_media` | | `storage` |

## Events

| ev | fields |
|---|---|
| `ready` | `logged_in`, `proto` (no saved session, or restoring it failed) |
| `logged_in` | `proto`, `user_id`, `device_id`, `homeserver`, `sliding_sync` |
| `logged_out` | `reason?` (`session_expired`), `soft_logout?` |
| `login_error` | `message` |
| `server_info` | `server`, `homeserver`, `password`, `sso`, `idps`, `sliding_sync`, `signup_url?` |
| `open_url` | `url` (SSO: to open in the browser) |
| `sync_state` | `state` (`idle`, `running`, `offline`, `error`, `terminated`), `message?` |
| `rooms` | `rooms`: room objects, invitations first, then by activity |
| `timeline` | `room_id`, `items`: timeline items, oldest first, the whole list each time |
| `paginated` | `room_id`, `at_start` |
| `typing` | `room_id`, `names` (others typing) |
| `media` | `room_id`, `event_id`, `thumb`, `exported`, `path` |
| `members` | `room_id`, `members`: `{user_id, name, power}` |
| `notification_mode` | `room_id`, `mode` (`all`, `mentions`, `mute`, `default`) |
| `joined` | `room_id` |
| `dm_ready` | `room_id`, `user_id` |
| `search_results` | `term`, `users`: `{user_id, name}`, `rooms`: `{room_id, name, alias, topic, members, joined}` |
| `verification` | `flow_id`, `state`, `other_user`, `we_started`, `self_verification` |
| `sas` | `flow_id`, `state`, `emojis?`, `decimals?`, `reason?` |
| `verification_state` | `state` (`verified`, `unverified`, `unknown`) |
| `recovery_state` | `state` (`enabled`, `disabled`, `incomplete`, `unknown`) |
| `recovery_key` | `key` (shown once) |
| `recovery_done` | |
| `keys_exported` / `keys_imported` | `path` / `imported`, `total` |
| `devices` | `devices`: `{device_id, name, last_seen_ts, last_seen_ip, current}` |
| `device_deleted` | `device_id` |
| `profile` | `display_name`, `user_id` |
| `storage` | `store`, `media_files` (bytes), `crypto?`, `state?`, `event_cache?`, `media_cache?` |
| `error` | `cmd`, `message` |
| `log` | `line` (a log line; the sandbox drops stderr, the UI puts it in the journal) |

### Room object
`room_id`, `name`, `topic`, `alias`, `avatar` (a cached file path), `has_avatar`,
`state` (`joined`, `invited`), `is_dm`, `is_fav`, `is_low`, `encrypted`,
`unread`, `notifications`, `highlights`, `ts` (last activity, ms),
`preview`, `preview_sender`.

### Timeline item
Every item: `id` (unique, stable), `kind` (`event`, `date`, `readmarker`, `start`).
Date dividers carry `ts`. Events carry `event_id` (absent for a local echo),
`txn_id`, `sender`, `sender_name`, `is_own`, `ts`, `read_by`, `editable`,
`highlight` (mentions us), `send_state` (`sending`, `failed`, `sent`),
`shield` (`red:<code>`, `grey:<code>` for unverified or unverifiable senders),
`reactions` (`{key, count, me}`), `reply_to` (`{event_id, sender, sender_name, body}`),
`thread_root`, `edited`, and by `type`:
`text`, `notice`, `emote` (`body`, `html`), `image`, `video`, `audio`, `file`
(`caption`, `mime`, `size`, `w`, `h`, `has_thumb`, `filename`, `duration`),
`sticker`, `location` (`geo_uri`), `poll` (`body` the question, `answers`:
`{id, text, count, mine}`, `max_selections`, `ended`), `verification`,
`membership` and `state` (`body`, one line), `redacted`, `utd` (not decrypted yet).
