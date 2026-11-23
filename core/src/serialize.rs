// SPDX-License-Identifier: GPL-3.0-or-later
//! JSON shapes handed to the UI: the room list and timeline items.

use std::collections::HashMap;
use std::sync::Arc;

use matrix_sdk::latest_events::LatestEventValue;
use matrix_sdk::notification_settings::{NotificationSettings, RoomNotificationMode};
use matrix_sdk::ruma::events::receipt::ReceiptThread;
use matrix_sdk::ruma::events::room::message::{MessageFormat, MessageType};
use matrix_sdk::ruma::events::{AnyMessageLikeEventContent, AnySyncTimelineEvent, StateEventType};
use matrix_sdk::ruma::{OwnedUserId, UserId};
use matrix_sdk::{Room, RoomState};
use matrix_sdk_ui::timeline::{
    EventSendState, MembershipChange, TimelineEventShieldState, MsgLikeKind, Profile, TimelineDetails, TimelineItem,
    TimelineItemContent, TimelineItemKind, VirtualTimelineItem,
};
use serde_json::{json, Map, Value};

/// Text made safe inside HTML and its attributes (&, <, >, "); line
/// breaks stay, the caller turns them into <br> where it wants them.
pub(crate) fn escape_html(text: &str) -> String {
    text.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

fn ms(t: matrix_sdk::ruma::MilliSecondsSinceUnixEpoch) -> u64 {
    u64::from(t.0)
}

/// A room member's display name, else the localpart of their id.
pub(crate) async fn member_name(room: &Room, user: &UserId) -> String {
    match room.get_member_no_sync(user).await {
        Ok(Some(m)) => m.name().to_owned(),
        _ => user.localpart().to_owned(),
    }
}

/// A timeline sender's display name, else the localpart of their id.
pub(crate) fn profile_name(profile: &TimelineDetails<Profile>, user: &UserId) -> String {
    match profile {
        TimelineDetails::Ready(p) => p.display_name.clone().unwrap_or_else(|| user.localpart().to_owned()),
        _ => user.localpart().to_owned(),
    }
}

/// A room's cached name, else its id.
pub(crate) fn room_name(room: &Room) -> String {
    room.cached_display_name().map(|n| n.to_string()).unwrap_or_else(|| room.room_id().to_string())
}

/// An event's raw JSON as a value (null if it cannot be read).
pub(crate) fn raw_value<T>(raw: &matrix_sdk::ruma::serde::Raw<T>) -> Value {
    serde_json::from_str(raw.json().get()).unwrap_or(Value::Null)
}

/// File name of a room's cached avatar; the UI detects the image format.
pub(crate) fn avatar_file(dir: &std::path::Path, room: &Room) -> std::path::PathBuf {
    let safe: String = room.room_id().as_str().chars().filter(|c| c.is_ascii_alphanumeric()).collect();
    dir.join(format!("{safe}.img"))
}

/// The id of the room's latest remote event, for a read receipt on a room
/// without an open timeline.
pub(crate) fn latest_event_id(room: &Room) -> Option<matrix_sdk::ruma::OwnedEventId> {
    match room.latest_event() {
        LatestEventValue::Remote(ev) => ev.event_id().map(ToOwned::to_owned),
        _ => None,
    }
}

/// One line for the room list: what the last message was, and who sent it
/// in a group (`None` sender for direct chats and own messages say "You").
async fn preview(room: &Room, is_dm: bool) -> (Option<String>, Option<String>) {
    let value = room.latest_event();
    let (text, sender) = match &value {
        LatestEventValue::None => return (None, None),
        LatestEventValue::RemoteInvite { .. } => (Some("Invitation".to_owned()), None),
        LatestEventValue::Remote(ev) => match ev.raw().deserialize() {
            // a membership change says whose: "Anna joined", no sender in front
            Ok(AnySyncTimelineEvent::State(matrix_sdk::ruma::events::AnySyncStateEvent::RoomMember(m))) => {
                return (Some(membership_preview(room, &m).await), None);
            }
            Ok(event) => (event_preview(&event), ev.sender()),
            Err(_) => (None, None),
        },
        LatestEventValue::LocalIsSending(local)
        | LatestEventValue::LocalHasBeenSent { value: local, .. }
        | LatestEventValue::LocalCannotBeSent(local) => {
            let text = match local.content.deserialize() {
                Ok(AnyMessageLikeEventContent::RoomMessage(m)) => Some(message_preview(&m.msgtype)),
                Ok(_) => None,
                Err(_) => None,
            };
            (text, Some(room.own_user_id().to_owned()))
        }
    };
    let Some(text) = text else { return (None, None) };
    // the sender only matters in a group
    if is_dm {
        return (Some(text), None);
    }
    let who = match sender {
        Some(user) if user == room.own_user_id() => Some("You".to_owned()),
        Some(user) => Some(member_name(room, &user).await),
        None => None,
    };
    (Some(text), who)
}

/// A membership event in the room list: who, and what happened to them.
async fn membership_preview(room: &Room, event: &matrix_sdk::ruma::events::room::member::SyncRoomMemberEvent) -> String {
    use matrix_sdk::ruma::events::room::member::MembershipState as M;
    let Some(original) = event.as_original() else { return "Membership changed".to_owned() };
    let target = &original.state_key;
    let name = match original.content.displayname.clone() {
        Some(n) => n,
        None => member_name(room, target).await,
    };
    let what = match original.content.membership {
        M::Join => match original.unsigned.prev_content.as_ref().map(|p| &p.membership) {
            Some(M::Join) => "changed their profile",
            _ => "joined",
        },
        M::Leave if original.sender == *target => "left",
        M::Leave => "was removed",
        M::Invite => "was invited",
        M::Ban => "was banned",
        M::Knock => "asked to join",
        _ => "membership changed",
    };
    format!("{name} {what}")
}

fn event_preview(event: &AnySyncTimelineEvent) -> Option<String> {
    use matrix_sdk::ruma::events::{AnySyncMessageLikeEvent as M, AnySyncStateEvent as S, SyncMessageLikeEvent};
    Some(match event {
        AnySyncTimelineEvent::MessageLike(M::RoomMessage(SyncMessageLikeEvent::Original(e))) => {
            message_preview(&e.content.msgtype)
        }
        AnySyncTimelineEvent::MessageLike(M::RoomEncrypted(_)) => "Encrypted message".to_owned(),
        AnySyncTimelineEvent::MessageLike(M::Sticker(_)) => "Sticker".to_owned(),
        AnySyncTimelineEvent::MessageLike(M::CallInvite(_)) => "Call".to_owned(),
        AnySyncTimelineEvent::MessageLike(M::PollStart(_)) => "Poll".to_owned(),
        AnySyncTimelineEvent::State(S::RoomMember(_)) => "Membership changed".to_owned(),
        AnySyncTimelineEvent::State(S::RoomTopic(_)) => "Topic changed".to_owned(),
        AnySyncTimelineEvent::State(S::RoomName(_)) => "Room renamed".to_owned(),
        AnySyncTimelineEvent::State(S::RoomAvatar(_)) => "Picture changed".to_owned(),
        _ => return None,
    })
}

fn message_preview(msg: &MessageType) -> String {
    // the text, line breaks kept, up to PREVIEW_CHARS characters: the list shows as
    // many lines as the user set (1 to 3) and fades the last
    let lines = |s: &str| {
        let t = s.trim();
        match t.char_indices().nth(crate::consts::PREVIEW_CHARS) {
            Some((i, _)) => t[..i].to_owned(),
            None => t.to_owned(),
        }
    };
    match msg {
        MessageType::Text(t) => lines(&t.body),
        MessageType::Notice(t) => lines(&t.body),
        MessageType::Emote(t) => format!("* {}", lines(&t.body)),
        MessageType::Image(_) => "Photo".to_owned(),
        MessageType::Video(_) => "Video".to_owned(),
        MessageType::Audio(_) => "Audio".to_owned(),
        MessageType::File(f) => format!("File: {}", f.filename.as_deref().unwrap_or(&f.body)),
        MessageType::Location(_) => "Location".to_owned(),
        other => lines(other.body()),
    }
}

/// `settings`: the account's notification settings, made once for a whole
/// room list (building them reads and parses the push rules). `prev`: the
/// room's last row, if any: an invitation keeps its `inviter_known` (a scan
/// of every joined room).
pub(crate) async fn room_json(room: &Room, avatar_dir: &std::path::Path, settings: &NotificationSettings, prev: Option<&Value>) -> Value {
    let name = match room.cached_display_name() {
        Some(n) => n.to_string(),
        None => match room.display_name().await {
            Ok(n) => n.to_string(),
            Err(_) => room.room_id().to_string(),
        },
    };
    let counts = room.unread_notification_counts();
    let avatar = avatar_file(avatar_dir, room);
    let avatar_meta = std::fs::metadata(&avatar).ok();
    let is_dm = room.is_direct().await.unwrap_or(false);
    let (preview, preview_sender) = preview(room, is_dm).await;
    // muted: the room's notification mode, as Room::notification_mode has it
    // (only a joined room has one; the default mode is never Mute)
    let muted = room.state() == RoomState::Joined
        && settings.get_user_defined_room_notification_mode(room.room_id()).await == Some(RoomNotificationMode::Mute);
    // the own last message was read by someone else
    let own_read = match room.latest_event() {
        LatestEventValue::Remote(ev) if ev.sender().as_deref() == Some(room.own_user_id()) => match ev.event_id() {
            Some(id) => room
                .load_event_receipts(
                    matrix_sdk::ruma::events::receipt::ReceiptType::Read,
                    &ReceiptThread::Unthreaded,
                    &id,
                )
                .await
                .map(|r| r.iter().any(|(user, _)| user != room.own_user_id()))
                .unwrap_or(false),
            None => false,
        },
        _ => false,
    };
    // an upgraded room points to its successor
    let replaced_by = room.successor_room().map(|s| s.room_id.to_string());
    // what the user may change about who joins, and a space's rooms
    let (can_join_rules, can_children) = match room.power_levels().await {
        Ok(l) => (
            l.user_can_send_state(room.own_user_id(), StateEventType::RoomJoinRules),
            l.user_can_send_state(room.own_user_id(), StateEventType::SpaceChild),
        ),
        Err(_) => (false, false),
    };
    let state_keys = |events: Vec<matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState>| -> Vec<String> {
        events
            .into_iter()
            .filter_map(|ev| match ev {
                matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState::Sync(raw) => {
                    let v = raw_value(&raw);
                    let live = v.get("content").and_then(Value::as_object).is_some_and(|c| !c.is_empty());
                    if live { v.get("state_key").and_then(Value::as_str).map(str::to_owned) } else { None }
                }
                _ => None,
            })
            .collect()
    };
    let children = if room.is_space() {
        state_keys(room.get_state_events(StateEventType::SpaceChild).await.unwrap_or_default()).len()
    } else {
        0
    };
    let parents = state_keys(room.get_state_events(StateEventType::SpaceParent).await.unwrap_or_default());
    // who sent an invitation: their name, else their id; and whether we
    // share a room with them (a stranger's invitation can stay quiet)
    let (inviter, inviter_known) = if room.state() == RoomState::Invited {
        match room.invite_details().await.ok() {
            Some(i) => {
                // once per invitation (until a full pass): a stranger's was a
                // store read for every joined room on every pass naming it
                let kept = prev.filter(|p| p["state"] == "invited").and_then(|p| p["inviter_known"].as_bool());
                let mut known = kept.unwrap_or(false);
                if kept.is_none() {
                    for r in room.client().joined_rooms() {
                        if matches!(r.get_member_no_sync(&i.inviter_id).await, Ok(Some(m)) if *m.membership() == matrix_sdk::ruma::events::room::member::MembershipState::Join) {
                            known = true;
                            break;
                        }
                    }
                }
                let name = match i.inviter {
                    Some(m) => m.name().to_owned(),
                    None => i.inviter_id.to_string(),
                };
                (Some(name), known)
            }
            None => (None, false),
        }
    } else {
        (None, false)
    };
    json!({
        "inviter": inviter,
        "inviter_known": inviter_known,
        "muted": muted,
        "own_read": own_read,
        "replaced_by": replaced_by,
        "has_avatar": avatar_meta.is_some(),
        "avatar_path": avatar_meta.as_ref().map(|_| avatar.to_string_lossy().into_owned()),
        // the file's age: the picture's URL changes with it, past Qt's image cache
        "avatar_v": avatar_meta.and_then(|m| m.modified().ok())
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map(|d| d.as_secs()),
        "room_id": room.room_id().as_str(),
        "name": name,
        "preview": preview,
        "preview_sender": preview_sender,
        "topic": room.topic(),
        "alias": room.canonical_alias().map(|a| a.to_string()),
        "is_dm": is_dm,
        // the person of a direct chat: the invite dialog's contacts
        "dm_user": if is_dm {
            room.direct_targets().iter().find_map(|t| t.as_user_id().map(|u| u.to_string()))
        } else {
            None
        },
        // a call is running here (someone is in it, from the room's state)
        "in_call": room.has_active_room_call(),
        "is_fav": room.is_favourite(),
        "is_low": room.is_low_priority(),
        "unread": room.num_unread_messages(),
        // how big the room is, for the room page (its member list loads later)
        "joined_members": room.joined_members_count(),
        // the server's counts, or the ones computed here from the push rules
        // and read receipts when those are higher (the server's stayed at 0
        // for a room set to "all messages")
        "notifications": counts.notification_count.max(room.num_unread_notifications()),
        "highlights": counts.highlight_count.max(room.num_unread_mentions()),
        "ts": room.latest_event_timestamp().map(ms),
        // what a notification's reaction answers
        "latest_event_id": latest_event_id(room).map(|e| e.to_string()),
        "state": match room.state() { RoomState::Invited => "invited", RoomState::Joined => "joined", _ => "other" },
        "encrypted": room.encryption_state().is_encrypted(),
        "is_space": room.is_space(),
        "join_rule": crate::core::join_rule_name(room.join_rule().as_ref()),
        "can_join_rules": can_join_rules,
        "can_manage_children": can_children,
        // a space's rooms (its children), and the spaces a room says it is in
        "children": children,
        "parents": parents,
    })
}

/// Rooms that belong in the list: joined or invited, spaces too (marked
/// is_space; the UI keeps them apart). Left rooms are left out.
pub(crate) fn listed(room: &Room) -> bool {
    matches!(room.state(), RoomState::Joined | RoomState::Invited)
}

/// The core's order of the list: invitations first, then by the last
/// activity, the id breaking ties.
fn room_order(r: &Value) -> (bool, std::cmp::Reverse<u64>, &str) {
    (r["state"] != "invited", std::cmp::Reverse(r["ts"].as_u64().unwrap_or(0)), r["room_id"].as_str().unwrap_or(""))
}

/// Invitations first, then by the last activity; the id breaks ties, so
/// rooms without a timestamp come in the same order in every list.
/// Favourites and the tabs' own orders are the UI's (RoomLists). Borrowed
/// keys: a String per comparison was most of the sort.
pub(crate) fn sorted_rooms<'a>(rooms: impl IntoIterator<Item = &'a Value>) -> Vec<&'a Value> {
    let mut rooms: Vec<&Value> = rooms.into_iter().collect();
    rooms.sort_by(|a, b| room_order(a).cmp(&room_order(b)));
    rooms
}

