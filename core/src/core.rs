// SPDX-License-Identifier: GPL-3.0-or-later
//! The core: one Client, one sync, one open timeline, and the command
//! handlers. Every handler reports back through events; errors go out as
//! `{"ev":"error","cmd":...,"message":...}` plus a specific event where the
//! UI needs to react (login).

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use anyhow::{anyhow, bail, Context, Result};
use futures_util::{FutureExt, StreamExt};
use matrix_sdk::authentication::matrix::MatrixSession;
use matrix_sdk::attachment::{AttachmentInfo, BaseAudioInfo, BaseImageInfo, Thumbnail};
use matrix_sdk::config::SyncSettings;
use matrix_sdk::encryption::recovery::RecoveryState;
use matrix_sdk::encryption::verification::{
    SasState, SasVerification, VerificationRequest, VerificationRequestState,
};
use matrix_sdk::encryption::{BackupDownloadStrategy, EncryptionSettings, VerificationState};
use matrix_sdk::media::{MediaEventContent, MediaFormat, MediaRequestParameters, MediaThumbnailSettings};
use matrix_sdk::notification_settings::RoomNotificationMode;
use matrix_sdk::room::edit::EditedContent;
use matrix_sdk::ruma::api::client::directory::get_public_rooms_filtered;
use matrix_sdk::ruma::api::client::room::{create_room, Visibility};
use matrix_sdk::ruma::events::room::encryption::RoomEncryptionEventContent;
use matrix_sdk::ruma::events::poll::unstable_response::UnstablePollResponseEventContent;
use matrix_sdk::ruma::events::InitialStateEvent;
use matrix_sdk::ruma::api::client::session::get_login_types::v3::LoginType;
use matrix_sdk::ruma::api::client::uiaa::{AuthData, MatrixUserIdentifier, Password, UserIdentifier};
use matrix_sdk::ruma::events::room::power_levels::UserPowerLevel;
use matrix_sdk::ruma::events::receipt::ReceiptThread;
use matrix_sdk::ruma::events::room::MediaSource;
use matrix_sdk::ruma::events::key::verification::request::ToDeviceKeyVerificationRequestEvent;
use matrix_sdk::ruma::events::room::message::{
    MessageType, OriginalSyncRoomMessageEvent, RoomMessageEventContent,
    RoomMessageEventContentWithoutRelation, TextMessageEventContent,
};
use matrix_sdk::ruma::api::client::receipt::create_receipt::v3::ReceiptType;
use matrix_sdk::ruma::{
    uint, EventId, OwnedDeviceId, UInt, OwnedMxcUri, OwnedRoomId, OwnedUserId, RoomId, RoomOrAliasId, UserId,
};
use matrix_sdk::sliding_sync::{Version, VersionBuilder};
use matrix_sdk::{Client, Room, RoomMemberships, RoomState, SessionChange};
use matrix_sdk_ui::sync_service::{State as SyncState, SyncService};
use matrix_sdk_ui::timeline::{
    AttachmentConfig, RoomExt, Timeline, TimelineEventItemId, TimelineItem, TimelineReadReceiptTracking,
};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tokio::runtime::Runtime;
use tokio::sync::Mutex;
use tokio::task::JoinHandle;

use crate::serialize::{avatar_file, items_json, room_json, sorted_rooms};
use crate::Emitter;

const DEVICE_NAME: &str = "Rustrx (Sailfish OS)";
const USER_AGENT: &str = concat!("Rustrx/", env!("CARGO_PKG_VERSION"), " (Sailfish OS)");

#[derive(Serialize, Deserialize)]
struct SavedSession {
    homeserver: String,
    session: MatrixSession,
    /// whether the server had native sliding sync at login; a restore then
    /// skips the discovery round trips
    #[serde(default)]
    sliding_sync: Option<bool>,
}

struct OpenRoom {
    room_id: OwnedRoomId,
    timeline: Arc<Timeline>,
    task: JoinHandle<()>,
}

#[derive(Default)]
struct State {
    client: Option<Client>,
    sync_service: Option<Arc<SyncService>>,
    sync_task: Option<JoinHandle<()>>,
    open: Option<OpenRoom>,
    requests: HashMap<String, VerificationRequest>,
    sas: HashMap<String, SasVerification>,
    avatars_tried: std::collections::HashSet<OwnedRoomId>,
    latest_listened: std::collections::HashSet<OwnedRoomId>,
    /// tasks that live as long as one login; aborted on logout
    login_tasks: Vec<JoinHandle<()>>,
}

