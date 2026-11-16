// SPDX-License-Identifier: GPL-3.0-or-later
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
    /// `emotes`: the custom emoji the text uses, shortcode (without the
    /// colons) to mxc address; the formatted body gets their pictures.
    SendText {
        room_id: String,
        body: String,
        reply_to: Option<String>,
        markdown: Option<bool>,
        emotes: Option<std::collections::HashMap<String, String>>,
        /// into this thread (its root event)
        thread_root: Option<String>,
    },
    /// A sticker from an image pack.
    SendSticker { room_id: String, url: String, body: String, info: Option<serde_json::Value>, thread_root: Option<String> },
    /// A thread's timeline beside the room's; answered by "thread_timeline".
    OpenThread { room_id: String, root: String },
    CloseThread,
    /// The room's threads, live; answered by "thread_list".
    ThreadList { room_id: String },
    CloseThreadList,
    /// The open thread read to its end.
    MarkThreadRead,
    /// The homeserver pushes this endpoint through this gateway (UnifiedPush);
    /// `old`, a previous endpoint, is removed. Answered by "pusher_set".
    PushSet { endpoint: String, gateway: String, old: Option<String>, lang: Option<String> },
    PushRemove { endpoint: String },
    /// A pushed message (room and event id): fetched and shown. Answered by
    /// "push_notification" (or nothing, when the account's rules keep it quiet).
    PushHandle { room_id: String, event_id: String },
    /// A file; pictures carry their size and a thumbnail from the UI, voice
    /// messages their duration.
    SendFile {
        room_id: String,
        path: String,
        /// what the file is, from its content (the name may have no or a
        /// wrong extension); else guessed from the name
        mime: Option<String>,
        caption: Option<String>,
        w: Option<u64>,
        h: Option<u64>,
        thumb_path: Option<String>,
        thumb_w: Option<u64>,
        thumb_h: Option<u64>,
        voice: Option<bool>,
        duration_ms: Option<u64>,
        thread_root: Option<String>,
    },
    React { room_id: String, event_id: String, key: String },
    /// A sent event (event_id) is redacted, a local echo (txn_id) discarded.
    Redact { room_id: String, event_id: Option<String>, txn_id: Option<String>, reason: Option<String> },
    Edit { room_id: String, event_id: Option<String>, txn_id: Option<String>, body: String },
    PollVote { room_id: String, event_id: String, answers: Vec<String> },
    Report { room_id: String, event_id: String, reason: Option<String> },
    /// The event's JSON as received (and its latest edit): "event_source".
    EventSource { room_id: String, event_id: String },
    MarkRead { room_id: String, private: Option<bool> },
    Typing { room_id: String, typing: Option<bool> },
    /// The picture (or its thumbnail) of an event, into the media cache;
    /// with dest_dir a copy for other apps. Answered by "media".
    GetMedia { room_id: String, event_id: String, thumb: Option<bool>, dest_dir: Option<String> },
    /// A custom emoji's picture (an mxc:// address from a message or a
    /// reaction), small, into the emote cache. Answered by "emote".
    FetchEmote { mxc: String },
    /// The image packs offered in a room (own, enabled everywhere, the
    /// room's). Answered by "image_packs".
    ImagePacks { room_id: String },
    /// A custom emoji into the user's own pack. Answered by "emote_added".
    AddUserEmote { shortcode: String, url: String },

    // --- rooms ---
    GetNotificationMode { room_id: String },
    /// "all", "mentions", "mute", anything else: back to the account's rule.
    SetNotificationMode { room_id: String, mode: String },
    SetFavourite { room_id: String, value: Option<bool> },
    SetLowPriority { room_id: String, value: Option<bool> },
    JoinRoom { room_id: String },
    LeaveRoom { room_id: String },
    /// A room id or alias (and the servers that know it, from a link);
    /// answered by "joined".
    JoinByIdOrAlias { id: String, via: Option<Vec<String>> },
    /// What a room or space not joined is; answered by "room_preview".
    RoomPreview { id: String, via: Option<Vec<String>> },
    /// Ask to join a room or space whose join rule is "knock"; answered by "knocked".
    Knock { id: String, reason: Option<String>, via: Option<Vec<String>> },
    /// The room's matrix.to link with its servers; answered by "room_link".
    RoomLink { room_id: String },
    /// Who may join: "invite", "knock", "public", "restricted", "knock_restricted".
    SetJoinRule { room_id: String, rule: String },
    /// A space's rooms, live; answered by "space_rooms" until space_close.
    SpaceOpen { space_id: String },
    SpaceClose { space_id: String },
    SpaceAddChild { space_id: String, room_id: String },
    SpaceRemoveChild { space_id: String, room_id: String },
    /// Leave a space; with_rooms also leaves its rooms and subspaces.
    SpaceLeave { space_id: String, with_rooms: Option<bool> },
    /// Answered by "dm_ready".
    CreateDm { user_id: String },
    /// Answered by "joined".
    CreateRoom {
        name: Option<String>,
        topic: Option<String>,
        public: Option<bool>,
        alias: Option<String>,
        encrypted: Option<bool>,
        /// a space instead of a room
        space: Option<bool>,
        /// the space the new room or space goes into
        parent: Option<String>,
        /// "invite", "knock", "public", "restricted" (members of the parent space)
        join_rule: Option<String>,
    },
    /// Answered by "members".
    RoomMembers { room_id: String },
    SetRoomName { room_id: String, name: String },
    /// A picture file (the UI has turned it upright and scaled it down).
    SetRoomAvatar { room_id: String, path: String, w: Option<u64>, h: Option<u64> },
    /// One's own picture from a file (prepared by the UI as for a room).
    SetOwnAvatar { path: String },
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
    /// No other device and no recovery key: new cross-signing keys, a new
    /// backup and recovery key; the old backup is deleted. An account service
    /// asks for approval in the browser ("identity_reset_approval").
    IdentityReset,
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
    // --- calls (Element Call in a WebView) ---
    /// Answered by "call_url"; then "call_message" events for the page.
    /// `skip_lobby` joins at once, without Element Call's lobby (tests).
    CallStart { room_id: String, voice: Option<bool>, skip_lobby: Option<bool> },
    /// A widget API postMessage from the page, passed to the driver as is.
    CallMessage { data: String },
    CallEnd,
    /// A request to join a room or space: accept (invite), or decline
    /// (kick; `ban` keeps them out).
    KnockAnswer { room_id: String, user_id: String, accept: bool, ban: Option<bool>, reason: Option<String> },
    /// Requests looked at: not notified again.
    KnockSeen { room_id: String, user_ids: Vec<String> },
    /// No to an incoming call's ring (`event_id` of the ring).
    CallDecline { room_id: String, event_id: String },
}

/// A non-empty string, or nothing (the UI sends "" for "no reply" and the like).
pub(crate) fn given(s: &Option<String>) -> Option<&str> {
    s.as_deref().map(str::trim).filter(|s| !s.is_empty())
}
