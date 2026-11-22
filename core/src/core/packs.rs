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
/// that syncs here is asked again at once (the state handler).
const PACKS_TTL: std::time::Duration = std::time::Duration::from_secs(30 * 60);

/// What image_packs asked the server, per account (its data folder) and
/// login. The own pack and the list of enabled packs come from the store,
/// which the sync keeps current; this holds what the sync does not bring.
#[derive(Default)]
struct PackCache {
    generation: u64,
    handlers: bool,
    /// bumped by every invalidation: an answer asked before it is not kept
    epoch: u64,
    /// the room's own packs: (state key, content) of each
    room: HashMap<String, (std::time::Instant, Vec<(String, Value)>)>,
    /// a pack in another room by (room, state key); None: the server has
    /// none (a pack removed but still listed as enabled, asked on every
    /// opening before), until that room's packs change
    global: HashMap<(String, String), (std::time::Instant, Option<Value>)>,
    /// the own pack as last written here, until the sync brings it back
    user_written: Option<Value>,
    /// account data the store did not have, as the server answered (once
    /// per login, kept until the sync brings that type)
    fetched: HashMap<&'static str, Option<Raw<AnyGlobalAccountDataEventContent>>>,
}

fn pack_caches() -> &'static std::sync::Mutex<HashMap<PathBuf, PackCache>> {
    static CACHES: std::sync::OnceLock<std::sync::Mutex<HashMap<PathBuf, PackCache>>> = std::sync::OnceLock::new();
    CACHES.get_or_init(Default::default)
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
/// are asked again.
fn forget_room(c: &mut PackCache, room: &str) {
    c.epoch += 1;
    c.room.remove(room);
    c.global.retain(|(r, _), _| r != room);
}

/// The server has no such event (as opposed to being unreachable).
fn is_missing(e: &matrix_sdk::HttpError) -> bool {
    use matrix_sdk::ruma::api::error::ErrorKind;
    matches!(e.client_api_error_kind(), Some(ErrorKind::NotFound | ErrorKind::Forbidden))
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
            with_cache(&self.data_dir, generation, |c| forget_room(c, room.room_id().as_str()));
        });
        client.add_event_handler(move |raw: Raw<matrix_sdk::ruma::events::AnyGlobalAccountDataEvent>| async move {
            // the store has it from now on
            match raw.get_field::<String>("type").ok().flatten().as_deref() {
                Some(USER_EMOTES) => with_cache(&self.data_dir, generation, |c| {
                    c.user_written = None;
                    c.fetched.remove(USER_EMOTES);
                }),
                Some(EMOTE_ROOMS) => with_cache(&self.data_dir, generation, |c| {
                    c.fetched.remove(EMOTE_ROOMS);
                }),
                _ => None,
            };
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

    /// Global account data from the store, or from the server where the
    /// store has none (asked once per login: none is the usual answer).
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
        *requests += 1;
        match account.fetch_account_data(GlobalAccountDataEventType::from(kind)).await {
            Ok(r) => {
                with_cache(&self.data_dir, generation, |c| c.fetched.insert(kind, r.clone()));
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
        // perf: the requests made and the bytes they brought
        let (mut requests, mut bytes, mut state_events) = (0, 0, 0);
        let generation = self.generation().await;
        let client = self.client().await?;
        let room = self.room(room_id).await?;
        let rid = room.room_id().as_str().to_owned();
        let epoch = self.pack_cache_ready(&client, generation);
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
                for (other, keys) in rooms {
                    let Ok(other) = OwnedRoomId::try_from(other.as_str()) else { continue };
                    for key in keys.as_object().map(|k| k.keys().cloned().collect::<Vec<_>>()).unwrap_or_default() {
                        let id = (other.as_str().to_owned(), key.clone());
                        let known = with_cache(&self.data_dir, generation, |c| {
                            c.global.get(&id).filter(|(at, _)| at.elapsed() < PACKS_TTL).map(|(_, v)| v.clone())
                        })
                        .flatten();
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
                                with_cache(&self.data_dir, generation, |c| if c.epoch == epoch {
                                    c.global.insert(id, (std::time::Instant::now(), answer.clone()));
                                });
                                answer
                            }
                        };
                        if let Some(p) = content.and_then(|c| pack_json(&format!("{other}/{key}"), "global", &c)) {
                            seen.insert(format!("{other}/{key}"));
                            packs.push(p);
                        }
                    }
                }
            }
        }

        // the room's own packs (one per state key): the room's whole state is
        // the only way to find them (the sync does not carry this type), so
        // it is asked once and only the pack events of it are read
        let known = with_cache(&self.data_dir, generation, |c| {
            c.room.get(&rid).filter(|(at, _)| at.elapsed() < PACKS_TTL).map(|(_, v)| v.clone())
        })
        .flatten();
        let own = match known {
            Some(v) => Some(v),
            None => {
                requests += 1;
                match client.send(get_state_events::v3::Request::new(room.room_id().to_owned())).await {
                    Ok(r) => {
                        state_events = r.room_state.len();
                        let mut own = Vec::new();
                        for ev in r.room_state {
                            bytes += ev.json().get().len();
                            if ev.get_field::<String>("type").ok().flatten().as_deref() != Some(ROOM_EMOTES) {
                                continue;
                            }
                            let key = ev.get_field::<String>("state_key").ok().flatten().unwrap_or_default();
                            if let Ok(Some(content)) = ev.get_field::<Value>("content") {
                                own.push((key, content));
                            }
                        }
                        with_cache(&self.data_dir, generation, |c| if c.epoch == epoch {
                            c.room.insert(rid.clone(), (std::time::Instant::now(), own.clone()));
                        });
                        Some(own)
                    }
                    Err(e) => {
                        tracing::info!(room = %room_id, "packs: room packs: {e}");
                        None
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

        tracing::debug!(target: "rustrx::perf", packs = packs.len(), requests, bytes, state_events,
                        ms = t.elapsed().as_millis() as u64, "image_packs");
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
        let now = std::time::Instant::now();
        with_cache(&dir, 1, |c| {
            c.room.insert("!a:x".into(), (now, vec![]));
            c.room.insert("!b:x".into(), (now, vec![]));
            c.global.insert(("!a:x".into(), "".into()), (now, None));
            c.global.insert(("!b:x".into(), "k".into()), (now, None));
        });
        let before = with_cache(&dir, 1, |c| c.epoch).unwrap();
        with_cache(&dir, 1, |c| forget_room(c, "!a:x"));
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
}
