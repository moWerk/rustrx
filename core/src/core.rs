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
use matrix_sdk::attachment::{AttachmentInfo, BaseAudioInfo, BaseImageInfo, BaseVideoInfo, Thumbnail};
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
use crate::serialize::{avatar_file, member_name, profile_name, raw_value, room_json, room_name, sorted_rooms};
use crate::protocol::{given, Command};
use crate::Emitter;

const DEVICE_NAME: &str = "Rustrx (Sailfish OS)";
const USER_AGENT: &str = concat!("Rustrx/", env!("CARGO_PKG_VERSION"), " (Sailfish OS)");

#[derive(Serialize, Deserialize)]
pub(crate) struct SavedSession {
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
    /// the reactions on its state rows (state_reactions.rs)
    state_reactions: Arc<state_reactions::SharedStateReactions>,
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
    /// bumped by every close and open of the thread and the thread list: an
    /// open whose build took long sees that a close came meanwhile
    thread_seq: u64,
    thread_list_seq: u64,
    /// the same for the room's page: a late open does not install its
    /// timeline over a newer open or after a close
    open_seq: u64,
    /// open space pages: the task that sends each one's rooms
    space_lists: HashMap<String, JoinHandle<()>>,
    /// per space, bumped by every open and close: a late open does not
    /// install its list after the page was closed
    space_seqs: HashMap<String, u64>,
    /// the same for Element Call: a hang-up while the call was starting
    /// stops it instead of it starting after the hang-up
    call_seq: u64,
    /// per room: the lock that serializes its setting changes, and what
    /// was sent until the sync shows it (moderation.rs)
    room_writes: HashMap<OwnedRoomId, Arc<Mutex<moderation::RoomWrites>>>,
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
        self.room_writes.clear();
        tasks.extend(self.space_lists.drain().map(|(_, task)| task));
        Teardown { client: self.client.take(), service: self.sync_service.take(), tasks }
    }
}

impl Teardown {
    async fn stop(self) -> Option<Client> {
        for t in &self.tasks {
            t.abort();
        }
        // awaited: an aborted task lets go of what it holds (a timeline,
        // and with it the SDK's tasks for it) only when it runs next
        let me = tokio::task::try_id();
        for t in self.tasks {
            if Some(t.id()) != me {
                let _ = t.await;
            }
        }
        if let Some(service) = self.service {
            service.stop().await;
        }
        self.client
    }
}

pub(crate) struct Core {
    /// where every task runs: the process's one runtime, shared by every
    /// account's core (lib.rs), or a test's own
    rt: tokio::runtime::Handle,
    /// the runtime when this core owns it (tests): taken and stopped by
    /// `shutdown`; none for the shared one, which rustrx_shutdown stops
    runtime: std::sync::Mutex<Option<Runtime>>,
    /// the running commands' tasks (dispatch), aborted first at shutdown
    commands: std::sync::Mutex<Vec<JoinHandle<()>>>,
    /// set by `shutdown`: no command starts any more
    stopped: std::sync::atomic::AtomicBool,
    /// a login is running (LoginGuard): a second one is refused
    login_busy: std::sync::atomic::AtomicBool,
    data_dir: PathBuf,
    emitter: Emitter,
    state: Mutex<State>,
    /// "login_cancel": ends a browser login (single sign-on or OAuth) still
    /// waiting for the browser
    sso_cancel: tokio::sync::Notify,
    /// a QR login whose other device showed its check code: where the code
    /// the user types goes ("qr_check_code")
    qr_check: Mutex<Option<matrix_sdk::authentication::oauth::qrcode::CheckCodeSender>>,
    /// one pushed message at a time: each notification client runs its own
    /// encryption sync over the one crypto store, and their mutexes are
    /// their own, so two at once would not exclude each other
    push_lock: Mutex<()>,
    /// the call events passed on, and a call ringing from a push (call.rs)
    voip_seen: std::sync::Mutex<call::VoipSeen>,
    /// bumped when a thread was read here: open timelines and the thread
    /// list check their threads' unread state again
    threads_read: tokio::sync::watch::Sender<u64>,
    /// work that holds a timeline beyond its stream (a page of history, a
    /// reply's original): the shutdown stops it before the stores close
    timeline_work: std::sync::Mutex<Vec<JoinHandle<()>>>,
    /// the rooms (and threads) that had a timeline lately, newest last:
    /// their event caches are held open at shutdown (session.rs)
    timeline_rooms: std::sync::Mutex<std::collections::VecDeque<(OwnedRoomId, Option<OwnedEventId>)>>,
    /// the event cache subscribers the shutdown took; never let go (a drop
    /// would ask the SDK to shrink the cache from the closed store)
    cache_pins: std::sync::Mutex<Vec<Box<dyn std::any::Any + Send>>>,
}

