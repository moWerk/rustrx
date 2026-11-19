// SPDX-License-Identifier: GPL-3.0-or-later
//! What owners, admins and moderators may do in a room, and doing it: one
//! place that reads the room's power levels for the UI ("room_settings"),
//! and the commands that change roles, permissions, history, guests,
//! encryption, the directory, addresses, the server access list, pins and
//! the room's version. Everything the UI offers is what the flags here
//! allow; the server still has the last word.

use super::*;
use matrix_sdk::ruma::api::client::reporting::report_user;
use matrix_sdk::ruma::api::client::room::upgrade_room;
use matrix_sdk::ruma::events::room::guest_access::{GuestAccess, RoomGuestAccessEventContent};
use matrix_sdk::ruma::events::room::history_visibility::HistoryVisibility;
use matrix_sdk::ruma::events::room::power_levels::{RoomPowerLevels, RoomPowerLevelsEventContent, UserPowerLevel};
use matrix_sdk::ruma::events::room::server_acl::RoomServerAclEventContent;
use matrix_sdk::ruma::events::{StateEventType, TimelineEventType};
use matrix_sdk::ruma::api::client::room::Visibility;
use matrix_sdk::ruma::events::space::child::SpaceChildEventContent;
use matrix_sdk::ruma::{Int, OwnedEventId, OwnedRoomAliasId, OwnedRoomId, RoomAliasId, RoomVersionId, SpaceChildOrder};

/// The levels a role stands for: what the UI offers when changing one.
pub(crate) const LEVEL_MODERATOR: i64 = 50;
pub(crate) const LEVEL_ADMIN: i64 = 100;

/// A level as the UI reads it: a number, or "infinite" for a creator of a
/// room version 12 room (who cannot be demoted).
fn level_json(p: UserPowerLevel) -> Value {
    match p {
        UserPowerLevel::Infinite => Value::from("infinite"),
        UserPowerLevel::Int(i) => Value::from(i64::from(i)),
        _ => Value::Null,
    }
}

/// A role's name from a level: what the members list and a person's page say.
pub(crate) fn role_of(p: UserPowerLevel) -> &'static str {
    match p {
        UserPowerLevel::Infinite => "creator",
        UserPowerLevel::Int(i) if i64::from(i) >= LEVEL_ADMIN => "admin",
        UserPowerLevel::Int(i) if i64::from(i) >= LEVEL_MODERATOR => "moderator",
        _ => "member",
    }
}

fn int(level: i64) -> Result<Int> {
    Int::new(level).ok_or_else(|| anyhow!("level out of range"))
}

/// The event types whose levels the Roles & permissions page shows.
const LEVEL_EVENTS: &[&str] = &[
    "m.room.name", "m.room.topic", "m.room.avatar", "m.room.pinned_events", "m.room.power_levels",
    "m.room.history_visibility", "m.room.canonical_alias", "m.room.server_acl", "m.room.encryption",
    "m.room.tombstone", "m.room.join_rules", "m.space.child", "m.reaction",
];

/// A server name matches an access list's entry (`*` and `?` as globs).
pub(crate) fn acl_matches(pattern: &str, server: &str) -> bool {
    fn go(p: &[u8], s: &[u8]) -> bool {
        match (p.first(), s.first()) {
            (None, None) => true,
            (Some(b'*'), _) => go(&p[1..], s) || (!s.is_empty() && go(p, &s[1..])),
            (Some(b'?'), Some(_)) => go(&p[1..], &s[1..]),
            (Some(a), Some(b)) if a.eq_ignore_ascii_case(b) => go(&p[1..], &s[1..]),
            _ => false,
        }
    }
    go(pattern.as_bytes(), server.as_bytes())
}

/// A server name as access lists see it: without its port ("[::1]:8448"
/// is "[::1]"). Matched with the port, a homeserver whose name has one
/// was refused a valid list.
pub(crate) fn acl_host(server: &str) -> &str {
    if server.starts_with('[') {
        return server.find(']').map_or(server, |e| &server[..=e]);
    }
    server.rsplit_once(':').map_or(server, |(host, _)| host)
}

/// A room state event this device sent, laid over the room's state until
/// the sync shows that state moved on. The SDK builds a change on the
/// room's local state, which the sync updates only later: two quick
/// changes reverted each other, and the reply showed the old value.
#[derive(Debug)]
pub(crate) struct Sent<T> {
    value: T,
    /// the room's state events under which `value` still holds: the one
    /// before the first write, and each of ours sent since but the last
    stale: Vec<Option<OwnedEventId>>,
    /// our last event: once the room's state is it, the room has caught up
    last: OwnedEventId,
    at: std::time::Instant,
}

