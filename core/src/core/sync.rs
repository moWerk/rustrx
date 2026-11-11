//! The sync, the room list, avatars, the encryption state, storage sizes.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

impl Core {
    pub(crate) async fn start_sync(&'static self, client: Client) -> Result<()> {
        if matches!(client.sliding_sync_version(), Version::None) {
            let c = client.clone();
            let task = self.rt.spawn(async move {
                loop {
                    self.emit(json!({"ev": "sync_state", "state": "running"}));
                    if let Err(e) = c.sync(SyncSettings::default()).await {
                        tracing::warn!("sync ended: {e}");
                        self.emit(json!({"ev": "sync_state", "state": "error", "message": e.to_string()}));
                        tokio::time::sleep(Duration::from_secs(5)).await;
                    }
                }
            });
            self.state.lock().await.sync_task = Some(task);
        } else {
            // Offline mode: after a network failure the service probes the
            // server and resumes by itself. Without it an error is final and
            // the app silently shows stale rooms (the Wi-Fi drops did that).
            // 20 events per room instead of 1: the SDK counts unread messages
            // from the events it has seen, so a fresh login otherwise shows
            // at most the one event the list sync carried.
            let service = Arc::new(
                SyncService::builder(client.clone())
                    .with_offline_mode()
                    .with_room_list_timeline_limit(20)
                    .build()
                    .await?,
            );
            // Subscribe before starting and report the current value first:
            // the subscriber only wakes on changes, so a Running set before
            // we listened left the cover on "Connecting" (0.2.0).
            let mut states = service.state();
            service.start().await;
            let state_task = self.rt.spawn(async move {
                let mut current = Some(states.get());
                loop {
                    let state = match current.take() {
                        Some(s) => s,
                        None => match states.next().await {
                            Some(s) => s,
                            None => break,
                        },
                    };
                    let (name, message) = match &state {
                        SyncState::Idle => ("idle", None),
                        SyncState::Running => ("running", None),
                        SyncState::Terminated => ("terminated", None),
                        SyncState::Error(e) => ("error", Some(e.to_string())),
                        _ => ("offline", None),
                    };
                    tracing::info!(state = name, message, "sync service");
                    self.emit(json!({"ev": "sync_state", "state": name, "message": message}));
                }
            });
            let mut st = self.state.lock().await;
            st.sync_service = Some(service);
            st.login_tasks.push(state_task);
        }
        let enc = self.rt.spawn(self.encryption_task(client));
        self.state.lock().await.login_tasks.push(enc);
        Ok(())
    }

    /// Room list snapshots on every notable room change, coalesced.
    /// The room list: every room once, then only the rooms the update
    /// stream names are serialised again; a lagged stream or a minute of
    /// updates rebuilds the whole list (and catches what the stream missed).
    pub(crate) async fn rooms_task(&'static self, client: Client) {
        let mut rx = client.room_info_notable_update_receiver();
        let avatar_dir = self.data_dir.join("avatars");
        let t = std::time::Instant::now();
        let mut cache: HashMap<OwnedRoomId, Value> = HashMap::new();
        for room in client.rooms().into_iter().filter(crate::serialize::listed) {
            cache.insert(room.room_id().to_owned(), room_json(&room, &avatar_dir).await);
        }
        tracing::info!(ms = t.elapsed().as_millis() as u64, rooms = cache.len(), "startup: first room list");
        self.emit(json!({"ev": "rooms", "rooms": sorted_rooms(cache.values().cloned().collect())}));
        self.listen_latest(&client).await;
        let mut avatars_new = self.fetch_avatars(&client).await;
        let mut last_full = std::time::Instant::now();
        loop {
            let mut changed: std::collections::HashSet<OwnedRoomId> = std::collections::HashSet::new();
            // new pictures change rows the update stream does not name
            let mut full = std::mem::take(&mut avatars_new);
            if !full {
                match rx.recv().await {
                    Ok(update) => {
                        changed.insert(update.room_id);
                    }
                    Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => full = true,
                    Err(_) => break,
                }
            }
            tokio::time::sleep(Duration::from_millis(150)).await;
            loop {
                match rx.try_recv() {
                    Ok(update) => {
                        changed.insert(update.room_id);
                    }
                    Err(tokio::sync::broadcast::error::TryRecvError::Lagged(_)) => full = true,
                    Err(_) => break,
                }
            }
            if full || last_full.elapsed() > Duration::from_secs(60) {
                cache.clear();
                for room in client.rooms().into_iter().filter(crate::serialize::listed) {
                    cache.insert(room.room_id().to_owned(), room_json(&room, &avatar_dir).await);
                }
                last_full = std::time::Instant::now();
            } else {
                for id in &changed {
                    match client.get_room(id).filter(crate::serialize::listed) {
                        Some(room) => {
                            cache.insert(id.clone(), room_json(&room, &avatar_dir).await);
                        }
                        None => {
                            cache.remove(id);
                        }
                    }
                }
            }
            self.emit(json!({"ev": "rooms", "rooms": sorted_rooms(cache.values().cloned().collect())}));
            Self::log_counts(&client);
            self.listen_latest(&client).await;
            avatars_new = self.fetch_avatars(&client).await;
        }
    }