/// The room list's stream as sent (protocol: `rooms`): each event has the
/// ids of all rooms in the list's order, and only the rows new or changed
/// since the stream's last event, all of them when `full`.
pub(crate) struct RoomsDiff {
    stream: u64,
    seq: u64,
    full: bool,
    order: Vec<String>,
}

impl RoomsDiff {
    pub(crate) fn new(stream: u64) -> Self {
        RoomsDiff { stream, seq: 0, full: true, order: Vec::new() }
    }

    /// The next pass sends every row (the UI asked, or a full rebuild).
    pub(crate) fn resync(&mut self) {
        self.full = true;
    }

    /// The event's fields for these rows (in the list's order,
    /// sorted_rooms), or None when no row changed and the order is the
    /// same; `changed` tells the rows made anew since the last pass that
    /// differ from what was sent.
    pub(crate) fn pass(&mut self, sorted: &[&Value], changed: impl Fn(&str) -> bool) -> Option<Map<String, Value>> {
        let full = std::mem::take(&mut self.full);
        let mut items = Map::new();
        let mut order = Vec::with_capacity(sorted.len());
        for r in sorted {
            let id = r["room_id"].as_str().unwrap_or("");
            order.push(id.to_owned());
            if full || changed(id) {
                items.insert(id.to_owned(), (*r).clone());
            }
        }
        if !full && items.is_empty() && order == self.order {
            return None;
        }
        self.order = order;
        self.seq += 1;
        let mut out = Map::new();
        out.insert("stream".into(), json!(self.stream));
        out.insert("seq".into(), json!(self.seq));
        out.insert("full".into(), json!(full));
        out.insert("order".into(), json!(self.order));
        out.insert("items".into(), Value::Object(items));
        Some(out)
    }
}

