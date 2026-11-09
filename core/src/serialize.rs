// SPDX-License-Identifier: GPL-3.0-or-later
//! JSON shapes handed to the UI: the room list and timeline items.

use std::sync::Arc;

use matrix_sdk::ruma::events::room::message::{MessageFormat, MessageType};
use matrix_sdk::ruma::UserId;
use matrix_sdk::{Client, Room, RoomState};
use matrix_sdk_ui::timeline::{
    EventSendState, MembershipChange, MsgLikeKind, TimelineDetails, TimelineItem, TimelineItemContent,
    TimelineItemKind, VirtualTimelineItem,
};
use serde_json::{json, Map, Value};

fn ms(t: matrix_sdk::ruma::MilliSecondsSinceUnixEpoch) -> u64 {
    u64::from(t.0)
}

pub(crate) async fn room_json(room: &Room) -> Value {
    let name = match room.cached_display_name() {
        Some(n) => n.to_string(),
        None => match room.display_name().await {
            Ok(n) => n.to_string(),
            Err(_) => room.room_id().to_string(),
        },
    };
    let counts = room.unread_notification_counts();
    json!({
        "id": room.room_id().as_str(),
        "name": name,
        "topic": room.topic(),
        "alias": room.canonical_alias().map(|a| a.to_string()),
        "is_dm": room.is_direct().await.unwrap_or(false),
        "is_fav": room.is_favourite(),
        "is_low": room.is_low_priority(),
        "unread": room.num_unread_messages(),
        "notifications": counts.notification_count,
        "highlights": counts.highlight_count,
        "ts": room.latest_event_timestamp().map(ms),
        "state": match room.state() { RoomState::Invited => "invited", RoomState::Joined => "joined", _ => "other" },
        "encrypted": room.encryption_state().is_encrypted(),
    })
}

/// Joined rooms and invitations, invitations first, then favourites, then
/// by activity. Spaces and left rooms are left out.
pub(crate) async fn rooms_json(client: &Client) -> Value {
    let mut rooms = Vec::new();
    for room in client.rooms() {
        if !matches!(room.state(), RoomState::Joined | RoomState::Invited) || room.is_space() {
            continue;
        }
        rooms.push(room_json(&room).await);
    }
    rooms.sort_by(|a, b| {
        let key = |r: &Value| {
            (
                r["state"] != "invited",
                !r["is_fav"].as_bool().unwrap_or(false),
                std::cmp::Reverse(r["ts"].as_u64().unwrap_or(0)),
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
            put(o, "filename", c.filename.clone().unwrap_or_else(|| c.body.clone()));
            if let Some(i) = &c.info {
                put(o, "mime", i.mimetype.clone());
                put(o, "size", i.size.map(u64::from));
            }
        }
        MessageType::Audio(c) => {
            put(o, "type", "audio");
            if let Some(i) = &c.info {
                put(o, "mime", i.mimetype.clone());
                put(o, "size", i.size.map(u64::from));
                put(o, "duration", i.duration.map(|d| d.as_millis() as u64));
            }
        }
        MessageType::Video(c) => {
            put(o, "type", "video");
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
                MsgLikeKind::Poll(_) => {
                    put(o, "type", "poll");
                    put(o, "body", "Poll");
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

pub(crate) fn item_json(item: &Arc<TimelineItem>, own: &UserId) -> Value {
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
            put(&mut o, "editable", e.is_editable());
            put(&mut o, "highlight", e.is_highlighted());
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

pub(crate) fn items_json<'a>(items: impl IntoIterator<Item = &'a Arc<TimelineItem>>, own: &UserId) -> Value {
    Value::Array(items.into_iter().map(|i| item_json(i, own)).collect())
}
