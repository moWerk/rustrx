// SPDX-License-Identifier: GPL-3.0-or-later
//! Spaces: rooms that group rooms (and other spaces). Joined spaces come
//! with the room list (marked is_space); this part holds what goes beyond
//! a room: a space's rooms (the server's hierarchy, through the SDK's
//! SpaceService), adding and removing them, leaving a space, who may join
//! a room or space, previews of rooms not joined, and knocking.
//! (part of `impl Core`)

use super::*;
use matrix_sdk::ruma::events::room::join_rules::{AllowRule, JoinRule, Restricted, RoomJoinRulesEventContent};
use matrix_sdk::ruma::room::{JoinRuleSummary, RoomType};
use matrix_sdk::ruma::events::StateEventType;
use matrix_sdk::ruma::{OwnedRoomOrAliasId, OwnedServerName};
use matrix_sdk_ui::spaces::room_list::SpaceRoomListPaginationState;
use matrix_sdk_ui::spaces::{SpaceRoom, SpaceService};

/// A join rule as the UI names it.
pub(crate) fn join_rule_name(rule: Option<&JoinRule>) -> &'static str {
    match rule {
        Some(JoinRule::Invite) => "invite",
        Some(JoinRule::Knock) => "knock",
        Some(JoinRule::Public) => "public",
        Some(JoinRule::Restricted(_)) => "restricted",
        Some(JoinRule::KnockRestricted(_)) => "knock_restricted",
        Some(JoinRule::Private) => "private",
        Some(_) => "custom",
        None => "",
    }
}

fn summary_rule_name(rule: Option<&JoinRuleSummary>) -> &'static str {
    match rule {
        Some(JoinRuleSummary::Invite) => "invite",
        Some(JoinRuleSummary::Knock) => "knock",
        Some(JoinRuleSummary::Public) => "public",
        Some(JoinRuleSummary::Restricted(_)) => "restricted",
        Some(JoinRuleSummary::KnockRestricted(_)) => "knock_restricted",
        Some(JoinRuleSummary::Private) => "private",
        Some(_) => "custom",
        None => "",
    }
}

fn state_name(state: Option<RoomState>) -> &'static str {
    match state {
        Some(RoomState::Joined) => "joined",
        Some(RoomState::Invited) => "invited",
        Some(RoomState::Knocked) => "knocked",
        Some(RoomState::Left) => "left",
        Some(RoomState::Banned) => "banned",
        _ => "none",
    }
}

/// The server names in a matrix.to or matrix: link's "via" (or given
/// alone), for rooms on other servers.
pub(super) fn servers(via: &Option<Vec<String>>) -> Vec<OwnedServerName> {
    via.iter().flatten().filter_map(|s| OwnedServerName::try_from(s.as_str()).ok()).collect()
}

fn space_room_json(r: &SpaceRoom) -> Value {
    json!({
        "room_id": r.room_id.as_str(),
        "name": r.display_name,
        "topic": r.topic,
        "alias": r.canonical_alias.as_ref().map(|a| a.to_string()),
        "avatar_mxc": r.avatar_url.as_ref().map(|u| u.to_string()),
        "is_space": r.room_type == Some(RoomType::Space),
        "members": r.num_joined_members,
        "children": r.children_count,
        "join_rule": summary_rule_name(r.join_rule.as_ref()),
        "state": state_name(r.state),
        "suggested": r.suggested,
        "is_dm": r.is_dm.unwrap_or(false),
        "via": r.via.iter().map(|s| s.as_str()).collect::<Vec<_>>(),
    })
}

impl Core {
    /// The SDK's space service, made once per login.
    pub(crate) async fn space_service(&self) -> Result<Arc<SpaceService>> {
        let client = self.client().await?;
        let existing = self.state.lock().await.space_service.clone();
        if let Some(service) = existing {
            return Ok(service);
        }
        // made without the lock held; a logout meanwhile leaves no service behind
        let service = Arc::new(SpaceService::new(client).await);
        let mut st = self.state.lock().await;
        if st.client.is_none() {
            bail!("not logged in");
        }
        Ok(st.space_service.get_or_insert(service).clone())
    }

    /// A space's rooms, page by page until all are in, and live after that:
    /// "space_rooms" each time. Closed with space_close (a space page each).
    pub(crate) async fn space_open(&'static self, space_id: &str) -> Result<()> {
        let id = OwnedRoomId::try_from(space_id)?;
        self.space_close(space_id).await;
        let service = self.space_service().await?;
        let list = service.space_room_list(id.clone()).await;
        let task = self.rt.spawn(async move {
            let (_, mut updates) = list.subscribe_to_room_updates().await;
            let emit = |rooms: Vec<Value>, loading: bool| {
                crate::instance().emit(json!({"ev": "space_rooms", "space_id": id.as_str(), "rooms": rooms, "loading": loading}));
            };
            // every page, as it comes
            loop {
                match list.pagination_state() {
                    SpaceRoomListPaginationState::Idle { end_reached: true } => break,
                    _ => {}
                }
                if let Err(e) = list.paginate().await {
                    tracing::info!(space = %id, "space: rooms: {e}");
                    break;
                }
                let rooms: Vec<Value> = list.rooms().await.iter().map(space_room_json).collect();
                emit(rooms, true);
            }
            let rooms: Vec<Value> = list.rooms().await.iter().map(space_room_json).collect();
            tracing::info!(space = %id, rooms = rooms.len(), "space: rooms");
            emit(rooms, false);
            while updates.next().await.is_some() {
                let rooms: Vec<Value> = list.rooms().await.iter().map(space_room_json).collect();
                emit(rooms, false);
            }
        });
        if let Some(old) = self.state.lock().await.space_lists.insert(space_id.to_owned(), task) {
            old.abort();
        }
        Ok(())
    }