/// What a logout has to stop, taken out of the state so the lock is not
/// held across the network or a service shutdown.
struct Teardown {
    client: Option<Client>,
    service: Option<Arc<SyncService>>,
    tasks: Vec<JoinHandle<()>>,
}

impl State {
    fn take_login(&mut self) -> Teardown {
        let mut tasks = std::mem::take(&mut self.login_tasks);
        if let Some(open) = self.open.take() {
            tasks.push(open.task);
        }
        if let Some(task) = self.sync_task.take() {
            tasks.push(task);
        }
        self.requests.clear();
        self.sas.clear();
        self.avatars_tried.clear();
        self.latest_listened.clear();
        Teardown { client: self.client.take(), service: self.sync_service.take(), tasks }
    }
}

impl Teardown {
    async fn stop(self) -> Option<Client> {
        for t in &self.tasks {
            t.abort();
        }
        if let Some(service) = self.service {
            service.stop().await;
        }
        self.client
    }
}

pub(crate) struct Core {
    rt: Runtime,
    data_dir: PathBuf,
    emitter: Emitter,
    state: Mutex<State>,
}

fn s(v: &Value, key: &str) -> Result<String> {
    v.get(key)
        .and_then(Value::as_str)
        .map(str::to_owned)
        .ok_or_else(|| anyhow!("missing \"{key}\""))
}

fn opt(v: &Value, key: &str) -> Option<String> {
    v.get(key).and_then(Value::as_str).map(str::to_owned)
}

impl Core {
    pub(crate) fn new(rt: Runtime, data_dir: PathBuf, emitter: Emitter) -> Self {
        Core { rt, data_dir, emitter, state: Mutex::new(State::default()) }
    }

    pub(crate) fn emit(&self, value: Value) {
        self.emitter.emit(value);
    }

    fn error(&self, cmd: &str, err: &anyhow::Error) {
        tracing::warn!(%cmd, "{err:#}");
        self.emit(json!({"ev": "error", "cmd": cmd, "message": format!("{err:#}")}));
    }

    fn session_path(&self) -> PathBuf {
        self.data_dir.join("session.json")
    }

    /// Restore a saved session, or report that a login is needed.
    pub(crate) fn boot(&'static self) {
        self.rt.spawn(async move {
            std::fs::create_dir_all(self.data_dir.join("media")).ok();
            std::fs::create_dir_all(self.data_dir.join("avatars")).ok();
            match self.restore().await {
                Ok(true) => {}
                Ok(false) => self.emit(json!({"ev": "ready", "logged_in": false})),
                Err(e) => {
                    self.error("restore", &e);
                    self.emit(json!({"ev": "ready", "logged_in": false}));
                }
            }
        });
    }

    async fn restore(&'static self) -> Result<bool> {
        let path = self.session_path();
        if !path.exists() {
            return Ok(false);
        }
        let saved: SavedSession = serde_json::from_slice(&std::fs::read(&path)?)?;
        let t = std::time::Instant::now();
        let client = match saved.sliding_sync {
            Some(native) => self.build_client_with(&saved.homeserver, true, Some(native)).await?,
            None => self.build_client(&saved.homeserver, true).await?,
        };
        tracing::info!(ms = t.elapsed().as_millis() as u64, "startup: client built");
        client.restore_session(saved.session).await?;
        tracing::info!(ms = t.elapsed().as_millis() as u64, "startup: session restored");
        self.after_login(client).await?;
        tracing::info!(ms = t.elapsed().as_millis() as u64, "startup: sync started");
        Ok(true)
    }

    async fn build_client(&self, server: &str, with_store: bool) -> Result<Client> {
        self.build_client_with(server, with_store, None).await
    }

