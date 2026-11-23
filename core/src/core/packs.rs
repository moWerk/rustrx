// SPDX-License-Identifier: GPL-3.0-or-later
//! Custom emoji and stickers: Matrix image packs (MSC2545, as Element,
//! Cinny, FluffyChat and nheko use them), sending them, and adding one to
//! the user's own pack. (part of `impl Core`; docs/EMOJI.md has the design)

use super::*;
use matrix_sdk::ruma::api::client::state::{get_state_event_for_key, get_state_events};
use matrix_sdk::ruma::events::room::ImageInfo;
use matrix_sdk::ruma::events::room::message::{FormattedBody, MessageFormat, MessageType};
use matrix_sdk::ruma::events::sticker::StickerEventContent;
use matrix_sdk::ruma::events::{
    AnyGlobalAccountDataEventContent, AnyMessageLikeEventContent, GlobalAccountDataEventType, StateEventType,
};
use matrix_sdk::ruma::serde::Raw;
use std::path::Path;
use crate::serialize::escape_html;

const USER_EMOTES: &str = "im.ponies.user_emotes";
const EMOTE_ROOMS: &str = "im.ponies.emote_rooms";
const ROOM_EMOTES: &str = "im.ponies.room_emotes";

/// One pack as the UI takes it: its images with their usage resolved
/// (an image without its own usage takes the pack's; none at all is both).
fn pack_json(id: &str, source: &str, content: &Value) -> Option<Value> {
    let images = content.get("images")?.as_object()?;
    let pack = content.get("pack");
    let pack_usage = pack.and_then(|p| p.get("usage")).and_then(Value::as_array).cloned();
    let mut out = Vec::new();
    for (code, img) in images {
        let Some(url) = img.get("url").and_then(Value::as_str).filter(|u| u.starts_with("mxc://")) else { continue };
        let usage = img.get("usage").and_then(Value::as_array).cloned().or_else(|| pack_usage.clone());
        let usage: Vec<&str> = usage.as_ref().map(|u| u.iter().filter_map(Value::as_str).collect()).unwrap_or_default();
        let emoticon = usage.is_empty() || usage.contains(&"emoticon");
        let sticker = usage.is_empty() || usage.contains(&"sticker");
        out.push(json!({
            "shortcode": code, "mxc": url,
            "body": img.get("body").and_then(Value::as_str).unwrap_or(code),
            "info": img.get("info").cloned().unwrap_or(Value::Null),
            "emoticon": emoticon, "sticker": sticker,
        }));
    }
    if out.is_empty() {
        return None;
    }
    out.sort_by(|a, b| a["shortcode"].as_str().cmp(&b["shortcode"].as_str()));
    let name = pack.and_then(|p| p.get("display_name")).and_then(Value::as_str).unwrap_or(id);
    let avatar = pack.and_then(|p| p.get("avatar_url")).and_then(Value::as_str);
    Some(json!({"id": format!("{source}:{id}"), "name": name, "source": source,
                "avatar_mxc": avatar.or_else(|| out[0]["mxc"].as_str()), "images": out}))
}

/// A message's text with its `:shortcode:` custom emoji: the body keeps the
/// shortcode, the formatted body gets the picture (as the spec has it).
pub(crate) fn with_emotes(content: &mut RoomMessageEventContent, emotes: &HashMap<String, String>) {
    let MessageType::Text(t) = &mut content.msgtype else { return };
    let used: Vec<(&String, &String)> = emotes
        .iter()
        .filter(|(code, url)| url.starts_with("mxc://") && t.body.contains(&format!(":{code}:")))
        .collect();
    if used.is_empty() {
        return;
    }
    let mut html = match &t.formatted {
        Some(f) if f.format == MessageFormat::Html => f.body.clone(),
        // a message that had no formatting of its own
        _ => escape_html(&t.body).replace('\n', "<br>"),
    };
    for (code, url) in used {
        let code = escape_html(code);
        html = html.replace(
            &format!(":{code}:"),
            &format!("<img data-mx-emoticon src=\"{url}\" alt=\":{code}:\" title=\":{code}:\" height=\"{EMOTE_HTML_HEIGHT}\">"),
        );
    }
    t.formatted = Some(FormattedBody::html(html));
}

/// How long a room's packs (and another room's pack turned on everywhere)
/// are answered without asking the server again. A pack changed in a room
/// that syncs here is asked again at once (the state handler); an older
/// answer of a room's own packs is still shown and asked again behind it.
const PACKS_TTL: u64 = 24 * 60 * 60;

