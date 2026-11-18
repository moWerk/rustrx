// SPDX-License-Identifier: GPL-3.0-or-later
//! The core's tunables and fixed names in one place. Changing a value here
//! changes it everywhere it is used.

use std::time::Duration;

/// The version of the JSON protocol between the core and the UI
/// (docs/PROTOCOL.md), sent with "ready" and "logged_in". Must match
/// `kProto` in src/Keys.h.
pub(crate) const PROTO: u32 = 2;

// Names

/// The app's id towards the homeserver (the pusher) and Element Call (the
/// widget's client id): the D-Bus interface name, as in src/Keys.h.
pub(crate) const APP_ID: &str = "net.mowerk.harbour_rustrx";
/// The hosted Element Call, at its room route: the bare address is its
/// own start page ("Name of call") and ignores the widget's room. Its
/// origin is ORIGIN in qml/call-bridge.js.
pub(crate) const ELEMENT_CALL_URL: &str = "https://call.element.io/room";
pub(crate) const WIDGET_ID: &str = "rustrx-element-call";
/// Rustrx's page, as it registers itself with an account service (OAuth).
pub(crate) const CLIENT_URI: &str = "https://github.com/moWerk/rustrx";

// Timing

/// Room list updates arriving this close together are sent as one.
pub(crate) const ROOM_LIST_BATCH: Duration = Duration::from_millis(150);
/// After a minute of small updates the room list is rebuilt in full.
pub(crate) const ROOM_LIST_FULL_REBUILD: Duration = Duration::from_secs(60);
/// Wait before syncing again after a sync error.
pub(crate) const SYNC_RETRY_DELAY: Duration = Duration::from_secs(5);
/// a read receipt that failed (a weak network) is tried again after these
pub(crate) const MARK_READ_RETRIES: [Duration; 3] =
    [Duration::from_secs(2), Duration::from_secs(5), Duration::from_secs(15)];
/// A single sign-on waits this long for the browser to come back.
pub(crate) const SSO_TIMEOUT: Duration = Duration::from_secs(10 * 60);
/// A token refresh the server refused is asked again after these waits
/// before the session counts as ended (the first at once).
pub(crate) const SESSION_GONE_WAITS: [Duration; 3] =
    [Duration::from_secs(0), Duration::from_secs(5), Duration::from_secs(30)];
/// A logout waits this long for the server; after it, the phone's side is
/// done anyway (the SDK alone would retry for up to 15 minutes).
pub(crate) const LOGOUT_SERVER_TIMEOUT: Duration = Duration::from_secs(15);
/// A room's send queue switched off by a network error is switched on
/// again after this (sooner when the sync comes back).
pub(crate) const SEND_RETRY_DELAY: Duration = Duration::from_secs(30);
/// Timeline changes this close together are sent as one snapshot
/// (rooms and threads).
pub(crate) const TIMELINE_COALESCE: Duration = Duration::from_millis(60);
/// How long a call ring lasts when the caller sets no lifetime (MSC4075).
pub(crate) const RING_LIFETIME_DEFAULT_MS: u64 = 30_000;
/// The longest ring lifetime accepted from a caller.
pub(crate) const RING_LIFETIME_MAX_MS: u64 = 120_000;

// Limits

/// Older events fetched per back-pagination, when the UI names no count,
/// and to fill the first screen of a room.
pub(crate) const PAGINATE_DEFAULT: u16 = 30;
/// Older replies fetched when a thread opens.
pub(crate) const THREAD_FILL: u16 = 50;
/// Events per room kept by the room list (for unread counting).
pub(crate) const ROOM_LIST_TIMELINE_LIMIT: u32 = 20;
/// Rooms asked from the public directory per search.
pub(crate) const SEARCH_ROOMS_LIMIT: u32 = 20;
/// People asked from the user directory per search.
pub(crate) const SEARCH_USERS_LIMIT: u64 = 10;
/// Characters of a room's latest message kept for the list (it shows 1 to
/// 3 lines and fades the last).
pub(crate) const PREVIEW_CHARS: usize = 400;
/// A message body up to this many bytes names an exported file.
pub(crate) const MAX_FILENAME_FROM_BODY: usize = 200;

// Picture sizes (pixels, square thumbnails asked from the server)

pub(crate) const ROOM_AVATAR_PX: u32 = 192;
pub(crate) const OWN_AVATAR_PX: u32 = 256;
pub(crate) const MEDIA_THUMB_PX: u32 = 640;
/// Small: a custom emoji is drawn at the height of a line.
pub(crate) const EMOTE_PX: u32 = 96;
/// The height given to custom emoji in sent HTML, for other clients.
pub(crate) const EMOTE_HTML_HEIGHT: u32 = 32;

// Power levels (Synapse's defaults, as Element sends them)

pub(crate) const POWER_MOD: i64 = 50;
pub(crate) const POWER_ADMIN: i64 = 100;

// Runtime

/// Tokio worker threads.
pub(crate) const WORKER_THREADS: usize = 2;

// Files and directories in the data directory

pub(crate) const SESSION_FILE: &str = "session.json";
/// Who was logged in and on which device, after the server ended a session.
pub(crate) const RELOGIN_FILE: &str = "relogin.json";
pub(crate) const DEVICE_ID_FILE: &str = "device_id";
/// The matrix-sdk sqlite store.
pub(crate) const STORE_DIR: &str = "store";
/// Marks a store that was made with a passphrase.
pub(crate) const ENCRYPTED_MARKER: &str = "rustrx-encrypted";
pub(crate) const MEDIA_DIR: &str = "media";
/// Inside MEDIA_DIR.
pub(crate) const EMOTE_DIR: &str = "emotes";
pub(crate) const AVATAR_DIR: &str = "avatars";
/// The own picture, inside AVATAR_DIR beside the rooms'.
pub(crate) const OWN_AVATAR_FILE: &str = "_own.img";

/// Removing a person's recent messages: how far back to look, how many of
/// theirs at most, and the pause between two (the server limits the rate).
pub(crate) const REDACT_RECENT_SCAN: u32 = 200;
pub(crate) const REDACT_RECENT_MAX: usize = 100;
pub(crate) const REDACT_RECENT_PACE_MS: u64 = 300;
