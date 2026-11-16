// SPDX-License-Identifier: GPL-3.0-or-later
//! Rooms: joining, leaving and making them, their settings, members and
//! the search for people and public rooms. (part of `impl Core`; spaces,
//! previews and join rules are in spaces.rs)

use super::*;

impl Core {
    /// The room's own notification rule, "default" without one: "notification_mode".
    pub(crate) async fn notification_mode(&self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        let settings = client.notification_settings().await;
        let mode = match settings.get_user_defined_room_notification_mode(room.room_id()).await {
            Some(RoomNotificationMode::AllMessages) => "all",
            Some(RoomNotificationMode::MentionsAndKeywordsOnly) => "mentions",
            Some(RoomNotificationMode::Mute) => "mute",
            None => "default",
        };
        self.emit(json!({"ev": "notification_mode", "room_id": room.room_id().as_str(), "mode": mode}));
        Ok(())
    }

    /// "all", "mentions", "mute"; anything else goes back to the account's rule.
    pub(crate) async fn set_notification_mode(&self, room_id: &str, mode: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        let settings = client.notification_settings().await;
        let id = room.room_id();
        match mode {
            "all" => settings.set_room_notification_mode(id, RoomNotificationMode::AllMessages).await?,
            "mentions" => settings.set_room_notification_mode(id, RoomNotificationMode::MentionsAndKeywordsOnly).await?,
            "mute" => settings.set_room_notification_mode(id, RoomNotificationMode::Mute).await?,
            _ => settings.delete_user_defined_room_rules(id).await?,
        }
        self.notification_mode(room_id).await
    }

    pub(crate) async fn set_favourite(&self, room_id: &str, value: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        room.set_is_favourite(value, None).await?;
        Ok(())
    }

    pub(crate) async fn set_low_priority(&self, room_id: &str, value: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        room.set_is_low_priority(value, None).await?;
        Ok(())
    }

    pub(crate) async fn join_room(&self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        room.join().await?;
        Ok(())
    }

    pub(crate) async fn leave_room(&self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        room.leave().await?;
        Ok(())
    }

    /// A room id or alias, with the servers that know it; answered by "joined".
    pub(crate) async fn join_by_id_or_alias(&self, id: &str, via: &Option<Vec<String>>) -> Result<()> {
        let client = self.client().await?;
        let id = RoomOrAliasId::parse(id.trim())?;
        let room = client.join_room_by_id_or_alias(&id, &spaces::servers(via)).await?;
        self.emit(json!({"ev": "joined", "room_id": room.room_id().as_str()}));
        Ok(())
    }

    /// The direct chat with someone, made when there is none: "dm_ready".
    pub(crate) async fn create_dm(&self, user_id: &str) -> Result<()> {
        let client = self.client().await?;
        let user = UserId::parse(user_id.trim())?;
        let room = match client.get_dm_room(&user) {
            Some(r) => r,
            None => client.create_dm(&user).await?,
        };
        self.emit(json!({"ev": "dm_ready", "room_id": room.room_id().as_str(), "user_id": user.as_str()}));
        Ok(())
    }

    /// A new room or space, put into `parent` when given; answered by "joined".
    #[allow(clippy::too_many_arguments)]
    pub(crate) async fn create_room(
        &self,
        name: Option<&str>,
        topic: Option<&str>,
        public: bool,
        alias: Option<&str>,
        encrypted: Option<bool>,
        space: bool,
        parent: Option<&str>,
        join_rule: Option<&str>,
    ) -> Result<()> {
        use matrix_sdk::ruma::events::room::join_rules::{AllowRule, JoinRule, Restricted, RoomJoinRulesEventContent};
        let client = self.client().await?;
        let mut req = create_room::v3::Request::new();
        req.name = name.map(str::to_owned);
        req.topic = topic.map(str::to_owned);
        req.preset = Some(if public { create_room::v3::RoomPreset::PublicChat } else { create_room::v3::RoomPreset::PrivateChat });
        if public {
            req.visibility = Visibility::Public;
            req.room_alias_name = alias.map(str::to_owned);
        }
        // a space holds no messages: nothing to encrypt
        if !space && encrypted.unwrap_or(!public) {
            let content = RoomEncryptionEventContent::with_recommended_defaults();
            req.initial_state.push(InitialStateEvent::with_empty_state_key(content).to_raw_any());
        }
        if space {
            req.creation_content = Some(matrix_sdk::ruma::serde::Raw::new(&json!({"type": "m.space"}))?.cast_unchecked());
        }
        // who may join, when not public: invite (the preset's), ask to
        // join, or the members of the parent space
        let rule = match join_rule {
            Some("knock") => Some(JoinRule::Knock),
            Some("restricted") => match parent {
                Some(p) => Some(JoinRule::Restricted(Restricted::new(vec![AllowRule::room_membership(OwnedRoomId::try_from(p)?)]))),
                None => None,
            },
            _ => None,
        };
        if let Some(rule) = rule {
            req.initial_state.push(InitialStateEvent::with_empty_state_key(RoomJoinRulesEventContent::new(rule)).to_raw_any());
        }
        // everyone may take part in calls (a call membership is a state
        // event); the map replaces the server's, so its defaults are
        // restated (Synapse's, as Element sends them)
        let mut events = json!({
            "m.room.name": POWER_MOD, "m.room.power_levels": POWER_ADMIN, "m.room.history_visibility": POWER_ADMIN,
            "m.room.canonical_alias": POWER_MOD, "m.room.avatar": POWER_MOD, "m.room.tombstone": POWER_ADMIN,
            "m.room.server_acl": POWER_ADMIN, "m.room.encryption": POWER_ADMIN,
        });
        for t in call::CALL_MEMBER_TYPES {
            events[t] = json!(0);
        }
        // in a space only admins post (its rooms are its state)
        let mut levels = json!({ "events": events });
        if space {
            levels["events_default"] = json!(POWER_ADMIN);
        }
        req.power_level_content_override = Some(matrix_sdk::ruma::serde::Raw::new(&levels)?.cast_unchecked());
        let room = client.create_room(req).await?;
        if let Some(p) = parent {
            let service = self.space_service().await?;
            if let Err(e) = service.add_child_to_space(room.room_id().to_owned(), OwnedRoomId::try_from(p)?).await {
                tracing::info!(room = %room.room_id(), "space: adding the new room: {e}");
            }
        }
        self.emit(json!({"ev": "joined", "room_id": room.room_id().as_str(), "is_space": space}));
        Ok(())
    }