/// Keys of the answers kept in the account's store (encrypted with it,
/// removed with it at logout): one per room, one for the enabled packs.
const STORE_ROOM_KEY: &str = "rustrx.packs.v1.room:";
const STORE_GLOBAL_KEY: &[u8] = b"rustrx.packs.v1.global";
/// The account-data types the server had none of, and when it said so.
const STORE_MISSING_KEY: &[u8] = b"rustrx.packs.v1.missing";

fn unix_now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// An answer from `at` (unix seconds) is still current at `now`; one from
/// the future (the clock went back) is not.
fn is_fresh(at: u64, now: u64) -> bool {
    now >= at && now - at < PACKS_TTL
}

type RoomPacks = Vec<(String, Value)>;

/// What image_packs asked the server, per account (its data folder) and
/// login, in memory and in the account's store. The own pack and the list
/// of enabled packs come from the store, which the sync keeps current; this
/// holds what the sync does not bring.
#[derive(Default)]
struct PackCache {
    generation: u64,
    handlers: bool,
    /// bumped by every invalidation: an answer asked before it is not kept
    epoch: u64,
    /// the room's own packs: when asked, (state key, content) of each
    room: HashMap<String, (u64, RoomPacks)>,
    /// rooms whose kept packs are being asked again behind the answer
    refreshing: std::collections::HashSet<String>,
    /// a pack in another room by (room, state key); None: the server has
    /// none (a pack removed but still listed as enabled, asked on every
    /// opening before), until that room's packs change
    global: HashMap<(String, String), (u64, Option<Value>)>,
    /// `global` was read from the store in this login
    global_loaded: bool,
    /// the own pack as last written here, until the sync brings it back
    user_written: Option<Value>,
    /// account data the store did not have, as the server answered (once
    /// per login, kept until the sync brings that type)
    fetched: HashMap<&'static str, Option<Raw<AnyGlobalAccountDataEventContent>>>,
    /// account-data types the server had none of, by type: when it said so
    /// (kept in the store too, so a restart does not ask again)
    missing: HashMap<String, u64>,
    /// `missing` was read from the store in this login
    missing_loaded: bool,
}

fn pack_caches() -> &'static std::sync::Mutex<HashMap<PathBuf, PackCache>> {
    static CACHES: std::sync::OnceLock<std::sync::Mutex<HashMap<PathBuf, PackCache>>> = std::sync::OnceLock::new();
    CACHES.get_or_init(Default::default)
}

/// Held around every read or write of the kept answers in the store and
/// the matching change in memory, so an invalidation cannot fall between
/// an answer's epoch check and its write.
fn store_lock() -> &'static tokio::sync::Mutex<()> {
    static LOCK: std::sync::OnceLock<tokio::sync::Mutex<()>> = std::sync::OnceLock::new();
    LOCK.get_or_init(Default::default)
}

/// The account's cache, emptied when another login began; None for a task
/// of a login that has ended.
fn with_cache<R>(dir: &Path, generation: u64, f: impl FnOnce(&mut PackCache) -> R) -> Option<R> {
    let mut all = pack_caches().lock().ok()?;
    let c = all.entry(dir.to_owned()).or_default();
    if generation < c.generation {
        return None;
    }
    if generation > c.generation {
        *c = PackCache { generation, ..Default::default() };
    }
    Some(f(c))
}

/// A room's packs changed: its own and the ones of it enabled everywhere
/// are asked again. True when an enabled one of it was known.
fn forget_room(c: &mut PackCache, room: &str) -> bool {
    c.epoch += 1;
    c.room.remove(room);
    let before = c.global.len();
    c.global.retain(|(r, _), _| r != room);
    c.global.len() != before
}

fn encode_room(at: u64, own: &RoomPacks) -> Vec<u8> {
    serde_json::to_vec(&json!({"at": at, "packs": own})).unwrap_or_default()
}

fn decode_room(bytes: &[u8]) -> Option<(u64, RoomPacks)> {
    let v: Value = serde_json::from_slice(bytes).ok()?;
    Some((v.get("at")?.as_u64()?, serde_json::from_value(v.get("packs")?.clone()).ok()?))
}

fn encode_global(global: &HashMap<(String, String), (u64, Option<Value>)>) -> Vec<u8> {
    let entries: Vec<Value> = global.iter().map(|((r, k), (at, v))| json!([r, k, at, v])).collect();
    serde_json::to_vec(&entries).unwrap_or_default()
}

