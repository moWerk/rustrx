// SPDX-License-Identifier: GPL-3.0-or-later
//! JSON shapes handed to the UI: the room list and timeline items.

use std::collections::HashMap;
use std::sync::Arc;

use matrix_sdk::latest_events::LatestEventValue;
use matrix_sdk::ruma::events::room::message::{MessageFormat, MessageType};
use matrix_sdk::ruma::events::{AnyMessageLikeEventContent, AnySyncTimelineEvent};
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
    let first_line = |s: &str| s.lines().find(|l| !l.trim().is_empty()).unwrap_or("").trim().to_owned();
    match msg {
        MessageType::Text(t) => first_line(&t.body),
        MessageType::Notice(t) => first_line(&t.body),
        MessageType::Emote(t) => format!("* {}", first_line(&t.body)),
        MessageType::Image(_) => "Photo".to_owned(),
        MessageType::Video(_) => "Video".to_owned(),
        MessageType::Audio(_) => "Audio".to_owned(),
        MessageType::File(f) => format!("File: {}", f.filename.as_deref().unwrap_or(&f.body)),
        MessageType::Location(_) => "Location".to_owned(),
        other => first_line(other.body()),
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
    json!({
        "has_avatar": avatar.exists(),
        "avatar": if avatar.exists() { Some(avatar.to_string_lossy().into_owned()) } else { None },
        "room_id": room.room_id().as_str(),
        "name": name,
        "preview": preview,
        "preview_sender": preview_sender,
        "topic": room.topic(),
        "alias": room.canonical_alias().map(|a| a.to_string()),
        "is_dm": room.is_direct().await.unwrap_or(false),
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
    })
}

/// Joined rooms and invitations, invitations first, then by activity.
/// Favourites are not pinned here; the UI has its own tab for them.
/// Spaces and left rooms are left out.
/// Rooms that belong in the list: joined or invited, no spaces.
pub(crate) fn listed(room: &Room) -> bool {
    matches!(room.state(), RoomState::Joined | RoomState::Invited) && !room.is_space()
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
                if let TimelineDetails::Ready(ev) = &reply.event {
                    put(&mut r, "sender", ev.sender.as_str());
                    if let TimelineDetails::Ready(p) = &ev.sender_profile {
                        put(&mut r, "sender_name", p.display_name.clone());
                    }
                    if let Some(msg) = ev.content.as_message() {
                        put(&mut r, "body", msg.body());
                    }
                }
                put(o, "reply_to", Value::Object(r));
            }
            if let Some(root) = &m.thread_root {
                put(o, "thread_root", root.as_str());
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
