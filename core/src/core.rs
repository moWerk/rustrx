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
use matrix_sdk::config::SyncSettings;
use matrix_sdk::encryption::recovery::RecoveryState;
use matrix_sdk::encryption::verification::{
    SasState, SasVerification, VerificationRequest, VerificationRequestState,
};
use matrix_sdk::encryption::{BackupDownloadStrategy, EncryptionSettings, VerificationState};
use matrix_sdk::media::{MediaEventContent, MediaThumbnailSettings};
use matrix_sdk::ruma::api::client::session::get_login_types::v3::LoginType;
use matrix_sdk::ruma::api::client::uiaa::{AuthData, MatrixUserIdentifier, Password, UserIdentifier};
use matrix_sdk::ruma::events::room::power_levels::UserPowerLevel;
use matrix_sdk::ruma::events::key::verification::request::ToDeviceKeyVerificationRequestEvent;
use matrix_sdk::ruma::events::room::message::{
    MessageType, OriginalSyncRoomMessageEvent, RoomMessageEventContent,
    RoomMessageEventContentWithoutRelation,
};
use matrix_sdk::ruma::api::client::receipt::create_receipt::v3::ReceiptType;
use matrix_sdk::ruma::{
    uint, EventId, OwnedDeviceId, OwnedRoomId, OwnedUserId, RoomId, RoomOrAliasId, UserId,
};
use matrix_sdk::sliding_sync::{Version, VersionBuilder};
use matrix_sdk::{Client, Room, RoomMemberships};
use matrix_sdk_ui::sync_service::{State as SyncState, SyncService};
use matrix_sdk_ui::timeline::{AttachmentConfig, RoomExt, Timeline, TimelineEventItemId};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tokio::runtime::Runtime;
use tokio::sync::Mutex;
use tokio::task::JoinHandle;

use crate::serialize::{items_json, rooms_json};
use crate::Emitter;

const DEVICE_NAME: &str = "Rustrx (Sailfish OS)";
const USER_AGENT: &str = concat!("Rustrx/", env!("CARGO_PKG_VERSION"), " (Sailfish OS)");