fn decode_global(bytes: &[u8]) -> HashMap<(String, String), (u64, Option<Value>)> {
    let entries: Vec<(String, String, u64, Option<Value>)> = serde_json::from_slice(bytes).unwrap_or_default();
    entries.into_iter().map(|(r, k, at, v)| ((r, k), (at, v.filter(|v| !v.is_null())))).collect()
}

fn encode_missing(missing: &HashMap<String, u64>) -> Vec<u8> {
    serde_json::to_vec(missing).unwrap_or_default()
}

fn decode_missing(bytes: &[u8]) -> HashMap<String, u64> {
    serde_json::from_slice(bytes).unwrap_or_default()
}

/// The (state key, content) of each pack event, by state key.
fn own_packs<'a>(events: impl Iterator<Item = &'a Raw<matrix_sdk::ruma::events::AnySyncStateEvent>>) -> RoomPacks {
    let mut own: RoomPacks = events
        .filter_map(|ev| {
            let key = ev.get_field::<String>("state_key").ok().flatten().unwrap_or_default();
            ev.get_field::<Value>("content").ok().flatten().map(|content| (key, content))
        })
        .collect();
    own.sort_by(|a, b| a.0.cmp(&b.0));
    own
}

/// The server has no such event (as opposed to being unreachable).
fn is_missing(e: &matrix_sdk::HttpError) -> bool {
    use matrix_sdk::ruma::api::error::ErrorKind;
    matches!(e.client_api_error_kind(), Some(ErrorKind::NotFound | ErrorKind::Forbidden))
}

/// With the classic /sync a joined room's whole state is in the store (pack
/// events included) and kept current by it; sliding sync brings only the
/// state types it asks for, which leave the packs out.
fn state_in_store(client: &Client, room: &Room) -> bool {
    matches!(client.sliding_sync_version(), matrix_sdk::sliding_sync::Version::None)
        && room.state() == matrix_sdk::RoomState::Joined
        && room.is_state_fully_synced()
}

/// The room's whole state, the only way to find its packs (the state API
/// has no type filter); only the pack events of it are read. With the
/// number of events and their bytes.
async fn fetch_room_packs(client: &Client, room_id: &matrix_sdk::ruma::RoomId) -> std::result::Result<(RoomPacks, usize, usize), matrix_sdk::HttpError> {
    let r = client.send(get_state_events::v3::Request::new(room_id.to_owned())).await?;
    let bytes = r.room_state.iter().map(|ev| ev.json().get().len()).sum();
    let packs: Vec<Raw<matrix_sdk::ruma::events::AnySyncStateEvent>> = r
        .room_state
        .iter()
        .filter(|ev| ev.get_field::<String>("type").ok().flatten().as_deref() == Some(ROOM_EMOTES))
        .map(|ev| ev.clone().cast_unchecked())
        .collect();
    Ok((own_packs(packs.iter()), r.room_state.len(), bytes))
}

impl Core {
    /// The cache's invalidation, on this login's client: a room's pack state
    /// event, and the own pack coming back through the sync.
    fn install_pack_handlers(&'static self, client: &Client, generation: u64) {
        client.add_event_handler(move |raw: Raw<matrix_sdk::ruma::events::AnySyncStateEvent>, room: Room| async move {
            // every state event comes here, member events included: the type first
            if raw.get_field::<String>("type").ok().flatten().as_deref() != Some(ROOM_EMOTES) {
                return;
            }
            let client = room.client();
            let _held = store_lock().lock().await;
            self.load_global(&client, generation).await;
            let rid = room.room_id().as_str();
            let Some(global_changed) = with_cache(&self.data_dir, generation, |c| forget_room(c, rid)) else { return };
            let store = client.state_store();
            if let Err(e) = store.remove_custom_value(format!("{STORE_ROOM_KEY}{rid}").as_bytes()).await {
                tracing::info!("packs: kept room packs not removed: {e}");
            }
            if global_changed {
                self.store_global(&client, generation).await;
            }
        });
        client.add_event_handler(move |raw: Raw<matrix_sdk::ruma::events::AnyGlobalAccountDataEvent>, client: Client| async move {
            // the store has it from now on
            let kind = match raw.get_field::<String>("type").ok().flatten().as_deref() {
                Some(USER_EMOTES) => USER_EMOTES,
                Some(EMOTE_ROOMS) => EMOTE_ROOMS,
                _ => return,
            };
            with_cache(&self.data_dir, generation, |c| {
                if kind == USER_EMOTES {
                    c.user_written = None;
                }
                c.fetched.remove(kind);
            });
            self.forget_missing(&client, generation, kind).await;
        });
    }