fn membership_text(change: Option<MembershipChange>) -> &'static str {
    use MembershipChange::*;
    match change {
        Some(Joined) => "joined",
        Some(Left) => "left",
        Some(Banned) => "was banned",
        Some(Unbanned) => "was unbanned",
        Some(Kicked) => "was removed",
        Some(Invited) => "was invited",
        Some(KickedAndBanned) => "was removed and banned",
        Some(InvitationAccepted) => "accepted the invitation",
        Some(InvitationRejected) => "declined the invitation",
        Some(InvitationRevoked) => "had the invitation withdrawn",
        Some(Knocked) => "asked to join",
        Some(KnockAccepted) => "was let in",
        Some(KnockRetracted) => "withdrew the request to join",
        Some(KnockDenied) => "was turned down",
        _ => "changed membership",
    }
}

fn put(o: &mut Map<String, Value>, k: &str, v: impl Into<Value>) {
    o.insert(k.to_string(), v.into());
}

fn message_json(o: &mut Map<String, Value>, msg: &matrix_sdk_ui::timeline::Message) {
    put(o, "body", msg.body());
    put(o, "edited", msg.is_edited());
    match msg.msgtype() {
        MessageType::Text(t) => {
            put(o, "type", "text");
            if let Some(f) = t.formatted.as_ref().filter(|f| f.format == MessageFormat::Html) {
                put(o, "html", f.body.as_str());
            }
        }
        MessageType::Notice(n) => {
            put(o, "type", "notice");
            if let Some(f) = n.formatted.as_ref().filter(|f| f.format == MessageFormat::Html) {
                put(o, "html", f.body.as_str());
            }
        }
        MessageType::Emote(_) => put(o, "type", "emote"),
        MessageType::Image(c) => {
            put(o, "type", "image");
            // the text sent with it, when the body is not just the file name
            put(o, "caption", c.caption());
            if let Some(i) = &c.info {
                put(o, "mime", i.mimetype.clone());
                put(o, "size", i.size.map(u64::from));
                put(o, "w", i.width.map(u64::from));
                put(o, "h", i.height.map(u64::from));
                put(o, "has_thumb", i.thumbnail_source.is_some());
            }
        }
        MessageType::File(c) => {
            put(o, "type", "file");
            // the text sent with it, when the body is not just the file name
            put(o, "caption", c.caption());
            put(o, "filename", c.filename.clone().unwrap_or_else(|| c.body.clone()));
            if let Some(i) = &c.info {
                put(o, "mime", i.mimetype.clone());
                put(o, "size", i.size.map(u64::from));
            }
        }
        MessageType::Audio(c) => {
            put(o, "type", "audio");
            // the text sent with it, when the body is not just the file name
            put(o, "caption", c.caption());
            if let Some(i) = &c.info {
                put(o, "mime", i.mimetype.clone());
                put(o, "size", i.size.map(u64::from));
                put(o, "duration", i.duration.map(|d| d.as_millis() as u64));
            }
        }
        MessageType::Video(c) => {
            put(o, "type", "video");
            // the text sent with it, when the body is not just the file name
            put(o, "caption", c.caption());
            if let Some(i) = &c.info {
                put(o, "mime", i.mimetype.clone());
                put(o, "size", i.size.map(u64::from));
                put(o, "duration", i.duration.map(|d| d.as_millis() as u64));
                put(o, "has_thumb", i.thumbnail_source.is_some());
            }
        }
        MessageType::Location(l) => {
            put(o, "type", "location");
            put(o, "geo_uri", l.geo_uri.as_str());
        }
        MessageType::VerificationRequest(_) => put(o, "type", "verification"),
        _ => put(o, "type", "text"),
    }
}

