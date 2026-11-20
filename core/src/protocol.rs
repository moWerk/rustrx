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
    /// Through the server's account service (OAuth 2.0, MAS) in the
    /// browser: open_url, then logged_in or login_error. `signup` offers to
    /// make an account first.
    LoginOauth { user: Option<String>, homeserver: Option<String>, signup: Option<bool> },
    /// With a QR code another, logged-in device scans: "qr_login" progress,
    /// then logged_in or login_error.
    LoginQr { user: Option<String>, homeserver: Option<String> },
    /// The two digits the other device shows during a QR login or while
    /// letting a device in.
    QrCheckCode { code: u8 },
    /// A QR code the camera read (base64 of its bytes): logged out, a
    /// logged-in device's code to log in with ("qr_login" progress); logged
    /// in, a new device's code to let in ("qr_grant").
    QrScanned { data: String },
    /// Letting a new device in by the code this one shows: "qr_grant"
    /// progress (qr, scanned, consent, syncing, done, error).
    GrantQr,
    /// A browser or QR login still waiting ends ("login_error").
    LoginCancel,
    /// Opens the saved session again after a start that could not (the
    /// store's key came later, from Secrets' prompt): "ready" as at start.
    /// Ignored while logged in.
    Restore,
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
        /// an emote, as /me sends it ("* name does this")
        emote: Option<bool>,
        /// people picked while typing: their names in the body become
        /// pills, and the message mentions them (m.mentions)
        mentions: Option<Vec<MentionArg>>,
    },
    /// A sticker from an image pack.
    SendSticker { room_id: String, mxc: String, body: String, info: Option<serde_json::Value>, thread_root: Option<String> },
    /// A thread's timeline beside the room's; answered by "thread_timeline".
    OpenThread { room_id: String, thread_root: String },
    CloseThread,
    /// The text being written in a room, kept encrypted; "" removes it.
    /// The room's draft comes with open_room as "draft".
    SaveDraft { room_id: String, text: String },
    /// A message the server did not take (its txn_id), sent again as it was.
    RetrySend { room_id: String, txn_id: String },
    /// The room's threads, live; answered by "thread_list".
    ThreadList { room_id: String },
    CloseThreadList,
    /// The open thread read to its end; `private`: a private receipt, as
    /// mark_read has it ("Send read receipts" off)
    MarkThreadRead { private: Option<bool> },
    /// The homeserver pushes this endpoint through this gateway (UnifiedPush);
    /// `old`, a previous endpoint, is removed. Answered by "pusher_set".
    PushSet { endpoint: String, gateway: String, old: Option<String>, lang: Option<String> },
    PushRemove { endpoint: String },
    /// Whether the homeserver still has our pusher: "pusher_checked".
    PushCheck { endpoint: String },
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
    /// `emotes` and `mentions` as for send_text; the message's own pills
    /// and custom emoji stay where the new text still has them.
    Edit {
        room_id: String,
        event_id: Option<String>,
        txn_id: Option<String>,
        body: String,
        emotes: Option<std::collections::HashMap<String, String>>,
        mentions: Option<Vec<MentionArg>>,
    },
    PollVote { room_id: String, event_id: String, answers: Vec<String> },
    Report { room_id: String, event_id: String, reason: Option<String> },
    /// The event's JSON as received (and its latest edit): "event_source".
    EventSource { room_id: String, event_id: String },
    /// marker: false sends the receipt only and leaves the fully-read
    /// marker (the unread line) until the messages were shown; event_id:
    /// read up to this message only (never backwards), else the whole room
    MarkRead { room_id: String, private: Option<bool>, marker: Option<bool>, event_id: Option<String> },
    Typing { room_id: String, typing: Option<bool> },
    /// Shown as online to others (servers with presence on); offline is
    /// set at every login, before the first sync
    SetPresence { online: bool },
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
    AddUserEmote { shortcode: String, mxc: String },

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
    JoinByIdOrAlias { room_or_alias: String, via: Option<Vec<String>> },
    /// What a room or space not joined is; answered by "room_preview".
    RoomPreview { room_or_alias: String, via: Option<Vec<String>> },
    /// Ask to join a room or space whose join rule is "knock"; answered by "knocked".
    Knock { room_or_alias: String, reason: Option<String>, via: Option<Vec<String>> },
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
    /// Public rooms and spaces in one server's directory (the own one when
    /// none is named), by part of a name or address: "room_search_results".
    SearchRooms { term: String, server: Option<String> },
    RefreshRooms,

    /// The users this account ignores: "ignored_users".
    IgnoredUsers,
    /// One person for their page (in a room: power, what we may do): "user_info".
    UserInfo { user_id: String, room_id: Option<String> },
    /// Removes someone from a room; they may come back.
    Kick { room_id: String, user_id: String, reason: Option<String> },
    /// Removes someone and keeps them out.
    Ban { room_id: String, user_id: String, reason: Option<String> },
    Unban { room_id: String, user_id: String, reason: Option<String> },

    /// What we may change in a room and how it is set: "room_settings".
    RoomSettings { room_id: String },
    /// A person's level (role): 0 member, 50 moderator, 100 admin, or any.
    SetUserLevel { room_id: String, user_id: String, level: i64 },
    /// One permission's level: a power-levels field (users_default,
    /// events_default, state_default, invite, kick, ban, redact,
    /// notify_room) or an event type (m.room.name, ...).
    SetLevel { room_id: String, permission: String, level: i64 },
    /// world_readable, shared, invited or joined.
    SetHistoryVisibility { room_id: String, value: String },
    SetGuestAccess { room_id: String, allowed: bool },
    /// One way: a room cannot be unencrypted again.
    EnableEncryption { room_id: String },
    /// Listed in the server's public room directory.
    SetDirectoryVisibility { room_id: String, listed: bool },
    /// The main address and the other addresses.
    SetAliases { room_id: String, canonical: Option<String>, alt: Vec<String> },
    /// A new address on its server, pointing to the room.
    AddAlias { room_id: String, alias: String },
    RemoveAlias { room_id: String, alias: String },
    /// The servers that may take part; refused if it shuts out our own.
    SetServerAcl { room_id: String, allow: Vec<String>, deny: Vec<String>, allow_ip_literals: bool },
    /// To a newer room version: "room_upgraded" with the new room.
    UpgradeRoom { room_id: String, version: String },
    Pin { room_id: String, event_id: String, pinned: bool },
    /// To the own server's admins: "reported".
    ReportRoom { room_id: String, reason: Option<String> },
    ReportUser { user_id: String, reason: Option<String> },
    /// A room of a space marked suggested, or given an `order`.
    SpaceChildSet { space_id: String, room_id: String, suggested: Option<bool>, order: Option<String> },
    /// A space's rooms in this order.
    SpaceReorder { space_id: String, room_ids: Vec<String> },
    /// Banned from a space and its rooms where we may: "space_banned".
    SpaceBan { space_id: String, user_id: String, reason: Option<String> },
    /// A person's recent messages in a room removed: "redact_progress".
    RedactRecent { room_id: String, user_id: String, reason: Option<String> },

    // --- encryption ---
    VerificationRequestSelf,
    /// Verifying someone else (in a direct chat): "verification", then as for oneself.
    VerifyUser { user_id: String },
    VerificationAccept { flow_id: String },
    VerificationStartSas { flow_id: String },
    VerificationConfirm { flow_id: String },
    VerificationMismatch { flow_id: String },
    VerificationCancel { flow_id: String },
    RecoveryRecover { recovery_key: String },
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
    // --- 1:1 calls (legacy m.call.*; the call itself is the UI's) ---
    /// One m.call.* event (VOIP_TYPES), its content as the UI built it;
    /// answered by "voip_sent".
    VoipSend { room_id: String, r#type: String, content: serde_json::Map<String, serde_json::Value> },
    /// The homeserver's TURN credentials: "turn_server".
    TurnServer,
}

