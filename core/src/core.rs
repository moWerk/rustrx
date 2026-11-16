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
    EventId, OwnedDeviceId, OwnedEventId, UInt, OwnedMxcUri, OwnedRoomId, OwnedUserId, RoomId, RoomOrAliasId, UserId,
};
use matrix_sdk::sliding_sync::{Version, VersionBuilder};
use matrix_sdk::{Client, Room, RoomMemberships, RoomState, SessionChange};
use matrix_sdk_ui::sync_service::{State as SyncState, SyncService};
use matrix_sdk_ui::timeline::{
    AttachmentConfig, RoomExt, Timeline, TimelineDetails, TimelineEventItemId, TimelineFocus, TimelineItem,
    TimelineReadReceiptTracking,
};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tokio::runtime::Runtime;
use tokio::sync::Mutex;
use tokio::task::JoinHandle;

use crate::consts::*;
use crate::serialize::{avatar_file, items_json, member_name, profile_name, raw_value, room_json, room_name, sorted_rooms};
use crate::protocol::{given, Command};
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
    /// an OAuth login (the account service): the id it registered Rustrx
    /// under, which its refresh token is bound to; none for a Matrix login
    #[serde(default)]
    client_id: Option<String>,
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
    /// tasks that live as long as one login; aborted on logout
    login_tasks: Vec<JoinHandle<()>>,
    /// the call shown in the WebView, if any
    call: Option<call::CallSession>,
    /// the SDK's space service (this login)
    space_service: Option<Arc<matrix_sdk_ui::spaces::SpaceService>>,
    /// the thread shown beside the open room, if any
    thread: Option<threads::OpenThread>,
    /// the room's thread list while its page is open
    thread_list: Option<JoinHandle<()>>,
    /// open space pages: the task that sends each one's rooms
    space_lists: HashMap<String, JoinHandle<()>>,
    /// which login this is: bumped by every login and every teardown, so
    /// work begun for one (a download, a logout's server call) can tell
    /// that another has begun since
    generation: u64,
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
        self.generation += 1;
        let mut tasks = std::mem::take(&mut self.login_tasks);
        if let Some(open) = self.open.take() {
            tasks.push(open.task);
        }
        if let Some(task) = self.sync_task.take() {
            tasks.push(task);
        }
        if let Some(call) = self.call.take() {
            call.stop();
        }
        self.requests.clear();
        self.sas.clear();
        if let Some(thread) = self.thread.take() {
            tasks.push(thread.task_handle());
        }
        if let Some(task) = self.thread_list.take() {
            tasks.push(task);
        }
        self.space_service = None;
        tasks.extend(self.space_lists.drain().map(|(_, task)| task));
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
    /// "login_cancel": ends a browser login (single sign-on or OAuth) still
    /// waiting for the browser
    sso_cancel: tokio::sync::Notify,
    /// one pushed message at a time: each notification client runs its own
    /// encryption sync over the one crypto store, and their mutexes are
    /// their own, so two at once would not exclude each other
    push_lock: Mutex<()>,
}

mod account;
mod call;
mod commands;
mod knock;
mod packs;
mod push;
mod rooms;
mod session;
mod spaces;
mod sync;
mod threads;
mod timeline;
mod verification;

pub(crate) use spaces::join_rule_name;

impl Core {
    pub(crate) fn new(rt: Runtime, data_dir: PathBuf, emitter: Emitter) -> Self {
        Core { rt, data_dir, emitter, state: Mutex::new(State::default()), sso_cancel: tokio::sync::Notify::new(), push_lock: Mutex::new(()) }
    }

    pub(crate) fn emit(&self, value: Value) {
        self.emitter.emit(value);
    }

    /// The current login's number (State::generation).
    pub(crate) async fn generation(&self) -> u64 {
        self.state.lock().await.generation
    }

    /// Err when another login (or a logout) began since `generation`: what
    /// was fetched for the old one must not be written for it any more.
    pub(crate) async fn same_login(&self, generation: u64) -> Result<()> {
        if self.generation().await != generation {
            bail!("logged out meanwhile");
        }
        Ok(())
    }

    pub(crate) async fn client(&self) -> Result<Client> {
        self.state.lock().await.client.clone().context("not logged in")
    }

    pub(crate) async fn room(&self, id: &str) -> Result<Room> {
        let client = self.client().await?;
        client.get_room(&RoomId::parse(id)?).with_context(|| format!("unknown room {id}"))
    }

    /// Spawns a task that lives as long as this login (aborted on logout).
    pub(crate) async fn spawn_login_task(&'static self, task: impl std::future::Future<Output = ()> + Send + 'static) {
        let handle = self.rt.spawn(task);
        self.state.lock().await.login_tasks.push(handle);
    }

    fn error(&self, cmd: &str, err: &anyhow::Error) {
        tracing::warn!(%cmd, "{err:#}");
        self.emit(json!({"ev": "error", "cmd": cmd, "message": format!("{err:#}")}));
    }
}