fn content_json(o: &mut Map<String, Value>, content: &TimelineItemContent, own: &UserId) {
    match content {
        TimelineItemContent::MsgLike(m) => {
            let reactions: Vec<Value> = m
                .reactions
                .iter()
                .map(|(key, senders)| {
                    json!({"key": key, "count": senders.len(), "me": senders.contains_key(own)})
                })
                .collect();
            if !reactions.is_empty() {
                put(o, "reactions", reactions);
            }
            if let Some(reply) = &m.in_reply_to {
                let mut r = Map::new();
                put(&mut r, "event_id", reply.event_id.as_str());
                match &reply.event {
                    TimelineDetails::Ready(ev) => {
                        put(&mut r, "sender", ev.sender.as_str());
                        if let TimelineDetails::Ready(p) = &ev.sender_profile {
                            put(&mut r, "sender_name", p.display_name.clone());
                        }
                        if let Some(msg) = ev.content.as_message() {
                            put(&mut r, "body", msg.body());
                            // the formatting too: custom emoji show in the quote
                            let formatted = match msg.msgtype() {
                                MessageType::Text(t) => t.formatted.as_ref(),
                                MessageType::Notice(n) => n.formatted.as_ref(),
                                _ => None,
                            };
                            if let Some(f) = formatted.filter(|f| f.format == MessageFormat::Html) {
                                put(&mut r, "html", f.body.as_str());
                            }
                        } else if let Some(st) = ev.content.as_sticker() {
                            put(&mut r, "body", st.content().body.as_str());
                        } else {
                            tracing::info!(event = %reply.event_id, "reply: the original is not a message");
                        }
                    }
                    // fetched by the timeline task; the quote says so meanwhile
                    TimelineDetails::Unavailable | TimelineDetails::Pending => put(&mut r, "pending", true),
                    TimelineDetails::Error(e) => {
                        tracing::info!(event = %reply.event_id, "reply: original not available: {e}");
                        put(&mut r, "missing", true);
                    }
                }
                put(o, "reply_to", Value::Object(r));
            }
            if let Some(root) = &m.thread_root {
                put(o, "thread_root", root.as_str());
            }
            // a thread's root: how many replies, and the latest
            if let Some(summary) = &m.thread_summary {
                let mut t = Map::new();
                put(&mut t, "count", summary.num_replies);
                if let TimelineDetails::Ready(latest) = &summary.latest_event {
                    let who = profile_name(&latest.sender_profile, &latest.sender);
                    put(&mut t, "latest_sender_name", who);
                    // for the read check (timeline.rs, thread_unread)
                    if let matrix_sdk_ui::timeline::TimelineEventItemId::EventId(id) = &latest.identifier {
                        put(&mut t, "latest_id", id.as_str());
                    }
                    put(&mut t, "latest_ts", ms(latest.timestamp));
                    put(&mut t, "latest_own", latest.sender == own);
                    if let Some(msg) = latest.content.as_message() {
                        put(&mut t, "latest_body", msg.body());
                    }
                }
                put(o, "thread", Value::Object(t));
            }
            match &m.kind {
                MsgLikeKind::Message(msg) => message_json(o, msg),
                MsgLikeKind::Sticker(s) => {
                    put(o, "type", "sticker");
                    put(o, "body", s.content().body.as_str());
                }
                MsgLikeKind::Poll(poll) => {
                    // question, answers with their votes, whether we voted, whether it ended
                    let r = poll.results();
                    put(o, "type", "poll");
                    put(o, "body", r.question.as_str());
                    let answers: Vec<Value> = r
                        .answers
                        .iter()
                        .map(|a| {
                            let voters = r.votes.get(&a.id).cloned().unwrap_or_default();
                            json!({
                                "id": a.id,
                                "text": a.text,
                                "count": voters.len(),
                                "mine": voters.iter().any(|v| v == own.as_str()),
                            })
                        })
                        .collect();
                    put(o, "answers", answers);
                    put(o, "max_selections", r.max_selections);
                    put(o, "ended", r.end_time.is_some());
                }
                MsgLikeKind::Redacted => put(o, "type", "redacted"),
                MsgLikeKind::UnableToDecrypt(_) => put(o, "type", "utd"),
                MsgLikeKind::LiveLocation(_) => {
                    put(o, "type", "location");
                    put(o, "body", "Live location");
                }
                _ => put(o, "type", "unknown"),
            }
        }
        TimelineItemContent::MembershipChange(ch) => {
            put(o, "type", "membership");
            let who = ch.display_name().unwrap_or_else(|| ch.user_id().to_string());
            put(o, "body", format!("{who} {}", membership_text(ch.change())));
        }
        TimelineItemContent::ProfileChange(p) => {
            put(o, "type", "state");
            put(o, "body", format!("{} changed their profile", p.user_id()));
        }
        TimelineItemContent::OtherState(s) => {
            put(o, "type", "state");
            put(o, "body", format!("{} changed", s.content().event_type()));
        }
        TimelineItemContent::FailedToParseMessageLike { .. }
        | TimelineItemContent::FailedToParseState { .. } => {
            put(o, "type", "unknown");
        }
        TimelineItemContent::CallInvite | TimelineItemContent::RtcNotification { .. } => {
            put(o, "type", "call");
            put(o, "body", "Call");
        }
    }
}

