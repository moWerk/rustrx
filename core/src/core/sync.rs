// SPDX-License-Identifier: GPL-3.0-or-later
//! The sync, the room list, avatars, the encryption state, storage sizes.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;
use std::collections::HashSet;

use crate::serialize::RoomsDiff;

use matrix_sdk::notification_settings::NotificationSettings;
use tokio::sync::mpsc;

/// What the room list task remembers between passes, for one login (the
/// task is aborted on logout, and this goes with it).
#[derive(Default)]
pub(super) struct ListSeen {
    /// joined rooms whose latest event is listened for (or handed to the
    /// task that registers them)
    latest_listened: HashSet<OwnedRoomId>,
    /// rooms and spaces whose requests to join are watched
    pub(super) knocks_watched: HashSet<OwnedRoomId>,
}

/// The new rows into the cache; the ids of those that differed from the
/// one before go into `changed`; whether any did.
fn take_rows(cache: &mut HashMap<OwnedRoomId, Value>, rows: Vec<(OwnedRoomId, Value)>, changed: &mut HashSet<OwnedRoomId>) -> bool {
    let mut any = false;
    for (id, row) in rows {
        if cache.get(&id) != Some(&row) {
            cache.insert(id.clone(), row);
            changed.insert(id);
            any = true;
        }
    }
    any
}

/// Each core's ask for its room list in full ("rooms_resync",
/// "refresh_rooms"), by the core's address: one per account, kept as
/// long as the process (the cores are never freed). A permit waits for
/// a list task that is busy or not yet running.
fn rooms_resync_for(core: &Core) -> Arc<tokio::sync::Notify> {
    static ASKS: std::sync::Mutex<Vec<(usize, Arc<tokio::sync::Notify>)>> = std::sync::Mutex::new(Vec::new());
    let key = core as *const Core as usize;
    let mut asks = ASKS.lock().unwrap_or_else(|e| e.into_inner());
    if let Some((_, n)) = asks.iter().find(|(k, _)| *k == key) {
        return n.clone();
    }
    let n = Arc::new(tokio::sync::Notify::new());
    asks.push((key, n.clone()));
    n
}

/// Two requests for pictures as one: the rooms of both, or every room
/// (None) when either asked for every room.
fn join_asks(a: Option<HashSet<OwnedRoomId>>, b: Option<HashSet<OwnedRoomId>>) -> Option<HashSet<OwnedRoomId>> {
    match (a, b) {
        (Some(mut a), Some(b)) => {
            a.extend(b);
            Some(a)
        }
        _ => None,
    }
}

/// A task that goes when its owner does (the room list's helpers end with
/// the room list task, which logout aborts).
struct AbortOnDrop(JoinHandle<()>);

impl Drop for AbortOnDrop {
    fn drop(&mut self) {
        self.0.abort();
    }
}

/// Rows of the room list made at once (their store reads overlap).
const ROOM_ROWS_AT_ONCE: usize = 8;
/// Room pictures downloaded at once, beside the room list.
const AVATAR_FETCHES_AT_ONCE: usize = 4;
/// Rooms whose latest event is registered before the task lets others run.
const LATEST_LISTEN_CHUNK: usize = 8;
/// The room list is rebuilt in full at the first update this long after the
/// last full pass (and after a lagged update stream): a safety net for what
/// the stream missed, not a cost of every message after a quiet minute.
const ROOM_LIST_SWEEP: Duration = ROOM_LIST_FULL_REBUILD;

/// The rows of `rooms`, made several at once (each one awaits its own
/// store reads); `prev`: the rows before this pass (room_json).
pub(super) async fn build_rows(
    rooms: Vec<Room>,
    avatar_dir: &std::path::Path,
    settings: &NotificationSettings,
    prev: &HashMap<OwnedRoomId, Value>,
) -> Vec<(OwnedRoomId, Value)> {
    futures_util::stream::iter(rooms)
        .map(|room| async move {
            let row = room_json(&room, avatar_dir, settings, prev.get(room.room_id())).await;
            (room.room_id().to_owned(), row)
        })
        .buffer_unordered(ROOM_ROWS_AT_ONCE)
        .collect()
        .await
}

