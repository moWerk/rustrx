//! Commands from the UI: parsing and dispatch.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

impl Core {
    pub(crate) fn dispatch(&'static self, v: Value) {
        let cmd = v.get("cmd").and_then(Value::as_str).unwrap_or("").to_owned();
        let room = v.get("room_id").and_then(Value::as_str).unwrap_or("").to_owned();
        tracing::info!(%cmd, %room, "command");
        self.rt.spawn(async move {
            if let Err(e) = self.handle(&cmd, &v).await {
                self.error(&cmd, &e);
            }
        });
    }

    pub(crate) async fn client(&self) -> Result<Client> {
        self.state.lock().await.client.clone().context("not logged in")
    }

    pub(crate) async fn room(&self, v: &Value) -> Result<Room> {
        let id = s(v, "room_id")?;
        let client = self.client().await?;
        client.get_room(&RoomId::parse(&id)?).with_context(|| format!("unknown room {id}"))
    }

    /// The open room's timeline when it matches, else a throwaway one.
    pub(crate) async fn timeline_for(&self, room: &Room) -> Result<Arc<Timeline>> {
        if let Some(open) = &self.state.lock().await.open {
            if open.room_id == room.room_id() {
                return Ok(open.timeline.clone());
            }
        }
        Ok(Arc::new(room.timeline().await?))
    }