pub(crate) fn item_json(item: &TimelineItem, own: &UserId, names: &HashMap<OwnedUserId, String>) -> Value {
    let mut o = Map::new();
    put(&mut o, "id", item.unique_id().0.as_str());
    match item.kind() {
        TimelineItemKind::Virtual(v) => match v {
            VirtualTimelineItem::DateDivider(ts) => {
                put(&mut o, "kind", "date");
                put(&mut o, "ts", ms(*ts));
            }
            VirtualTimelineItem::ReadMarker => put(&mut o, "kind", "readmarker"),
            VirtualTimelineItem::TimelineStart => put(&mut o, "kind", "start"),
        },
        TimelineItemKind::Event(e) => {
            put(&mut o, "kind", "event");
            put(&mut o, "event_id", e.event_id().map(|id| id.to_string()));
            put(&mut o, "txn_id", e.transaction_id().map(|id| id.to_string()));
            put(&mut o, "sender", e.sender().as_str());
            if let TimelineDetails::Ready(p) = e.sender_profile() {
                put(&mut o, "sender_name", p.display_name.clone());
            }
            put(&mut o, "is_own", e.is_own());
            put(&mut o, "ts", ms(e.timestamp()));
            // who has read up to here (a receipt sits on the last event read)
            let readers: Vec<&str> = e
                .read_receipts()
                .keys()
                .filter(|u| u.as_str() != own.as_str())
                .map(|u| names.get(u).map(String::as_str).unwrap_or_else(|| u.localpart()))
                .collect();
            if !readers.is_empty() {
                put(&mut o, "read_by", readers);
            }
            put(&mut o, "editable", e.is_editable());
            // a warning on messages from unverified or unknown devices
            match e.get_shield(false) {
                TimelineEventShieldState::Red { code } => put(&mut o, "shield", format!("red:{code:?}")),
                TimelineEventShieldState::Grey { code } => put(&mut o, "shield", format!("grey:{code:?}")),
                TimelineEventShieldState::None => {}
            }
            put(&mut o, "highlight", e.is_highlighted());
            if let Some(EventSendState::NotSentYet { progress: Some(p) }) = e.send_state() {
                put(&mut o, "upload_done", p.progress.current as u64);
                put(&mut o, "upload_total", p.progress.total as u64);
            }
            // why a send failed, where it is no use trying again: the file
            // is larger than the server takes (tchncs.de's proxy answered
            // 413 to a video, and "send again" could never work)
            if let Some(EventSendState::SendingFailed { error, .. }) = e.send_state() {
                if send_error_too_large(&error.to_string()) {
                    put(&mut o, "send_error", "too_large");
                }
            }
            put(
                &mut o,
                "send_state",
                match e.send_state() {
                    Some(EventSendState::NotSentYet { .. }) => "sending",
                    Some(EventSendState::SendingFailed { .. }) => "failed",
                    _ => "sent",
                },
            );
            content_json(&mut o, e.content(), own);
            // a call membership (Element Call's): who joined or left the call,
            // from the event itself (the timeline knows it only as some state)
            if let TimelineItemContent::OtherState(st) = e.content() {
                let kind = st.content().event_type().to_string();
                if kind.ends_with("call.member") {
                    let left = e
                        .original_json()
                        .and_then(|raw| raw.get_field::<Value>("content").ok().flatten())
                        .map_or(false, |c| c.as_object().map_or(true, |m| m.is_empty()));
                    let who = profile_name(e.sender_profile(), e.sender());
                    put(&mut o, "body", format!("{who} {}", if left { "left the call" } else { "joined the call" }));
                }
            }
        }
    }
    Value::Object(o)
}

/// Whether an item's JSON names readers other than us (read_by, which
/// takes their display names from `names`).
pub(crate) fn has_other_readers(item: &TimelineItem, own: &UserId) -> bool {
    item.as_event().map_or(false, |e| e.read_receipts().keys().any(|u| u.as_str() != own.as_str()))
}

/// A thread root's latest reply, for the unread check: (root, latest, its
/// time, whether it is our own), as `content_json` puts it into `thread`.
pub(crate) fn thread_latest(item: &TimelineItem, own: &UserId) -> Option<(String, String, u64, bool)> {
    let e = item.as_event()?;
    let root = e.event_id()?.to_string();
    let summary = e.content().as_msglike()?.thread_summary.as_ref()?;
    let TimelineDetails::Ready(latest) = &summary.latest_event else { return None };
    let matrix_sdk_ui::timeline::TimelineEventItemId::EventId(id) = &latest.identifier else { return None };
    Some((root, id.to_string(), ms(latest.timestamp), latest.sender == own))
}

/// What the core puts on an item besides its own JSON: our read receipt,
/// a thread's unread mark, a state row's reactions (state_reactions.rs),
/// a reply's original fetched in an earlier open (timeline.rs).
#[derive(Clone, Debug, Default, PartialEq)]
pub(crate) struct Extras {
    pub(crate) own_read: bool,
    pub(crate) thread_unread: Option<bool>,
    pub(crate) reactions: Option<Value>,
    pub(crate) reply: Option<Value>,
}

impl Extras {
    fn put_on(&self, base: &Value) -> Value {
        let mut v = base.clone();
        if self.own_read {
            v["own_read"] = Value::Bool(true);
        }
        if let (Some(u), Some(t)) = (self.thread_unread, v.get_mut("thread")) {
            t["unread"] = Value::Bool(u);
        }
        if let Some(r) = &self.reactions {
            v["reactions"] = r.clone();
        }
        if let Some(r) = &self.reply {
            v["reply_to"] = r.clone();
        }
        v
    }
}

struct Sent<T> {
    /// the item it was made from: the SDK's items never change, a change
    /// is a new one (kept, not its address, so a freed one cannot match)
    item: Arc<T>,
    /// made with these reader names (`names_ver`) when it shows readers
    names_ver: Option<u64>,
    base: Value,
    extras: Extras,
}

/// One timeline stream's last snapshot as sent, per item id: each pass
/// sends the order of all ids and only the items that are new or changed
/// (protocol: `timeline`). An item is made into JSON again only when the
/// SDK replaced it or the reader names it shows were learned since; the
/// extras are compared on their own. `T` is the SDK's TimelineItem (any
/// type in the tests: the SDK makes its items only itself).
pub(crate) struct TimelineDiff<T> {
    stream: u64,
    seq: u64,
    full: bool,
    names_ver: u64,
    sent: HashMap<String, Sent<T>>,
    order: Vec<String>,
}

impl<T> TimelineDiff<T> {
    pub(crate) fn new(stream: u64) -> Self {
        TimelineDiff { stream, seq: 0, full: true, names_ver: 0, sent: HashMap::new(), order: Vec::new() }
    }

    /// The next pass sends every item (the UI asked, or the SDK reset the
    /// timeline).
    pub(crate) fn resync(&mut self) {
        self.full = true;
    }

    /// Reader names were learned: items showing readers are made again.
    pub(crate) fn names_changed(&mut self) {
        self.names_ver += 1;
    }

