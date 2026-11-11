//! The commands the UI sends, one JSON object each with a "cmd" field.
//! This enum is the contract (docs/PROTOCOL.md is written from it): a
//! command that does not match fails before anything runs, with serde's
//! message ("missing field `body`") in the error event.
//!
//! Fields that may be absent or null are `Option`; the UI sends JavaScript
//! `undefined` as null. Unknown extra fields are ignored.

use serde::Deserialize;

#[derive(Debug, Deserialize)]
#[serde(tag = "cmd", rename_all = "snake_case")]
pub(crate) enum Command {
    // --- session ---
    /// What the server offers for login; answered by "server_info".
    ProbeServer { user: Option<String>, homeserver: Option<String> },
    LoginPassword { user: String, password: String, homeserver: Option<String> },
    LoginSso { user: Option<String>, homeserver: Option<String>, idp: Option<String> },
    Logout,

    // --- the open room ---
    OpenRoom { room_id: String },
    CloseRoom,
    /// Older history; answered by "paginated".
    PaginateBack { room_id: String, count: Option<u16> },
    SendText { room_id: String, body: String, reply_to: Option<String>, markdown: Option<bool> },
    /// A file; pictures carry their size and a thumbnail from the UI, voice
    /// messages their duration.
    SendFile {
        room_id: String,
        path: String,
        caption: Option<String>,
        w: Option<u64>,
        h: Option<u64>,
        thumb_path: Option<String>,
        thumb_w: Option<u64>,
        thumb_h: Option<u64>,
        voice: Option<bool>,
        duration_ms: Option<u64>,
    },
    React { room_id: String, event_id: String, key: String },
    /// A sent event (event_id) is redacted, a local echo (txn_id) discarded.
    Redact { room_id: String, event_id: Option<String>, txn_id: Option<String>, reason: Option<String> },
    Edit { room_id: String, event_id: Option<String>, txn_id: Option<String>, body: String },
    PollVote { room_id: String, event_id: String, answers: Vec<String> },
    Report { room_id: String, event_id: String, reason: Option<String> },
    MarkRead { room_id: String, private: Option<bool> },
    Typing { room_id: String, typing: Option<bool> },
    /// The picture (or its thumbnail) of an event, into the media cache;
    /// with dest_dir a copy for other apps. Answered by "media".
    GetMedia { room_id: String, event_id: String, thumb: Option<bool>, dest_dir: Option<String> },

    // --- rooms ---
    GetNotificationMode { room_id: String },
    /// "all", "mentions", "mute", anything else: back to the account's rule.
    SetNotificationMode { room_id: String, mode: String },
    SetFavourite { room_id: String, value: Option<bool> },
    SetLowPriority { room_id: String, value: Option<bool> },
    JoinRoom { room_id: String },
    LeaveRoom { room_id: String },
    /// A room id or alias; answered by "joined".
    JoinByIdOrAlias { id: String },
    /// Answered by "dm_ready".
    CreateDm { user_id: String },
    /// Answered by "joined".
    CreateRoom {
        name: Option<String>,
        topic: Option<String>,
        public: Option<bool>,
        alias: Option<String>,
        encrypted: Option<bool>,
    },
    /// Answered by "members".
    RoomMembers { room_id: String },
    SetRoomName { room_id: String, name: String },
    SetRoomTopic { room_id: String, topic: String },
    Invite { room_id: String, user_id: String },
    /// Users and public rooms; answered by "search_results".
    Search { term: String },
    RefreshRooms,

    // --- encryption ---
    VerificationRequestSelf,
    VerificationAccept { flow_id: String },
    VerificationStartSas { flow_id: String },
    VerificationConfirm { flow_id: String },
    VerificationMismatch { flow_id: String },
    VerificationCancel { flow_id: String },
    RecoveryRecover { key: String },
    RecoveryEnable,
    RecoveryReset,
    ExportKeys { path: String, passphrase: String },
    ImportKeys { path: String, passphrase: String },

    // --- account and devices ---
    Devices,
    DeleteDevice { device_id: String, password: String },
    Profile,
    SetDisplayName { name: String },
    IgnoreUser { user_id: String },
    UnignoreUser { user_id: String },
    StorageInfo,
    ClearMedia,
}

/// A non-empty string, or nothing (the UI sends "" for "no reply" and the like).
pub(crate) fn given(s: &Option<String>) -> Option<&str> {
    s.as_deref().map(str::trim).filter(|s| !s.is_empty())
}