mod account;
mod call;
mod checks;
mod commands;
mod knock;
mod media_cache;
mod moderation;
mod packs;
mod push;
mod qrlink;
mod rooms;
mod session;
mod session_file;
mod spaces;
mod state_reactions;
mod sync;
mod threads;
mod timeline;
mod verification;

pub(crate) use spaces::join_rule_name;
#[cfg(test)]
pub(crate) use timeline::emote_stem;

impl Core {
    /// A core on `rt`; `owned` is the runtime behind it when the core is
    /// to stop it itself (tests), none for the shared one.
    pub(crate) fn new(rt: tokio::runtime::Handle, owned: Option<Runtime>, data_dir: PathBuf, emitter: Emitter) -> Self {
        let voip_seen = std::sync::Mutex::new(call::VoipSeen::load(&data_dir.join(call::VOIP_SEEN_FILE)));
        Core { rt, runtime: std::sync::Mutex::new(owned), commands: std::sync::Mutex::new(Vec::new()),
               stopped: std::sync::atomic::AtomicBool::new(false), login_busy: std::sync::atomic::AtomicBool::new(false),
               data_dir, emitter, state: Mutex::new(State::default()), sso_cancel: tokio::sync::Notify::new(), push_lock: Mutex::new(()), voip_seen, qr_check: Mutex::new(None),
               threads_read: tokio::sync::watch::channel(0).0, timeline_work: std::sync::Mutex::new(Vec::new()),
               timeline_rooms: std::sync::Mutex::new(std::collections::VecDeque::new()), cache_pins: std::sync::Mutex::new(Vec::new()) }
    }

    #[cfg(test)]
    pub(crate) fn runtime_handle(&self) -> tokio::runtime::Handle {
        self.rt.clone()
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

    /// Spawns work that holds a timeline (see `timeline_work`).
    pub(crate) fn spawn_timeline_work(&self, work: impl std::future::Future<Output = ()> + Send + 'static) {
        let handle = self.rt.spawn(work);
        if let Ok(mut all) = self.timeline_work.lock() {
            all.retain(|t| !t.is_finished());
            all.push(handle);
        }
    }

    /// Notes that a timeline on this room (and thread) was made.
    pub(crate) fn note_timeline(&self, room: &RoomId, thread: Option<&EventId>) {
        let Ok(mut rooms) = self.timeline_rooms.lock() else { return };
        let entry = (room.to_owned(), thread.map(ToOwned::to_owned));
        rooms.retain(|e| *e != entry);
        rooms.push_back(entry);
        while rooms.len() > TIMELINE_ROOMS_KEPT {
            rooms.pop_front();
        }
    }

    #[cfg(test)]
    pub(crate) fn timeline_rooms(&self) -> Vec<(OwnedRoomId, Option<OwnedEventId>)> {
        self.timeline_rooms.lock().unwrap().iter().cloned().collect()
    }

    fn error(&self, cmd: &str, err: &anyhow::Error) {
        tracing::warn!(%cmd, "{err:#}");
        self.emit(checks::with_refusal(json!({"ev": "error", "cmd": cmd, "message": format!("{err:#}")}), err));
    }
}