impl<T: Clone> Sent<T> {
    /// The value to build on and to show while the room's state is still
    /// at `local` (its event id) from before our last write; None once
    /// the room caught up or moved on (another's change), or after
    /// SENT_OVERLAY_MAX, and the slot is cleared.
    pub(crate) fn current(slot: &mut Option<Sent<T>>, local: Option<&EventId>) -> Option<T> {
        let holds = slot.as_ref().is_some_and(|s| {
            s.at.elapsed() < SENT_OVERLAY_MAX && s.stale.iter().any(|id| id.as_deref() == local)
        });
        if !holds {
            *slot = None;
        }
        slot.as_ref().map(|s| s.value.clone())
    }

    /// After a write that `current` was asked for under the same lock:
    /// `local` the room's state event then, `sent` ours.
    pub(crate) fn record(slot: &mut Option<Sent<T>>, local: Option<OwnedEventId>, sent: OwnedEventId, value: T) {
        let stale = match slot.take() {
            Some(s) => {
                let mut stale = s.stale;
                stale.push(Some(s.last));
                stale
            }
            None => vec![local],
        };
        *slot = Some(Sent { value, stale, last: sent, at: std::time::Instant::now() });
    }
}

/// What this device last sent to one room's settings (State::room_writes);
/// its lock serializes the changes to that room.
#[derive(Default)]
pub(crate) struct RoomWrites {
    power_levels: Option<Sent<RoomPowerLevels>>,
    history: Option<Sent<HistoryVisibility>>,
    guests: Option<Sent<bool>>,
    acl: Option<Sent<RoomServerAclEventContent>>,
    aliases: Option<Sent<(Option<OwnedRoomAliasId>, Vec<OwnedRoomAliasId>)>>,
    pinned: Option<Sent<Vec<OwnedEventId>>>,
}

/// One level of the room's permissions: a field ("invite", "ban",
/// "users_default", "notify_room", ...) or an event type ("m.room.name").
fn set_permission(levels: &mut RoomPowerLevels, key: &str, value: Int) -> Result<()> {
    match key {
        "users_default" => levels.users_default = value,
        "events_default" => levels.events_default = value,
        "state_default" => levels.state_default = value,
        "invite" => levels.invite = value,
        "kick" => levels.kick = value,
        "ban" => levels.ban = value,
        "redact" => levels.redact = value,
        "notify_room" => levels.notifications.room = value,
        t if LEVEL_EVENTS.contains(&t) => {
            levels.events.insert(TimelineEventType::from(t), value);
        }
        other => bail!("no such permission {other}"),
    }
    Ok(())
}

/// An event pinned (at the end) or unpinned; false when nothing changes.
fn set_pinned(list: &mut Vec<OwnedEventId>, event: &EventId, pinned: bool) -> bool {
    let at = list.iter().position(|e| e == event);
    match (pinned, at) {
        (true, None) => list.push(event.to_owned()),
        (false, Some(i)) => {
            list.remove(i);
        }
        _ => return false,
    }
    true
}

/// The id of the room's current state event of this type (empty key).
async fn state_event_id(room: &Room, kind: StateEventType) -> Option<OwnedEventId> {
    match room.get_state_event(kind, "").await.ok()?? {
        matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState::Sync(raw) => {
            raw_value(&raw).get("event_id").and_then(Value::as_str).and_then(|e| EventId::parse(e).ok())
        }
        _ => None,
    }
}

/// Whether a server may take part under an access list.
pub(crate) fn acl_allows(allow: &[String], deny: &[String], server: &str) -> bool {
    allow.iter().any(|p| acl_matches(p, server)) && !deny.iter().any(|p| acl_matches(p, server))
}

impl Core {
    /// The lock and the sent values of one room's settings.
    pub(crate) async fn room_writes(&self, room: &RoomId) -> Arc<Mutex<RoomWrites>> {
        self.state.lock().await.room_writes.entry(room.to_owned()).or_default().clone()
    }

    /// The room's power levels as this device last set them while the
    /// sync has not brought them, else the room's own.
    pub(crate) async fn effective_levels(&self, room: &Room) -> Result<RoomPowerLevels> {
        let lock = self.room_writes(room.room_id()).await;
        let mut w = lock.lock().await;
        let local = state_event_id(room, StateEventType::RoomPowerLevels).await;
        match Sent::current(&mut w.power_levels, local.as_deref()) {
            Some(levels) => Ok(levels),
            None => Ok(room.power_levels().await?),
        }
    }

    /// A change of the room's power levels, one at a time per room and on
    /// top of the last one sent. `change` returns false for nothing to send.
    pub(crate) async fn write_levels(&self, room: &Room, change: impl FnOnce(&mut RoomPowerLevels) -> Result<bool>) -> Result<bool> {
        let lock = self.room_writes(room.room_id()).await;
        let mut w = lock.lock().await;
        let local = state_event_id(room, StateEventType::RoomPowerLevels).await;
        let mut levels = match Sent::current(&mut w.power_levels, local.as_deref()) {
            Some(levels) => levels,
            None => room.power_levels().await?,
        };
        if !change(&mut levels)? {
            return Ok(false);
        }
        let sent = room.send_state_event(RoomPowerLevelsEventContent::try_from(levels.clone())?).await?;
        Sent::record(&mut w.power_levels, local, sent.event_id, levels);
        Ok(true)
    }

