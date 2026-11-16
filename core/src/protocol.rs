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
    /// A browser login still waiting ends ("login_error").
    LoginCancel,
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
    /// The text being written in a room, kept encrypted; "" removes it.
    /// The room's draft comes with open_room as "draft".
    SaveDraft { room_id: String, text: String },
    /// A message the server did not take (its txn_id), sent again as it was.
    RetrySend { room_id: String, txn_id: String },
    /// The room's threads, live; answered by "thread_list".
    ThreadList { room_id: String },
    CloseThreadList,
    /// The open thread read to its end.
    MarkThreadRead,
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
/// A command from its JSON. Only an object is one: serde's internally
/// tagged enums would also take a sequence with the tag first, so
/// `["logout"]` would run.
pub(crate) fn parse(v: serde_json::Value) -> Result<Command, String> {
    if !v.is_object() {
        return Err("a command is a JSON object".to_owned());
    }
    serde_json::from_value::<Command>(v).map_err(|e| e.to_string())
}

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
        "probe_server", "login_password", "login_sso", "login_oauth", "login_cancel", "logout",
        "open_room", "close_room", "paginate_back", "send_text", "send_sticker",
        "open_thread", "close_thread", "save_draft", "retry_send", "thread_list", "close_thread_list", "mark_thread_read",
        "push_set", "push_remove", "push_check", "push_handle", "send_file", "react", "redact", "edit",
        "poll_vote", "report", "event_source", "mark_read", "typing", "get_media",
        "fetch_emote", "image_packs", "add_user_emote",
        "get_notification_mode", "set_notification_mode", "set_favourite", "set_low_priority",
        "join_room", "leave_room", "join_by_id_or_alias", "room_preview", "knock", "room_link",
        "set_join_rule", "space_open", "space_close", "space_add_child", "space_remove_child",
        "space_leave", "create_dm", "create_room", "room_members", "set_room_name",
        "set_room_avatar", "set_own_avatar", "set_room_topic", "invite", "search", "refresh_rooms",
        "verification_request_self", "verification_accept", "verification_start_sas",
        "verification_confirm", "verification_mismatch", "verification_cancel",
        "recovery_recover", "recovery_enable", "recovery_reset", "identity_reset",
        "export_keys", "import_keys",
        "devices", "delete_device", "profile", "set_display_name", "ignore_user",
        "unignore_user", "storage_info", "clear_media",
        "call_start", "call_message", "call_end", "knock_answer", "knock_seen", "call_decline",
    ];

    /// How many fields the commands require, all together. Like COMMANDS, a
    /// change here is a change of the contract.
    const REQUIRED_FIELDS: usize = 101;


    /// The names serde knows, from its own error for an unknown tag
    /// ("unknown variant `x`, expected one of `a`, `b`, ...").
    fn enum_names() -> Vec<String> {
        let err = parse(json!({"cmd": "__none__"})).unwrap_err();
        let (_, list) = err.split_once("expected one of").expect("serde lists the variants");
        list.split('`').skip(1).step_by(2).map(str::to_owned).collect()
    }

    /// The smallest object that parses as `name`: each field serde says is
    /// missing gets the first of "", false, [] that it takes.
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
            let fill = [json!(""), json!(false), json!([])]
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
                for wrong in [Value::Null, json!(12345)] {
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