    /// `known`: the sync flavour from a previous login, which skips discovery.
    async fn build_client_with(&self, server: &str, with_store: bool, known: Option<bool>) -> Result<Client> {
        let make = |version: VersionBuilder| {
            let mut b = Client::builder()
                .sliding_sync_version_builder(version)
                .with_encryption_settings(EncryptionSettings {
                    auto_enable_cross_signing: true,
                    backup_download_strategy: BackupDownloadStrategy::AfterDecryptionFailure,
                    auto_enable_backups: true,
                })
                .handle_refresh_tokens()
                .user_agent(USER_AGENT);
            if with_store {
                b = b.sqlite_store(self.data_dir.join("store"), None);
            }
            if server.starts_with("http://") || server.starts_with("https://") {
                b.homeserver_url(server)
            } else {
                b.server_name_or_homeserver_url(server)
            }
        };
        if let Some(native) = known {
            let version = if native { VersionBuilder::Native } else { VersionBuilder::None };
            return make(version).build().await.map_err(|e| anyhow!("{e}"));
        }
        // Native sliding sync where the server has it, the classic sync otherwise.
        match make(VersionBuilder::DiscoverNative).build().await {
            Ok(c) => Ok(c),
            Err(first) => {
                tracing::info!("no native sliding sync ({first}), using the classic sync");
                make(VersionBuilder::None).build().await.map_err(|e| anyhow!("{e}"))
            }
        }
    }

    fn save_session(&self, client: &Client) -> Result<()> {
        let session = client.matrix_auth().session().context("no Matrix session after login")?;
        let saved = SavedSession {
            homeserver: client.homeserver().to_string(),
            session,
            sliding_sync: Some(!matches!(client.sliding_sync_version(), Version::None)),
        };
        // the tokens: owner-only, and complete or absent (a crash mid-write
        // must not leave a half file for the next start)
        use std::io::Write;
        use std::os::unix::fs::OpenOptionsExt;
        let path = self.session_path();
        let tmp = path.with_extension("json.tmp");
        {
            let mut f = std::fs::OpenOptions::new()
                .write(true)
                .create(true)
                .truncate(true)
                .mode(0o600)
                .open(&tmp)?;
            f.write_all(&serde_json::to_vec(&saved)?)?;
            f.sync_all()?;
        }
        std::fs::rename(&tmp, &path)?;
        Ok(())
    }

    async fn after_login(&'static self, client: Client) -> Result<()> {
        self.save_session(&client)?;
        let user_id = client.user_id().map(|u| u.to_string());
        let device_id = client.device_id().map(|d| d.to_string());
        self.emit(json!({
            "ev": "logged_in",
            "user_id": user_id,
            "device_id": device_id,
            "homeserver": client.homeserver().to_string(),
            "sliding_sync": !matches!(client.sliding_sync_version(), Version::None),
        }));
        self.install_handlers(&client);
        {
            let mut st = self.state.lock().await;
            st.client = Some(client.clone());
        }
        let watch = self.rt.spawn(self.session_watch(client.clone()));
        // the previews come from the event cache; the sync service subscribes
        // it itself, the classic sync loop does not
        let _ = client.event_cache().subscribe();
        // the cached room list first, before anything waits for the network
        let rooms = self.rt.spawn(self.rooms_task(client.clone()));
        self.state.lock().await.login_tasks.extend([watch, rooms]);
        self.start_sync(client).await
    }

    /// Servers with refresh tokens rotate them while the app runs; the
    /// rotated pair has to reach session.json or the next start restores a
    /// dead session ("Invalid refresh token", 0.2.0 after a day).
    async fn session_watch(&'static self, client: Client) {
        let mut changes = client.subscribe_to_session_changes();
        loop {
            match changes.recv().await {
                Ok(SessionChange::TokensRefreshed) => match self.save_session(&client) {
                    Ok(()) => tracing::info!("tokens refreshed, session saved"),
                    Err(e) => tracing::warn!("saving the refreshed session: {e}"),
                },
                // on a task of its own: the teardown aborts this one
                Ok(SessionChange::UnknownToken(data)) => {
                    self.rt.spawn(self.session_expired(data.soft_logout));
                    break;
                }
                Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => continue,
                Err(tokio::sync::broadcast::error::RecvError::Closed) => break,
            }
        }
    }