    pub(crate) async fn handle(&'static self, cmd: &str, v: &Value) -> Result<()> {
        match cmd {
            "probe_server" => self.probe_server(v).await,
            "login_password" => self.login_password(v).await,
            "login_sso" => self.login_sso(v).await,
            "logout" => self.logout().await,
            "open_room" => self.open_room(v).await,
            "close_room" => {
                let mut st = self.state.lock().await;
                if let Some(open) = st.open.take() {
                    open.task.abort();
                }
                Ok(())
            }
            "paginate_back" => {
                let room = self.room(v).await?;
                let n = v.get("count").and_then(Value::as_u64).unwrap_or(30) as u16;
                let tl = self.timeline_for(&room).await?;
                let more = tl.paginate_backwards(n).await?;
                self.emit(json!({"ev": "paginated", "room_id": room.room_id().as_str(), "at_start": more}));
                Ok(())
            }
            "send_text" => {
                let room = self.room(v).await?;
                let body = s(v, "body")?;
                let tl = self.timeline_for(&room).await?;
                let content = if v.get("markdown").and_then(Value::as_bool).unwrap_or(true) {
                    RoomMessageEventContent::text_markdown(&body)
                } else {
                    RoomMessageEventContent::text_plain(&body)
                };
                match opt(v, "reply_to") {
                    Some(reply) => {
                        let without: RoomMessageEventContentWithoutRelation = content.into();
                        tl.send_reply(without, EventId::parse(&reply)?).await?;
                    }
                    None => {
                        tl.send(content.into()).await?;
                    }
                }
                Ok(())
            }
            "send_file" => {
                let room = self.room(v).await?;
                let path = PathBuf::from(s(v, "path")?);
                let mime = mime_guess::from_path(&path).first_or_octet_stream();
                let tl = self.timeline_for(&room).await?;
                let dim = |key: &str| v.get(key).and_then(Value::as_u64).and_then(|n| UInt::try_from(n).ok());
                let mut config = AttachmentConfig::default();
                if let (Some(w), Some(h)) = (dim("w"), dim("h")) {
                    let size = std::fs::metadata(&path).ok().and_then(|m| UInt::try_from(m.len()).ok());
                    config.info = Some(AttachmentInfo::Image(BaseImageInfo {
                        width: Some(w),
                        height: Some(h),
                        size,
                        ..Default::default()
                    }));
                }
                if let (Some(tp), Some(w), Some(h)) = (opt(v, "thumb_path"), dim("thumb_w"), dim("thumb_h")) {
                    if let Ok(data) = std::fs::read(&tp) {
                        let size = UInt::try_from(data.len() as u64).unwrap_or_default();
                        config.thumbnail = Some(Thumbnail {
                            data,
                            content_type: mime::IMAGE_JPEG,
                            width: w,
                            height: h,
                            size,
                        });
                    }
                    let _ = std::fs::remove_file(&tp);
                }
                if let Some(caption) = opt(v, "caption") {
                    config.caption = Some(TextMessageEventContent::markdown(caption));
                }
                // a voice message: other clients show it as one (a player, not a file)
                if v.get("voice").and_then(Value::as_bool).unwrap_or(false) {
                    let duration = v.get("duration_ms").and_then(Value::as_u64).map(Duration::from_millis);
                    let size = std::fs::metadata(&path).ok().and_then(|m| UInt::try_from(m.len()).ok());
                    config.info = Some(AttachmentInfo::Voice(BaseAudioInfo { duration, size, ..Default::default() }));
                }
                tl.send_attachment(path, mime, config).await?;
                Ok(())
            }
            "react" => {
                let room = self.room(v).await?;
                let tl = self.timeline_for(&room).await?;
                let id = TimelineEventItemId::EventId(EventId::parse(s(v, "event_id")?)?);
                tl.toggle_reaction(&id, &s(v, "key")?).await?;
                Ok(())
            }
            "redact" => {
                // a sent event is redacted; a local echo (txn_id) is discarded
                let room = self.room(v).await?;
                let tl = self.timeline_for(&room).await?;
                tl.redact(&Self::item_id(v)?, opt(v, "reason").as_deref()).await?;
                Ok(())
            }
            "edit" => {
                let room = self.room(v).await?;
                let tl = self.timeline_for(&room).await?;
                let body = s(v, "body")?;
                let content: RoomMessageEventContentWithoutRelation =
                    RoomMessageEventContent::text_markdown(&body).into();
                tl.edit(&Self::item_id(v)?, EditedContent::RoomMessage(content)).await?;
                Ok(())
            }
            "poll_vote" => {
                let room = self.room(v).await?;
                let poll = EventId::parse(s(v, "event_id")?)?;
                let answers: Vec<String> = v
                    .get("answers")
                    .and_then(Value::as_array)
                    .map(|a| a.iter().filter_map(|x| x.as_str().map(str::to_owned)).collect())
                    .unwrap_or_default();
                room.send(UnstablePollResponseEventContent::new(answers, poll)).await?;
                Ok(())
            }
            "storage_info" => self.storage_info().await,
            "clear_media" => {
                // the decrypted copies this app keeps, and the SDK's media cache
                for dir in ["media"] {
                    std::fs::remove_dir_all(self.data_dir.join(dir)).ok();
                    std::fs::create_dir_all(self.data_dir.join(dir)).ok();
                }
                let client = self.client().await?;
                client.media().clean().await?;
                self.storage_info().await
            }
            "report" => {
                let room = self.room(v).await?;
                let event_id = EventId::parse(s(v, "event_id")?)?;
                room.report_content(event_id, opt(v, "reason")).await?;
                Ok(())
            }
            "get_notification_mode" => {
                let room = self.room(v).await?;
                self.notification_mode(&room).await
            }
            "set_notification_mode" => {
                let room = self.room(v).await?;
                let client = self.client().await?;
                let settings = client.notification_settings().await;
                match s(v, "mode")?.as_str() {
                    "all" => settings.set_room_notification_mode(room.room_id(), RoomNotificationMode::AllMessages).await?,
                    "mentions" => settings
                        .set_room_notification_mode(room.room_id(), RoomNotificationMode::MentionsAndKeywordsOnly)
                        .await?,
                    "mute" => settings.set_room_notification_mode(room.room_id(), RoomNotificationMode::Mute).await?,
                    _ => settings.delete_user_defined_room_rules(room.room_id()).await?,
                }
                self.notification_mode(&room).await
            }
            "export_keys" => {
                let client = self.client().await?;
                let path = PathBuf::from(s(v, "path")?);
                client.encryption().export_room_keys(path.clone(), &s(v, "passphrase")?, |_| true).await?;
                self.emit(json!({"ev": "keys_exported", "path": path.to_string_lossy()}));
                Ok(())
            }
            "import_keys" => {
                let client = self.client().await?;
                let result = client
                    .encryption()
                    .import_room_keys(PathBuf::from(s(v, "path")?), &s(v, "passphrase")?)
                    .await?;
                self.emit(json!({"ev": "keys_imported", "imported": result.imported_count, "total": result.total_count}));
                Ok(())
            }
            "create_room" => {
                let client = self.client().await?;
                let mut req = create_room::v3::Request::new();
                req.name = opt(v, "name");
                req.topic = opt(v, "topic");
                let public = v.get("public").and_then(Value::as_bool).unwrap_or(false);
                req.preset = Some(if public { create_room::v3::RoomPreset::PublicChat } else { create_room::v3::RoomPreset::PrivateChat });
                if public {
                    req.visibility = Visibility::Public;
                    req.room_alias_name = opt(v, "alias");
                }
                if v.get("encrypted").and_then(Value::as_bool).unwrap_or(!public) {
                    let content = RoomEncryptionEventContent::with_recommended_defaults();
                    req.initial_state = vec![InitialStateEvent::with_empty_state_key(content).to_raw_any()];
                }
                let room = client.create_room(req).await?;
                self.emit(json!({"ev": "joined", "room_id": room.room_id().as_str()}));
                Ok(())
            }
            "mark_read" => {
                let room = self.room(v).await?;
                // the open room's timeline knows its newest item; any other room
                // gets the receipt on its latest event (a throwaway timeline is
                // empty and marked nothing, so "Mark as read" in the list did nothing)
                let is_open = {
                    let st = self.state.lock().await;
                    st.open.as_ref().map(|o| o.room_id == room.room_id()).unwrap_or(false)
                };
                // a private receipt moves our marker without telling the room
                let kind = if v.get("private").and_then(Value::as_bool).unwrap_or(false) {
                    ReceiptType::ReadPrivate
                } else {
                    ReceiptType::Read
                };
                if is_open {
                    let tl = self.timeline_for(&room).await?;
                    tl.mark_as_read(kind).await?;
                } else if let Some(event_id) = crate::serialize::latest_event_id(&room) {
                    room.send_single_receipt(kind, ReceiptThread::Unthreaded, event_id).await?;
                }
                let _ = room.set_unread_flag(false).await;
                Ok(())
            }
            "typing" => {
                let room = self.room(v).await?;
                room.typing_notice(v.get("typing").and_then(Value::as_bool).unwrap_or(false)).await?;
                Ok(())
            }
            "get_media" => self.get_media(v).await,
            "set_favourite" => {
                let room = self.room(v).await?;
                room.set_is_favourite(v.get("value").and_then(Value::as_bool).unwrap_or(true), None).await?;
                Ok(())
            }
            "set_low_priority" => {
                let room = self.room(v).await?;
                room.set_is_low_priority(v.get("value").and_then(Value::as_bool).unwrap_or(true), None).await?;
                Ok(())
            }
            "join_room" => {
                let room = self.room(v).await?;
                room.join().await?;
                Ok(())
            }
            "leave_room" => {
                let room = self.room(v).await?;
                room.leave().await?;
                Ok(())
            }
            "search" => {
                // People from the user directory, rooms from the public
                // directory of the own server; both are best effort.
                let client = self.client().await?;
                let term = s(v, "term")?;
                let mut req = get_public_rooms_filtered::v3::Request::new();
                req.limit = Some(uint!(20));
                req.filter.generic_search_term = Some(term.clone());
                let (users, rooms) =
                    tokio::join!(client.search_users(&term, 10), client.public_rooms_filtered(req));
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
                        let joined = client
                            .get_room(&r.room_id)
                            .is_some_and(|room| room.state() == RoomState::Joined);
                        json!({
                            "room_id": r.room_id.as_str(),
                            "name": r.name,
                            "alias": r.canonical_alias.map(|a| a.to_string()),
                            "topic": r.topic,
                            "members": r.num_joined_members,
                            "joined": joined,
                        })
                    })
                    .collect();
                self.emit(json!({"ev": "search_results", "term": term, "users": users, "rooms": rooms}));
                Ok(())
            }
            "join_by_id_or_alias" => {
                let client = self.client().await?;
                let id = RoomOrAliasId::parse(s(v, "id")?)?;
                let room = client.join_room_by_id_or_alias(&id, &[]).await?;
                self.emit(json!({"ev": "joined", "room_id": room.room_id().as_str()}));
                Ok(())
            }
            "create_dm" => {
                let client = self.client().await?;
                let user = UserId::parse(s(v, "user_id")?)?;
                let room = match client.get_dm_room(&user) {
                    Some(r) => r,
                    None => client.create_dm(&user).await?,
                };
                self.emit(json!({"ev": "dm_ready", "room_id": room.room_id().as_str(), "user_id": user.as_str()}));
                Ok(())
            }
            "room_members" => {
                let room = self.room(v).await?;
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
            "set_room_name" => {
                let room = self.room(v).await?;
                room.set_name(s(v, "name")?).await?;
                Ok(())
            }
            "set_room_topic" => {
                let room = self.room(v).await?;
                room.set_room_topic(&s(v, "topic")?).await?;
                Ok(())
            }
            "invite" => {
                let room = self.room(v).await?;
                room.invite_user_by_id(&UserId::parse(s(v, "user_id")?)?).await?;
                Ok(())
            }
            "verification_request_self" => {
                let client = self.client().await?;
                let own = client.user_id().context("no user id")?.to_owned();
                let identity = client
                    .encryption()
                    .get_user_identity(&own)
                    .await?
                    .context("own identity not known yet; wait for the first sync")?;
                let req = identity.request_verification().await?;
                self.rt.spawn(self.watch_request(req));
                Ok(())
            }
            "verification_accept" => {
                let req = self.request(v).await?;
                req.accept().await?;
                Ok(())
            }
            "verification_start_sas" => {
                let req = self.request(v).await?;
                req.start_sas().await?;
                Ok(())
            }
            "verification_confirm" => {
                let sas = self.sas(v).await?;
                sas.confirm().await?;
                Ok(())
            }
            "verification_mismatch" => {
                let sas = self.sas(v).await?;
                sas.mismatch().await?;
                Ok(())
            }
            "verification_cancel" => {
                let req = self.request(v).await?;
                req.cancel().await?;
                Ok(())
            }
            "recovery_recover" => {
                let client = self.client().await?;
                client.encryption().recovery().recover(s(v, "key")?.trim()).await?;
                self.emit(json!({"ev": "recovery_done"}));
                Ok(())
            }
            "recovery_enable" => {
                let client = self.client().await?;
                let key = client.encryption().recovery().enable().wait_for_backups_to_upload().await?;
                self.emit(json!({"ev": "recovery_key", "key": key}));
                Ok(())
            }
            "recovery_reset" => {
                let client = self.client().await?;
                let key = client.encryption().recovery().reset_key().await?;
                self.emit(json!({"ev": "recovery_key", "key": key}));
                Ok(())
            }
            "devices" => {
                let client = self.client().await?;
                let own = client.device_id().map(|d| d.to_string());
                let devices: Vec<Value> = client
                    .devices()
                    .await?
                    .devices
                    .iter()
                    .map(|d| {
                        json!({
                            "device_id": d.device_id.as_str(),
                            "name": d.display_name,
                            "last_seen_ts": d.last_seen_ts.map(|t| u64::from(t.0)),
                            "last_seen_ip": d.last_seen_ip,
                            "current": Some(d.device_id.to_string()) == own,
                        })
                    })
                    .collect();
                self.emit(json!({"ev": "devices", "devices": devices}));
                Ok(())
            }
            "delete_device" => self.delete_device(v).await,
            "profile" => {
                let client = self.client().await?;
                let name = client.account().get_display_name().await?;
                self.emit(json!({"ev": "profile", "display_name": name,
                                 "user_id": client.user_id().map(|u| u.to_string())}));
                Ok(())
            }
            "set_display_name" => {
                let client = self.client().await?;
                client.account().set_display_name(Some(&s(v, "name")?)).await?;
                Ok(())
            }
            "ignore_user" => {
                let client = self.client().await?;
                client.account().ignore_user(&UserId::parse(s(v, "user_id")?)?).await?;
                Ok(())
            }
            "unignore_user" => {
                let client = self.client().await?;
                client.account().unignore_user(&UserId::parse(s(v, "user_id")?)?).await?;
                Ok(())
            }
            "refresh_rooms" => {
                let client = self.client().await?;
                let dir = self.data_dir.join("avatars");
                let mut rooms = Vec::new();
                for room in client.rooms().into_iter().filter(crate::serialize::listed) {
                    rooms.push(room_json(&room, &dir).await);
                }
                self.emit(json!({"ev": "rooms", "rooms": sorted_rooms(rooms)}));
                Ok(())
            }
            other => bail!("unknown command {other}"),
        }
    }
}