#[derive(Serialize, Deserialize)]
struct SavedSession {
    homeserver: String,
    session: MatrixSession,
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
        let client = self.build_client(&saved.homeserver, true).await?;
        client.restore_session(saved.session).await?;
        self.after_login(client).await?;
        Ok(true)
    }

    async fn build_client(&self, server: &str, with_store: bool) -> Result<Client> {
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
        let saved = SavedSession { homeserver: client.homeserver().to_string(), session };
        std::fs::write(self.session_path(), serde_json::to_vec(&saved)?)?;
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
        self.start_sync(client).await
    }

    async fn start_sync(&'static self, client: Client) -> Result<()> {
        if matches!(client.sliding_sync_version(), Version::None) {
            let c = client.clone();
            let task = self.rt.spawn(async move {
                self.emit(json!({"ev": "sync_state", "state": "running"}));
                loop {
                    if let Err(e) = c.sync(SyncSettings::default()).await {
                        tracing::warn!("sync ended: {e}");
                        self.emit(json!({"ev": "sync_state", "state": "error", "message": e.to_string()}));
                        tokio::time::sleep(Duration::from_secs(5)).await;
                    }
                }
            });
            self.state.lock().await.sync_task = Some(task);
        } else {
            let service = Arc::new(SyncService::builder(client.clone()).build().await?);
            service.start().await;
            let mut states = service.state();
            self.rt.spawn(async move {
                while let Some(state) = states.next().await {
                    let (name, message) = match &state {
                        SyncState::Idle => ("idle", None),
                        SyncState::Running => ("running", None),
                        SyncState::Terminated => ("terminated", None),
                        SyncState::Error(e) => ("error", Some(e.to_string())),
                        _ => ("offline", None),
                    };
                    self.emit(json!({"ev": "sync_state", "state": name, "message": message}));
                }
            });
            self.state.lock().await.sync_service = Some(service);
        }
        self.rt.spawn(self.rooms_task(client.clone()));
        self.rt.spawn(self.encryption_task(client));
        Ok(())
    }

    /// Room list snapshots on every notable room change, coalesced.
    async fn rooms_task(&'static self, client: Client) {
        let mut rx = client.room_info_notable_update_receiver();
        self.emit(json!({"ev": "rooms", "rooms": rooms_json(&client).await}));
        loop {
            match rx.recv().await {
                Ok(_) | Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => {}
                Err(_) => break,
            }
            tokio::time::sleep(Duration::from_millis(150)).await;
            while rx.try_recv().is_ok() {}
            self.emit(json!({"ev": "rooms", "rooms": rooms_json(&client).await}));
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
            if let Some(req) = client
                .encryption()
                .get_verification_request(&ev.sender, ev.content.transaction_id.as_str())
                .await
            {
                crate::core().watch_request(req).await;
            }
        });
        client.add_event_handler(|ev: OriginalSyncRoomMessageEvent, client: Client| async move {
            if let MessageType::VerificationRequest(_) = &ev.content.msgtype {
                if let Some(req) =
                    client.encryption().get_verification_request(&ev.sender, ev.event_id.as_str()).await
                {
                    crate::core().watch_request(req).await;
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
                SasState::KeysExchanged { .. } => {
                    let emojis: Vec<Value> = sas
                        .emoji()
                        .map(|e| e.iter().map(|e| json!({"symbol": e.symbol, "description": e.description})).collect())
                        .unwrap_or_default();
                    let decimals = sas.decimals().map(|(a, b, c)| vec![a, b, c]);
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

    /// Timeline snapshots for the open room, coalesced per update burst.
    async fn timeline_task(&'static self, room_id: OwnedRoomId, timeline: Arc<Timeline>, own: OwnedUserId) {
        let (items, mut stream) = timeline.subscribe().await;
        self.emit(json!({"ev": "timeline", "room_id": room_id.as_str(), "items": items_json(&items, &own)}));
        while stream.next().await.is_some() {
            tokio::time::sleep(Duration::from_millis(60)).await;
            while let Some(Some(_)) = stream.next().now_or_never() {}
            let items = timeline.items().await;
            self.emit(json!({"ev": "timeline", "room_id": room_id.as_str(), "items": items_json(&items, &own)}));
        }
    }

    pub(crate) fn dispatch(&'static self, v: Value) {
        let cmd = v.get("cmd").and_then(Value::as_str).unwrap_or("").to_owned();
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
                tl.send_attachment(path, mime, AttachmentConfig::default()).await?;
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
                let room = self.room(v).await?;
                let tl = self.timeline_for(&room).await?;
                let id = TimelineEventItemId::EventId(EventId::parse(s(v, "event_id")?)?);
                tl.redact(&id, opt(v, "reason").as_deref()).await?;
                Ok(())
            }
            "mark_read" => {
                let room = self.room(v).await?;
                let tl = self.timeline_for(&room).await?;
                tl.mark_as_read(ReceiptType::Read).await?;
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
                self.emit(json!({"ev": "rooms", "rooms": rooms_json(&client).await}));
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
        self.emit(json!({
            "ev": "server_info",
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
        let result: Result<Client> = async {
            let server = Self::server_of(v)?;
            let user = s(v, "user")?;
            let password = s(v, "password")?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            client
                .matrix_auth()
                .login_username(user.trim(), &password)
                .initial_device_display_name(DEVICE_NAME)
                .request_refresh_token()
                .send()
                .await
                .map_err(|e| anyhow!("{e}"))?;
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
        let result: Result<Client> = async {
            let server = Self::server_of(v)?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            let mut builder = client
                .matrix_auth()
                .login_sso(|url| async move {
                    crate::core().emit(json!({"ev": "open_url", "url": url}));
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
        let mut st = self.state.lock().await;
        if let Some(open) = st.open.take() {
            open.task.abort();
        }
        if let Some(service) = st.sync_service.take() {
            service.stop().await;
        }
        if let Some(task) = st.sync_task.take() {
            task.abort();
        }
        if let Some(client) = st.client.take() {
            if let Err(e) = client.logout().await {
                tracing::warn!("logout: {e}");
            }
        }
        drop(st);
        std::fs::remove_file(self.session_path()).ok();
        std::fs::remove_dir_all(self.data_dir.join("store")).ok();
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
        let timeline = Arc::new(room.timeline().await?);
        let task = self.rt.spawn(self.timeline_task(room_id.clone(), timeline.clone(), own));
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
        let name = format!("{safe}{}.{ext}", if thumb { "-thumb" } else { "" });
        let path = self.data_dir.join("media").join(name);
        tokio::fs::write(&path, &bytes).await?;
        self.emit(json!({"ev": "media", "room_id": room.room_id().as_str(), "event_id": event_id,
                         "thumb": thumb, "path": path.to_string_lossy()}));
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