    /// Everything the room's info page needs to offer the right controls:
    /// what we may change (from the room's power levels), the settings as
    /// they are, the levels, the access list, pins. "room_settings".
    pub(crate) async fn room_settings(&self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let me = room.own_user_id().to_owned();
        // what this device sent last, where the sync has not brought it yet
        // (the reply to a change shows the change)
        let (levels, history, guests, acl, aliases, pinned) = {
            let lock = self.room_writes(room.room_id()).await;
            let mut w = lock.lock().await;
            let id = |kind: StateEventType| state_event_id(&room, kind);
            let levels = match Sent::current(&mut w.power_levels, id(StateEventType::RoomPowerLevels).await.as_deref()) {
                Some(l) => Some(l),
                None => room.power_levels().await.ok(),
            };
            let history = Sent::current(&mut w.history, id(StateEventType::RoomHistoryVisibility).await.as_deref())
                .unwrap_or_else(|| room.history_visibility_or_default());
            let guests = Sent::current(&mut w.guests, id(StateEventType::RoomGuestAccess).await.as_deref())
                .unwrap_or_else(|| room.guest_access() == GuestAccess::CanJoin);
            let acl = match Sent::current(&mut w.acl, id(StateEventType::RoomServerAcl).await.as_deref()) {
                Some(c) => Some(c),
                None => room
                    .get_state_event_static::<RoomServerAclEventContent>()
                    .await
                    .ok()
                    .flatten()
                    .and_then(|raw| raw.deserialize().ok())
                    .and_then(|ev| match ev {
                        matrix_sdk::deserialized_responses::SyncOrStrippedState::Sync(s) => s.as_original().map(|o| o.content.clone()),
                        _ => None,
                    }),
            };
            let aliases = Sent::current(&mut w.aliases, id(StateEventType::RoomCanonicalAlias).await.as_deref())
                .unwrap_or_else(|| (room.canonical_alias(), room.alt_aliases()));
            let pinned = Sent::current(&mut w.pinned, id(StateEventType::RoomPinnedEvents).await.as_deref())
                .unwrap_or_else(|| room.pinned_event_ids().unwrap_or_default());
            (levels, history, guests, acl, aliases, pinned)
        };
        let can_state = |t: StateEventType| levels.as_ref().is_some_and(|l| l.user_can_send_state(&me, t));
        let own = match &levels {
            Some(l) => l.for_user(&me),
            None => room.get_user_power_level(&me).await.unwrap_or(UserPowerLevel::Int(Int::from(0u8))),
        };
        let may = json!({
            "name": can_state(StateEventType::RoomName),
            "topic": can_state(StateEventType::RoomTopic),
            "avatar": can_state(StateEventType::RoomAvatar),
            "join_rules": can_state(StateEventType::RoomJoinRules),
            "history": can_state(StateEventType::RoomHistoryVisibility),
            "guest": can_state(StateEventType::RoomGuestAccess),
            "encryption": can_state(StateEventType::RoomEncryption),
            "server_acl": can_state(StateEventType::RoomServerAcl),
            "upgrade": can_state(StateEventType::RoomTombstone),
            "aliases": can_state(StateEventType::RoomCanonicalAlias),
            "pin": can_state(StateEventType::RoomPinnedEvents),
            "power_levels": can_state(StateEventType::RoomPowerLevels),
            "space_child": can_state(StateEventType::SpaceChild),
            "invite": levels.as_ref().is_some_and(|l| l.user_can_invite(&me)),
            "kick": levels.as_ref().is_some_and(|l| l.user_can_kick(&me)),
            "ban": levels.as_ref().is_some_and(|l| l.user_can_ban(&me)),
            "redact_other": levels.as_ref().is_some_and(|l| l.user_can_redact_event_of_other(&me)),
            "notify_room": levels.as_ref().is_some_and(|l| l.user_can_trigger_room_notification(&me)),
        });
        let level_map = levels.as_ref().map(|l| {
            let mut events = serde_json::Map::new();
            for t in LEVEL_EVENTS {
                let v = l.events.get(&TimelineEventType::from(*t)).map(|i| i64::from(*i));
                events.insert((*t).to_owned(), json!(v));
            }
            json!({
                "users_default": i64::from(l.users_default),
                "events_default": i64::from(l.events_default),
                "state_default": i64::from(l.state_default),
                "invite": i64::from(l.invite), "kick": i64::from(l.kick),
                "ban": i64::from(l.ban), "redact": i64::from(l.redact),
                "notify_room": i64::from(l.notifications.room),
                "events": events,
            })
        });
        let acl = acl.map(|c| json!({"allow": c.allow, "deny": c.deny, "allow_ip_literals": c.allow_ip_literals}));
        // the versions a room can be upgraded to: the server's default, and
        // the stable ones it offers (only for those who may upgrade)
        let (version_default, versions) = if may["upgrade"].as_bool() == Some(true) {
            match room.client().homeserver_capabilities().room_versions().await {
                Ok(c) => (
                    Some(c.default.to_string()),
                    c.available.iter()
                        .filter(|(_, s)| matches!(s, matrix_sdk::ruma::api::client::discovery::get_capabilities::v3::RoomVersionStability::Stable))
                        .map(|(v, _)| v.to_string()).collect::<Vec<_>>(),
                ),
                Err(e) => {
                    tracing::info!("room versions: {e}");
                    (None, Vec::new())
                }
            }
        } else {
            (None, Vec::new())
        };
        // the directory lives on the server, not in the room's state
        let public = room.privacy_settings().get_room_visibility().await.ok().map(|v| v == Visibility::Public);
        let history = match history {
            HistoryVisibility::WorldReadable => "world_readable",
            HistoryVisibility::Invited => "invited",
            HistoryVisibility::Joined => "joined",
            _ => "shared",
        };
        self.emit(json!({
            "ev": "room_settings",
            "room_id": room.room_id().as_str(),
            "own_level": level_json(own),
            "role": role_of(own),
            "version": room.version().map(|v| v.to_string()),
            "version_default": version_default,
            "versions": versions,
            "may": may,
            "levels": level_map,
            "history_visibility": history,
            "guests": guests,
            "encrypted": room.encryption_state().is_encrypted(),
            "directory_public": public,
            "canonical_alias": aliases.0.map(|a| a.to_string()),
            "alt_aliases": aliases.1.iter().map(|a| a.to_string()).collect::<Vec<_>>(),
            "server_acl": acl,
            "own_server": acl_host(me.server_name().as_str()),
            "pinned": pinned.iter().map(|e| e.to_string()).collect::<Vec<_>>(),
            "creators": room.creators().unwrap_or_default().iter().map(|u| u.to_string()).collect::<Vec<_>>(),
        }));
        Ok(())
    }

