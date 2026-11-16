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
            "shortcode": code, "url": url,
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
                "avatar": avatar.or_else(|| out[0]["url"].as_str()), "images": out}))
}

/// Text into HTML, for a message that had no formatting of its own.
fn escape_html(text: &str) -> String {
    text.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;").replace('\n', "<br>")
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
        _ => escape_html(&t.body),
    };
    for (code, url) in used {
        let code = escape_html(code);
        html = html.replace(
            &format!(":{code}:"),
            &format!("<img data-mx-emoticon src=\"{url}\" alt=\":{code}:\" title=\":{code}:\" height=\"32\">"),
        );
    }
    t.formatted = Some(FormattedBody::html(html));
}

impl Core {
    /// The packs offered in a room: the user's own, the ones the user
    /// enabled everywhere, the room's own. Answered by "image_packs".
    pub(crate) async fn image_packs(&'static self, room_id: &str) -> Result<()> {
        let client = self.client().await?;
        let account = client.account();
        let mut packs = Vec::new();

        let user = account.fetch_account_data(GlobalAccountDataEventType::from(USER_EMOTES)).await;
        match user {
            Ok(Some(raw)) => packs.extend(pack_json("user", "user", &raw_value(&raw))),
            Ok(None) => {}
            Err(e) => tracing::info!("packs: own pack: {e}"),
        }

        // other rooms' packs the user turned on for every room
        let mut seen = std::collections::HashSet::new();
        if let Ok(Some(raw)) = account.fetch_account_data(GlobalAccountDataEventType::from(EMOTE_ROOMS)).await {
            let v = raw_value(&raw);
            if let Some(rooms) = v.get("rooms").and_then(Value::as_object) {
                for (rid, keys) in rooms {
                    let Ok(rid) = OwnedRoomId::try_from(rid.as_str()) else { continue };
                    for key in keys.as_object().map(|k| k.keys().cloned().collect::<Vec<_>>()).unwrap_or_default() {
                        let request = get_state_event_for_key::v3::Request::new(
                            rid.clone(), StateEventType::from(ROOM_EMOTES), key.clone());
                        match client.send(request).await {
                            Ok(r) => {
                                let content: Value = serde_json::from_str(r.event_or_content.get()).unwrap_or(Value::Null);
                                if let Some(p) = pack_json(&format!("{rid}/{key}"), "global", &content) {
                                    seen.insert(format!("{rid}/{key}"));
                                    packs.push(p);
                                }
                            }
                            Err(e) => tracing::info!(room = %rid, "packs: enabled pack: {e}"),
                        }
                    }
                }
            }
        }

        // the room's own packs (one per state key)
        let room = self.room(room_id).await?;
        match client.send(get_state_events::v3::Request::new(room.room_id().to_owned())).await {
            Ok(r) => {
                for ev in r.room_state {
                    let v = raw_value(&ev);
                    if v.get("type").and_then(Value::as_str) != Some(ROOM_EMOTES) {
                        continue;
                    }
                    let key = v.get("state_key").and_then(Value::as_str).unwrap_or("");
                    let id = format!("{}/{key}", room.room_id());
                    if seen.contains(&id) {
                        continue;
                    }
                    if let Some(content) = v.get("content") {
                        packs.extend(pack_json(&id, "room", content));
                    }
                }
            }
            Err(e) => tracing::info!(room = %room_id, "packs: room packs: {e}"),
        }

        tracing::info!(room = %room_id, packs = packs.len(), "packs");
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
        self.emit(json!({"ev": "emote_added", "shortcode": name, "url": url}));
        Ok(())
    }
}