    /// Local side of a logout the server already did: stop syncing, drop
    /// the saved session, tell the UI. A soft logout keeps the crypto store
    /// for a new login on the same device.
    async fn session_expired(&'static self, soft_logout: bool) {
        let teardown = {
            let mut st = self.state.lock().await;
            if st.client.is_none() {
                return; // already handled; every failing request reports it once more
            }
            st.take_login()
        };
        tracing::warn!(soft_logout, "the server no longer accepts this session");
        let client = teardown.stop().await;
        std::fs::remove_file(self.session_path()).ok();
        if soft_logout {
            // the crypto store belongs to this device: the next login asks for it again
            if let Some(device) = client.as_ref().and_then(|c| c.device_id()) {
                std::fs::write(self.data_dir.join("device_id"), device.as_str()).ok();
            }
        } else {
            self.wipe_local_data();
        }
        self.emit(json!({"ev": "logged_out", "reason": "session_expired", "soft_logout": soft_logout}));
    }

    /// Everything of the account on this phone: store, decrypted media, avatars.
    fn wipe_local_data(&self) {
        std::fs::remove_dir_all(self.data_dir.join("store")).ok();
        std::fs::remove_file(self.data_dir.join("device_id")).ok();
        for dir in ["media", "avatars"] {
            std::fs::remove_dir_all(self.data_dir.join(dir)).ok();
            std::fs::create_dir_all(self.data_dir.join(dir)).ok();
        }
    }