    pub(crate) async fn space_close(&self, space_id: &str) {
        if let Some(task) = self.state.lock().await.space_lists.remove(space_id) {
            task.abort();
        }
    }

    pub(crate) async fn space_add_child(&'static self, space_id: &str, room_id: &str) -> Result<()> {
        let service = self.space_service().await?;
        service
            .add_child_to_space(OwnedRoomId::try_from(room_id)?, OwnedRoomId::try_from(space_id)?)
            .await
            .map_err(|e| anyhow!("{e}"))?;
        // the list follows the space's state; open it anew so it shows
        self.space_open(space_id).await
    }

    pub(crate) async fn space_remove_child(&'static self, space_id: &str, room_id: &str) -> Result<()> {
        let service = self.space_service().await?;
        service
            .remove_child_from_space(OwnedRoomId::try_from(room_id)?, OwnedRoomId::try_from(space_id)?)
            .await
            .map_err(|e| anyhow!("{e}"))?;
        self.space_open(space_id).await
    }

    /// Leaves a space, alone or with all of its rooms and subspaces.
    pub(crate) async fn space_leave(&'static self, space_id: &str, with_rooms: bool) -> Result<()> {
        let id = OwnedRoomId::try_from(space_id)?;
        if !with_rooms {
            let room = self.room(space_id).await?;
            room.leave().await?;
            return Ok(());
        }
        let service = self.space_service().await?;
        let handle = service.leave_space(&id).await.map_err(|e| anyhow!("{e}"))?;
        handle.leave(|_| true).await.map_err(|e| anyhow!("{e}"))?;
        Ok(())
    }

    /// Who may join a room or space: "invite", "knock" (ask to join),
    /// "public", "restricted" (members of its spaces), "knock_restricted"
    /// (members of its spaces, others may ask).
    pub(crate) async fn set_join_rule(&'static self, room_id: &str, rule: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let parents = || async {
            let mut ids: Vec<OwnedRoomId> = Vec::new();
            if let Ok(service) = self.space_service().await {
                ids.extend(service.joined_parent_ids_of_child(room.room_id()).await);
            }
            if let Ok(events) = room.get_state_events(StateEventType::SpaceParent).await {
                for ev in events {
                    if let matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState::Sync(raw) = ev {
                        if let Ok(Some(key)) = raw.get_field::<String>("state_key") {
                            if let Ok(id) = OwnedRoomId::try_from(key) {
                                if !ids.contains(&id) {
                                    ids.push(id);
                                }
                            }
                        }
                    }
                }
            }
            ids
        };
        let rule = match rule {
            "invite" => JoinRule::Invite,
            "knock" => JoinRule::Knock,
            "public" => JoinRule::Public,
            "restricted" | "knock_restricted" => {
                let spaces = parents().await;
                if spaces.is_empty() {
                    bail!("this room is in no space");
                }
                let allow = spaces.into_iter().map(AllowRule::room_membership).collect();
                if rule == "restricted" {
                    JoinRule::Restricted(Restricted::new(allow))
                } else {
                    JoinRule::KnockRestricted(Restricted::new(allow))
                }
            }
            other => bail!("unknown join rule {other}"),
        };
        room.send_state_event(RoomJoinRulesEventContent::new(rule)).await?;
        Ok(())
    }

    /// What a room or space is before joining it (from a link or an
    /// address): "room_preview"; when the server will not tell (a private
    /// space), ok is false and joining or asking is still offered.
    pub(crate) async fn room_preview(&'static self, id: &str, via: &Option<Vec<String>>) -> Result<()> {
        let client = self.client().await?;
        let rid = RoomOrAliasId::parse(id.trim())?;
        match client.get_room_preview(&rid, servers(via)).await {
            Ok(p) => self.emit(json!({
                "ev": "room_preview", "id": id, "ok": true,
                "room_id": p.room_id.as_str(),
                "name": p.name,
                "topic": p.topic,
                "alias": p.canonical_alias.map(|a| a.to_string()),
                "avatar_mxc": p.avatar_url.map(|u| u.to_string()),
                "members": p.num_joined_members,
                "is_space": p.room_type == Some(RoomType::Space),
                "join_rule": summary_rule_name(p.join_rule.as_ref()),
                "state": state_name(p.state),
            })),
            Err(e) => {
                tracing::info!(%id, "preview: {e}");
                self.emit(json!({"ev": "room_preview", "id": id, "ok": false, "error": e.to_string()}));
            }
        }
        Ok(())
    }

    /// Asks to join (knocks); answered by "knocked".
    pub(crate) async fn knock(&'static self, id: &str, reason: Option<&str>, via: &Option<Vec<String>>) -> Result<()> {
        let client = self.client().await?;
        let rid = OwnedRoomOrAliasId::try_from(id.trim())?;
        let room = client.knock(rid, reason.map(str::to_owned), servers(via)).await?;
        tracing::info!(room = %room.room_id(), "knock: asked to join");
        self.emit(json!({"ev": "knocked", "room_id": room.room_id().as_str()}));
        Ok(())
    }

    /// A room's permalink with the servers that reach it (via), for sharing.
    pub(crate) async fn room_link(&'static self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let link = room.matrix_to_permalink().await?;
        self.emit(json!({"ev": "room_link", "room_id": room_id, "link": link.to_string()}));
        Ok(())
    }
}