/// The legacy 1:1 call events (m.call.*) the UI may send and is given.
/// Anything else through voip_send would be a way to post arbitrary events.
pub(crate) const VOIP_TYPES: [&str; 7] = [
    "m.call.invite",
    "m.call.answer",
    "m.call.candidates",
    "m.call.hangup",
    "m.call.select_answer",
    "m.call.reject",
    "m.call.negotiate",
];

pub(crate) fn voip_type_allowed(kind: &str) -> bool {
    VOIP_TYPES.contains(&kind)
}

/// A person mentioned in a message: the name as written in its body.
#[derive(Deserialize, Debug, Clone)]
pub(crate) struct MentionArg {
    pub(crate) user_id: String,
    pub(crate) name: String,
}

/// A command from its JSON. Only an object is one: serde's internally
/// tagged enums would also take a sequence with the tag first, so
/// `["logout"]` would run.
pub(crate) fn parse(v: serde_json::Value) -> Result<Command, String> {
    if !v.is_object() {
        return Err("a command is a JSON object".to_owned());
    }
    serde_json::from_value::<Command>(v).map_err(|e| e.to_string())
}

/// A non-empty string, or nothing (the UI sends "" for "no reply" and the like).
pub(crate) fn given(s: &Option<String>) -> Option<&str> {
    s.as_deref().map(str::trim).filter(|s| !s.is_empty())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{json, Value};

    /// Every command the core accepts, in the enum's order. Adding a command
    /// is a reviewed act: update this list and docs/PROTOCOL.md.
    const COMMANDS: &[&str] = &[
        "probe_server", "login_password", "login_sso", "login_oauth", "login_qr", "qr_check_code", "qr_scanned", "grant_qr", "login_cancel", "restore", "logout",
        "open_room", "close_room", "paginate_back", "send_text", "send_sticker",
        "open_thread", "close_thread", "save_draft", "retry_send", "thread_list", "close_thread_list", "mark_thread_read",
        "push_set", "push_remove", "push_check", "push_handle", "send_file", "react", "redact", "edit",
        "poll_vote", "report", "event_source", "mark_read", "typing", "set_presence", "get_media",
        "fetch_emote", "image_packs", "add_user_emote",
        "get_notification_mode", "set_notification_mode", "set_favourite", "set_low_priority",
        "join_room", "leave_room", "join_by_id_or_alias", "room_preview", "knock", "room_link",
        "set_join_rule", "space_open", "space_close", "space_add_child", "space_remove_child",
        "space_leave", "create_dm", "create_room", "room_members", "set_room_name",
        "set_room_avatar", "set_own_avatar", "set_room_topic", "invite", "search", "search_rooms", "refresh_rooms", "ignored_users", "user_info", "kick", "ban", "unban",
        "room_settings", "set_user_level", "set_level", "set_history_visibility", "set_guest_access",
        "enable_encryption", "set_directory_visibility", "set_aliases", "add_alias", "remove_alias",
        "set_server_acl", "upgrade_room", "pin", "report_room", "report_user",
        "space_child_set", "space_reorder", "space_ban", "redact_recent",
        "verification_request_self", "verify_user", "verification_accept", "verification_start_sas",
        "verification_confirm", "verification_mismatch", "verification_cancel",
        "recovery_recover", "recovery_enable", "recovery_reset", "identity_reset",
        "export_keys", "import_keys",
        "devices", "delete_device", "profile", "set_display_name", "ignore_user",
        "unignore_user", "storage_info", "clear_media",
        "call_start", "call_message", "call_end", "knock_answer", "knock_seen", "call_decline",
        "voip_send", "turn_server",
    ];

    /// How many fields the commands require, all together. Like COMMANDS, a
    /// change here is a change of the contract.
    const REQUIRED_FIELDS: usize = 155;


    /// The names serde knows, from its own error for an unknown tag
    /// ("unknown variant `x`, expected one of `a`, `b`, ...").
    fn enum_names() -> Vec<String> {
        let err = parse(json!({"cmd": "__none__"})).unwrap_err();
        let (_, list) = err.split_once("expected one of").expect("serde lists the variants");
        list.split('`').skip(1).step_by(2).map(str::to_owned).collect()
    }

    /// The smallest object that parses as `name`: each field serde says is
    /// missing gets the first of "", false, [], 0, {} that it takes.
    fn minimal(name: &str) -> Value {
        let mut v = json!({"cmd": name});
        for _ in 0..16 {
            let err = match parse(v.clone()) {
                Ok(_) => return v,
                Err(e) => e,
            };
            let field = err
                .split_once("missing field `")
                .and_then(|(_, rest)| rest.split_once('`'))
                .map(|(f, _)| f.to_owned())
                .unwrap_or_else(|| panic!("{name}: {err}"));
            let fill = [json!(""), json!(false), json!([]), json!(0), json!({})]
                .into_iter()
                .find(|candidate| {
                    let mut t = v.clone();
                    t[&field] = candidate.clone();
                    parse(t).map_or_else(|e| !e.contains("invalid type"), |_| true)
                })
                .unwrap_or_else(|| panic!("{name}.{field}: no simple value fits"));
            v[&field] = fill;
        }
        panic!("{name}: too many required fields");
    }

    /// The enum's variant names, from serde's own list, are exactly the list
    /// above.
    /// Planted-bug: rename `Logout` to `LogOut`, or delete `CallDecline`.
    #[test]
    fn command_names_are_pinned() {
        let pinned: Vec<String> = COMMANDS.iter().map(|s| s.to_string()).collect();
        assert_eq!(enum_names(), pinned);
    }

    /// Each pinned name has a valid form, and parses to the variant of that
    /// name (not to a neighbour through an alias).
    /// Planted-bug: add `#[serde(rename = "log_out")]` to `Logout`.
    #[test]
    fn every_command_has_a_minimal_form() {
        for name in COMMANDS {
            let v = minimal(name);
            let debug = format!("{:?}", parse(v).unwrap());
            let variant: String = debug.chars().take_while(char::is_ascii_alphanumeric).collect();
            let snake: String = variant
                .chars()
                .enumerate()
                .flat_map(|(i, c)| {
                    let sep = (i > 0 && c.is_ascii_uppercase()).then_some('_');
                    sep.into_iter().chain(std::iter::once(c.to_ascii_lowercase()))
                })
                .collect();
            assert_eq!(&snake, name);
        }
    }

    /// docs/PROTOCOL.md's command tables name exactly the enum's commands.
    /// Planted-bug: delete the `call_decline` row from docs/PROTOCOL.md.
    #[test]
    fn protocol_md_lists_every_command() {
        let doc = include_str!("../../docs/PROTOCOL.md");
        let start = doc.find("## Commands").expect("a Commands section");
        let end = doc.find("## Events").expect("an Events section");
        let mut documented: Vec<String> = doc[start..end]
            .lines()
            .filter(|l| l.starts_with("| `"))
            .flat_map(|l| {
                let cell = l.split('|').nth(1).unwrap_or("");
                cell.split('`').skip(1).step_by(2).map(str::to_owned).collect::<Vec<_>>()
            })
            .collect();
        documented.sort();
        let mut names: Vec<String> = COMMANDS.iter().map(|s| s.to_string()).collect();
        names.sort();
        assert_eq!(documented, names);
    }

    /// Every required field is required: without it, null in its place or a
    /// number in its place, the command fails to parse. Their number is
    /// pinned too, so a field that quietly becomes optional is noticed.
    /// Planted-bug: put `#[serde(default)]` on `SendText::body`.
    #[test]
    fn required_fields_are_enforced() {
        let mut checked = Vec::new();
        for name in COMMANDS {
            let full = minimal(name);
            let fields: Vec<String> = full.as_object().unwrap().keys().filter(|k| *k != "cmd").cloned().collect();
            for field in fields {
                let mut without = full.clone();
                without.as_object_mut().unwrap().remove(&field);
                let err = parse(without).expect_err(&format!("{name} without {field}"));
                assert!(err.contains(&format!("missing field `{field}`")), "{name}: {err}");
                // a number field takes 12345, so a string is its wrong value
                let other = if full[&field].is_number() { json!("x") } else { json!(12345) };
                for wrong in [Value::Null, other] {
                    let mut t = full.clone();
                    t[&field] = wrong.clone();
                    assert!(parse(t).is_err(), "{name}.{field} = {wrong}");
                }
                checked.push(format!("{name}.{field}"));
            }
        }
        assert_eq!(checked.len(), REQUIRED_FIELDS, "required fields now: {checked:?}");
    }

    /// Input the UI should never send, but a bug or a hostile caller could:
    /// each is refused by the parser rather than reaching a handler.
    /// Planted-bug: add `#[serde(other)] Unknown,` as the enum's last variant.
    #[test]
    fn hostile_commands_are_refused() {
        let long = "x".repeat(1 << 20);
        let cases = [
            json!({}),
            json!({"room_id": "!a:b"}),
            json!({"cmd": null}),
            json!({"cmd": 5}),
            json!({"cmd": ["logout"]}),
            json!({"cmd": ""}),
            json!({"cmd": "Logout"}),
            json!({"cmd": "logout "}),
            json!({"cmd": "log\u{0}out"}),
            json!({"cmd": long}),
            json!({"cmd": "send_text", "room_id": "!a:b"}),
            json!({"cmd": "send_text", "room_id": "!a:b", "body": 1}),
            json!({"cmd": "send_text", "room_id": "!a:b", "body": "x", "markdown": "yes"}),
            json!({"cmd": "paginate_back", "room_id": "!a:b", "count": 70000}),
            json!({"cmd": "paginate_back", "room_id": "!a:b", "count": -1}),
            json!({"cmd": "paginate_back", "room_id": "!a:b", "count": 1.5}),
            json!({"cmd": "paginate_back", "room_id": "!a:b", "count": "30"}),
            json!({"cmd": "poll_vote", "room_id": "!a:b", "event_id": "$e", "answers": [1, 2]}),
            json!({"cmd": "poll_vote", "room_id": "!a:b", "event_id": "$e", "answers": "a"}),
            json!({"cmd": "knock_answer", "room_id": "!a:b", "user_id": "@u:b"}),
            json!({"cmd": "knock_answer", "room_id": "!a:b", "user_id": "@u:b", "accept": "true"}),
            json!({"cmd": "send_text", "room_id": "!a:b", "body": "x", "emotes": {"a": 1}}),
            json!({"cmd": "send_file", "room_id": "!a:b", "path": "/x", "w": -5}),
            json!({"cmd": "voip_send", "room_id": "!a:b", "content": {}}),
            json!({"cmd": "voip_send", "room_id": "!a:b", "type": "m.call.hangup"}),
            json!("logout"),
            json!(null),
        ];
        for case in cases {
            assert!(parse(case.clone()).is_err(), "accepted: {}", &case.to_string()[..case.to_string().len().min(120)]);
        }
    }

    /// A command is an object. serde's internally tagged enums also take a
    /// sequence with the tag first, so `["logout"]` (any command without
    /// fields: identity_reset, clear_media) ran before `parse` refused it.
    /// Planted-bug: drop the is_object check in `parse` (it failed against
    /// the code before it).
    #[test]
    fn arrays_are_refused() {
        let accepted: Vec<String> = [json!(["logout"]), json!(["identity_reset"]), json!(["send_text", "!a:b", "hi"])]
            .into_iter()
            .filter_map(|case| parse(case).ok().map(|c| format!("{c:?}")))
            .collect();
        assert!(accepted.is_empty(), "accepted: {accepted:?}");
    }

    /// What the parser must keep accepting: extra fields, null for optional
    /// ones, empty and odd strings (the handlers judge those, not the parser).
    /// Planted-bug: add `#[serde(deny_unknown_fields)]` to the enum.
    #[test]
    fn lenient_where_documented() {
        let long = "y".repeat(1 << 20);
        let cases = [
            json!({"cmd": "logout", "extra": [1, 2, 3]}),
            json!({"cmd": "send_text", "room_id": "!a:b", "body": "x", "reply_to": null, "markdown": null,
                   "emotes": null, "thread_root": null, "from_the_future": true}),
            json!({"cmd": "send_text", "room_id": "", "body": ""}),
            json!({"cmd": "send_text", "room_id": "!a:b", "body": long}),
            json!({"cmd": "send_text", "room_id": "!a:b", "body": "nul \u{0} and \"quotes\" and \\ and \u{1F600}"}),
            json!({"cmd": "paginate_back", "room_id": "!a:b", "count": 65535}),
            json!({"cmd": "create_room"}),
            json!({"cmd": "mark_read", "room_id": "!a:b", "event_id": null}),
            json!({"cmd": "turn_server", "room_id": null}),
            // the type is judged by the handler (an error event), not here
            json!({"cmd": "voip_send", "room_id": "!a:b", "type": "m.room.message", "content": {}}),
        ];
        for case in cases {
            assert!(parse(case.clone()).is_ok(), "refused: {}", &case.to_string()[..case.to_string().len().min(120)]);
        }
        // the text arrives as sent, escapes decoded
        let raw = r#"{"cmd":"send_text","room_id":"!a:b","body":"a\u0000b\n\"c\""}"#;
        match serde_json::from_str::<Command>(raw).unwrap() {
            Command::SendText { body, .. } => assert_eq!(body, "a\0b\n\"c\""),
            other => panic!("{other:?}"),
        }
        // read up to one message: the event arrives with the command
        match parse(json!({"cmd": "mark_read", "room_id": "!a:b", "event_id": "$e:b"})).unwrap() {
            Command::MarkRead { event_id, .. } => assert_eq!(event_id.as_deref(), Some("$e:b")),
            other => panic!("{other:?}"),
        }
        // an edit carries its custom emoji and people, as a new message
        let edit = json!({"cmd": "edit", "room_id": "!a:b", "event_id": "$e:b", "body": "hi Ann :w:",
                          "emotes": {"w": "mxc://b/w"}, "mentions": [{"user_id": "@ann:b", "name": "Ann"}]});
        match parse(edit).unwrap() {
            Command::Edit { emotes, mentions, .. } => {
                assert_eq!(emotes.unwrap().get("w").map(String::as_str), Some("mxc://b/w"));
                assert_eq!(mentions.unwrap()[0].user_id, "@ann:b");
            }
            other => panic!("{other:?}"),
        }
        assert!(parse(json!({"cmd": "edit", "room_id": "!a:b", "event_id": "$e:b", "body": "x"})).is_ok());
    }

    /// voip_send's content is the event's content: only an object is one,
    /// and it reaches the handler whole (the SDP and the candidates are
    /// nested, a lost field is a call that never connects).
    /// Planted-bug: make `content` a `serde_json::Value` (null, a string
    /// and an array would then be sent as an event's content).
    #[test]
    fn voip_send_content_is_an_object_kept_whole() {
        for content in [Value::Null, json!("x"), json!([1]), json!(5)] {
            let case = json!({"cmd": "voip_send", "room_id": "!a:b", "type": "m.call.invite", "content": content});
            assert!(parse(case).is_err(), "accepted content {content}");
        }
        let content = json!({"call_id": "c1", "party_id": "p", "version": "1", "lifetime": 60000,
                             "offer": {"type": "offer", "sdp": "v=0\r\n"}, "future_field": [1, {"x": null}]});
        let case = json!({"cmd": "voip_send", "room_id": "!a:b", "type": "m.call.invite", "content": content, "extra": 1});
        match parse(case).unwrap() {
            Command::VoipSend { r#type, content: got, .. } => {
                assert_eq!(r#type, "m.call.invite");
                assert_eq!(serde_json::to_value(&got).unwrap(), content);
            }
            other => panic!("{other:?}"),
        }
    }

    /// Only the seven m.call.* types pass; voip_send must not become a way
    /// to post any event (a state-looking name, a ring, a message).
    /// Planted-bug: `kind.starts_with("m.call.")` in `voip_type_allowed`.
    #[test]
    fn voip_types_are_the_legacy_call_events_only() {
        for t in VOIP_TYPES {
            assert!(voip_type_allowed(t), "{t}");
        }
        for t in ["", "m.call", "m.call.", "m.call.member", "m.call.invite ", "M.call.invite", "m.room.message",
                  "m.call.notify", "org.matrix.msc4075.rtc.notification", "m.call.invitex"] {
            assert!(!voip_type_allowed(t), "{t:?}");
        }
    }

    /// `given`: the UI's "" (and blank) mean nothing; text is trimmed.
    /// Planted-bug: drop the `.map(str::trim)`.
    #[test]
    fn given_treats_blank_as_absent() {
        assert_eq!(given(&None), None);
        assert_eq!(given(&Some(String::new())), None);
        assert_eq!(given(&Some("  \t\n".to_owned())), None);
        assert_eq!(given(&Some(" $event ".to_owned())), Some("$event"));
        assert_eq!(given(&Some("x".to_owned())), Some("x"));
    }
}
