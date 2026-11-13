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
use crate::protocol::{given, Command};
use crate::Emitter;

/// The version of the JSON protocol between the core and the UI (docs/PROTOCOL.md),
/// sent with "ready" and "logged_in".
pub(crate) const PROTO: u32 = 1;

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
    /// the call shown in the WebView, if any
    call: Option<call::CallSession>,
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
        if let Some(call) = self.call.take() {
            call.stop();
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



mod call;
mod commands;
mod session;
mod sync;
mod timeline;
mod verification;

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
}