    /// A person's level (their role); then the room's settings and their
    /// page again.
    pub(crate) async fn set_user_level(&self, room_id: &str, user_id: &str, level: i64) -> Result<()> {
        let room = self.room(room_id).await?;
        let user = UserId::parse(user_id.trim())?;
        let value = int(level)?;
        // as the SDK's update_power_levels, on top of the last change sent
        self.write_levels(&room, |levels| {
            if value == levels.users_default {
                levels.users.remove(&user);
            } else {
                levels.users.insert(user.clone(), value);
            }
            Ok(true)
        })
        .await?;
        self.user_info(user_id, Some(room_id)).await?;
        self.room_settings(room_id).await
    }

    /// One level of the room's permissions: a field ("invite", "ban",
    /// "users_default", "notify_room", ...) or an event type ("m.room.name").
    pub(crate) async fn set_level(&self, room_id: &str, key: &str, level: i64) -> Result<()> {
        let room = self.room(room_id).await?;
        let value = int(level)?;
        self.write_levels(&room, |levels| {
            set_permission(levels, key, value)?;
            Ok(true)
        })
        .await?;
        self.room_settings(room_id).await
    }

    pub(crate) async fn set_history_visibility(&self, room_id: &str, value: &str) -> Result<()> {
        use matrix_sdk::ruma::events::room::history_visibility::RoomHistoryVisibilityEventContent;
        let room = self.room(room_id).await?;
        let v = match value {
            "world_readable" => HistoryVisibility::WorldReadable,
            "shared" => HistoryVisibility::Shared,
            "invited" => HistoryVisibility::Invited,
            "joined" => HistoryVisibility::Joined,
            other => bail!("no such history visibility {other}"),
        };
        let lock = self.room_writes(room.room_id()).await;
        let mut w = lock.lock().await;
        let local = state_event_id(&room, StateEventType::RoomHistoryVisibility).await;
        Sent::current(&mut w.history, local.as_deref());
        let sent = room.send_state_event(RoomHistoryVisibilityEventContent::new(v.clone())).await?;
        Sent::record(&mut w.history, local, sent.event_id, v);
        drop(w);
        self.room_settings(room_id).await
    }

    pub(crate) async fn set_guest_access(&self, room_id: &str, allowed: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        let access = if allowed { GuestAccess::CanJoin } else { GuestAccess::Forbidden };
        let lock = self.room_writes(room.room_id()).await;
        let mut w = lock.lock().await;
        let local = state_event_id(&room, StateEventType::RoomGuestAccess).await;
        Sent::current(&mut w.guests, local.as_deref());
        let sent = room.send_state_event(RoomGuestAccessEventContent::new(access)).await?;
        Sent::record(&mut w.guests, local, sent.event_id, allowed);
        drop(w);
        self.room_settings(room_id).await
    }