    /// The handlers on this login's client, once; returns the cache's epoch.
    fn pack_cache_ready(&'static self, client: &Client, generation: u64) -> u64 {
        let (install, epoch) = with_cache(&self.data_dir, generation, |c| (!std::mem::replace(&mut c.handlers, true), c.epoch))
            .unwrap_or((false, 0));
        if install {
            self.install_pack_handlers(client, generation);
        }
        epoch
    }

    /// The enabled packs kept in the store, into memory once per login.
    /// Called with store_lock held.
    async fn load_global(&self, client: &Client, generation: u64) {
        if with_cache(&self.data_dir, generation, |c| c.global_loaded) != Some(false) {
            return;
        }
        let kept = match client.state_store().get_custom_value(STORE_GLOBAL_KEY).await {
            Ok(v) => v.map(|b| decode_global(&b)).unwrap_or_default(),
            Err(e) => {
                tracing::info!("packs: kept enabled packs not read: {e}");
                HashMap::new()
            }
        };
        with_cache(&self.data_dir, generation, |c| {
            c.global_loaded = true;
            for (id, entry) in kept {
                c.global.entry(id).or_insert(entry);
            }
        });
    }

    /// Called with store_lock held.
    async fn store_global(&self, client: &Client, generation: u64) {
        let Some(bytes) = with_cache(&self.data_dir, generation, |c| encode_global(&c.global)) else { return };
        if let Err(e) = client.state_store().set_custom_value_no_read(STORE_GLOBAL_KEY, bytes).await {
            tracing::info!("packs: enabled packs not kept: {e}");
        }
    }

    /// The account-data types the server had none of, from the store into
    /// memory once per login. Called with store_lock held.
    async fn load_missing(&self, client: &Client, generation: u64) {
        if with_cache(&self.data_dir, generation, |c| c.missing_loaded) != Some(false) {
            return;
        }
        let kept = match client.state_store().get_custom_value(STORE_MISSING_KEY).await {
            Ok(v) => v.map(|b| decode_missing(&b)).unwrap_or_default(),
            Err(e) => {
                tracing::info!("packs: kept missing account data not read: {e}");
                HashMap::new()
            }
        };
        with_cache(&self.data_dir, generation, |c| {
            c.missing_loaded = true;
            for (kind, at) in kept {
                c.missing.entry(kind).or_insert(at);
            }
        });
    }

    /// Called with store_lock held.
    async fn store_missing(&self, client: &Client, generation: u64) {
        let Some(bytes) = with_cache(&self.data_dir, generation, |c| encode_missing(&c.missing)) else { return };
        if let Err(e) = client.state_store().set_custom_value_no_read(STORE_MISSING_KEY, bytes).await {
            tracing::info!("packs: missing account data not kept: {e}");
        }
    }

    /// The account data of `kind` exists now (the sync brought it, or it
    /// was written here): its "none" answer goes, in memory and the store.
    async fn forget_missing(&self, client: &Client, generation: u64, kind: &str) {
        let _held = store_lock().lock().await;
        self.load_missing(client, generation).await;
        if with_cache(&self.data_dir, generation, |c| c.missing.remove(kind).is_some()) == Some(true) {
            self.store_missing(client, generation).await;
        }
    }

    /// A room's own packs from memory, or from the store into memory.
    async fn kept_room_packs(&self, client: &Client, generation: u64, rid: &str, from_store: &mut usize) -> Option<(u64, RoomPacks)> {
        if let Some(known) = with_cache(&self.data_dir, generation, |c| c.room.get(rid).cloned()).flatten() {
            return Some(known);
        }
        let _held = store_lock().lock().await;
        let bytes = match client.state_store().get_custom_value(format!("{STORE_ROOM_KEY}{rid}").as_bytes()).await {
            Ok(b) => b?,
            Err(e) => {
                tracing::info!("packs: kept room packs not read: {e}");
                return None;
            }
        };
        let kept = decode_room(&bytes)?;
        *from_store += 1;
        with_cache(&self.data_dir, generation, |c| c.room.entry(rid.to_owned()).or_insert(kept).clone())
    }

