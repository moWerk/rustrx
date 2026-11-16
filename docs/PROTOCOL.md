# The core protocol

Rustrx is two halves: the Rust core (`core/`, matrix-rust-sdk) and the Qt/QML
UI. They talk through three C functions (`src/rustrx_core.h`) and JSON:

- `rustrx_command(json)`: one command object, `{"cmd": "...", ...}`.
- The callback given to `rustrx_start`: one event object per call,
  `{"ev": "...", ...}`, on a core thread; the bridge (`src/Rustrx.cpp`)
  hands it to the GUI thread.
- `rustrx_stop()`: no event reaches the callback afterwards.

**Version 2** (since 0.4.7: one name per concept). The core sends `"proto": 2` with `ready` and `logged_in`
(`PROTO` in `core/src/consts.rs`); the bridge warns in the log when it expects
another (`kProto` in `src/Keys.h`). Both halves ship in one package, so a
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
| `login_sso` | `user?`, `homeserver?`, `idp?` | `open_url`, then `logged_in` or `login_error` (after 10 minutes without the browser coming back, too) |
| `login_oauth` | `user?`, `homeserver?`, `signup?` (make an account first) | `open_url` (the account service's page), then `logged_in` or `login_error` (also after 10 minutes) |
| `login_cancel` | | `login_error` of the waiting `login_sso` or `login_oauth` |
| `logout` | | `logged_out` |

### The open room
| cmd | fields | answered by |
|---|---|---|
| `open_room` | `room_id` | `draft`, then `timeline` (repeatedly), `typing` |
| `save_draft` | `room_id`, `text` ("" removes it; kept in the encrypted store) | |
| `close_room` | | |
| `paginate_back` | `room_id`, `count?` (30) | `paginated` |
| `send_text` | `room_id`, `body`, `reply_to?`, `markdown?` (true), `emotes?` (shortcode → mxc: custom emoji in the HTML), `thread_root?` | `timeline` |
| `send_sticker` | `room_id`, `mxc`, `body`, `info?`, `thread_root?` | `timeline` |
| `open_thread` | `room_id`, `thread_root` | `thread_timeline` (live until `close_thread`) |
| `close_thread` | | |
| `retry_send` | `room_id`, `txn_id` (a message the server did not take) | `timeline` |
| `send_file` | `room_id`, `path`, `mime?`, `caption?`, `w?`, `h?`, `thumb_path?`, `thumb_w?`, `thumb_h?`, `voice?`, `duration_ms?`, `thread_root?` | `timeline` |
| `react` | `room_id`, `event_id`, `key` (toggles) | `timeline` |
| `redact` | `room_id`, `event_id?` or `txn_id?` (discards a local echo), `reason?` | `timeline` |
| `edit` | `room_id`, `event_id?` or `txn_id?`, `body` | `timeline` |
| `poll_vote` | `room_id`, `event_id`, `answers` (answer ids) | `timeline` |
| `report` | `room_id`, `event_id`, `reason?` | |
| `mark_read` | `room_id`, `private?` (a private receipt) | `rooms` |
| `typing` | `room_id`, `typing?` | |
| `get_media` | `room_id`, `event_id`, `thumb?`, `dest_dir?` (a copy for other apps) | `media` |
| `fetch_emote` | `mxc` (a custom emoji's address) | `emote` |
| `image_packs` | `room_id` | `image_packs` |
| `room_preview` | `room_or_alias`, `via?` | `room_preview` |
| `knock` | `room_or_alias`, `reason?`, `via?` | `knocked` |
| `room_link` | `room_id` | `room_link` |
| `set_join_rule` | `room_id`, `rule` (invite, knock, public, restricted, knock_restricted) | `rooms` |
| `space_open` / `space_close` | `space_id` | `space_rooms` (live until closed) |
| `space_add_child` / `space_remove_child` | `space_id`, `room_id` | `space_rooms` |
| `space_leave` | `space_id`, `with_rooms?` | `rooms` |
| `knock_answer` | `room_id`, `user_id`, `accept`, `ban?`, `reason?` | `knock_requests` |
| `knock_seen` | `room_id`, `user_ids` | `knock_requests` |
| `add_user_emote` | `shortcode`, `mxc` | `emote_added` |
| `event_source` | `room_id`, `event_id` | `event_source` |
| `thread_list` | `room_id` | `thread_list` (live until `close_thread_list`) |
| `close_thread_list` | | |
| `mark_thread_read` | (the open thread) | |

### Rooms
| cmd | fields | answered by |
|---|---|---|
| `get_notification_mode` | `room_id` | `notification_mode` |
| `set_notification_mode` | `room_id`, `mode` (`all`, `mentions`, `mute`, other: the account's default) | `notification_mode` |
| `set_favourite` / `set_low_priority` | `room_id`, `value?` (true) | `rooms` |
| `join_room` / `leave_room` | `room_id` | `rooms` |
| `join_by_id_or_alias` | `room_or_alias`, `via?` | `joined` |
| `create_dm` | `user_id` | `dm_ready` |
| `create_room` | `name?`, `topic?`, `public?`, `alias?`, `encrypted?` (not public), `space?` (a space instead of a room), `parent?` (the space it goes into), `join_rule?` (invite, knock, public, restricted) | `joined` |
| `room_members` | `room_id` | `members` |
| `set_room_name` / `set_room_topic` | `room_id`, `name` / `topic` | `rooms` |
| `invite` | `room_id`, `user_id` | |
| `search` | `term` | `search_results` |
| `set_room_avatar` | `room_id`, `path`, `w?`, `h?` (a prepared picture) | `rooms` (through the sync) |
| `refresh_rooms` | | `rooms` (for tests: `tools/test/dev.sh`) |

### Encryption
| cmd | fields | answered by |
|---|---|---|
| `verification_request_self` | | `verification` |
| `verification_accept`, `verification_start_sas`, `verification_cancel` | `flow_id` | `verification`, `sas` |
| `verification_confirm`, `verification_mismatch` | `flow_id` | `sas`, `verification_state` |
| `recovery_recover` | `recovery_key` | `recovery_done` |
| `recovery_enable`, `recovery_reset` | | `recovery_key` |
| `identity_reset` | | `identity_reset_approval` and `open_url` (where the server asks for approval in the browser), then `recovery_key` |
| `export_keys` / `import_keys` | `path`, `passphrase` | `keys_exported` / `keys_imported` |

### Account and devices
| cmd | fields | answered by |
|---|---|---|
| `devices` | | `devices` |
| `delete_device` | `device_id`, `password` | `device_deleted` |
| `profile` | | `profile` |
| `set_display_name` | `name` | |
| `set_own_avatar` | `path` (a prepared picture) | `profile` |
| `ignore_user` / `unignore_user` | `user_id` | |
| `storage_info` | | `storage` |
| `clear_media` | | `storage` |

### Push (UnifiedPush, docs/PUSH.md)
| cmd | fields | answered by |
|---|---|---|
| `push_set` | `endpoint`, `gateway`, `old?` (an endpoint to remove), `lang?` | `pusher_set` |
| `push_remove` | `endpoint` | |
| `push_check` | `endpoint` | `pusher_checked` |
| `push_handle` | `room_id`, `event_id` (a pushed message) | `push_notification`, or nothing when the account's rules keep it quiet |

### Calls (docs/CALLS.md)
| cmd | fields | answered by |
|---|---|---|
| `call_start` | `room_id`, `voice?`, `skip_lobby?` (tests) | `call_url`, then `call_message` |
| `call_message` | `data` (a widget API postMessage from the page) | `call_message` |
| `call_end` | | `call_ended` |
| `call_decline` | `room_id`, `event_id` (the ring) | |

## Events

| ev | fields |
|---|---|
| `ready` | `logged_in`, `proto`, `kept_user?` (a store kept from an ended session), `restore_failed?` (a saved session could not be opened: no session, or restoring it failed) |
| `logged_in` | `proto`, `user_id`, `device_id`, `homeserver`, `sliding_sync` |
| `logged_out` | `reason?` (`session_expired`), `soft_logout?` |
| `login_error` | `message` |
| `server_info` | `server`, `homeserver`, `password`, `sso`, `idps`, `sliding_sync`, `signup_url?` |
| `open_url` | `url` (SSO: to open in the browser) |
| `sync_state` | `state` (`idle`, `running`, `offline`, `error`, `terminated`), `message?` |
| `rooms` | `rooms`: room objects, invitations first, then by activity |
| `timeline` | `room_id`, `items`: timeline items, oldest first, the whole list each time |
| `paginated` | `room_id`, `at_start` |
| `draft` | `room_id`, `text` (what was being written there; "" for none) |
| `typing` | `room_id`, `names` (others typing) |
| `media` | `room_id`, `event_id`, `thumb`, `exported`, `path` |
| `emote` | `mxc`, `path` |
| `image_packs` | `room_id`, `packs` (`id`, `name`, `source` user/global/room, `avatar_mxc`, `images`: `shortcode`, `mxc`, `body`, `info`, `emoticon`, `sticker`) |
| `emote_added` | `shortcode` (as stored, a number added when taken), `mxc` |
| `members` | `room_id`, `members`: `{user_id, name, power}` |
| `notification_mode` | `room_id`, `mode` (`all`, `mentions`, `mute`, `default`) |
| `joined` | `room_id` |
| `dm_ready` | `room_id`, `user_id` |
| `search_results` | `term`, `users`: `{user_id, name}`, `rooms`: `{room_id, name, alias, topic, member_count, joined}` |
| `verification` | `flow_id`, `state`, `other_user`, `we_started`, `self_verification` |
| `sas` | `flow_id`, `state`, `emojis?`, `decimals?`, `reason?` |
| `verification_state` | `state` (`verified`, `unverified`, `unknown`) |
| `recovery_state` | `state` (`enabled`, `disabled`, `incomplete`, `unknown`) |
| `recovery_key` | `recovery_key` (shown once) |
| `recovery_done` | |
| `keys_exported` / `keys_imported` | `path` / `imported`, `total` |
| `devices` | `devices`: `{device_id, name, last_seen_ts, last_seen_ip, current}` |
| `device_deleted` | `device_id` |
| `profile` | `display_name`, `user_id`, `avatar_path` (a cached file), `avatar_v` |
| `storage` | `store`, `media_files` (bytes), `crypto?`, `state?`, `event_cache?`, `media_cache?` |
| `error` | `cmd`, `message` |
| `thread_timeline` | `room_id`, `thread_root`, `items` (as `timeline`; a thread root carries `thread`: `count`, `latest_sender_name`, `latest_body`, `latest_id`, `latest_ts`, `latest_own`, `unread` (no read receipt of ours, threaded or not, reaches the latest reply)) |
| `thread_list` | `room_id`, `threads` (`root`, `latest`: `event_id`, `sender`, `sender_name`, `is_own`, `ts`, `body`; `count`, `unread`), `loading` |
| `space_rooms` | `space_id`, `loading`, `rooms` (`room_id`, `name`, `topic`, `alias`, `avatar_mxc`, `is_space`, `member_count`, `children`, `join_rule`, `state`, `suggested`, `via`) |
| `room_preview` | `room_or_alias`, `ok`, and when ok: `room_id`, `name`, `topic`, `alias`, `avatar_mxc`, `member_count`, `is_space`, `join_rule`, `state`; else `error` |
| `knocked` | `room_id` |
| `room_link` | `room_id`, `link` (matrix.to with via) |
| `knock_requests` | `room_id`, `room_name`, `is_space`, `requests` (`event_id`, `user_id`, `name`, `reason`, `ts`, `seen`), for rooms and spaces with the knock join rule where we may invite |
| `event_source` | `event_id`, `json` (as received), `edit` (its latest edit, or null) |
| `identity_reset_approval` | `url` (the account service's approval page) |
| `pusher_set` | `endpoint` |
| `pusher_checked` | `endpoint`, `present` (the homeserver still has our pusher) |
| `push_notification` | `room_id`, `event_id`, `room_name`, `sender_name`, `body`, `is_dm`, `noisy` |
| `call_url` | `room_id`, `url` (Element Call, for the WebView) |
| `call_message` | `data` (for the page) |
| `call_ended` | `room_id` |
| `call_ring` | `room_id`, `room_name`, `sender`, `sender_name`, `ring`, `event_id`, `expires_ms` |
| `calls_allowed` | `room_id` |
| `log` | `line` (a log line; the sandbox drops stderr, the UI puts it in the journal) |

### Room object
`room_id`, `name`, `topic`, `alias`, `avatar_path` (a cached file), `avatar_v`, `has_avatar`,
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
