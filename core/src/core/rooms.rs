// SPDX-License-Identifier: GPL-3.0-or-later
//! Rooms: joining, leaving and making them, their settings, members and
//! the search for people and public rooms. (part of `impl Core`; spaces,
//! previews and join rules are in spaces.rs)

use super::*;
use matrix_sdk::ruma::directory::RoomTypeFilter;
use matrix_sdk::ruma::room::RoomType;
use matrix_sdk::ruma::{OwnedRoomOrAliasId, OwnedServerName, RoomAliasId};

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

    /// The account's notification rules as the server keeps them, now and
    /// whenever they change: "account_notifications". With the master rule
    /// on (another client's "notifications for this account" off) the server
    /// notifies of nothing, and Rustrx showed nothing, with no hint why
    /// (davodego, 2026-11-21: push arrived, only as badge updates).
    pub(crate) async fn account_notifications_task(&'static self, client: Client) {
        let settings = client.notification_settings().await;
        let mut changes = settings.subscribe_to_changes();
        loop {
            self.report_account_notifications(&settings).await;
            match changes.recv().await {
                Ok(()) | Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => {}
                Err(tokio::sync::broadcast::error::RecvError::Closed) => break,
            }
        }
    }

    async fn report_account_notifications(&self, settings: &matrix_sdk::notification_settings::NotificationSettings) {
        use matrix_sdk::notification_settings::{IsEncrypted, IsOneToOne};
        use matrix_sdk::ruma::push::{PredefinedOverrideRuleId, RuleKind};
        let muted = settings
            .is_push_rule_enabled(RuleKind::Override, PredefinedOverrideRuleId::Master.as_str())
            .await
            .unwrap_or(false);
        let name = |m: RoomNotificationMode| match m {
            RoomNotificationMode::AllMessages => "all",
            RoomNotificationMode::MentionsAndKeywordsOnly => "mentions",
            RoomNotificationMode::Mute => "mute",
        };
        let dm = name(settings.get_default_room_notification_mode(IsEncrypted::Yes, IsOneToOne::Yes).await);
        let group = name(settings.get_default_room_notification_mode(IsEncrypted::Yes, IsOneToOne::No).await);
        tracing::info!(enabled = !muted, dm, group, "push rules of the account");
        self.emit(json!({"ev": "account_notifications", "enabled": !muted, "dm": dm, "group": group}));
    }

    pub(crate) async fn set_account_notifications(&self, enabled: bool) -> Result<()> {
        use matrix_sdk::ruma::push::{PredefinedOverrideRuleId, RuleKind};
        let client = self.client().await?;
        let settings = client.notification_settings().await;
        settings
            .set_push_rule_enabled(RuleKind::Override, PredefinedOverrideRuleId::Master.as_str(), !enabled)
            .await?;
        self.report_account_notifications(&settings).await;
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
    /// The join itself is bounded: a room on a far server may take a while,
    /// but the SDK alone retried a refusal for as long as the app ran.
    pub(crate) async fn join_by_id_or_alias(&self, id: &str, via: &Option<Vec<String>>) -> Result<()> {
        let client = self.client().await?;
        let id = RoomOrAliasId::parse(id.trim())?;
        let mut servers = spaces::servers(via);
        let target = Self::look_up_briefly(&client, &id, &mut servers).await?;
        let room = tokio::time::timeout(JOIN_TIMEOUT, client.join_room_by_id_or_alias(&target, &servers))
            .await
            .map_err(|_| anyhow!("joining {id}: your server did not finish the join in time"))??;
        self.emit(json!({"ev": "joined", "room_id": room.room_id().as_str()}));
        Ok(())
    }

    /// An address as its room ID, with the servers that know it added to
    /// `servers`; a room ID as it is. With few retries: the SDK's own
    /// lookup (in joining, previewing and knocking) retried a refusal
    /// without end (a server without federation answers 502), and the page
    /// said "Joining…" forever.
    pub(crate) async fn look_up_briefly(client: &Client, id: &RoomOrAliasId, servers: &mut Vec<OwnedServerName>) -> Result<OwnedRoomOrAliasId> {
        use matrix_sdk::ruma::api::client::alias::get_alias;
        let as_alias: Result<&RoomAliasId, &RoomId> = id.try_into();
        let Ok(alias) = as_alias else { return Ok(id.to_owned()) };
        let found = client
            .send(get_alias::v3::Request::new(alias.to_owned()))
            .with_request_config(matrix_sdk::config::RequestConfig::short_retry())
            .await
            .map_err(|e| anyhow!("{alias} could not be looked up from your server: {e}"))?;
        for s in found.servers {
            if !servers.contains(&s) {
                servers.push(s);
            }
        }
        Ok(found.room_id.into())
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
        // A rule that cannot be made is an error, not quietly "invite only"
        // (the audit found "knock_restricted" ignored and "restricted"
        // without a space turned into invite only)
        let space_rule = |knock: bool| -> Result<Option<JoinRule>> {
            let Some(p) = parent else { bail!("members of a space can join only a room in a space") };
            let allow = vec![AllowRule::room_membership(OwnedRoomId::try_from(p)?)];
            Ok(Some(if knock { JoinRule::KnockRestricted(Restricted::new(allow)) } else { JoinRule::Restricted(Restricted::new(allow)) }))
        };
        let rule = match join_rule {
            Some("knock") => Some(JoinRule::Knock),
            Some("restricted") => space_rule(false)?,
            Some("knock_restricted") => space_rule(true)?,
            Some("invite") | Some("public") | None => None,
            Some(other) => bail!("unknown join rule {other}"),
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
        let t = std::time::Instant::now();
        let room = self.room(room_id).await?;
        let members: Vec<Value> = room
            .members(RoomMemberships::ACTIVE | RoomMemberships::BAN)
            .await?
            .iter()
            .map(|m| {
                let power = match m.power_level() {
                    UserPowerLevel::Infinite => Value::from("infinite"),
                    UserPowerLevel::Int(i) => Value::from(i64::from(i)),
                    _ => Value::Null,
                };
                // join, invite, knock or ban: the page sorts them in sections
                json!({"user_id": m.user_id().as_str(), "name": m.name(), "power": power,
                       "role": super::moderation::role_of(m.power_level()),
                       "membership": m.membership().to_string()})
            })
            .collect();
        tracing::debug!(target: "rustrx::perf", members = members.len(), ms = t.elapsed().as_millis() as u64, "room_members");
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
        let data = tokio::fs::read(path).await?;
        let mime = mime_guess::from_path(path).first_or(mime::IMAGE_JPEG);
        let mut info = matrix_sdk::ruma::events::room::avatar::ImageInfo::new();
        info.width = w.and_then(|n| UInt::try_from(n).ok());
        info.height = h.and_then(|n| UInt::try_from(n).ok());
        info.size = UInt::try_from(data.len() as u64).ok();
        info.mimetype = Some(mime.to_string());
        room.upload_avatar(&mime, data, Some(info)).await?;
        tokio::fs::remove_file(path).await.ok();
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

    /// Rooms and spaces from one server's public directory (the own one
    /// without `server`), by part of their name, topic or address, for
    /// joining without the exact address. Answered by
    /// "room_search_results"; a server that will not answer gives none.
    pub(crate) async fn search_rooms(&self, term: &str, server: Option<&str>) -> Result<()> {
        let client = self.client().await?;
        let mut req = get_public_rooms_filtered::v3::Request::new();
        req.limit = Some(SEARCH_ROOMS_LIMIT.into());
        req.filter.generic_search_term = Some(term.to_owned());
        req.server = server.and_then(|s| OwnedServerName::try_from(s.trim()).ok());
        // spaces too: the directory leaves them out unless asked
        req.filter.room_types = vec![RoomTypeFilter::Default, RoomTypeFilter::Space];
        let rooms: Vec<Value> = client
            .public_rooms_filtered(req)
            .await
            .map(|r| r.chunk)
            .unwrap_or_else(|e| {
                tracing::info!("room directory search: {e}");
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
                    "avatar_mxc": r.avatar_url.map(|u| u.to_string()),
                    "is_space": r.room_type == Some(RoomType::Space),
                    "joined": joined,
                })
            })
            .collect();
        self.emit(json!({"ev": "room_search_results", "term": term, "server": server, "rooms": rooms}));
        Ok(())
    }

    /// The whole room list once more: "rooms".
    pub(crate) async fn refresh_rooms(&self) -> Result<()> {
        let t = std::time::Instant::now();
        let client = self.client().await?;
        let dir = self.data_dir.join(AVATAR_DIR);
        let settings = client.notification_settings().await;
        let listed: Vec<Room> = client.rooms().into_iter().filter(crate::serialize::listed).collect();
        let rooms: Vec<Value> = super::sync::build_rows(listed, &dir, &settings, &HashMap::new())
            .await
            .into_iter()
            .map(|(_, row)| row)
            .collect();
        tracing::debug!(target: "rustrx::perf", full = true, rooms = rooms.len(), rebuilt = rooms.len(), reused = 0,
                        refresh = true, build_us = t.elapsed().as_micros() as u64, "rooms_build");
        self.emit(json!({"ev": "rooms", "rooms": sorted_rooms(rooms)}));
        Ok(())
    }

    /// One person, for their page: the name, whether we verified them,
    /// whether we ignore them, and in a room their power and what ours
    /// lets us do to them: "user_info".
    pub(crate) async fn user_info(&self, user_id: &str, room_id: Option<&str>) -> Result<()> {
        let client = self.client().await?;
        let user = UserId::parse(user_id.trim())?;
        let identity = client.encryption().get_user_identity(&user).await.ok().flatten();
        let ignored = client.subscribe_to_ignore_user_list_changes().get().iter().any(|u| u == user.as_str());
        let mut info = json!({
            "ev": "user_info", "user_id": user.as_str(),
            "has_identity": identity.is_some(),
            "verified": identity.as_ref().map(|i| i.is_verified()).unwrap_or(false),
            "ignored": ignored,
            "is_self": client.user_id() == Some(&*user),
        });
        let power = |p: UserPowerLevel| match p {
            UserPowerLevel::Infinite => i64::MAX,
            UserPowerLevel::Int(i) => i64::from(i),
            _ => 0,
        };
        if let Some(room) = room_id.and_then(|r| RoomId::parse(r).ok()).and_then(|r| client.get_room(&r)) {
            info["room_id"] = json!(room.room_id().as_str());
            if let Ok(Some(member)) = room.get_member_no_sync(&user).await {
                // the levels as this device last set them, where the sync
                // has not brought that yet (moderation.rs): a changed role
                // shows at once
                let levels = self.effective_levels(&room).await.ok();
                let level_of = |u: &UserId, synced: UserPowerLevel| levels.as_ref().map_or(synced, |l| l.for_user(u));
                let theirs = level_of(&user, member.power_level());
                info["name"] = json!(member.name());
                info["membership"] = json!(member.membership().to_string());
                info["power"] = json!(power(theirs));
                info["role"] = json!(super::moderation::role_of(theirs));
                if let Some(own) = client.user_id() {
                    if let Ok(Some(me)) = room.get_member_no_sync(own).await {
                        let mine = level_of(own, me.power_level());
                        // the server also wants our power above theirs
                        let above = power(mine) > power(theirs);
                        let (may_kick, may_ban) = match &levels {
                            Some(l) => (l.user_can_kick(own), l.user_can_ban(own)),
                            None => (me.can_kick(), me.can_ban()),
                        };
                        info["can_kick"] = json!(may_kick && above);
                        info["can_ban"] = json!(may_ban && above);
                        // their role from ours: only below our own level, and
                        // ourselves only downwards (it cannot be undone)
                        info["can_set_level"] = json!(levels.as_ref().is_some_and(|l| l.user_can_send_state(own, matrix_sdk::ruma::events::StateEventType::RoomPowerLevels))
                            && (above || client.user_id() == Some(&*user))
                            && !matches!(theirs, UserPowerLevel::Infinite));
                        info["own_level"] = json!(power(mine));
                        info["can_mute"] = json!(levels.as_ref().is_some_and(|l| l.user_can_send_state(own, matrix_sdk::ruma::events::StateEventType::RoomPowerLevels)) && above);
                        info["events_default"] = json!(levels.as_ref().map(|l| i64::from(l.events_default)));
                    }
                }
            }
        }
        if info.get("name").is_none() {
            let name = client.account().fetch_user_profile_of(&user).await.ok()
                .and_then(|p| p.get("displayname").and_then(Value::as_str).map(str::to_owned));
            info["name"] = json!(name);
        }
        self.emit(info);
        Ok(())
    }

    /// Out of the room (kick), out and kept out (ban), or let back (unban);
    /// the room's member list and the person's page follow.
    pub(crate) async fn moderate(&self, action: &str, room_id: &str, user_id: &str, reason: Option<&str>) -> Result<()> {
        let room = self.room(room_id).await?;
        let user = UserId::parse(user_id.trim())?;
        match action {
            "kick" => room.kick_user(&user, reason).await?,
            "ban" => room.ban_user(&user, reason).await?,
            "unban" => room.unban_user(&user, reason).await?,
            other => bail!("no such action {other}"),
        }
        self.user_info(user_id, Some(room_id)).await
    }
}