    /// A room's packs as the server answered, in memory and in the store,
    /// unless an invalidation came since `epoch`.
    async fn keep_room_packs(&self, client: &Client, generation: u64, epoch: u64, rid: &str, own: &RoomPacks) {
        let _held = store_lock().lock().await;
        let at = unix_now();
        let kept = with_cache(&self.data_dir, generation, |c| {
            if c.epoch != epoch {
                return false;
            }
            c.room.insert(rid.to_owned(), (at, own.clone()));
            true
        });
        if kept != Some(true) {
            return;
        }
        if let Err(e) = client.state_store().set_custom_value_no_read(format!("{STORE_ROOM_KEY}{rid}").as_bytes(),
                                                                      encode_room(at, own)).await {
            tracing::info!("packs: room packs not kept: {e}");
        }
    }

    /// Kept room packs older than PACKS_TTL, asked again behind the answer
    /// already shown; a changed answer is sent as a new "image_packs".
    fn revalidate_room_packs(&'static self, client: Client, generation: u64, room_id: String, rid: String, old: RoomPacks) {
        let start = with_cache(&self.data_dir, generation, |c| c.refreshing.insert(rid.clone()));
        if start != Some(true) {
            return;
        }
        self.rt.spawn(async move {
            let t = std::time::Instant::now();
            let epoch = self.pack_cache_ready(&client, generation);
            let changed = match OwnedRoomId::try_from(rid.as_str()) {
                Ok(id) => match fetch_room_packs(&client, &id).await {
                    Ok((own, state_events, bytes)) => {
                        self.keep_room_packs(&client, generation, epoch, &rid, &own).await;
                        let changed = own != old;
                        tracing::debug!(target: "rustrx::perf", changed, state_events, bytes,
                                        ms = t.elapsed().as_millis() as u64, "image_packs_revalidate");
                        changed
                    }
                    Err(e) => {
                        tracing::info!(room = %rid, "packs: room packs again: {e}");
                        false
                    }
                },
                Err(_) => false,
            };
            with_cache(&self.data_dir, generation, |c| c.refreshing.remove(&rid));
            if changed && self.generation().await == generation {
                if let Err(e) = self.image_packs(&room_id).await {
                    tracing::info!("packs: {e}");
                }
            }
        });
    }

