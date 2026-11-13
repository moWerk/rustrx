//! Commands from the UI: parsing and dispatch.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

impl Core {
    pub(crate) fn dispatch(&'static self, v: Value) {
        let cmd = v.get("cmd").and_then(Value::as_str).unwrap_or("").to_owned();
        let room = v.get("room_id").and_then(Value::as_str).unwrap_or("").to_owned();
        tracing::info!(%cmd, %room, "command");
        // the shape is checked before anything runs (protocol.rs)
        let command: Command = match serde_json::from_value(v) {
            Ok(c) => c,
            Err(e) => {
                self.error(&cmd, &anyhow!("{e}"));
                return;
            }
        };
        self.rt.spawn(async move {
            if let Err(e) = self.handle(command).await {
                self.error(&cmd, &e);
            }
        });
    }

    pub(crate) async fn client(&self) -> Result<Client> {
        self.state.lock().await.client.clone().context("not logged in")
    }

    pub(crate) async fn room(&self, id: &str) -> Result<Room> {
        let client = self.client().await?;
        client.get_room(&RoomId::parse(id)?).with_context(|| format!("unknown room {id}"))
    }

    /// A timeline item by event id, or by transaction id for a local echo.
    fn item_id(event_id: Option<String>, txn_id: Option<String>) -> Result<TimelineEventItemId> {
        if let Some(txn) = given(&txn_id) {
            return Ok(TimelineEventItemId::TransactionId(txn.into()));
        }
        let id = given(&event_id).context("missing field `event_id`")?;
        Ok(TimelineEventItemId::EventId(EventId::parse(id)?))
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

    pub(crate) async fn handle(&'static self, command: Command) -> Result<()> {
        use Command::*;
        match command {
            ProbeServer { user, homeserver } => self.probe_server(user, homeserver).await,
            LoginPassword { user, password, homeserver } => self.login_password(user, password, homeserver).await,
            LoginSso { user, homeserver, idp } => self.login_sso(user, homeserver, idp).await,
            Logout => self.logout().await,
            OpenRoom { room_id } => self.open_room(&room_id).await,
            CloseRoom => {
                let mut st = self.state.lock().await;
                if let Some(open) = st.open.take() {
                    open.task.abort();
                }
                Ok(())
            }
            PaginateBack { room_id, count } => {
                let room = self.room(&room_id).await?;
                let n = count.unwrap_or(30);
                let tl = self.timeline_for(&room).await?;
                let more = tl.paginate_backwards(n).await?;
                self.emit(json!({"ev": "paginated", "room_id": room.room_id().as_str(), "at_start": more}));
                Ok(())
            }
            SendText { room_id, body, reply_to, markdown } => {
                let room = self.room(&room_id).await?;
                let tl = self.timeline_for(&room).await?;
                let content = if markdown.unwrap_or(true) {
                    RoomMessageEventContent::text_markdown(&body)
                } else {
                    RoomMessageEventContent::text_plain(&body)
                };
                match given(&reply_to) {
                    Some(reply) => {
                        let without: RoomMessageEventContentWithoutRelation = content.into();
                        tl.send_reply(without, EventId::parse(reply)?).await?;
                    }
                    None => {
                        tl.send(content.into()).await?;
                    }
                }
                Ok(())
            }
            SendFile { room_id, path, caption, w, h, thumb_path, thumb_w, thumb_h, voice, duration_ms } => {
                let room = self.room(&room_id).await?;
                let path = PathBuf::from(path);
                let mime = mime_guess::from_path(&path).first_or_octet_stream();
                let tl = self.timeline_for(&room).await?;
                let dim = |n: Option<u64>| n.and_then(|n| UInt::try_from(n).ok());
                let mut config = AttachmentConfig::default();
                if let (Some(w), Some(h)) = (dim(w), dim(h)) {
                    let size = std::fs::metadata(&path).ok().and_then(|m| UInt::try_from(m.len()).ok());
                    config.info = Some(AttachmentInfo::Image(BaseImageInfo {
                        width: Some(w),
                        height: Some(h),
                        size,
                        ..Default::default()
                    }));
                }
                if let (Some(tp), Some(w), Some(h)) = (thumb_path, dim(thumb_w), dim(thumb_h)) {
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
                if let Some(caption) = given(&caption) {
                    config.caption = Some(TextMessageEventContent::markdown(caption));
                }
                // a voice message: other clients show it as one (a player, not a file)
                if voice.unwrap_or(false) {
                    let duration = duration_ms.map(Duration::from_millis);
                    let size = std::fs::metadata(&path).ok().and_then(|m| UInt::try_from(m.len()).ok());
                    config.info = Some(AttachmentInfo::Voice(BaseAudioInfo { duration, size, ..Default::default() }));
                }
                // through the send queue: the message shows at once as a local
                // echo with its upload progress, instead of only once uploaded
                tl.send_attachment(path, mime, config).use_send_queue().await?;
                Ok(())
            }
            React { room_id, event_id, key } => {
                let room = self.room(&room_id).await?;
                let tl = self.timeline_for(&room).await?;
                let id = TimelineEventItemId::EventId(EventId::parse(&event_id)?);
                tl.toggle_reaction(&id, &key).await?;
                Ok(())
            }
            Redact { room_id, event_id, txn_id, reason } => {
                // a sent event is redacted; a local echo (txn_id) is discarded
                let room = self.room(&room_id).await?;
                let tl = self.timeline_for(&room).await?;
                tl.redact(&Self::item_id(event_id, txn_id)?, given(&reason)).await?;
                Ok(())
            }
            Edit { room_id, event_id, txn_id, body } => {
                let room = self.room(&room_id).await?;
                let tl = self.timeline_for(&room).await?;
                let content: RoomMessageEventContentWithoutRelation =
                    RoomMessageEventContent::text_markdown(&body).into();
                tl.edit(&Self::item_id(event_id, txn_id)?, EditedContent::RoomMessage(content)).await?;
                Ok(())
            }
            PollVote { room_id, event_id, answers } => {
                let room = self.room(&room_id).await?;
                let poll = EventId::parse(&event_id)?;
                room.send(UnstablePollResponseEventContent::new(answers, poll)).await?;
                Ok(())
            }
            StorageInfo => self.storage_info().await,
            CallStart { room_id, voice } => self.call_start(&room_id, voice.unwrap_or(false)).await,
            CallMessage { data } => self.call_message(&data).await,
            CallEnd => {
                self.call_end().await;
                Ok(())
            }
            ClearMedia => {
                // the decrypted copies this app keeps, and the SDK's media cache
                for dir in ["media"] {
                    std::fs::remove_dir_all(self.data_dir.join(dir)).ok();
                    std::fs::create_dir_all(self.data_dir.join(dir)).ok();
                }
                let client = self.client().await?;
                client.media().clean().await?;
                self.storage_info().await
            }
            Report { room_id, event_id, reason } => {
                let room = self.room(&room_id).await?;
                let event_id = EventId::parse(&event_id)?;
                room.report_content(event_id, given(&reason).map(str::to_owned)).await?;
                Ok(())
            }
            GetNotificationMode { room_id } => {
                let room = self.room(&room_id).await?;
                self.notification_mode(&room).await
            }
            SetNotificationMode { room_id, mode } => {
                let room = self.room(&room_id).await?;
                let client = self.client().await?;
                let settings = client.notification_settings().await;
                match mode.as_str() {
                    "all" => settings.set_room_notification_mode(room.room_id(), RoomNotificationMode::AllMessages).await?,
                    "mentions" => settings
                        .set_room_notification_mode(room.room_id(), RoomNotificationMode::MentionsAndKeywordsOnly)
                        .await?,
                    "mute" => settings.set_room_notification_mode(room.room_id(), RoomNotificationMode::Mute).await?,
                    _ => settings.delete_user_defined_room_rules(room.room_id()).await?,
                }
                self.notification_mode(&room).await
            }
            ExportKeys { path, passphrase } => {
                let client = self.client().await?;
                let path = PathBuf::from(path);
                client.encryption().export_room_keys(path.clone(), &passphrase, |_| true).await?;
                self.emit(json!({"ev": "keys_exported", "path": path.to_string_lossy()}));
                Ok(())
            }
            ImportKeys { path, passphrase } => {
                let client = self.client().await?;
                let result = client
                    .encryption()
                    .import_room_keys(PathBuf::from(path), &passphrase)
                    .await?;
                self.emit(json!({"ev": "keys_imported", "imported": result.imported_count, "total": result.total_count}));
                Ok(())
            }
            CreateRoom { name, topic, public, alias, encrypted } => {
                let client = self.client().await?;
                let mut req = create_room::v3::Request::new();
                req.name = given(&name).map(str::to_owned);
                req.topic = given(&topic).map(str::to_owned);
                let public = public.unwrap_or(false);
                req.preset = Some(if public { create_room::v3::RoomPreset::PublicChat } else { create_room::v3::RoomPreset::PrivateChat });
                if public {
                    req.visibility = Visibility::Public;
                    req.room_alias_name = given(&alias).map(str::to_owned);
                }
                if encrypted.unwrap_or(!public) {
                    let content = RoomEncryptionEventContent::with_recommended_defaults();
                    req.initial_state = vec![InitialStateEvent::with_empty_state_key(content).to_raw_any()];
                }
                let room = client.create_room(req).await?;
                self.emit(json!({"ev": "joined", "room_id": room.room_id().as_str()}));
                Ok(())
            }
            MarkRead { room_id, private } => {
                let room = self.room(&room_id).await?;
                // the open room's timeline knows its newest item; any other room
                // gets the receipt on its latest event (a throwaway timeline is
                // empty and marked nothing, so "Mark as read" in the list did nothing)
                let is_open = {
                    let st = self.state.lock().await;
                    st.open.as_ref().map(|o| o.room_id == room.room_id()).unwrap_or(false)
                };
                // a private receipt moves our marker without telling the room
                let kind = if private.unwrap_or(false) {
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
            Typing { room_id, typing } => {
                let room = self.room(&room_id).await?;
                room.typing_notice(typing.unwrap_or(false)).await?;
                Ok(())
            }
            GetMedia { room_id, event_id, thumb, dest_dir } => {
                self.get_media(&room_id, &event_id, thumb.unwrap_or(false), dest_dir).await
            }
            SetFavourite { room_id, value } => {
                let room = self.room(&room_id).await?;
                room.set_is_favourite(value.unwrap_or(true), None).await?;
                Ok(())
            }
            SetLowPriority { room_id, value } => {
                let room = self.room(&room_id).await?;
                room.set_is_low_priority(value.unwrap_or(true), None).await?;
                Ok(())
            }
            JoinRoom { room_id } => {
                let room = self.room(&room_id).await?;
                room.join().await?;
                Ok(())
            }
            LeaveRoom { room_id } => {
                let room = self.room(&room_id).await?;
                room.leave().await?;
                Ok(())
            }
            Search { term } => {
                // People from the user directory, rooms from the public
                // directory of the own server; both are best effort.
                let client = self.client().await?;
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
            JoinByIdOrAlias { id } => {
                let client = self.client().await?;
                let id = RoomOrAliasId::parse(id.trim())?;
                let room = client.join_room_by_id_or_alias(&id, &[]).await?;
                self.emit(json!({"ev": "joined", "room_id": room.room_id().as_str()}));
                Ok(())
            }
            CreateDm { user_id } => {
                let client = self.client().await?;
                let user = UserId::parse(user_id.trim())?;
                let room = match client.get_dm_room(&user) {
                    Some(r) => r,
                    None => client.create_dm(&user).await?,
                };
                self.emit(json!({"ev": "dm_ready", "room_id": room.room_id().as_str(), "user_id": user.as_str()}));
                Ok(())
            }
            RoomMembers { room_id } => {
                let room = self.room(&room_id).await?;
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
            SetRoomName { room_id, name } => {
                let room = self.room(&room_id).await?;
                room.set_name(name).await?;
                Ok(())
            }
            SetRoomTopic { room_id, topic } => {
                let room = self.room(&room_id).await?;
                room.set_room_topic(&topic).await?;
                Ok(())
            }
            Invite { room_id, user_id } => {
                let room = self.room(&room_id).await?;
                room.invite_user_by_id(&UserId::parse(user_id.trim())?).await?;
                Ok(())
            }
            VerificationRequestSelf => {
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
            VerificationAccept { flow_id } => {
                let req = self.request(&flow_id).await?;
                req.accept().await?;
                Ok(())
            }
            VerificationStartSas { flow_id } => {
                let req = self.request(&flow_id).await?;
                req.start_sas().await?;
                Ok(())
            }
            VerificationConfirm { flow_id } => {
                let sas = self.sas(&flow_id).await?;
                sas.confirm().await?;
                Ok(())
            }
            VerificationMismatch { flow_id } => {
                let sas = self.sas(&flow_id).await?;
                sas.mismatch().await?;
                Ok(())
            }
            VerificationCancel { flow_id } => {
                let req = self.request(&flow_id).await?;
                req.cancel().await?;
                Ok(())
            }
            RecoveryRecover { key } => {
                let client = self.client().await?;
                client.encryption().recovery().recover(key.trim()).await?;
                self.emit(json!({"ev": "recovery_done"}));
                Ok(())
            }
            RecoveryEnable => {
                let client = self.client().await?;
                let key = client.encryption().recovery().enable().wait_for_backups_to_upload().await?;
                self.emit(json!({"ev": "recovery_key", "key": key}));
                Ok(())
            }
            RecoveryReset => {
                let client = self.client().await?;
                let key = client.encryption().recovery().reset_key().await?;
                self.emit(json!({"ev": "recovery_key", "key": key}));
                Ok(())
            }
            Devices => {
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
            DeleteDevice { device_id, password } => self.delete_device(device_id, password).await,
            Profile => {
                let client = self.client().await?;
                let name = client.account().get_display_name().await?;
                self.emit(json!({"ev": "profile", "display_name": name,
                                 "user_id": client.user_id().map(|u| u.to_string())}));
                Ok(())
            }
            SetDisplayName { name } => {
                let client = self.client().await?;
                client.account().set_display_name(Some(&name)).await?;
                Ok(())
            }
            IgnoreUser { user_id } => {
                let client = self.client().await?;
                client.account().ignore_user(&UserId::parse(user_id.trim())?).await?;
                Ok(())
            }
            UnignoreUser { user_id } => {
                let client = self.client().await?;
                client.account().unignore_user(&UserId::parse(user_id.trim())?).await?;
                Ok(())
            }
            RefreshRooms => {
                let client = self.client().await?;
                let dir = self.data_dir.join("avatars");
                let mut rooms = Vec::new();
                for room in client.rooms().into_iter().filter(crate::serialize::listed) {
                    rooms.push(room_json(&room, &dir).await);
                }
                self.emit(json!({"ev": "rooms", "rooms": sorted_rooms(rooms)}));
                Ok(())
            }
        }
    }
}
