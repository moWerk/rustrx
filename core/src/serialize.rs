// SPDX-License-Identifier: GPL-3.0-or-later
//! JSON shapes handed to the UI: the room list and timeline items.

use std::collections::HashMap;
use std::sync::Arc;

use matrix_sdk::latest_events::LatestEventValue;
use matrix_sdk::notification_settings::RoomNotificationMode;
use matrix_sdk::ruma::events::receipt::ReceiptThread;
use matrix_sdk::ruma::events::room::message::{MessageFormat, MessageType};
use matrix_sdk::ruma::events::{AnyMessageLikeEventContent, AnySyncTimelineEvent, StateEventType};
use matrix_sdk::ruma::{OwnedUserId, UserId};
use matrix_sdk::{Room, RoomState};
use matrix_sdk_ui::timeline::{
    EventSendState, MembershipChange, TimelineEventShieldState, MsgLikeKind, TimelineDetails, TimelineItem, TimelineItemContent,
    TimelineItemKind, VirtualTimelineItem,
};
use serde_json::{json, Map, Value};

fn ms(t: matrix_sdk::ruma::MilliSecondsSinceUnixEpoch) -> u64 {
    u64::from(t.0)
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
async fn preview(room: &Room) -> (Option<String>, Option<String>) {
    let value = room.latest_event();
    let (text, sender) = match &value {
        LatestEventValue::None => return (None, None),
        LatestEventValue::RemoteInvite { .. } => (Some("Invitation".to_owned()), None),
        LatestEventValue::Remote(ev) => match ev.raw().deserialize() {
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
    if room.is_direct().await.unwrap_or(false) {
        return (Some(text), None);
    }
    let who = match sender {
        Some(user) if user == room.own_user_id() => Some("You".to_owned()),
        Some(user) => Some(match room.get_member_no_sync(&user).await {
            Ok(Some(m)) => m.name().to_owned(),
            _ => user.localpart().to_owned(),
        }),
        None => None,
    };
    (Some(text), who)
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
    // the text, line breaks kept, up to 400 characters: the list shows as
    // many lines as the user set (1 to 3) and fades the last
    let lines = |s: &str| {
        let t = s.trim();
        match t.char_indices().nth(400) {
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

pub(crate) async fn room_json(room: &Room, avatar_dir: &std::path::Path) -> Value {
    let name = match room.cached_display_name() {
        Some(n) => n.to_string(),
        None => match room.display_name().await {
            Ok(n) => n.to_string(),
            Err(_) => room.room_id().to_string(),
        },
    };
    let counts = room.unread_notification_counts();
    let avatar = avatar_file(avatar_dir, room);
    let (preview, preview_sender) = preview(room).await;
    // muted: the room's notification mode
    let muted = matches!(room.notification_mode().await, Some(RoomNotificationMode::Mute));
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
    // who sent an invitation: their name, else their id
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
                    let v: Value = serde_json::from_str(raw.json().get()).ok()?;
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
    let inviter = if room.state() == RoomState::Invited {
        room.invite_details().await.ok().map(|i| match i.inviter {
            Some(m) => m.name().to_owned(),
            None => i.inviter_id.to_string(),
        })
    } else {
        None
    };
    json!({
        "inviter": inviter,
        "muted": muted,
        "own_read": own_read,
        "replaced_by": replaced_by,
        "has_avatar": avatar.exists(),
        "avatar": if avatar.exists() { Some(avatar.to_string_lossy().into_owned()) } else { None },
        // the file's age: the picture's URL changes with it, past Qt's image cache
        "avatar_v": std::fs::metadata(&avatar).and_then(|m| m.modified()).ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map(|d| d.as_secs()),
        "room_id": room.room_id().as_str(),
        "name": name,
        "preview": preview,
        "preview_sender": preview_sender,
        "topic": room.topic(),
        "alias": room.canonical_alias().map(|a| a.to_string()),
        "is_dm": room.is_direct().await.unwrap_or(false),
        // a call is running here (someone is in it, from the room's state)
        "in_call": room.has_active_room_call(),
        "is_fav": room.is_favourite(),
        "is_low": room.is_low_priority(),
        "unread": room.num_unread_messages(),
        // the server's counts, or the ones computed here from the push rules
        // and read receipts when those are higher (the server's stayed at 0
        // for a room set to "all messages")
        "notifications": counts.notification_count.max(room.num_unread_notifications()),
        "highlights": counts.highlight_count.max(room.num_unread_mentions()),
        "ts": room.latest_event_timestamp().map(ms),
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

/// Joined rooms and invitations, invitations first, then by activity.
/// Favourites are not pinned here; the UI has its own tab for them.
/// Spaces and left rooms are left out.
/// Rooms that belong in the list: joined or invited, no spaces.
pub(crate) fn listed(room: &Room) -> bool {
    // spaces too, marked is_space: the UI keeps them apart
    matches!(room.state(), RoomState::Joined | RoomState::Invited)
}

/// Invitations first, then by the last activity; the id breaks ties, so
/// rooms without a timestamp keep their order.
pub(crate) fn sorted_rooms(mut rooms: Vec<Value>) -> Value {
    rooms.sort_by(|a, b| {
        let key = |r: &Value| {
            (
                r["state"] != "invited",
                std::cmp::Reverse(r["ts"].as_u64().unwrap_or(0)),
                r["room_id"].as_str().unwrap_or("").to_owned(),
            )
        };
        key(a).cmp(&key(b))
    });
    Value::Array(rooms)
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
                    let who = match &latest.sender_profile {
                        TimelineDetails::Ready(p) => p.display_name.clone().unwrap_or_else(|| latest.sender.localpart().to_owned()),
                        _ => latest.sender.localpart().to_owned(),
                    };
                    put(&mut t, "latest_sender", who);
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

pub(crate) fn item_json(item: &Arc<TimelineItem>, own: &UserId, names: &HashMap<OwnedUserId, String>) -> Value {
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
                    let who = match e.sender_profile() {
                        TimelineDetails::Ready(p) => p.display_name.clone().unwrap_or_else(|| e.sender().localpart().to_owned()),
                        _ => e.sender().localpart().to_owned(),
                    };
                    put(&mut o, "body", format!("{who} {}", if left { "left the call" } else { "joined the call" }));
                }
            }
        }
    }
    Value::Object(o)
}

pub(crate) fn items_json<'a>(
    items: impl IntoIterator<Item = &'a Arc<TimelineItem>>,
    own: &UserId,
    names: &HashMap<OwnedUserId, String>,
) -> Value {
    Value::Array(items.into_iter().map(|i| item_json(i, own, names)).collect())
}