    /// Global account data from the store, or from the server where the
    /// store has none (asked once per login: none is the usual answer, and
    /// that one is kept in the store for PACKS_TTL). The store comes first:
    /// a kept "none" never hides what the sync brought since.
    async fn pack_account_data(&self, client: &Client, generation: u64, kind: &'static str, requests: &mut usize)
                               -> Option<Raw<AnyGlobalAccountDataEventContent>> {
        let account = client.account();
        match account.account_data_raw(GlobalAccountDataEventType::from(kind)).await {
            Ok(Some(raw)) => return Some(raw),
            Ok(None) => {}
            Err(e) => tracing::info!("packs: {kind} from the store: {e}"),
        }
        if let Some(known) = with_cache(&self.data_dir, generation, |c| c.fetched.get(kind).cloned()).flatten() {
            return known;
        }
        {
            let _held = store_lock().lock().await;
            self.load_missing(client, generation).await;
        }
        let now = unix_now();
        if with_cache(&self.data_dir, generation, |c| c.missing.get(kind).is_some_and(|at| is_fresh(*at, now))) == Some(true) {
            return None;
        }
        *requests += 1;
        match account.fetch_account_data(GlobalAccountDataEventType::from(kind)).await {
            Ok(r) => {
                with_cache(&self.data_dir, generation, |c| c.fetched.insert(kind, r.clone()));
                let _held = store_lock().lock().await;
                let changed = with_cache(&self.data_dir, generation, |c| match &r {
                    None => {
                        c.missing.insert(kind.to_owned(), now);
                        true
                    }
                    Some(_) => c.missing.remove(kind).is_some(),
                });
                if changed == Some(true) {
                    self.store_missing(client, generation).await;
                }
                r
            }
            Err(e) => {
                // not kept: asked again next time
                tracing::info!("packs: {kind}: {e}");
                None
            }
        }
    }

    /// The packs offered in a room: the user's own, the ones the user
    /// enabled everywhere, the room's own. Answered by "image_packs".
    pub(crate) async fn image_packs(&'static self, room_id: &str) -> Result<()> {
        let t = std::time::Instant::now();
        // perf: the requests made and the bytes they brought; answers read
        // from the store (kept ones, or the room state of the classic sync)
        let (mut requests, mut bytes, mut state_events, mut from_store, mut revalidate) = (0, 0, 0, 0, false);
        let generation = self.generation().await;
        let client = self.client().await?;
        let room = self.room(room_id).await?;
        let rid = room.room_id().as_str().to_owned();
        let epoch = self.pack_cache_ready(&client, generation);
        let now = unix_now();
        let mut packs = Vec::new();

        let written = with_cache(&self.data_dir, generation, |c| c.user_written.clone()).flatten();
        let user = match written {
            Some(v) => Some(v),
            None => self.pack_account_data(&client, generation, USER_EMOTES, &mut requests).await.map(|raw| {
                bytes += raw.json().get().len();
                raw_value(&raw)
            }),
        };
        if let Some(v) = user {
            packs.extend(pack_json("user", "user", &v));
        }

        // other rooms' packs the user turned on for every room
        let mut seen = std::collections::HashSet::new();
        if let Some(raw) = self.pack_account_data(&client, generation, EMOTE_ROOMS, &mut requests).await {
            let v = raw_value(&raw);
            if let Some(rooms) = v.get("rooms").and_then(Value::as_object) {
                {
                    let _held = store_lock().lock().await;
                    self.load_global(&client, generation).await;
                }
                let mut asked = Vec::new();
                for (other, keys) in rooms {
                    let Ok(other) = OwnedRoomId::try_from(other.as_str()) else { continue };
                    let local = client.get_room(&other).filter(|r| state_in_store(&client, r));
                    for key in keys.as_object().map(|k| k.keys().cloned().collect::<Vec<_>>()).unwrap_or_default() {
                        let id = (other.as_str().to_owned(), key.clone());
                        let known = match &local {
                            Some(r) => {
                                from_store += 1;
                                match r.get_state_event(StateEventType::from(ROOM_EMOTES), &key).await {
                                    Ok(Some(matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState::Sync(ev))) => {
                                        Some(ev.get_field::<Value>("content").ok().flatten())
                                    }
                                    Ok(_) => Some(None),
                                    Err(e) => {
                                        tracing::info!(room = %other, "packs: enabled pack from the store: {e}");
                                        None
                                    }
                                }
                            }
                            None => with_cache(&self.data_dir, generation, |c| {
                                c.global.get(&id).filter(|(at, _)| is_fresh(*at, now)).map(|(_, v)| v.clone())
                            })
                            .flatten(),
                        };
                        let content = match known {
                            Some(v) => v,
                            None => {
                                let request = get_state_event_for_key::v3::Request::new(
                                    other.clone(), StateEventType::from(ROOM_EMOTES), key.clone());
                                requests += 1;
                                let answer = match client.send(request).await {
                                    Ok(r) => {
                                        bytes += r.event_or_content.get().len();
                                        Some(serde_json::from_str(r.event_or_content.get()).unwrap_or(Value::Null))
                                    }
                                    Err(e) if is_missing(&e) => {
                                        tracing::info!(room = %other, key = %key, "packs: enabled pack is gone: {e}");
                                        None
                                    }
                                    Err(e) => {
                                        // not kept: asked again at the next opening
                                        tracing::info!(room = %other, key = %key, "packs: enabled pack: {e}");
                                        continue;
                                    }
                                };
                                asked.push((id, answer.clone()));
                                answer
                            }
                        };
                        if let Some(p) = content.and_then(|c| pack_json(&format!("{other}/{key}"), "global", &c)) {
                            seen.insert(format!("{other}/{key}"));
                            packs.push(p);
                        }
                    }
                }
                if !asked.is_empty() {
                    let _held = store_lock().lock().await;
                    let keep = with_cache(&self.data_dir, generation, |c| {
                        if c.epoch != epoch {
                            return false;
                        }
                        for (id, answer) in asked {
                            c.global.insert(id, (now, answer));
                        }
                        true
                    });
                    if keep == Some(true) {
                        self.store_global(&client, generation).await;
                    }
                }
            }
        }

        // the room's own packs (one per state key)
        let own = if state_in_store(&client, &room) {
            from_store += 1;
            match room.get_state_events(StateEventType::from(ROOM_EMOTES)).await {
                Ok(events) => {
                    use matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState;
                    let events: Vec<_> = events
                        .into_iter()
                        .filter_map(|e| match e {
                            RawAnySyncOrStrippedState::Sync(ev) => Some(ev),
                            RawAnySyncOrStrippedState::Stripped(_) => None,
                        })
                        .collect();
                    Some(own_packs(events.iter()))
                }
                Err(e) => {
                    tracing::info!(room = %room_id, "packs: room packs from the store: {e}");
                    None
                }
            }
        } else {
            match self.kept_room_packs(&client, generation, &rid, &mut from_store).await {
                Some((at, own)) => {
                    if !is_fresh(at, now) {
                        revalidate = true;
                        self.revalidate_room_packs(client.clone(), generation, room_id.to_owned(), rid.clone(), own.clone());
                    }
                    Some(own)
                }
                None => {
                    requests += 1;
                    match fetch_room_packs(&client, room.room_id()).await {
                        Ok((own, events, size)) => {
                            state_events = events;
                            bytes += size;
                            self.keep_room_packs(&client, generation, epoch, &rid, &own).await;
                            Some(own)
                        }
                        Err(e) => {
                            tracing::info!(room = %room_id, "packs: room packs: {e}");
                            None
                        }
                    }
                }
            }
        };
        for (key, content) in own.unwrap_or_default() {
            let id = format!("{rid}/{key}");
            if !seen.contains(&id) {
                packs.extend(pack_json(&id, "room", &content));
            }
        }

        tracing::debug!(target: "rustrx::perf", packs = packs.len(), requests, bytes, state_events, from_store,
                        revalidate, ms = t.elapsed().as_millis() as u64, "image_packs");
        self.emit(json!({"ev": "image_packs", "room_id": room_id, "packs": packs}));
        Ok(())
    }

    /// A sticker from a pack, through the timeline so it shows at once.
    pub(crate) async fn send_sticker(
        &'static self,
        room_id: &str,
        url: &str,
        body: &str,
        info: Option<Value>,
        thread_root: Option<&str>,
    ) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_target(&room, thread_root).await?;
        let url = OwnedMxcUri::from(url);
        url.validate()?;
        let info: ImageInfo = info
            .filter(|i| i.is_object())
            .and_then(|i| serde_json::from_value(i).ok())
            .unwrap_or_default();
        let content = StickerEventContent::new(body.to_owned(), info, url);
        tl.send(AnyMessageLikeEventContent::Sticker(content)).await?;
        Ok(())
    }