    /// The event's fields for these items, oldest first, or None when
    /// nothing changed. `make` gives an item's JSON and whether it shows
    /// reader names; `extras` what goes on top.
    pub(crate) fn pass(
        &mut self,
        items: &[(String, Arc<T>)],
        mut make: impl FnMut(&T) -> (Value, bool),
        mut extras: impl FnMut(&T, &Value) -> Extras,
    ) -> Option<Value> {
        let full = std::mem::take(&mut self.full);
        let mut changed = Map::new();
        let mut order = Vec::with_capacity(items.len());
        for (id, item) in items {
            order.push(id.clone());
            let fresh = self.sent.get(id).map_or(false, |s| {
                Arc::ptr_eq(&s.item, item) && s.names_ver.map_or(true, |v| v == self.names_ver)
            });
            if fresh {
                let s = self.sent.get_mut(id).expect("looked up");
                let x = extras(item, &s.base);
                if x != s.extras {
                    s.extras = x;
                    changed.insert(id.clone(), s.extras.put_on(&s.base));
                } else if full {
                    changed.insert(id.clone(), s.extras.put_on(&s.base));
                }
                continue;
            }
            let (base, uses_names) = make(item);
            let x = extras(item, &base);
            let names_ver = uses_names.then_some(self.names_ver);
            let same = self.sent.get(id).map_or(false, |s| s.base == base && s.extras == x);
            if !same || full {
                changed.insert(id.clone(), x.put_on(&base));
            }
            self.sent.insert(id.clone(), Sent { item: item.clone(), names_ver, base, extras: x });
        }
        if self.sent.len() != order.len() {
            let keep: std::collections::HashSet<&String> = order.iter().collect();
            self.sent.retain(|id, _| keep.contains(id));
        }
        if !full && changed.is_empty() && order == self.order {
            return None;
        }
        self.order = order;
        self.seq += 1;
        Some(json!({"stream": self.stream, "seq": self.seq, "full": full, "order": self.order, "items": changed}))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::consts::PREVIEW_CHARS;
    use matrix_sdk::ruma::user_id;

    fn ids(sorted: Value) -> Vec<String> {
        sorted.as_array().unwrap().iter().map(|r| r["room_id"].as_str().unwrap_or("?").to_owned()).collect()
    }

    fn room(id: &str, state: &str, ts: Value) -> Value {
        json!({"room_id": id, "state": state, "ts": ts})
    }

    fn in_order(rooms: Vec<Value>) -> Value {
        Value::Array(sorted_rooms(&rooms).into_iter().cloned().collect())
    }

    /// Invitations first, then the latest activity; favourites are not
    /// pinned (the UI has a tab for them).
    /// Planted-bug: drop the `Reverse` (oldest first), or compare
    /// `r["state"] == "invited"` (invitations last).
    #[test]
    fn rooms_sort_invitations_then_activity() {
        let rooms = vec![
            room("!old", "joined", json!(100)),
            room("!fav", "joined", json!(50)),
            room("!inv-old", "invited", json!(10)),
            room("!new", "joined", json!(300)),
            room("!inv-new", "invited", json!(200)),
        ];
        let mut rooms = rooms;
        rooms[1]["is_fav"] = json!(true);
        assert_eq!(ids(in_order(rooms)), ["!inv-new", "!inv-old", "!new", "!old", "!fav"]);
    }

    /// Equal times sort by id, so the list does not shuffle between two
    /// updates. The input is in reverse id order: a stable sort without the
    /// id would keep it.
    /// Planted-bug: drop the room id from the sort key.
    #[test]
    fn rooms_with_equal_times_sort_by_id() {
        let rooms = vec![room("!c", "joined", json!(5)), room("!b", "joined", json!(5)), room("!a", "joined", json!(5))];
        assert_eq!(ids(in_order(rooms)), ["!a", "!b", "!c"]);
        let rooms = vec![room("!c", "joined", Value::Null), room("!b", "joined", Value::Null), room("!a", "joined", Value::Null)];
        assert_eq!(ids(in_order(rooms)), ["!a", "!b", "!c"]);
    }

    /// A room without a usable time counts as never active (last among its
    /// kind); one without a state is not an invitation; nothing panics on
    /// odd shapes, and an empty list stays empty.
    /// Planted-bug: `r["ts"].as_u64().unwrap_or(u64::MAX)`.
    #[test]
    fn rooms_with_missing_fields_sort_last() {
        let rooms = vec![
            json!({"room_id": "!no-ts", "state": "joined"}),
            room("!str-ts", "joined", json!("12")),
            room("!neg-ts", "joined", json!(-1)),
            room("!ts", "joined", json!(1)),
            json!({"room_id": "!no-state", "ts": 2}),
            json!({"state": "invited"}),
            json!(null),
            json!("not a room"),
        ];
        let sorted = ids(in_order(rooms));
        // the invitation without an id; then by time; then all the "never"s by id
        // (the id-less non-objects sort as "" first among those)
        assert_eq!(sorted, ["?", "!no-state", "!ts", "?", "?", "!neg-ts", "!no-ts", "!str-ts"]);
        assert_eq!(in_order(vec![]), json!([]));
    }

    fn item_ids(ev: &Map<String, Value>) -> Vec<String> {
        let mut ids: Vec<String> = ev["items"].as_object().unwrap().keys().cloned().collect();
        ids.sort();
        ids
    }

    /// The first pass sends every row, marked full, in the list's order; a
    /// pass with nothing changed sends nothing; a changed row goes out
    /// alone, with the whole order.
    /// Planted-bug: put every row into `items` on a pass that is not full.
    #[test]
    fn rooms_diff_sends_only_changed_rows() {
        let mut d = RoomsDiff::new(4);
        let mut rows = vec![room("!a", "joined", json!(1)), room("!b", "joined", json!(2))];
        let first = d.pass(&sorted_rooms(&rows), |_| false).unwrap();
        assert_eq!(first["full"], json!(true));
        assert_eq!(first["stream"], json!(4));
        assert_eq!(first["seq"], json!(1));
        assert_eq!(first["order"], json!(["!b", "!a"]));
        assert_eq!(item_ids(&first), ["!a", "!b"]);
        assert_eq!(d.pass(&sorted_rooms(&rows), |_| false), None);
        rows[0]["ts"] = json!(3);
        let next = d.pass(&sorted_rooms(&rows), |id| id == "!a").unwrap();
        assert_eq!(next["full"], json!(false));
        assert_eq!(next["seq"], json!(2));
        assert_eq!(next["order"], json!(["!a", "!b"]));
        assert_eq!(item_ids(&next), ["!a"]);
        assert_eq!(next["items"]["!a"]["ts"], json!(3));
    }

    /// A room that went is told by the order alone; a resync sends every
    /// row again, even when nothing changed.
    /// Planted-bug: return None when no row changed (a left room stays in
    /// the UI's list).
    #[test]
    fn rooms_diff_removal_and_resync() {
        let mut d = RoomsDiff::new(1);
        let rows = vec![room("!a", "joined", json!(1)), room("!b", "joined", json!(2))];
        d.pass(&sorted_rooms(&rows), |_| false);
        let gone = d.pass(&sorted_rooms(&rows[..1]), |_| false).unwrap();
        assert_eq!(gone["order"], json!(["!a"]));
        assert!(item_ids(&gone).is_empty());
        d.resync();
        let again = d.pass(&sorted_rooms(&rows[..1]), |_| false).unwrap();
        assert_eq!(again["full"], json!(true));
        assert_eq!(again["seq"], json!(3));
        assert_eq!(item_ids(&again), ["!a"]);
        assert_eq!(d.pass(&sorted_rooms(&rows[..1]), |_| false), None);
    }

    fn msg(v: Value) -> MessageType {
        serde_json::from_value(v).unwrap()
    }

    /// The room list's line for a message: the text trimmed, emotes marked,
    /// media by kind, a file by name.
    /// Planted-bug: drop the `trim()` in `message_preview`, or the "* " of
    /// emotes.
    #[test]
    fn message_previews() {
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.text", "body": "  hi\nthere \n"}))), "hi\nthere");
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.notice", "body": "bot says"}))), "bot says");
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.emote", "body": " waves "}))), "* waves");
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.image", "body": "a.png", "url": "mxc://s/i"}))), "Photo");
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.video", "body": "v", "url": "mxc://s/v"}))), "Video");
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.audio", "body": "a", "url": "mxc://s/a"}))), "Audio");
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.location", "body": "here", "geo_uri": "geo:1,2"}))), "Location");
        assert_eq!(
            message_preview(&msg(json!({"msgtype": "m.file", "body": "see this", "filename": "r.pdf", "url": "mxc://s/f"}))),
            "File: r.pdf"
        );
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.file", "body": "r.pdf", "url": "mxc://s/f"}))), "File: r.pdf");
        assert_eq!(message_preview(&msg(json!({"msgtype": "org.example.custom", "body": " own kind "}))), "own kind");
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.text", "body": ""}))), "");
    }

    /// Long texts are cut at PREVIEW_CHARS characters, on a character
    /// boundary (multi-byte text must not panic or lose a character).
    /// Planted-bug: `nth(PREVIEW_CHARS - 1)`, or cut by bytes (`t[..PREVIEW_CHARS]`).
    #[test]
    fn long_previews_are_cut_at_characters() {
        let long: String = "ä😀".repeat(PREVIEW_CHARS);
        let got = message_preview(&msg(json!({"msgtype": "m.text", "body": long})));
        assert_eq!(got.chars().count(), PREVIEW_CHARS);
        assert!(long.starts_with(&got));
        let exact = "x".repeat(PREVIEW_CHARS);
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.text", "body": exact.clone()}))), exact);
        // the cut comes after the trim: leading blanks do not eat the budget
        let padded = format!("{}{}", " ".repeat(50), exact);
        assert_eq!(message_preview(&msg(json!({"msgtype": "m.text", "body": padded}))), exact);
    }

    fn event(v: Value) -> AnySyncTimelineEvent {
        serde_json::from_value(v).unwrap()
    }

    /// The other events the room list names.
    /// Planted-bug: map `RoomEncrypted` to None.
    #[test]
    fn event_previews() {
        let base = |kind: &str, content: Value, state: bool| {
            let mut e = json!({"type": kind, "content": content, "event_id": "$e", "sender": "@a:s", "origin_server_ts": 1});
            if state {
                e["state_key"] = json!("");
            }
            event(e)
        };
        let text = base("m.room.message", json!({"msgtype": "m.text", "body": "hello"}), false);
        assert_eq!(event_preview(&text).as_deref(), Some("hello"));
        let encrypted = base(
            "m.room.encrypted",
            json!({"algorithm": "m.megolm.v1.aes-sha2", "ciphertext": "x", "session_id": "s", "device_id": "D", "sender_key": "k"}),
            false,
        );
        assert_eq!(event_preview(&encrypted).as_deref(), Some("Encrypted message"));
        let topic = base("m.room.topic", json!({"topic": "t"}), true);
        assert_eq!(event_preview(&topic).as_deref(), Some("Topic changed"));
        let name = base("m.room.name", json!({"name": "n"}), true);
        assert_eq!(event_preview(&name).as_deref(), Some("Room renamed"));
        let reaction = base("m.reaction", json!({"m.relates_to": {"rel_type": "m.annotation", "event_id": "$x", "key": "👍"}}), false);
        assert_eq!(event_preview(&reaction), None);
        // a redacted message has no text to show
        let redacted = event(json!({"type": "m.room.message", "content": {}, "event_id": "$e", "sender": "@a:s",
            "origin_server_ts": 1, "unsigned": {"redacted_because": {"type": "m.room.redaction", "content": {},
            "redacts": "$e", "event_id": "$r", "sender": "@a:s", "origin_server_ts": 2}}}));
        assert_eq!(event_preview(&redacted), None);
    }

    /// The timeline's membership lines, each change its own words.
    /// Planted-bug: swap "was removed" and "was banned".
    #[test]
    fn membership_texts() {
        use MembershipChange::*;
        let cases = [
            (Joined, "joined"), (Left, "left"), (Banned, "was banned"), (Unbanned, "was unbanned"),
            (Kicked, "was removed"), (Invited, "was invited"), (KickedAndBanned, "was removed and banned"),
            (InvitationAccepted, "accepted the invitation"), (InvitationRejected, "declined the invitation"),
            (InvitationRevoked, "had the invitation withdrawn"), (Knocked, "asked to join"),
            (KnockAccepted, "was let in"), (KnockRetracted, "withdrew the request to join"),
            (KnockDenied, "was turned down"),
        ];
        for (change, text) in cases {
            assert_eq!(membership_text(Some(change)), text, "{change:?}");
        }
        // (the glob import shadows Option's None with the change "None")
        assert_eq!(membership_text(Option::None), "changed membership");
        assert_eq!(membership_text(Some(NotImplemented)), "changed membership");
    }

    /// A sender's name: the display name when the profile is there, else
    /// the localpart (never the whole id, never empty).
    /// Planted-bug: fall back to `user.to_string()`.
    #[test]
    fn profile_names() {
        let user = user_id!("@anna:example.org");
        let named = Profile { display_name: Some("Anna K.".to_owned()), ..Default::default() };
        assert_eq!(profile_name(&TimelineDetails::Ready(named), user), "Anna K.");
        assert_eq!(profile_name(&TimelineDetails::Ready(Profile::default()), user), "anna");
        assert_eq!(profile_name(&TimelineDetails::Pending, user), "anna");
        assert_eq!(profile_name(&TimelineDetails::Unavailable, user), "anna");
    }
}

/// A server's (or its proxy's) "too large" for an upload or an event.
pub(crate) fn send_error_too_large(error: &str) -> bool {
    error.contains("[413")
        || error.contains("M_TOO_LARGE")
        || error.contains("Request Entity Too Large")
        || error.contains("Payload Too Large")
}

#[cfg(test)]
mod send_error_tests {
    use super::send_error_too_large;