/// How long the sync service rests after its `fails`-th failure in a row
/// right after starting: from SYNC_REST_FIRST, doubling, at most SYNC_REST_MAX.
fn sync_rest(fails: u32) -> Duration {
    let doublings = fails.saturating_sub(SYNC_QUICK_FAILS).min(16);
    SYNC_REST_FIRST.saturating_mul(1 << doublings).min(SYNC_REST_MAX)
}

/// The sync state differs from the one sent last (which it becomes).
fn sync_state_changed(sent: &mut Option<(&'static str, Option<String>)>, name: &'static str, message: &Option<String>) -> bool {
    if sent.as_ref().is_some_and(|(n, m)| *n == name && m == message) {
        return false;
    }
    *sent = Some((name, message.clone()));
    true
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
            let svc = service.clone();
            let state_task = self.rt.spawn(async move {
                let mut current = Some(states.get());
                // A request the server refuses (a 400, not a network
                // failure) ends the service, the offline mode finds the
                // server reachable and starts it again at once: on the Prawn
                // every 18 ms for hours. Failures right after starting count
                // up; from the third the service rests, longer each time.
                let mut running_since: Option<std::time::Instant> = None;
                let mut quick_fails = 0u32;
                let mut sent = None;
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
                    // the first value and the stream's first item are mostly
                    // the same state: sent (and logged) once
                    if sync_state_changed(&mut sent, name, &message) {
                        tracing::info!(state = name, message, "sync service");
                        self.emit(json!({"ev": "sync_state", "state": name, "message": message}));
                    }
                    // the server is reachable again: rooms whose queue a
                    // network error switched off send what waits
                    if matches!(state, SyncState::Running) {
                        queues.send_queue().set_enabled(true).await;
                        running_since = Some(std::time::Instant::now());
                    } else if let Some(since) = running_since.take() {
                        if since.elapsed() < SYNC_QUICK_FAIL {
                            quick_fails += 1;
                        } else {
                            quick_fails = 0;
                        }
                        if quick_fails >= SYNC_QUICK_FAILS {
                            let rest = sync_rest(quick_fails);
                            tracing::warn!(quick_fails, secs = rest.as_secs(), "sync service: failing at once, resting");
                            svc.stop().await;
                            tokio::time::sleep(rest).await;
                            svc.start().await;
                        }
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
    /// stream names are serialised again, and sent only when a row changed;
    /// a lagged stream, or the first update ROOM_LIST_SWEEP after the
    /// last full pass, rebuilds the whole list (and catches what the stream
    /// missed). Pictures and the latest-event listeners have tasks of their
    /// own, so that neither holds the list back. Each run is a stream of
    /// its own: the first event full, then the order and the changed rows
    /// (RoomsDiff); a full rebuild that changed something, and the UI's ask
    /// (rooms_resync), send everything again.
    pub(crate) async fn rooms_task(&'static self, client: Client) {
        static STREAMS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
        let mut diff = RoomsDiff::new(STREAMS.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1);
        let resync = rooms_resync_for(self);
        // an ask from before this login: the first list is full anyway
        resync.notified().now_or_never();
        let mut rx = client.room_info_notable_update_receiver();
        let avatar_dir = self.data_dir.join(AVATAR_DIR);
        let t = std::time::Instant::now();
        let mut seen = ListSeen::default();
        let mut cache: HashMap<OwnedRoomId, Value> = {
            // the push rules once per pass, not once per room
            let settings = client.notification_settings().await;
            let rooms: Vec<Room> = client.rooms().into_iter().filter(crate::serialize::listed).collect();
            build_rows(rooms, &avatar_dir, &settings, &HashMap::new()).await.into_iter().collect()
        };
        tracing::info!(ms = t.elapsed().as_millis() as u64, rooms = cache.len(), "startup: first room list");
        self.emit_rooms_list(&mut diff, &cache, &HashSet::new(), t, true, cache.len());
        // the helpers: what they are asked, and the rooms whose picture came
        let (avatar_tx, avatar_rx) = mpsc::unbounded_channel::<Option<HashSet<OwnedRoomId>>>();
        let (pictures_tx, mut pictures) = mpsc::unbounded_channel::<OwnedRoomId>();
        let (latest_tx, latest_rx) = mpsc::unbounded_channel::<Vec<OwnedRoomId>>();
        let _avatars = AbortOnDrop(self.rt.spawn(self.avatars_task(client.clone(), avatar_rx, pictures_tx)));
        let _latest = AbortOnDrop(self.rt.spawn(Self::latest_task(client.clone(), latest_rx)));
        Self::listen_latest(&client, &cache, &mut seen, &latest_tx);
        self.watch_knocks(&client, None, &mut seen).await;
        avatar_tx.send(None).ok();
        let mut last_full = std::time::Instant::now();
        loop {
            // the rooms the updates named, and those whose picture came
            let mut changed: HashSet<OwnedRoomId> = HashSet::new();
            let mut full = false;
            // the UI asked for everything: sent even when nothing changed
            let mut asked = false;
            tokio::select! {
                update = rx.recv() => match update {
                    Ok(u) => {
                        changed.insert(u.room_id);
                    }
                    Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => full = true,
                    Err(_) => break,
                },
                Some(id) = pictures.recv() => {
                    changed.insert(id);
                }
                _ = resync.notified() => {
                    full = true;
                    asked = true;
                }
            }
            tokio::time::sleep(ROOM_LIST_BATCH).await;
            loop {
                match rx.try_recv() {
                    Ok(u) => {
                        changed.insert(u.room_id);
                    }
                    Err(tokio::sync::broadcast::error::TryRecvError::Lagged(_)) => full = true,
                    Err(_) => break,
                }
            }
            while let Ok(id) = pictures.try_recv() {
                changed.insert(id);
            }
            // long since the last full pass: everything once more, to catch
            // what the stream missed
            if last_full.elapsed() > ROOM_LIST_SWEEP {
                full = true;
            }
            let built = std::time::Instant::now();
            let settings = client.notification_settings().await;
            let rebuilt;
            // the rows to send: those that differ from the ones sent (a
            // full rebuild that changed any sends all)
            let mut differ: HashSet<OwnedRoomId> = HashSet::new();
            if full {
                let rooms: Vec<Room> = client.rooms().into_iter().filter(crate::serialize::listed).collect();
                let rows: HashMap<OwnedRoomId, Value> =
                    build_rows(rooms, &avatar_dir, &settings, &HashMap::new()).await.into_iter().collect();
                rebuilt = rows.len();
                // what the stream missed is sent in full (the UI's copy is
                // checked against it as well)
                if asked || rows != cache {
                    diff.resync();
                }
                cache = rows;
                last_full = std::time::Instant::now();
            } else {
                let mut rooms = Vec::new();
                for id in &changed {
                    match client.get_room(id).filter(crate::serialize::listed) {
                        Some(room) => rooms.push(room),
                        None => {
                            cache.remove(id);
                        }
                    }
                }
                let rows = build_rows(rooms, &avatar_dir, &settings, &cache).await;
                rebuilt = rows.len();
                take_rows(&mut cache, rows, &mut differ);
            }
            // nothing the list shows changed: nothing is sent (RoomsDiff
            // compares the order; a removal is the order alone)
            self.emit_rooms_list(&mut diff, &cache, &differ, built, full, rebuilt);
            Self::log_counts(&client);
            // only what changed is looked at again: on a full rebuild every
            // room, else the rooms the update named (the list's cost no
            // longer grows with the number of rooms on every receipt)
            let only = if full { None } else { Some(&changed) };
            if full || changed.iter().any(|id| !seen.latest_listened.contains(id)) {
                Self::listen_latest(&client, &cache, &mut seen, &latest_tx);
            }
            self.watch_knocks(&client, only, &mut seen).await;
            avatar_tx.send(only.cloned()).ok();
        }
    }

    /// The cached rows as "rooms": their order and the rows in `changed`
    /// (all on a full event), nothing when neither changed; perf: how long
    /// the pass since `built` took, how many rows it made anew (`full`: a
    /// full rebuild), how many it sent and whether it sent anything.
    fn emit_rooms_list(&self, diff: &mut RoomsDiff, cache: &HashMap<OwnedRoomId, Value>, changed: &HashSet<OwnedRoomId>,
                       built: std::time::Instant, full: bool, rebuilt: usize) {
        let build = built.elapsed();
        let changed: HashSet<&str> = changed.iter().map(|id| id.as_str()).collect();
        let fields = diff.pass(&sorted_rooms(cache.values()), |id| changed.contains(id));
        let (sent, event_full, items) = match &fields {
            Some(f) => (true, f["full"] == true, f["items"].as_object().map_or(0, |m| m.len())),
            None => (false, false, 0),
        };
        tracing::debug!(target: "rustrx::perf", full, rooms = cache.len(), rebuilt, reused = cache.len().saturating_sub(rebuilt),
                        sent, event_full, items, build_us = build.as_micros() as u64,
                        sort_us = (built.elapsed() - build).as_micros() as u64, "rooms_build");
        if let Some(fields) = fields {
            let mut ev = json!({"ev": "rooms"});
            if let Value::Object(ev) = &mut ev {
                ev.extend(fields);
            }
            self.emit(ev);
        }
    }

    /// The UI lost track of the room list (a skipped event, a row it does
    /// not have; "rooms_resync"), or a test wants it built anew
    /// ("refresh_rooms"): the list task rebuilds it and sends it in full.
    pub(crate) fn rooms_resync(&self) {
        rooms_resync_for(self).notify_one();
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
    /// once someone listens for it. Every joined room once, handed to
    /// latest_task, the rooms on top of the list first.
    fn listen_latest(client: &Client, cache: &HashMap<OwnedRoomId, Value>, seen: &mut ListSeen, to: &mpsc::UnboundedSender<Vec<OwnedRoomId>>) {
        let mut new: Vec<OwnedRoomId> = client
            .rooms()
            .into_iter()
            .filter(|r| r.state() == RoomState::Joined)
            .map(|r| r.room_id().to_owned())
            .filter(|id| seen.latest_listened.insert(id.clone()))
            .collect();
        if new.is_empty() {
            return;
        }
        new.sort_by_cached_key(|id| std::cmp::Reverse(cache.get(id).and_then(|r| r["ts"].as_u64()).unwrap_or(0)));
        to.send(new).ok();
    }

    /// Registers the rooms it is given with the latest-event service, a few
    /// at a time with a yield between, beside the room list: each one reads
    /// the room's event cache, and 136 in a row held the first updates back.
    /// perf: how long each batch took.
    async fn latest_task(client: Client, mut rx: mpsc::UnboundedReceiver<Vec<OwnedRoomId>>) {
        let latest = client.latest_events().await;
        while let Some(ids) = rx.recv().await {
            let t = std::time::Instant::now();
            for (i, id) in ids.iter().enumerate() {
                if let Err(e) = latest.listen_to_room(id).await {
                    tracing::info!(room = %id, "latest event: {e}");
                }
                if (i + 1) % LATEST_LISTEN_CHUNK == 0 {
                    tokio::task::yield_now().await;
                }
            }
            tracing::debug!(target: "rustrx::perf", rooms = ids.len(), ms = t.elapsed().as_millis() as u64, "listen_latest");
        }
    }

    /// Caches the avatars that are missing or changed (the picture's mxc
    /// address is kept beside it, "<room>.url"), once per picture, for the
    /// rooms it is asked about (None: every room), a few downloads at once;
    /// and the picture of each group's latest sender, one file per mxc
    /// (sender_avatar_file) for every room it is the latest sender's in.
    /// Sends back each room whose picture arrived (its row needs it).
    async fn avatars_task(
        &'static self,
        client: Client,
        mut rx: mpsc::UnboundedReceiver<Option<HashSet<OwnedRoomId>>>,
        arrived: mpsc::UnboundedSender<OwnedRoomId>,
    ) {
        let avatar_dir = self.data_dir.join(AVATAR_DIR);
        let own = client.user_id().map(ToOwned::to_owned);
        // room and picture pairs tried once ("!room|mxc://..."), a sender's
        // picture once ("sender|mxc://..."); a new picture is tried again
        let mut tried: HashSet<String> = HashSet::new();
        while let Some(first) = rx.recv().await {
            // what was asked meanwhile in one go: every room if one asked for all
            let mut only = first;
            while let Ok(more) = rx.try_recv() {
                only = join_asks(only, more);
            }
            // (rooms whose row waits for it, picture, file, marker)
            let mut wanted: Vec<(Vec<OwnedRoomId>, OwnedMxcUri, PathBuf, Option<PathBuf>)> = Vec::new();
            // a sender's picture: its place in wanted, by mxc
            let mut senders: HashMap<OwnedMxcUri, usize> = HashMap::new();
            for room in client.rooms() {
                if only.as_ref().is_some_and(|o| !o.contains(room.room_id())) {
                    continue;
                }
                if room.state() == RoomState::Joined {
                    let is_dm = room.is_direct().await.unwrap_or(false);
                    if let (_, _, Some(url)) = crate::serialize::preview(&room, is_dm).await {
                        let file = crate::serialize::sender_avatar_file(&avatar_dir, url.as_str());
                        if let Some(&at) = senders.get(&url) {
                            wanted[at].0.push(room.room_id().to_owned());
                        } else if !file.exists() && tried.insert(format!("sender|{url}")) {
                            senders.insert(url.clone(), wanted.len());
                            wanted.push((vec![room.room_id().to_owned()], url, file, None));
                        }
                    }
                }
                // Rooms without a known picture are looked at again next time:
                // a DM's peer may only become known after a later sync.
                let Some(url) = Self::avatar_source(&room, own.as_deref()).await else { continue };
                let file = avatar_file(&avatar_dir, &room);
                let marker = file.with_extension("url");
                // the picture under its 0.8.3 name, where it is this one
                if !file.exists() && crate::serialize::take_old_avatar(&avatar_dir, room.room_id().as_str(), url.as_str(), &file) {
                    arrived.send(room.room_id().to_owned()).ok();
                    continue;
                }
                // the cached one is this picture (a file from before the marker
                // counts as current, as before)
                if file.exists() {
                    match std::fs::read_to_string(&marker) {
                        Ok(known) if known.trim() != url.as_str() => {}
                        _ => continue,
                    }
                }
                if !tried.insert(format!("{}|{}", room.room_id(), url)) {
                    continue;
                }
                wanted.push((vec![room.room_id().to_owned()], url, file, Some(marker)));
            }
            let client = &client;
            let mut downloads = futures_util::stream::iter(wanted)
                .map(|(ids, url, file, marker)| async move {
                    let request = MediaRequestParameters {
                        source: MediaSource::Plain(url.clone()),
                        format: MediaFormat::Thumbnail(MediaThumbnailSettings::new(ROOM_AVATAR_PX.into(), ROOM_AVATAR_PX.into())),
                    };
                    match client.media().get_media_content(&request, true).await {
                        // only a JPEG, PNG, GIF or WebP is kept (audit UC2: Qt
                        // would pick any decoder for an .img by its content)
                        Ok(bytes) if crate::core::timeline::picture_extension(&bytes).is_none() => {
                            tracing::info!(room = %ids[0], "avatar: not a picture");
                            Vec::new()
                        }
                        Ok(bytes) => {
                            // whole or not at all: a row made meanwhile names the file once it exists
                            let part = file.with_extension("part");
                            if tokio::fs::write(&part, &bytes).await.is_ok() && tokio::fs::rename(&part, &file).await.is_ok() {
                                if let Some(marker) = marker {
                                    tokio::fs::write(&marker, url.as_str()).await.ok();
                                }
                                return ids;
                            }
                            tokio::fs::remove_file(&part).await.ok();
                            Vec::new()
                        }
                        Err(e) => {
                            tracing::info!(room = %ids[0], "avatar: {e}");
                            Vec::new()
                        }
                    }
                })
                .buffer_unordered(AVATAR_FETCHES_AT_ONCE);
            while let Some(done) = downloads.next().await {
                for id in done {
                    arrived.send(id).ok();
                }
            }
        }
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
        // the walk is blocking file work: off the runtime's two workers
        let dir = self.data_dir.clone();
        let (store, media) = tokio::task::spawn_blocking(move || {
            (dir_size(&dir.join(STORE_DIR)), dir_size(&dir.join(MEDIA_DIR)) + dir_size(&dir.join(AVATAR_DIR)))
        })
        .await?;
        self.emit(json!({
            "ev": "storage",
            "store": store,
            "media_files": media,
            "crypto": sizes.crypto_store,
            "state": sizes.state_store,
            "event_cache": sizes.event_cache_store,
            "media_cache": sizes.media_store,
        }));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The same state twice in a row is sent once; a change, a new error
    /// text and a state coming back are sent again.
    /// Planted-bug: compare only the name (a second, different error is
    /// not shown).
    #[test]
    fn a_repeated_sync_state_is_sent_once() {
        let mut sent = None;
        assert!(sync_state_changed(&mut sent, "running", &None));
        assert!(!sync_state_changed(&mut sent, "running", &None));
        assert!(sync_state_changed(&mut sent, "error", &Some("a".into())));
        assert!(sync_state_changed(&mut sent, "error", &Some("b".into())));
        assert!(!sync_state_changed(&mut sent, "error", &Some("b".into())));
        assert!(sync_state_changed(&mut sent, "running", &None));
    }

    fn id(s: &str) -> OwnedRoomId {
        OwnedRoomId::try_from(s).unwrap()
    }

    // Planted-bug: take_rows reports a change for a row equal to the cached one
    // (every quiet update sent the whole list again).
    #[test]
    fn equal_rows_change_nothing() {
        let mut cache = HashMap::from([(id("!a:x"), json!({"room_id": "!a:x", "ts": 1}))]);
        let mut changed = HashSet::new();
        assert!(!take_rows(&mut cache, vec![(id("!a:x"), json!({"room_id": "!a:x", "ts": 1}))], &mut changed));
        assert!(changed.is_empty());
        assert!(take_rows(&mut cache, vec![(id("!a:x"), json!({"room_id": "!a:x", "ts": 2}))], &mut changed));
        assert_eq!(cache[&id("!a:x")]["ts"], 2);
        assert!(take_rows(&mut cache, vec![(id("!b:x"), json!({"room_id": "!b:x"}))], &mut changed));
        assert!(!take_rows(&mut cache, vec![], &mut changed));
        assert_eq!(changed, HashSet::from([id("!a:x"), id("!b:x")]));
    }

    // Planted-bug: a request for some rooms after one for every room narrows it.
    #[test]
    fn picture_requests_join() {
        let some = |ids: &[&str]| Some(ids.iter().map(|s| id(s)).collect::<HashSet<_>>());
        assert_eq!(join_asks(some(&["!a:x"]), some(&["!b:x"])), some(&["!a:x", "!b:x"]));
        assert_eq!(join_asks(None, some(&["!b:x"])), None);
        assert_eq!(join_asks(some(&["!a:x"]), None), None);
    }

    #[test]
    fn sync_rests_longer_each_time_up_to_the_most() {
        assert_eq!(sync_rest(SYNC_QUICK_FAILS), SYNC_REST_FIRST);
        assert_eq!(sync_rest(SYNC_QUICK_FAILS + 1), SYNC_REST_FIRST * 2);
        assert_eq!(sync_rest(SYNC_QUICK_FAILS + 40), SYNC_REST_MAX);
    }
}