    /// A custom emoji seen somewhere into the user's own pack (its address
    /// is reused, nothing is uploaded). Answered by "emote_added".
    pub(crate) async fn add_user_emote(&'static self, shortcode: &str, url: &str) -> Result<()> {
        let code: String = shortcode
            .trim_matches(':')
            .chars()
            .filter(|c| !c.is_whitespace() && *c != ':')
            .collect();
        if code.is_empty() {
            bail!("no shortcode");
        }
        OwnedMxcUri::from(url).validate()?;
        let generation = self.generation().await;
        let client = self.client().await?;
        let account = client.account();
        let kind = GlobalAccountDataEventType::from(USER_EMOTES);
        let mut v = match account.fetch_account_data(kind.clone()).await? {
            Some(raw) => raw_value(&raw),
            None => json!({}),
        };
        if !v.is_object() {
            v = json!({});
        }
        let images = v.as_object_mut().unwrap().entry("images").or_insert_with(|| json!({}));
        if !images.is_object() {
            *images = json!({});
        }
        // a taken shortcode gets a number
        let mut name = code.clone();
        let mut n = 2;
        while images.get(&name).is_some_and(|i| i.get("url").and_then(Value::as_str) != Some(url)) {
            name = format!("{code}{n}");
            n += 1;
        }
        images[&name] = json!({"url": url});
        let raw: Raw<AnyGlobalAccountDataEventContent> = Raw::new(&v)?.cast_unchecked();
        account.set_account_data_raw(kind, raw).await?;
        // the store has it once the sync brings it back; offered from now
        self.pack_cache_ready(&client, generation);
        with_cache(&self.data_dir, generation, |c| c.user_written = Some(v));
        self.forget_missing(&client, generation, USER_EMOTES).await;
        self.emit(json!({"ev": "emote_added", "shortcode": name, "mxc": url}));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cache_is_per_login_and_old_tasks_are_ignored() {
        let dir = PathBuf::from("/test/packs/login");
        with_cache(&dir, 1, |c| c.user_written = Some(json!({"a": 1})));
        assert!(with_cache(&dir, 1, |c| c.user_written.clone()).flatten().is_some());
        // a new login starts empty
        assert!(with_cache(&dir, 2, |c| c.user_written.clone()).flatten().is_none());
        // a task of the old login writes nothing any more
        // (Planted-bug: reset on any other generation)
        assert!(with_cache(&dir, 1, |c| c.user_written = Some(json!({}))).is_none());
        assert!(with_cache(&dir, 2, |c| c.user_written.clone()).flatten().is_none());
    }

    #[test]
    fn a_rooms_pack_event_forgets_its_packs_only() {
        let dir = PathBuf::from("/test/packs/forget");
        let now = unix_now();
        with_cache(&dir, 1, |c| {
            c.room.insert("!a:x".into(), (now, vec![]));
            c.room.insert("!b:x".into(), (now, vec![]));
            c.global.insert(("!a:x".into(), "".into()), (now, None));
            c.global.insert(("!b:x".into(), "k".into()), (now, None));
        });
        let before = with_cache(&dir, 1, |c| c.epoch).unwrap();
        assert_eq!(with_cache(&dir, 1, |c| forget_room(c, "!a:x")), Some(true));
        with_cache(&dir, 1, |c| {
            assert!(c.epoch > before);
            assert!(!c.room.contains_key("!a:x") && c.room.contains_key("!b:x"));
            assert_eq!(c.global.len(), 1);
            assert!(c.global.contains_key(&("!b:x".to_owned(), "k".to_owned())));
        });
    }

    #[test]
    fn account_data_from_the_server_is_kept_for_the_login() {
        // Planted-bug: remember only that it was asked, not the answer
        let dir = PathBuf::from("/test/packs/fetched");
        let raw: Raw<AnyGlobalAccountDataEventContent> = Raw::new(&json!({"images": {}})).unwrap().cast_unchecked();
        with_cache(&dir, 1, |c| c.fetched.insert(USER_EMOTES, Some(raw)));
        let again = with_cache(&dir, 1, |c| c.fetched.get(USER_EMOTES).cloned()).flatten();
        assert!(matches!(again, Some(Some(_))));
        // the server had none: that answer is kept too, and asked no more
        with_cache(&dir, 1, |c| c.fetched.insert(EMOTE_ROOMS, None));
        assert!(matches!(with_cache(&dir, 1, |c| c.fetched.get(EMOTE_ROOMS).cloned()).flatten(), Some(None)));
    }

    #[test]
    fn kept_answers_expire_and_a_clock_gone_back_asks_again() {
        // Planted-bug: is_fresh without the now >= at check
        let now = 1_800_000_000;
        assert!(is_fresh(now, now));
        assert!(is_fresh(now - PACKS_TTL + 1, now));
        assert!(!is_fresh(now - PACKS_TTL, now));
        assert!(!is_fresh(now + 60, now));
    }

    #[test]
    fn kept_answers_survive_the_store() {
        // Planted-bug: a gone pack (None) read back as Some(Null)
        let own = vec![("".to_owned(), json!({"images": {"a": {"url": "mxc://x/a"}}})), ("k".to_owned(), json!({}))];
        assert_eq!(decode_room(&encode_room(7, &own)), Some((7, own)));
        assert_eq!(decode_room(b"garbage"), None);
        let mut global = HashMap::new();
        global.insert(("!a:x".to_owned(), "".to_owned()), (5, Some(json!({"images": {}}))));
        global.insert(("!b:x".to_owned(), "k".to_owned()), (6, None));
        assert_eq!(decode_global(&encode_global(&global)), global);
        assert!(decode_global(b"{").is_empty());
    }

    /// The server's "none" for an account-data type survives the store
    /// with its time, and expires like a kept pack.
    /// Planted-bug: decode_missing reading the times as 0 (every kept
    /// "none" is stale: a restart asks again).
    #[test]
    fn missing_account_data_survives_the_store() {
        let now = unix_now();
        let mut missing = HashMap::new();
        missing.insert(EMOTE_ROOMS.to_owned(), now);
        missing.insert(USER_EMOTES.to_owned(), now - PACKS_TTL);
        let back = decode_missing(&encode_missing(&missing));
        assert_eq!(back, missing);
        assert!(is_fresh(back[EMOTE_ROOMS], now));
        assert!(!is_fresh(back[USER_EMOTES], now));
        assert!(decode_missing(b"[").is_empty());
    }

    #[test]
    fn own_packs_reads_the_pack_events_by_state_key() {
        let raw = |key: &str| -> Raw<matrix_sdk::ruma::events::AnySyncStateEvent> {
            Raw::new(&json!({"type": ROOM_EMOTES, "state_key": key, "content": {"images": {}},
                             "event_id": "$e", "sender": "@a:x", "origin_server_ts": 1}))
                .unwrap()
                .cast_unchecked()
        };
        let events = [raw("z"), raw("")];
        let own = own_packs(events.iter());
        assert_eq!(own.iter().map(|(k, _)| k.as_str()).collect::<Vec<_>>(), vec!["", "z"]);
    }
}