    /// One journal line per room with anything unread: the server's counts
    /// (nc notifications, hc highlights) next to the SDK's own (nu unread,
    /// nn notifying), to see which of the two a stale badge comes from.
    pub(crate) fn log_counts(client: &Client) {
        let mut parts = Vec::new();
        for room in client.rooms() {
            let c = room.unread_notification_counts();
            let (nu, nn) = (room.num_unread_messages(), room.num_unread_notifications());
            if c.notification_count > 0 || c.highlight_count > 0 || nu > 0 || nn > 0 {
                let name = room.cached_display_name().map(|n| n.to_string()).unwrap_or_default();
                parts.push(format!(
                    "{}:nc{}/hc{}/nu{}/nn{}",
                    name.chars().take(12).collect::<String>(),
                    c.notification_count,
                    c.highlight_count,
                    nu,
                    nn
                ));
            }
        }
        // room names: debug only, the journal is not the place for them
        tracing::debug!(rooms = %parts.join(" "), "counts");
    }

    /// The latest-event service is lazy: a room's preview is only computed
    /// once someone listens for it. Register every joined room, once.
    pub(crate) async fn listen_latest(&'static self, client: &Client) {
        let latest = client.latest_events().await;
        for room in client.rooms() {
            if room.state() != RoomState::Joined {
                continue;
            }
            if !self.state.lock().await.latest_listened.insert(room.room_id().to_owned()) {
                continue;
            }
            if let Err(e) = latest.listen_to_room(room.room_id()).await {
                tracing::info!(room = %room.room_id(), "latest event: {e}");
            }
        }
    }

    /// Cache the avatars that are missing, once per room, then resend the list.
    /// Returns whether a new picture arrived (the room list then needs it).
    pub(crate) async fn fetch_avatars(&'static self, client: &Client) -> bool {
        let avatar_dir = self.data_dir.join("avatars");
        let own = client.user_id().map(ToOwned::to_owned);
        let mut fetched = false;
        for room in client.rooms() {
            if avatar_file(&avatar_dir, &room).exists() {
                continue;
            }
            // Rooms without a known picture are looked at again next time:
            // a DM's peer may only become known after a later sync.
            let Some(url) = Self::avatar_source(&room, own.as_deref()).await else { continue };
            if !self.state.lock().await.avatars_tried.insert(room.room_id().to_owned()) {
                continue;
            }
            let request = MediaRequestParameters {
                source: MediaSource::Plain(url),
                format: MediaFormat::Thumbnail(MediaThumbnailSettings::new(uint!(192), uint!(192))),
            };
            match client.media().get_media_content(&request, true).await {
                Ok(bytes) => {
                    if tokio::fs::write(avatar_file(&avatar_dir, &room), &bytes).await.is_ok() {
                        fetched = true;
                    }
                }
                Err(e) => tracing::info!(room = %room.room_id(), "avatar: {e}"),
            }
        }
        fetched
    }

    /// The room's own picture, or for a direct chat the other person's.
    pub(crate) async fn avatar_source(room: &Room, own: Option<&UserId>) -> Option<OwnedMxcUri> {
        if let Some(url) = room.avatar_url() {
            return Some(url);
        }
        if !room.is_direct().await.unwrap_or(false) {
            return None;
        }
        let hero = room.heroes().await.into_iter().find(|h| Some(h.user_id.as_ref()) != own);
        if let Some(url) = hero.and_then(|h| h.avatar_url) {
            return Some(url);
        }
        let members = room.members_no_sync(RoomMemberships::ACTIVE).await.ok()?;
        members
            .into_iter()
            .find(|m| Some(m.user_id()) != own)
            .and_then(|m| m.avatar_url().map(ToOwned::to_owned))
    }

    pub(crate) async fn encryption_task(&'static self, client: Client) {
        let enc = client.encryption();
        let mut verification = enc.verification_state();
        let mut recovery = enc.recovery().state_stream();
        self.emit_recovery(enc.recovery().state());
        loop {
            tokio::select! {
                Some(v) = verification.next() => {
                    let name = match v {
                        VerificationState::Verified => "verified",
                        VerificationState::Unverified => "unverified",
                        VerificationState::Unknown => "unknown",
                    };
                    self.emit(json!({"ev": "verification_state", "state": name}));
                }
                Some(r) = recovery.next() => self.emit_recovery(r),
                else => break,
            }
        }
    }

    pub(crate) fn emit_recovery(&self, state: RecoveryState) {
        let name = match state {
            RecoveryState::Enabled => "enabled",
            RecoveryState::Disabled => "disabled",
            RecoveryState::Incomplete => "incomplete",
            RecoveryState::Unknown => "unknown",
        };
        self.emit(json!({"ev": "recovery_state", "state": name}));
    }

    pub(crate) async fn storage_info(&self) -> Result<()> {
        fn dir_size(p: &std::path::Path) -> u64 {
            std::fs::read_dir(p)
                .map(|rd| {
                    rd.filter_map(|e| e.ok())
                        .map(|e| match e.metadata() {
                            Ok(m) if m.is_dir() => dir_size(&e.path()),
                            Ok(m) => m.len(),
                            Err(_) => 0,
                        })
                        .sum()
                })
                .unwrap_or(0)
        }
        let client = self.client().await?;
        let sizes = client.get_store_sizes().await?;
        self.emit(json!({
            "ev": "storage",
            "store": dir_size(&self.data_dir.join("store")),
            "media_files": dir_size(&self.data_dir.join("media")) + dir_size(&self.data_dir.join("avatars")),
            "crypto": sizes.crypto_store,
            "state": sizes.state_store,
            "event_cache": sizes.event_cache_store,
            "media_cache": sizes.media_store,
        }));
        Ok(())
    }
}