    async fn start_sync(&'static self, client: Client) -> Result<()> {
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
    async fn rooms_task(&'static self, client: Client) {
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
    fn log_counts(client: &Client) {
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
    async fn listen_latest(&'static self, client: &Client) {
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
    async fn fetch_avatars(&'static self, client: &Client) -> bool {
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
    async fn avatar_source(room: &Room, own: Option<&UserId>) -> Option<OwnedMxcUri> {
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

    /// Display names for the people whose read receipts sit on these items;
    /// unknown members fall back to the localpart in the UI.
    async fn receipt_names<'a>(
        room: &Room,
        items: impl IntoIterator<Item = &'a Arc<TimelineItem>>,
        names: &mut HashMap<OwnedUserId, String>,
    ) {
        for item in items {
            let Some(event) = item.as_event() else { continue };
            for user in event.read_receipts().keys() {
                if names.contains_key(user) {
                    continue;
                }
                let name = match room.get_member_no_sync(user).await {
                    Ok(Some(m)) => m.name().to_owned(),
                    _ => user.localpart().to_owned(),
                };
                names.insert(user.clone(), name);
            }
        }
    }

    async fn encryption_task(&'static self, client: Client) {
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

    fn emit_recovery(&self, state: RecoveryState) {
        let name = match state {
            RecoveryState::Enabled => "enabled",
            RecoveryState::Disabled => "disabled",
            RecoveryState::Incomplete => "incomplete",
            RecoveryState::Unknown => "unknown",
        };
        self.emit(json!({"ev": "recovery_state", "state": name}));
    }

    /// Incoming verification requests, to-device and in-room.
    fn install_handlers(&'static self, client: &Client) {
        client.add_event_handler(|ev: ToDeviceKeyVerificationRequestEvent, client: Client| async move {
            // spawned: a handler that awaits the whole flow would hold up the sync
            if let Some(req) = client
                .encryption()
                .get_verification_request(&ev.sender, ev.content.transaction_id.as_str())
                .await
            {
                tokio::spawn(async move { crate::instance().watch_request(req).await });
            }
        });
        client.add_event_handler(|ev: OriginalSyncRoomMessageEvent, client: Client| async move {
            if let MessageType::VerificationRequest(_) = &ev.content.msgtype {
                if let Some(req) =
                    client.encryption().get_verification_request(&ev.sender, ev.event_id.as_str()).await
                {
                    tokio::spawn(async move { crate::instance().watch_request(req).await });
                }
            }
        });
    }

    fn request_json(req: &VerificationRequest, state: &str, extra: Value) -> Value {
        let mut v = json!({
            "ev": "verification",
            "flow_id": req.flow_id(),
            "state": state,
            "other_user": req.other_user_id().as_str(),
            "we_started": req.we_started(),
            "self_verification": req.is_self_verification(),
        });
        if let (Value::Object(o), Value::Object(e)) = (&mut v, extra) {
            o.extend(e);
        }
        v
    }

    async fn watch_request(&'static self, req: VerificationRequest) {
        let flow = req.flow_id().to_owned();
        {
            let mut st = self.state.lock().await;
            if st.requests.contains_key(&flow) {
                return;
            }
            st.requests.insert(flow.clone(), req.clone());
        }
        let mut changes = req.changes();
        let mut current = Some(req.state());
        loop {
            let Some(state) = current.take() else { break };
            let done = match &state {
                VerificationRequestState::Created { .. } => {
                    self.emit(Self::request_json(&req, "created", json!({})));
                    false
                }
                VerificationRequestState::Requested { other_device_data, .. } => {
                    self.emit(Self::request_json(
                        &req,
                        "requested",
                        json!({"other_device": other_device_data.device_id().as_str(),
                               "other_device_name": other_device_data.display_name()}),
                    ));
                    false
                }
                VerificationRequestState::Ready { other_device_data, .. } => {
                    self.emit(Self::request_json(
                        &req,
                        "ready",
                        json!({"other_device": other_device_data.device_id().as_str(),
                               "other_device_name": other_device_data.display_name()}),
                    ));
                    // The requester drives the SAS flow; the other side waits.
                    if req.we_started() {
                        if let Err(e) = req.start_sas().await {
                            self.error("verification_start_sas", &anyhow!("{e}"));
                        }
                    }
                    false
                }
                VerificationRequestState::Transitioned { verification } => {
                    if let Some(sas) = verification.clone().sas() {
                        self.emit(Self::request_json(&req, "sas", json!({})));
                        self.state.lock().await.sas.insert(flow.clone(), sas.clone());
                        self.rt.spawn(self.watch_sas(flow.clone(), sas));
                    } else {
                        self.emit(Self::request_json(&req, "unsupported", json!({})));
                    }
                    false
                }
                VerificationRequestState::Done => {
                    self.emit(Self::request_json(&req, "done", json!({})));
                    true
                }
                VerificationRequestState::Cancelled(info) => {
                    self.emit(Self::request_json(
                        &req,
                        "cancelled",
                        json!({"reason": info.reason(), "by_us": info.cancelled_by_us()}),
                    ));
                    true
                }
            };
            if done {
                break;
            }
            current = changes.next().await;
        }
        let mut st = self.state.lock().await;
        st.requests.remove(&flow);
        st.sas.remove(&flow);
    }

    async fn watch_sas(&'static self, flow: String, sas: SasVerification) {
        let mut changes = sas.changes();
        let mut current = Some(sas.state());
        while let Some(state) = current.take() {
            match state {
                SasState::KeysExchanged { emojis: state_emojis, decimals: state_decimals } => {
                    // the state carries them; the accessor is only a fallback
                    let list = state_emojis.map(|e| e.emojis).or_else(|| sas.emoji());
                    let emojis: Vec<Value> = list
                        .map(|e| e.iter().map(|e| json!({"symbol": e.symbol, "description": e.description})).collect())
                        .unwrap_or_default();
                    let (a, b, c) = state_decimals;
                    let decimals = Some(vec![a, b, c]);
                    tracing::info!(%flow, emojis = emojis.len(), "sas keys exchanged");
                    self.emit(json!({"ev": "sas", "flow_id": flow, "state": "keys_exchanged",
                                     "emojis": emojis, "decimals": decimals}));
                }
                SasState::Confirmed => {
                    self.emit(json!({"ev": "sas", "flow_id": flow, "state": "confirmed"}));
                }
                SasState::Done { .. } => {
                    self.emit(json!({"ev": "sas", "flow_id": flow, "state": "done"}));
                    break;
                }
                SasState::Cancelled(info) => {
                    self.emit(json!({"ev": "sas", "flow_id": flow, "state": "cancelled",
                                     "reason": info.reason()}));
                    break;
                }
                _ => {}
            }
            current = changes.next().await;
        }
    }

    /// Timeline snapshots for the open room, coalesced per update burst, and
    /// who is typing there (the own user left out).
    async fn timeline_task(&'static self, room: Room, timeline: Arc<Timeline>, own: OwnedUserId) {
        let room_id = room.room_id().to_owned();
        let mut names = HashMap::new();
        let (items, mut stream) = timeline.subscribe().await;
        let items: Vec<Arc<TimelineItem>> = items.into_iter().collect();
        Self::receipt_names(&room, &items, &mut names).await;
        self.emit(json!({"ev": "timeline", "room_id": room_id.as_str(), "items": items_json(&items, &own, &names)}));
        // the guard keeps the typing handler registered while this task lives
        let (_typing_guard, mut typing) = room.subscribe_to_typing_notifications();
        loop {
            tokio::select! {
                update = stream.next() => {
                    if update.is_none() {
                        break;
                    }
                    tokio::time::sleep(Duration::from_millis(60)).await;
                    while let Some(Some(_)) = stream.next().now_or_never() {}
                    let items = timeline.items().await;
                    Self::receipt_names(&room, &items, &mut names).await;
                    self.emit(json!({"ev": "timeline", "room_id": room_id.as_str(), "items": items_json(&items, &own, &names)}));
                }
                users = typing.recv() => {
                    let Ok(users) = users else { continue };
                    let mut who = Vec::new();
                    for u in users.iter().filter(|u| **u != own) {
                        let name = match room.get_member_no_sync(u).await {
                            Ok(Some(m)) => m.name().to_owned(),
                            _ => u.localpart().to_owned(),
                        };
                        who.push(name);
                    }
                    self.emit(json!({"ev": "typing", "room_id": room_id.as_str(), "names": who}));
                }
            }
        }
    }

    /// A timeline item by event id, or by transaction id for a local echo.
    fn item_id(v: &Value) -> Result<TimelineEventItemId> {
        if let Some(txn) = opt(v, "txn_id") {
            return Ok(TimelineEventItemId::TransactionId(txn.into()));
        }
        Ok(TimelineEventItemId::EventId(EventId::parse(s(v, "event_id")?)?))
    }

    async fn storage_info(&self) -> Result<()> {
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

    async fn notification_mode(&self, room: &Room) -> Result<()> {
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

    async fn client(&self) -> Result<Client> {
        self.state.lock().await.client.clone().context("not logged in")
    }

    async fn room(&self, v: &Value) -> Result<Room> {
        let id = s(v, "room_id")?;
        let client = self.client().await?;
        client.get_room(&RoomId::parse(&id)?).with_context(|| format!("unknown room {id}"))
    }

    /// The open room's timeline when it matches, else a throwaway one.
    async fn timeline_for(&self, room: &Room) -> Result<Arc<Timeline>> {
        if let Some(open) = &self.state.lock().await.open {
            if open.room_id == room.room_id() {
                return Ok(open.timeline.clone());
            }
        }
        Ok(Arc::new(room.timeline().await?))
    }

    async fn handle(&'static self, cmd: &str, v: &Value) -> Result<()> {
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

    async fn request(&self, v: &Value) -> Result<VerificationRequest> {
        let flow = s(v, "flow_id")?;
        self.state.lock().await.requests.get(&flow).cloned().context("no such verification")
    }

    async fn sas(&self, v: &Value) -> Result<SasVerification> {
        let flow = s(v, "flow_id")?;
        self.state.lock().await.sas.get(&flow).cloned().context("no emoji verification in progress")
    }

    /// Server from a full user id, or the homeserver field.
    fn server_of(v: &Value) -> Result<String> {
        if let Some(h) = opt(v, "homeserver").filter(|h| !h.trim().is_empty()) {
            return Ok(h.trim().to_owned());
        }
        let user = s(v, "user")?;
        let user = user.trim();
        match user.strip_prefix('@').and_then(|u| u.split_once(':')) {
            Some((_, server)) if !server.is_empty() => Ok(server.to_owned()),
            _ => bail!("enter the full Matrix ID, like @name:server.org, or a homeserver"),
        }
    }

    async fn probe_server(&'static self, v: &Value) -> Result<()> {
        let server = Self::server_of(v)?;
        let client = self.build_client(&server, false).await.map_err(|e| anyhow!("{server}: {e}"))?;
        let flows = client.matrix_auth().get_login_types().await?.flows;
        let mut password = false;
        let mut idps: Vec<String> = Vec::new();
        let mut sso = false;
        for flow in &flows {
            match flow {
                LoginType::Password(_) => password = true,
                LoginType::Sso(s) => {
                    sso = true;
                    idps.extend(s.identity_providers.iter().map(|p| p.name.clone()));
                }
                _ => {}
            }
        }
        // a server with an account service (MAS, matrix.org since 2025) signs
        // new people up in the browser; the password login works afterwards
        let signup = match client.oauth().server_metadata().await {
            Ok(meta) if meta.prompt_values_supported.iter().any(|p| p.as_str() == "create") => {
                meta.issuer.join("register").ok().map(|u| u.to_string())
            }
            _ => None,
        };
        self.emit(json!({
            "ev": "server_info",
            "signup_url": signup,
            "server": server,
            "homeserver": client.homeserver().to_string(),
            "password": password,
            "sso": sso,
            "idps": idps,
            "sliding_sync": !matches!(client.sliding_sync_version(), Version::None),
        }));
        Ok(())
    }

    async fn login_password(&'static self, v: &Value) -> Result<()> {
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        let result: Result<Client> = async {
            let server = Self::server_of(v)?;
            let user = s(v, "user")?;
            let password = s(v, "password")?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            // after a soft logout: the same device again, so the kept crypto store fits
            let device = std::fs::read_to_string(self.data_dir.join("device_id")).ok();
            let mut login = client
                .matrix_auth()
                .login_username(user.trim(), &password)
                .initial_device_display_name(DEVICE_NAME)
                .request_refresh_token();
            if let Some(d) = device.as_deref() {
                login = login.device_id(d.trim());
            }
            login.send().await.map_err(|e| anyhow!("{e}"))?;
            std::fs::remove_file(self.data_dir.join("device_id")).ok();
            Ok(client)
        }
        .await;
        match result {
            Ok(client) => self.after_login(client).await,
            Err(e) => {
                self.emit(json!({"ev": "login_error", "message": format!("{e:#}")}));
                Ok(())
            }
        }
    }

    async fn login_sso(&'static self, v: &Value) -> Result<()> {
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        let result: Result<Client> = async {
            let server = Self::server_of(v)?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            let mut builder = client
                .matrix_auth()
                .login_sso(|url| async move {
                    crate::instance().emit(json!({"ev": "open_url", "url": url}));
                    Ok(())
                })
                .initial_device_display_name(DEVICE_NAME)
                .request_refresh_token();
            if let Some(idp) = opt(v, "idp") {
                builder = builder.identity_provider_id(&idp);
            }
            builder.send().await.map_err(|e| anyhow!("{e}"))?;
            Ok(client)
        }
        .await;
        match result {
            Ok(client) => self.after_login(client).await,
            Err(e) => {
                self.emit(json!({"ev": "login_error", "message": format!("{e:#}")}));
                Ok(())
            }
        }
    }

    async fn logout(&'static self) -> Result<()> {
        // out of the state first: nothing below may hold the lock, or every
        // other command waits for the server (or its timeout) too
        let teardown = self.state.lock().await.take_login();
        if let Some(client) = teardown.stop().await {
            if let Err(e) = client.logout().await {
                tracing::warn!("logout: {e}");
            }
        }
        std::fs::remove_file(self.session_path()).ok();
        self.wipe_local_data();
        self.emit(json!({"ev": "logged_out"}));
        Ok(())
    }

    async fn open_room(&'static self, v: &Value) -> Result<()> {
        let room = self.room(v).await?;
        let client = self.client().await?;
        let own = client.user_id().context("no user id")?.to_owned();
        let room_id = room.room_id().to_owned();
        if let Some(service) = self.state.lock().await.sync_service.clone() {
            service.room_list_service().set_room_subscriptions(&[&room_id]).await;
        }
        let timeline = Arc::new(
            room.timeline_builder()
                .track_read_marker_and_receipts(TimelineReadReceiptTracking::MessageLikeEvents)
                .build()
                .await?,
        );
        let task = self.rt.spawn(self.timeline_task(room.clone(), timeline.clone(), own));
        let mut st = self.state.lock().await;
        if let Some(previous) = st.open.take() {
            previous.task.abort();
        }
        st.open = Some(OpenRoom { room_id, timeline: timeline.clone(), task });
        drop(st);
        // Sliding sync brings a short window; fill the first screen.
        let tl = timeline.clone();
        self.rt.spawn(async move {
            let _ = tl.paginate_backwards(30).await;
        });
        Ok(())
    }

    async fn fetch_media<C: MediaEventContent>(
        client: &Client,
        content: &C,
        thumb: bool,
    ) -> Result<Option<Vec<u8>>> {
        if thumb {
            let settings = MediaThumbnailSettings::new(uint!(640), uint!(640));
            Ok(client.media().get_thumbnail(content, settings, true).await?)
        } else {
            Ok(client.media().get_file(content, true).await?)
        }
    }

    async fn get_media(&'static self, v: &Value) -> Result<()> {
        let room = self.room(v).await?;
        let client = self.client().await?;
        let event_id = s(v, "event_id")?;
        let thumb = v.get("thumb").and_then(Value::as_bool).unwrap_or(false);
        let tl = self.timeline_for(&room).await?;
        let item = tl.item_by_event_id(&EventId::parse(&event_id)?).await.context("event not in timeline")?;
        let (bytes, mime) = match item.content().as_message().map(|m| m.msgtype()) {
            Some(MessageType::Image(c)) => (
                Self::fetch_media(&client, c, thumb).await?,
                c.info.as_ref().and_then(|i| i.mimetype.clone()),
            ),
            Some(MessageType::File(c)) => (
                Self::fetch_media(&client, c, false).await?,
                c.info.as_ref().and_then(|i| i.mimetype.clone()),
            ),
            Some(MessageType::Audio(c)) => (
                Self::fetch_media(&client, c, false).await?,
                c.info.as_ref().and_then(|i| i.mimetype.clone()),
            ),
            Some(MessageType::Video(c)) => (
                Self::fetch_media(&client, c, thumb).await?,
                c.info.as_ref().and_then(|i| i.mimetype.clone()),
            ),
            _ => match item.content().as_sticker() {
                Some(st) => (Self::fetch_media(&client, st.content(), false).await?, None),
                None => bail!("no media in this event"),
            },
        };
        let bytes = bytes.context("media not available")?;
        let ext = mime
            .as_deref()
            .and_then(|m| mime_guess::get_mime_extensions_str(m).and_then(|e| e.first().copied()))
            .unwrap_or("bin");
        let safe: String = event_id.chars().filter(|c| c.is_ascii_alphanumeric()).collect();
        // "dest_dir": a copy for other apps (the data dir is private to the sandbox),
        // named after the file where the message has a name
        let path = match opt(v, "dest_dir") {
            Some(dir) => {
                let original = item
                    .content()
                    .as_message()
                    .map(|m| m.body().to_owned())
                    .filter(|b| !b.is_empty() && b.len() < 200)
                    .map(|b| b.chars().map(|c| if c == '/' || c == '\\' || c.is_control() { '_' } else { c }).collect::<String>())
                    .unwrap_or_else(|| format!("{safe}.{ext}"));
                let dir = PathBuf::from(dir);
                tokio::fs::create_dir_all(&dir).await?;
                let mut path = dir.join(&original);
                if path.exists() {
                    path = dir.join(format!("{safe}-{original}"));
                }
                path
            }
            None => self.data_dir.join("media").join(format!("{safe}{}.{ext}", if thumb { "-thumb" } else { "" })),
        };
        tokio::fs::write(&path, &bytes).await?;
        self.emit(json!({"ev": "media", "room_id": room.room_id().as_str(), "event_id": event_id,
                         "thumb": thumb, "exported": opt(v, "dest_dir").is_some(),
                         "path": path.to_string_lossy()}));
        Ok(())
    }

    async fn delete_device(&'static self, v: &Value) -> Result<()> {
        let client = self.client().await?;
        let id: OwnedDeviceId = s(v, "device_id")?.into();
        let password = s(v, "password")?;
        let ids = [id];
        if let Err(e) = client.delete_devices(&ids, None).await {
            let info = e.as_uiaa_response().context(format!("{e}"))?;
            let localpart = client.user_id().context("no user id")?.localpart().to_owned();
            let mut auth = Password::new(UserIdentifier::Matrix(MatrixUserIdentifier::new(localpart)), password);
            auth.session = info.session.clone();
            client.delete_devices(&ids, Some(AuthData::Password(auth))).await?;
        }
        self.emit(json!({"ev": "device_deleted", "device_id": ids[0].as_str()}));
        Ok(())
    }
}