    /// Encryption on, for good: a room cannot be made unencrypted again.
    pub(crate) async fn enable_encryption(&self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        room.enable_encryption().await?;
        self.room_settings(room_id).await
    }

    /// Listed in the server's public room directory, or not.
    pub(crate) async fn set_directory_visibility(&self, room_id: &str, listed: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        let v = if listed { Visibility::Public } else { Visibility::Private };
        room.privacy_settings().update_room_visibility(v).await?;
        self.room_settings(room_id).await
    }

    /// The room's main address and its other addresses (each must point to
    /// the room on its server already: add_alias).
    pub(crate) async fn set_aliases(&self, room_id: &str, canonical: Option<&str>, alt: &[String]) -> Result<()> {
        use matrix_sdk::ruma::events::room::canonical_alias::RoomCanonicalAliasEventContent;
        let room = self.room(room_id).await?;
        let canonical = canonical.map(|a| OwnedRoomAliasId::try_from(a.trim())).transpose()?;
        let alt = alt.iter().map(|a| OwnedRoomAliasId::try_from(a.trim())).collect::<Result<Vec<_>, _>>()?;
        let lock = self.room_writes(room.room_id()).await;
        let mut w = lock.lock().await;
        let local = state_event_id(&room, StateEventType::RoomCanonicalAlias).await;
        Sent::current(&mut w.aliases, local.as_deref());
        let mut content = RoomCanonicalAliasEventContent::new();
        content.alias = canonical.clone();
        content.alt_aliases = alt.clone();
        let sent = room.send_state_event(content).await?;
        Sent::record(&mut w.aliases, local, sent.event_id, (canonical, alt));
        drop(w);
        self.room_settings(room_id).await
    }

    /// A new address on its server, pointing to the room (`#name:server`).
    pub(crate) async fn add_alias(&self, room_id: &str, alias: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let alias = RoomAliasId::parse(alias.trim())?;
        let client = self.client().await?;
        client.create_room_alias(&alias, room.room_id()).await?;
        self.emit(json!({"ev": "alias_added", "room_id": room.room_id().as_str(), "alias": alias.as_str()}));
        self.room_settings(room_id).await
    }

    /// An address taken off its server (rooms that used it keep their name).
    pub(crate) async fn remove_alias(&self, room_id: &str, alias: &str) -> Result<()> {
        let alias = RoomAliasId::parse(alias.trim())?;
        let client = self.client().await?;
        client.remove_room_alias(&alias).await?;
        self.room_settings(room_id).await
    }

    /// The servers that may take part. Refused when the list would shut out
    /// the own server: every later change, the fix included, would be
    /// rejected by the others.
    pub(crate) async fn set_server_acl(&self, room_id: &str, allow: &[String], deny: &[String], allow_ip_literals: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        let own = acl_host(room.own_user_id().server_name().as_str()).to_owned();
        let clean = |v: &[String]| -> Vec<String> { v.iter().map(|s| s.trim().to_owned()).filter(|s| !s.is_empty()).collect() };
        let (allow, deny) = (clean(allow), clean(deny));
        if !acl_allows(&allow, &deny, &own) {
            bail!("the list would shut out your own server {own}");
        }
        let content = RoomServerAclEventContent::new(allow_ip_literals, allow, deny);
        let lock = self.room_writes(room.room_id()).await;
        let mut w = lock.lock().await;
        let local = state_event_id(&room, StateEventType::RoomServerAcl).await;
        Sent::current(&mut w.acl, local.as_deref());
        let sent = room.send_state_event(content.clone()).await?;
        Sent::record(&mut w.acl, local, sent.event_id, content);
        drop(w);
        self.room_settings(room_id).await
    }

    /// The room in a newer version: a new room, this one points to it.
    pub(crate) async fn upgrade_room(&self, room_id: &str, version: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        let version = RoomVersionId::try_from(version.trim())?;
        let response = client.send(upgrade_room::v3::Request::new(room.room_id().to_owned(), version)).await?;
        self.emit(json!({"ev": "room_upgraded", "room_id": room.room_id().as_str(), "replacement": response.replacement_room.as_str()}));
        Ok(())
    }

    /// A message pinned above the chat, or no longer.
    pub(crate) async fn pin(&self, room_id: &str, event_id: &str, pinned: bool) -> Result<()> {
        use matrix_sdk::ruma::events::room::pinned_events::RoomPinnedEventsEventContent;
        let room = self.room(room_id).await?;
        let event = EventId::parse(event_id.trim())?;
        // as the SDK's pin_event and unpin_event, on top of the last change
        let lock = self.room_writes(room.room_id()).await;
        let mut w = lock.lock().await;
        let local = state_event_id(&room, StateEventType::RoomPinnedEvents).await;
        let mut list = match Sent::current(&mut w.pinned, local.as_deref()) {
            Some(list) => list,
            None => match room.pinned_event_ids() {
                Some(list) => list,
                None => room.load_pinned_events().await?.unwrap_or_default(),
            },
        };
        if set_pinned(&mut list, &event, pinned) {
            let sent = room.send_state_event(RoomPinnedEventsEventContent::new(list.clone())).await?;
            Sent::record(&mut w.pinned, local, sent.event_id, list);
        }
        drop(w);
        self.room_settings(room_id).await
    }

