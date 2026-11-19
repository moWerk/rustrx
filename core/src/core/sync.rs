// SPDX-License-Identifier: GPL-3.0-or-later
//! The sync, the room list, avatars, the encryption state, storage sizes.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;
use std::collections::HashSet;

/// What the room list task remembers between passes, for one login (the
/// task is aborted on logout, and this goes with it).
#[derive(Default)]
pub(super) struct ListSeen {
    /// room and picture pairs tried once ("!room|mxc://..."); a new picture
    /// of the room is tried again
    avatars_tried: HashSet<String>,
    /// joined rooms whose latest event is listened for
    latest_listened: HashSet<OwnedRoomId>,
    /// rooms and spaces whose requests to join are watched
    pub(super) knocks_watched: HashSet<OwnedRoomId>,
}

impl Core {
    pub(crate) async fn start_sync(&'static self, client: Client) -> Result<()> {
        let generation = self.generation().await;
        if matches!(client.sliding_sync_version(), Version::None) {
            let c = client.clone();
            let task = self.rt.spawn(async move {
                loop {
                    self.emit(json!({"ev": "sync_state", "state": "running"}));
                    if let Err(e) = c.sync(SyncSettings::default()).await {
                        tracing::warn!("sync ended: {e}");
                        self.emit(json!({"ev": "sync_state", "state": "error", "message": e.to_string()}));
                        tokio::time::sleep(SYNC_RETRY_DELAY).await;
                    }
                }
            });
            // as below: a session that ended meanwhile does not get a sync
            // that nothing would stop (it reported errors into the next login)
            let mut st = self.state.lock().await;
            if st.generation != generation {
                drop(st);
                task.abort();
                return Ok(());
            }
            st.sync_task = Some(task);
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
                    .with_room_list_timeline_limit(ROOM_LIST_TIMELINE_LIMIT)
                    .build()
                    .await?,
            );
            // Subscribe before starting and report the current value first:
            // the subscriber only wakes on changes, so a Running set before
            // we listened left the cover on "Connecting" (0.2.0).
            let mut states = service.state();
            service.start().await;
            let queues = client.clone();
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
                    // the server is reachable again: rooms whose queue a
                    // network error switched off send what waits
                    if matches!(state, SyncState::Running) {
                        queues.send_queue().set_enabled(true).await;
                    }
                }
            });
            let mut st = self.state.lock().await;
            // the session ended while the service was being built: it is
            // not this login's any more, and nothing would ever stop it
            if st.generation != generation {
                drop(st);
                state_task.abort();
                service.stop().await;
                return Ok(());
            }
            st.sync_service = Some(service);
            st.login_tasks.push(state_task);
        }
        let enc = self.rt.spawn(self.encryption_task(client));
        let mut st = self.state.lock().await;
        if st.generation != generation {
            drop(st);
            enc.abort();
            return Ok(());
        }
        st.login_tasks.push(enc);
        Ok(())
    }

    /// Room list snapshots on every notable room change, coalesced.
    /// The room list: every room once, then only the rooms the update
    /// stream names are serialised again; a lagged stream or a minute of
    /// updates rebuilds the whole list (and catches what the stream missed).
    pub(crate) async fn rooms_task(&'static self, client: Client) {
        let mut rx = client.room_info_notable_update_receiver();
        let avatar_dir = self.data_dir.join(AVATAR_DIR);
        let t = std::time::Instant::now();
        let mut cache: HashMap<OwnedRoomId, Value> = HashMap::new();
        let mut seen = ListSeen::default();
        {
            // the push rules once per pass, not once per room
            let settings = client.notification_settings().await;
            for room in client.rooms().into_iter().filter(crate::serialize::listed) {
                cache.insert(room.room_id().to_owned(), room_json(&room, &avatar_dir, &settings).await);
            }
        }
        tracing::info!(ms = t.elapsed().as_millis() as u64, rooms = cache.len(), "startup: first room list");
        self.emit(json!({"ev": "rooms", "rooms": sorted_rooms(cache.values().cloned().collect())}));
        Self::listen_latest(&client, &mut seen).await;
        self.watch_knocks(&client, None, &mut seen).await;
        let mut avatars_new = self.fetch_avatars(&client, None, &mut seen).await;
        let mut last_full = std::time::Instant::now();
        loop {
            let mut changed: HashSet<OwnedRoomId> = HashSet::new();
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
            tokio::time::sleep(ROOM_LIST_BATCH).await;
            loop {
                match rx.try_recv() {
                    Ok(update) => {
                        changed.insert(update.room_id);
                    }
                    Err(tokio::sync::broadcast::error::TryRecvError::Lagged(_)) => full = true,
                    Err(_) => break,
                }
            }
            // a minute of updates: everything once more, to catch what the
            // stream missed
            if last_full.elapsed() > ROOM_LIST_FULL_REBUILD {
                full = true;
            }
            let settings = client.notification_settings().await;
            if full {
                cache.clear();
                for room in client.rooms().into_iter().filter(crate::serialize::listed) {
                    cache.insert(room.room_id().to_owned(), room_json(&room, &avatar_dir, &settings).await);
                }
                last_full = std::time::Instant::now();
            } else {
                for id in &changed {
                    match client.get_room(id).filter(crate::serialize::listed) {
                        Some(room) => {
                            cache.insert(id.clone(), room_json(&room, &avatar_dir, &settings).await);
                        }
                        None => {
                            cache.remove(id);
                        }
                    }
                }
            }
            self.emit(json!({"ev": "rooms", "rooms": sorted_rooms(cache.values().cloned().collect())}));
            Self::log_counts(&client);
            // only what changed is looked at again: on a full rebuild every
            // room, else the rooms the update named (the list's cost no
            // longer grows with the number of rooms on every receipt)
            let only = if full { None } else { Some(&changed) };
            let unknown = changed.iter().any(|id| !seen.latest_listened.contains(id));
            if unknown || full {
                Self::listen_latest(&client, &mut seen).await;
            }
            self.watch_knocks(&client, only, &mut seen).await;
            avatars_new = self.fetch_avatars(&client, only, &mut seen).await;
        }
    }

    /// One journal line per room with anything unread: the server's counts
    /// (nc notifications, hc highlights) next to the SDK's own (nu unread,
    /// nn notifying), to see which of the two a stale badge comes from.
    pub(crate) fn log_counts(client: &Client) {
        // the walk over every room only when the line is written
        if !tracing::enabled!(tracing::Level::DEBUG) {
            return;
        }
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
    async fn listen_latest(client: &Client, seen: &mut ListSeen) {
        let latest = client.latest_events().await;
        for room in client.rooms() {
            if room.state() != RoomState::Joined {
                continue;
            }
            if !seen.latest_listened.insert(room.room_id().to_owned()) {
                continue;
            }
            if let Err(e) = latest.listen_to_room(room.room_id()).await {
                tracing::info!(room = %room.room_id(), "latest event: {e}");
            }
        }
    }

    /// Cache the avatars that are missing or changed (the picture's mxc
    /// address is kept beside it, "<room>.url"), once per picture.
    /// Returns whether a new picture arrived (the room list then needs it).
    async fn fetch_avatars(&self, client: &Client, only: Option<&HashSet<OwnedRoomId>>, seen: &mut ListSeen) -> bool {
        let avatar_dir = self.data_dir.join(AVATAR_DIR);
        let own = client.user_id().map(ToOwned::to_owned);
        let mut fetched = false;
        for room in client.rooms() {
            if only.is_some_and(|o| !o.contains(room.room_id())) {
                continue;
            }
            // Rooms without a known picture are looked at again next time:
            // a DM's peer may only become known after a later sync.
            let Some(url) = Self::avatar_source(&room, own.as_deref()).await else { continue };
            let file = avatar_file(&avatar_dir, &room);
            let marker = file.with_extension("url");
            // the cached one is this picture (a file from before the marker
            // counts as current, as before)
            if file.exists() {
                match std::fs::read_to_string(&marker) {
                    Ok(known) if known.trim() != url.as_str() => {}
                    _ => continue,
                }
            }
            if !seen.avatars_tried.insert(format!("{}|{}", room.room_id(), url)) {
                continue;
            }
            let request = MediaRequestParameters {
                source: MediaSource::Plain(url.clone()),
                format: MediaFormat::Thumbnail(MediaThumbnailSettings::new(ROOM_AVATAR_PX.into(), ROOM_AVATAR_PX.into())),
            };
            match client.media().get_media_content(&request, true).await {
                Ok(bytes) => {
                    if tokio::fs::write(&file, &bytes).await.is_ok() {
                        tokio::fs::write(&marker, url.as_str()).await.ok();
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
        // the stream says changes only: the state now, once, as recovery's
        // (the security status would wait for a change after a cold start)
        self.emit_verification(verification.get());
        self.emit_recovery(enc.recovery().state());
        loop {
            tokio::select! {
                Some(v) = verification.next() => self.emit_verification(v),
                Some(r) = recovery.next() => self.emit_recovery(r),
                else => break,
            }
        }
    }

    fn emit_verification(&self, state: VerificationState) {
        let name = match state {
            VerificationState::Verified => "verified",
            VerificationState::Unverified => "unverified",
            VerificationState::Unknown => "unknown",
        };
        self.emit(json!({"ev": "verification_state", "state": name}));
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
            "store": dir_size(&self.data_dir.join(STORE_DIR)),
            "media_files": dir_size(&self.data_dir.join(MEDIA_DIR)) + dir_size(&self.data_dir.join(AVATAR_DIR)),
            "crypto": sizes.crypto_store,
            "state": sizes.state_store,
            "event_cache": sizes.event_cache_store,
            "media_cache": sizes.media_store,
        }));
        Ok(())
    }
}