    #[test]
    fn a_413_is_too_large_and_a_timeout_is_not() {
        assert!(send_error_too_large("the server returned an error: [413] <non-json bytes>"));
        assert!(send_error_too_large("[400 / M_TOO_LARGE] too big"));
        assert!(!send_error_too_large("error sending request: operation timed out"));
    }
}

#[cfg(test)]
mod timeline_diff_tests {
    use super::*;

    /// A stand-in for the SDK's items: its JSON is the string, and "@" in
    /// it means it shows reader names (made with the current `names`).
    type Item = Arc<String>;

    fn item(s: &str) -> Item {
        Arc::new(s.to_owned())
    }

    struct Run {
        diff: TimelineDiff<String>,
        made: usize,
        names: String,
        own_read: String,
    }

    impl Run {
        fn new() -> Self {
            Run { diff: TimelineDiff::new(7), made: 0, names: "anna".into(), own_read: String::new() }
        }

        fn pass(&mut self, items: &[(&str, &Item)]) -> Option<Value> {
            let list: Vec<(String, Item)> = items.iter().map(|(id, i)| (id.to_string(), (*i).clone())).collect();
            let (made, names, own_read) = (&mut self.made, &self.names, &self.own_read);
            self.diff.pass(
                &list,
                |s| {
                    *made += 1;
                    let uses = s.contains('@');
                    let body = if uses { s.replace('@', names) } else { s.clone() };
                    (json!({ "body": body }), uses)
                },
                |s, _| Extras { own_read: *s == *own_read, ..Default::default() },
            )
        }
    }