    /// The whole room reported to the own server's admins.
    pub(crate) async fn report_room(&self, room_id: &str, reason: Option<&str>) -> Result<()> {
        let room = self.room(room_id).await?;
        room.report_room(reason.unwrap_or("").to_owned()).await?;
        self.emit(json!({"ev": "reported", "room_id": room.room_id().as_str()}));
        Ok(())
    }

    /// A person reported to the own server's admins.
    pub(crate) async fn report_user(&self, user_id: &str, reason: Option<&str>) -> Result<()> {
        let client = self.client().await?;
        let user = UserId::parse(user_id.trim())?;
        client.send(report_user::v3::Request::new(user.clone(), reason.unwrap_or("").to_owned())).await?;
        self.emit(json!({"ev": "reported", "user_id": user.as_str()}));
        Ok(())
    }
}

/// The `order` strings that keep a space's rooms in the given order: the
/// spec compares them as plain strings, so zero-padded steps of ten.
pub(crate) fn space_orders(count: usize) -> Vec<String> {
    (0..count).map(|i| format!("{:05}", (i + 1) * 10)).collect()
}

impl Core {
    /// A room of a space marked suggested, or moved (its `order`); the rest
    /// of its m.space.child event (via) stays as it is.
    pub(crate) async fn space_child_set(&self, space_id: &str, room_id: &str, suggested: Option<bool>, order: Option<&str>) -> Result<()> {
        let space = self.room(space_id).await?;
        let child = OwnedRoomId::try_from(room_id.trim())?;
        let mut content = space
            .get_state_event_static_for_key::<SpaceChildEventContent, _>(&child)
            .await?
            .and_then(|raw| raw.deserialize().ok())
            .and_then(|ev| match ev {
                matrix_sdk::deserialized_responses::SyncOrStrippedState::Sync(s) => s.as_original().map(|o| o.content.clone()),
                _ => None,
            })
            .ok_or_else(|| anyhow!("the room is not in this space"))?;
        if let Some(s) = suggested {
            content.suggested = s;
        }
        if let Some(o) = order {
            content.order = if o.is_empty() { None } else { Some(SpaceChildOrder::parse(o)?.to_owned()) };
        }
        space.send_state_event_for_key(&child, content).await?;
        Ok(())
    }

    /// A space's rooms in this order: each gets an `order` (only those that
    /// change are sent).
    pub(crate) async fn space_reorder(&self, space_id: &str, room_ids: &[String]) -> Result<()> {
        for (room_id, order) in room_ids.iter().zip(space_orders(room_ids.len())) {
            self.space_child_set(space_id, room_id, None, Some(&order)).await?;
        }
        Ok(())
    }

    /// A person banned from a space and from each of its rooms where we may
    /// ban them; "space_banned" says in how many, and how many were left
    /// out (no power there, or not joined).
    pub(crate) async fn space_ban(&self, space_id: &str, user_id: &str, reason: Option<&str>) -> Result<()> {
        let client = self.client().await?;
        let space = self.room(space_id).await?;
        let user = UserId::parse(user_id.trim())?;
        let mut rooms = vec![space.clone()];
        for raw in space.get_state_events(StateEventType::SpaceChild).await.unwrap_or_default() {
            if let matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState::Sync(raw) = raw {
                let v = crate::serialize::raw_value(&raw);
                let live = v.get("content").and_then(Value::as_object).is_some_and(|c| !c.is_empty());
                let id = v.get("state_key").and_then(Value::as_str).and_then(|k| RoomId::parse(k).ok());
                if let (true, Some(id)) = (live, id) {
                    if let Some(r) = client.get_room(&id) {
                        rooms.push(r);
                    }
                }
            }
        }
        let (mut banned, mut skipped) = (0, 0);
        for room in rooms {
            let may = room.state() == RoomState::Joined
                && room.power_levels().await.is_ok_and(|l| l.user_can_ban_user(room.own_user_id(), &user));
            if !may {
                skipped += 1;
                continue;
            }
            match room.ban_user(&user, reason).await {
                Ok(()) => banned += 1,
                Err(e) => {
                    tracing::info!(room = %room.room_id(), "space ban: {e}");
                    skipped += 1;
                }
            }
        }
        self.emit(json!({"ev": "space_banned", "space_id": space_id, "user_id": user.as_str(), "rooms": banned, "skipped": skipped}));
        Ok(())
    }