    /// Answered by "members".
    pub(crate) async fn room_members(&self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let members: Vec<Value> = room
            .members(RoomMemberships::ACTIVE)
            .await?
            .iter()
            .map(|m| {
                let power = match m.power_level() {
                    UserPowerLevel::Infinite => Value::from("infinite"),
                    UserPowerLevel::Int(i) => Value::from(i64::from(i)),
                    _ => Value::Null,
                };
                json!({"user_id": m.user_id().as_str(), "name": m.name(), "power": power})
            })
            .collect();
        self.emit(json!({"ev": "members", "room_id": room.room_id().as_str(), "members": members}));
        Ok(())
    }

    pub(crate) async fn set_room_name(&self, room_id: &str, name: String) -> Result<()> {
        let room = self.room(room_id).await?;
        room.set_name(name).await?;
        Ok(())
    }

    /// A picture file (the UI has turned it upright and scaled it down).
    pub(crate) async fn set_room_avatar(&self, room_id: &str, path: &str, w: Option<u64>, h: Option<u64>) -> Result<()> {
        let room = self.room(room_id).await?;
        let data = std::fs::read(path)?;
        let mime = mime_guess::from_path(path).first_or(mime::IMAGE_JPEG);
        let mut info = matrix_sdk::ruma::events::room::avatar::ImageInfo::new();
        info.width = w.and_then(|n| UInt::try_from(n).ok());
        info.height = h.and_then(|n| UInt::try_from(n).ok());
        info.size = UInt::try_from(data.len() as u64).ok();
        info.mimetype = Some(mime.to_string());
        room.upload_avatar(&mime, data, Some(info)).await?;
        std::fs::remove_file(path).ok();
        // the room list fetches the new picture once the sync brings it
        Ok(())
    }

    pub(crate) async fn set_room_topic(&self, room_id: &str, topic: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        room.set_room_topic(topic).await?;
        Ok(())
    }

    pub(crate) async fn invite(&self, room_id: &str, user_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        room.invite_user_by_id(&UserId::parse(user_id.trim())?).await?;
        Ok(())
    }

    /// People from the user directory, rooms from the public directory of
    /// the own server; both are best effort. Answered by "search_results".
    pub(crate) async fn search(&self, term: &str) -> Result<()> {
        let client = self.client().await?;
        let mut req = get_public_rooms_filtered::v3::Request::new();
        req.limit = Some(SEARCH_ROOMS_LIMIT.into());
        req.filter.generic_search_term = Some(term.to_owned());
        let (users, rooms) = tokio::join!(client.search_users(term, SEARCH_USERS_LIMIT), client.public_rooms_filtered(req));
        let users: Vec<Value> = users
            .map(|r| r.results)
            .unwrap_or_else(|e| {
                tracing::info!("user search: {e}");
                Vec::new()
            })
            .into_iter()
            .filter(|u| Some(u.user_id.as_ref()) != client.user_id())
            .map(|u| json!({"user_id": u.user_id.as_str(), "name": u.display_name}))
            .collect();
        let rooms: Vec<Value> = rooms
            .map(|r| r.chunk)
            .unwrap_or_else(|e| {
                tracing::info!("room search: {e}");
                Vec::new()
            })
            .into_iter()
            .map(|r| {
                let joined = client.get_room(&r.room_id).is_some_and(|room| room.state() == RoomState::Joined);
                json!({
                    "room_id": r.room_id.as_str(),
                    "name": r.name,
                    "alias": r.canonical_alias.map(|a| a.to_string()),
                    "topic": r.topic,
                    "member_count": r.num_joined_members,
                    "joined": joined,
                })
            })
            .collect();
        self.emit(json!({"ev": "search_results", "term": term, "users": users, "rooms": rooms}));
        Ok(())
    }

    /// The whole room list once more: "rooms".
    pub(crate) async fn refresh_rooms(&self) -> Result<()> {
        let client = self.client().await?;
        let dir = self.data_dir.join(AVATAR_DIR);
        let settings = client.notification_settings().await;
        let mut rooms = Vec::new();
        for room in client.rooms().into_iter().filter(crate::serialize::listed) {
            rooms.push(room_json(&room, &dir, &settings).await);
        }
        self.emit(json!({"ev": "rooms", "rooms": sorted_rooms(rooms)}));
        Ok(())
    }
}