    fn changed(v: &Value) -> Vec<String> {
        let mut ids: Vec<String> = v["items"].as_object().unwrap().keys().cloned().collect();
        ids.sort();
        ids
    }

    /// The first pass sends everything, marked full; the same items again
    /// send nothing and are not made again.
    /// Planted-bug: compare by value instead of `Arc::ptr_eq` (every pass
    /// makes all items again: `made` grows).
    #[test]
    fn the_first_pass_is_full_and_an_unchanged_one_is_none() {
        let mut r = Run::new();
        let (a, b) = (item("a"), item("b"));
        let first = r.pass(&[("1", &a), ("2", &b)]).unwrap();
        assert_eq!(first["full"], json!(true));
        assert_eq!(first["stream"], json!(7));
        assert_eq!(first["seq"], json!(1));
        assert_eq!(first["order"], json!(["1", "2"]));
        assert_eq!(first["items"]["2"], json!({"body": "b"}));
        assert_eq!(r.made, 2);
        assert_eq!(r.pass(&[("1", &a), ("2", &b)]), None);
        assert_eq!(r.made, 2);
    }

    /// A replaced item goes out alone, with the whole order; an item the
    /// SDK replaced with the same JSON does not go out.
    /// Planted-bug: send every item on a change (`changed` has both).
    #[test]
    fn only_changed_items_go_out() {
        let mut r = Run::new();
        let (a, b) = (item("a"), item("b"));
        r.pass(&[("1", &a), ("2", &b)]);
        let b2 = item("b edited");
        let next = r.pass(&[("1", &a), ("2", &b2)]).unwrap();
        assert_eq!(next["full"], json!(false));
        assert_eq!(next["seq"], json!(2));
        assert_eq!(next["order"], json!(["1", "2"]));
        assert_eq!(changed(&next), ["2"]);
        assert_eq!(next["items"]["2"], json!({"body": "b edited"}));
        let same = item("b edited");
        assert_eq!(r.pass(&[("1", &a), ("2", &same)]), None);
    }

    /// New items, a page of older ones, and removals: the order says it,
    /// the items carry only the new ones.
    /// Planted-bug: return None when no item changed (a removal never
    /// reaches the UI).
    #[test]
    fn order_changes_go_out_without_items() {
        let mut r = Run::new();
        let (a, b, c, old) = (item("a"), item("b"), item("c"), item("old"));
        r.pass(&[("1", &a), ("2", &b)]);
        let grown = r.pass(&[("0", &old), ("1", &a), ("2", &b), ("3", &c)]).unwrap();
        assert_eq!(grown["order"], json!(["0", "1", "2", "3"]));
        assert_eq!(changed(&grown), ["0", "3"]);
        let removed = r.pass(&[("0", &old), ("2", &b), ("3", &c)]).unwrap();
        assert_eq!(removed["order"], json!(["0", "2", "3"]));
        assert!(changed(&removed).is_empty());
        // a removed id coming back is new again
        let back = r.pass(&[("0", &old), ("1", &a), ("2", &b), ("3", &c)]).unwrap();
        assert_eq!(changed(&back), ["1"]);
    }

    /// Our read receipt moving changes the two rows it moves between, not
    /// their JSON.
    /// Planted-bug: compare only the base JSON (the move is never sent).
    #[test]
    fn extras_alone_change_a_row() {
        let mut r = Run::new();
        let (a, b) = (item("a"), item("b"));
        r.own_read = "a".into();
        assert_eq!(r.pass(&[("1", &a), ("2", &b)]).unwrap()["items"]["1"], json!({"body": "a", "own_read": true}));
        r.own_read = "b".into();
        let moved = r.pass(&[("1", &a), ("2", &b)]).unwrap();
        assert_eq!(changed(&moved), ["1", "2"]);
        assert_eq!(moved["items"]["1"], json!({"body": "a"}));
        assert_eq!(moved["items"]["2"], json!({"body": "b", "own_read": true}));
        assert_eq!(r.made, 2);
    }

    /// Names learned after the first pass: the items showing readers are
    /// made again, the others not.
    /// Planted-bug: drop the `names_ver` check (the localparts stay).
    #[test]
    fn learned_names_remake_reader_rows_only() {
        let mut r = Run::new();
        let (a, b) = (item("read by @"), item("b"));
        r.pass(&[("1", &a), ("2", &b)]);
        assert_eq!(r.made, 2);
        r.names = "Anna K.".into();
        r.diff.names_changed();
        let named = r.pass(&[("1", &a), ("2", &b)]).unwrap();
        assert_eq!(changed(&named), ["1"]);
        assert_eq!(named["items"]["1"], json!({"body": "read by Anna K."}));
        assert_eq!(r.made, 3);
        assert_eq!(r.pass(&[("1", &a), ("2", &b)]), None);
    }

    /// A resync sends every item again, full, without making them anew.
    /// Planted-bug: forget to clear `full` after a pass (every pass full).
    #[test]
    fn a_resync_sends_everything_once() {
        let mut r = Run::new();
        let (a, b) = (item("a"), item("b"));
        r.pass(&[("1", &a), ("2", &b)]);
        r.diff.resync();
        let again = r.pass(&[("1", &a), ("2", &b)]).unwrap();
        assert_eq!(again["full"], json!(true));
        assert_eq!(changed(&again), ["1", "2"]);
        assert_eq!(r.made, 2);
        assert_eq!(r.pass(&[("1", &a), ("2", &b)]), None);
    }

    /// The extras land where the UI reads them.
    #[test]
    fn extras_are_put_on() {
        let base = json!({"body": "x", "thread": {"count": 2}, "reply_to": {"event_id": "$o", "pending": true}});
        let x = Extras { own_read: true, thread_unread: Some(true), reactions: Some(json!([{"key": "👋"}])),
                         reply: Some(json!({"event_id": "$o", "body": "hi"})) };
        assert_eq!(x.put_on(&base), json!({"body": "x", "own_read": true, "thread": {"count": 2, "unread": true},
                                            "reactions": [{"key": "👋"}], "reply_to": {"event_id": "$o", "body": "hi"}}));
        // no thread: no unread mark of its own
        let plain = Extras { thread_unread: Some(false), ..Default::default() }.put_on(&json!({"body": "x"}));
        assert_eq!(plain, json!({"body": "x"}));
    }
}