    /// A person's recent messages in a room removed, newest first, paced
    /// (the server limits the rate): from the last REDACT_RECENT_SCAN
    /// events, at most REDACT_RECENT_MAX of theirs. "redact_progress" after
    /// each, with `finished` at the end.
    pub(crate) async fn redact_recent(&'static self, room_id: &str, user_id: &str, reason: Option<&str>) -> Result<()> {
        let room = self.room(room_id).await?;
        let user = UserId::parse(user_id.trim())?.to_owned();
        let mut options = matrix_sdk::room::MessagesOptions::backward();
        options.limit = REDACT_RECENT_SCAN.into();
        options.filter.senders = Some(vec![user.clone()]);
        let events: Vec<OwnedEventId> = room
            .messages(options)
            .await?
            .chunk
            .iter()
            .filter_map(|ev| {
                let v = ev.raw().deserialize_as::<Value>().ok()?;
                // a message, not a state event or one removed already
                if v.get("state_key").is_some() || v.get("content").and_then(Value::as_object).is_some_and(|c| c.is_empty()) {
                    return None;
                }
                if v.get("sender").and_then(Value::as_str) != Some(user.as_str()) {
                    return None;
                }
                v.get("event_id").and_then(Value::as_str).and_then(|e| EventId::parse(e).ok())
            })
            .take(REDACT_RECENT_MAX)
            .collect();
        let total = events.len();
        let reason = reason.map(str::to_owned);
        let (rid, uid) = (room.room_id().to_owned(), user.clone());
        // a login's task: a logout stops it (up to 100 removals at a pace
        // ran on with the old client otherwise)
        self.spawn_login_task(async move {
            let mut done = 0;
            for id in events {
                if let Err(e) = room.redact(&id, reason.as_deref(), None).await {
                    tracing::info!(room = %rid, "remove recent: {e}");
                }
                done += 1;
                self.emit(json!({"ev": "redact_progress", "room_id": rid.as_str(), "user_id": uid.as_str(), "done": done, "total": total, "finished": done == total}));
                tokio::time::sleep(std::time::Duration::from_millis(REDACT_RECENT_PACE_MS)).await;
            }
            if total == 0 {
                self.emit(json!({"ev": "redact_progress", "room_id": rid.as_str(), "user_id": uid.as_str(), "done": 0, "total": 0, "finished": true}));
            }
        })
        .await;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Planted-bug: make `*` match only one character: the wildcard tests fail.
    #[test]
    fn acl_globs() {
        assert!(acl_matches("*", "tchncs.de"));
        assert!(acl_matches("*.matrix.org", "a.b.matrix.org"));
        assert!(!acl_matches("*.matrix.org", "matrix.org"));
        assert!(acl_matches("matrix.???", "matrix.org"));
        assert!(acl_matches("TCHNCS.DE", "tchncs.de"));
        assert!(!acl_matches("evil.*", "tchncs.de"));
    }

    /// The own server must stay in: allowed and not denied.
    /// Planted-bug: `acl_allows` with `||` instead of `&& !` (a denied own
    /// server passes).
    #[test]
    fn acl_keeps_the_own_server() {
        let s = |v: &[&str]| v.iter().map(|x| x.to_string()).collect::<Vec<_>>();
        assert!(acl_allows(&s(&["*"]), &s(&["evil.example"]), "tchncs.de"));
        assert!(!acl_allows(&s(&["*"]), &s(&["tchncs.de"]), "tchncs.de"));
        assert!(!acl_allows(&s(&["matrix.org"]), &s(&[]), "tchncs.de"));
        assert!(!acl_allows(&s(&[]), &s(&[]), "tchncs.de"));
    }

    /// Access lists name servers without a port.
    /// Planted-bug: `acl_host` returns the name as given (a server with a
    /// port never matches "example.org").
    #[test]
    fn acl_host_drops_the_port() {
        assert_eq!(acl_host("example.org:8448"), "example.org");
        assert_eq!(acl_host("example.org"), "example.org");
        assert_eq!(acl_host("[::1]:8448"), "[::1]");
        assert_eq!(acl_host("[::1]"), "[::1]");
        assert_eq!(acl_host("1.2.3.4:80"), "1.2.3.4");
        let s = |v: &[&str]| v.iter().map(|x| x.to_string()).collect::<Vec<_>>();
        assert!(acl_allows(&s(&["example.org"]), &s(&[]), acl_host("example.org:8448")));
    }

    /// Planted-bug: drop the zero padding: "100" sorts before "20".
    #[test]
    fn space_orders_sort_as_given() {
        let o = space_orders(12);
        let mut sorted = o.clone();
        sorted.sort();
        assert_eq!(o, sorted);
        assert!(o.iter().all(|x| x.len() <= 50));
    }

    /// A room as the sync shows it: its state event's id and content.
    struct Synced {
        id: Option<OwnedEventId>,
        levels: RoomPowerLevels,
    }

    fn event(id: &str) -> OwnedEventId {
        OwnedEventId::try_from(id).unwrap()
    }

    /// One change as `write_levels` makes it, the server's answer `sent`:
    /// returns what was sent.
    fn change(slot: &mut Option<Sent<RoomPowerLevels>>, room: &Synced, key: &str, level: i64, sent: &str) -> RoomPowerLevels {
        let mut levels = Sent::current(slot, room.id.as_deref()).unwrap_or_else(|| room.levels.clone());
        set_permission(&mut levels, key, int(level).unwrap()).unwrap();
        Sent::record(slot, room.id.clone(), event(sent), levels.clone());
        levels
    }

    /// What room_settings shows.
    fn shown(slot: &mut Option<Sent<RoomPowerLevels>>, room: &Synced) -> RoomPowerLevels {
        Sent::current(slot, room.id.as_deref()).unwrap_or_else(|| room.levels.clone())
    }

    /// Two quick changes: the second is built on the first, not on the
    /// room's state from before both, and what is shown is the newest
    /// sent until the sync brings it; then the room's own state again,
    /// also when someone else changed it meanwhile.
    /// Planted-bugs: in Sent::current, never hold (`let holds = false`):
    /// the second change reverts the first; in Sent::record, forget the
    /// earlier sends (`let stale = vec![local]`): once the first change
    /// is synced the second one's value vanishes from the reply.
    #[test]
    fn quick_changes_build_on_each_other() {
        use matrix_sdk::ruma::events::room::power_levels::RoomPowerLevelsSource;
        use matrix_sdk::ruma::room_version_rules::AuthorizationRules;
        let start = RoomPowerLevels::new(RoomPowerLevelsSource::None, &AuthorizationRules::V1, Vec::<matrix_sdk::ruma::OwnedUserId>::new());
        let (kick0, ban0) = (i64::from(start.kick), i64::from(start.ban));
        assert_eq!((kick0, ban0), (50, 50));
        let mut room = Synced { id: Some(event("$a:x")), levels: start.clone() };
        let mut slot = None;

        change(&mut slot, &room, "kick", 0, "$e1:x");
        let second = change(&mut slot, &room, "ban", 100, "$e2:x");
        assert_eq!((i64::from(second.kick), i64::from(second.ban)), (0, 100), "the second change undid the first");
        let s = shown(&mut slot, &room);
        assert_eq!((i64::from(s.kick), i64::from(s.ban)), (0, 100));

        // the sync brings our first change: the second is still shown
        let mut first = start.clone();
        first.kick = int(0).unwrap();
        room = Synced { id: Some(event("$e1:x")), levels: first };
        let s = shown(&mut slot, &room);
        assert_eq!((i64::from(s.kick), i64::from(s.ban)), (0, 100), "the reply lost the second change");

        // then the second: the room's own state, the overlay gone
        room = Synced { id: Some(event("$e2:x")), levels: second };
        let s = shown(&mut slot, &room);
        assert_eq!((i64::from(s.kick), i64::from(s.ban)), (0, 100));
        assert!(slot.is_none());

        // someone else's change after ours: theirs wins
        change(&mut slot, &room, "invite", 100, "$e3:x");
        let mut theirs = start.clone();
        theirs.invite = int(10).unwrap();
        room = Synced { id: Some(event("$other:x")), levels: theirs };
        assert_eq!(i64::from(shown(&mut slot, &room).invite), 10);
        assert!(slot.is_none());
    }

    /// Pinning on top of an earlier pin keeps it; unpinning what is not
    /// pinned, or pinning twice, sends nothing.
    /// Planted-bug: `set_pinned` replaces the list (`*list = vec![..]`).
    #[test]
    fn pins_build_on_the_list() {
        let mut list = vec![event("$a:x")];
        assert!(set_pinned(&mut list, &event("$b:x"), true));
        assert!(!set_pinned(&mut list, &event("$b:x"), true));
        assert_eq!(list, vec![event("$a:x"), event("$b:x")]);
        assert!(set_pinned(&mut list, &event("$a:x"), false));
        assert!(!set_pinned(&mut list, &event("$a:x"), false));
        assert_eq!(list, vec![event("$b:x")]);
    }

    /// Planted-bug: map `Infinite` to "admin" (room creators of v12 rooms
    /// show as admins).
    #[test]
    fn roles_from_levels() {
        assert_eq!(role_of(UserPowerLevel::Infinite), "creator");
        assert_eq!(role_of(UserPowerLevel::Int(Int::from(100u8))), "admin");
        assert_eq!(role_of(UserPowerLevel::Int(Int::from(50u8))), "moderator");
        assert_eq!(role_of(UserPowerLevel::Int(Int::from(0u8))), "member");
    }
}
