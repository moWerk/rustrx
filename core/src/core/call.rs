// SPDX-License-Identifier: GPL-3.0-or-later
//! Calls through Element Call (MatrixRTC with a LiveKit server), the way
//! Element X does them: Element Call is a web app shown in a WebView, and
//! the SDK's widget driver answers its Matrix requests (MSC2762, MSC3819).
//! The UI only carries the widget API's postMessage strings: "call_message"
//! commands go to the driver, "call_message" events go to the page.
//!
//! One call at a time. The widget gets of what it asks for only what a
//! call needs (`CallCapabilities`): Element Call is remote code, loaded
//! fresh for every call.

use super::*;
use matrix_sdk::ruma::events::{AnySyncMessageLikeEvent, StateEventType, TimelineEventType};
use matrix_sdk::ruma::Int;
use matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState;
use matrix_sdk::ruma::serde::Raw;
use matrix_sdk::widget::{
    Capabilities, CapabilitiesProvider, ClientProperties, EncryptionSystem, Filter, Intent,
    MessageLikeEventFilter, StateEventFilter, ToDeviceEventFilter, VirtualElementCallWidgetConfig,
    VirtualElementCallWidgetProperties, WidgetDriver, WidgetDriverHandle, WidgetSettings,
};

pub(crate) struct CallSession {
    handle: Arc<WidgetDriverHandle>,
    tasks: Vec<JoinHandle<()>>,
    room: Room,
}

impl CallSession {
    pub(crate) fn stop(self) {
        for t in self.tasks {
            t.abort();
        }
    }
}

/// What Element Call may do in a call, granted where it asks for it:
/// Element X's set (matrix-sdk-ffi 0.19.1,
/// `get_element_call_required_permissions`), with the stable names of the
/// call member and ring types and the additions on the SDK's main branch
/// (to-device `m.rtc.encryption_key`, the MSC4143 `rtc.member` events, the
/// functional members). Anything else, the room's messages among it, is
/// refused and logged by its type.
struct CallCapabilities {
    allowed: Capabilities,
}

impl CallCapabilities {
    fn new(own_user: &str, own_device: &str) -> Self {
        let message = |t: &str| Filter::MessageLike(MessageLikeEventFilter::WithType(t.into()));
        let to_device = |t: &str| Filter::ToDevice(ToDeviceEventFilter::new(t.into()));
        let both = [
            "org.matrix.rageshake_request",
            "io.element.call.encryption_keys",
            "io.element.call.reaction",
            "m.reaction",
            "m.room.redaction",
            DECLINE_TYPE,
            "m.rtc.decline",
            "org.matrix.msc4143.rtc.member",
            "m.rtc.member",
        ]
        .into_iter()
        .map(message)
        .chain(["io.element.call.encryption_keys", "m.rtc.encryption_key"].into_iter().map(to_device));
        let read = CALL_MEMBER_TYPES
            .into_iter()
            .chain(["m.room.name", "m.room.member", "m.room.encryption", "m.room.create", "io.element.functional_members"])
            .map(|t| Filter::State(StateEventFilter::WithType(t.into())))
            .chain(both.clone())
            .collect();
        // the own membership only: one per user (legacy), per device, per
        // device and application, and the same with a leading underscore
        let keys = [
            own_user.to_owned(),
            format!("{own_user}_{own_device}"),
            format!("{own_user}_{own_device}_m.call"),
            format!("_{own_user}_{own_device}"),
            format!("_{own_user}_{own_device}_m.call"),
        ];
        let send = CALL_MEMBER_TYPES
            .into_iter()
            .flat_map(|t| keys.iter().map(move |k| Filter::State(StateEventFilter::WithTypeAndStateKey(t.into(), k.clone()))))
            .chain(RING_TYPES.into_iter().map(message))
            .chain(both)
            .collect();
        let allowed = Capabilities {
            read,
            send,
            requires_client: true,
            update_delayed_event: true,
            send_delayed_event: true,
            download_file: true,
            rtc_transports: true,
        };
        Self { allowed }
    }

    /// The part of `asked` that is allowed: a filter for a whole type the
    /// allowed set has only with given state keys becomes those keys.
    fn grant(&self, asked: Capabilities) -> (Capabilities, Vec<String>) {
        let mut refused = Vec::new();
        let read = narrow(asked.read, &self.allowed.read, "read", &mut refused);
        let send = narrow(asked.send, &self.allowed.send, "send", &mut refused);
        let a = &self.allowed;
        let granted = Capabilities {
            read,
            send,
            requires_client: asked.requires_client && a.requires_client,
            update_delayed_event: asked.update_delayed_event && a.update_delayed_event,
            send_delayed_event: asked.send_delayed_event && a.send_delayed_event,
            download_file: asked.download_file && a.download_file,
            rtc_transports: asked.rtc_transports && a.rtc_transports,
        };
        (granted, refused)
    }
}

/// A filter as kind, type and state key (None: any key); ruma's types
/// print an alias under its primary name, as the SDK compares them.
fn filter_key(f: &Filter) -> (&'static str, String, Option<String>) {
    match f {
        Filter::MessageLike(MessageLikeEventFilter::WithType(t)) => ("message", t.to_string(), None),
        // m.room.message by msgtype is never allowed
        Filter::MessageLike(MessageLikeEventFilter::RoomMessageWithMsgtype(m)) => ("message", "m.room.message".to_owned(), Some(m.clone())),
        Filter::State(StateEventFilter::WithType(t)) => ("state", t.to_string(), None),
        Filter::State(StateEventFilter::WithTypeAndStateKey(t, k)) => ("state", t.to_string(), Some(k.clone())),
        Filter::ToDevice(t) => ("to_device", t.event_type.to_string(), None),
    }
}

/// The filters of `asked` that `allowed` covers; the rest go to `refused`.
fn narrow(asked: Vec<Filter>, allowed: &[Filter], how: &str, refused: &mut Vec<String>) -> Vec<Filter> {
    let mut out: Vec<Filter> = Vec::new();
    let push = |f: &Filter, out: &mut Vec<Filter>| {
        if !out.iter().any(|o| filter_key(o) == filter_key(f)) {
            out.push(f.clone());
        }
    };
    for f in asked {
        let (kind, ty, key) = filter_key(&f);
        let same_type = |a: &&Filter| {
            let (k, t, _) = filter_key(a);
            k == kind && t == ty
        };
        if kind == "message" && key.is_some() {
            refused.push(format!("{how} {kind} {ty}#"));
        } else if allowed.iter().filter(same_type).any(|a| filter_key(a).2.is_none() || filter_key(a).2 == key) {
            push(&f, &mut out);
        } else if key.is_none() && allowed.iter().any(|a| same_type(&a)) {
            for a in allowed.iter().filter(same_type) {
                push(a, &mut out);
            }
        } else {
            refused.push(format!("{how} {kind} {ty}"));
        }
    }
    out
}

impl CapabilitiesProvider for CallCapabilities {
    async fn acquire_capabilities(&self, capabilities: Capabilities) -> Capabilities {
        let (granted, refused) = self.grant(capabilities);
        if !refused.is_empty() {
            tracing::info!(?refused, "call: capabilities refused");
        }
        granted
    }
}

/// The event types a starting call rings with: MSC4075 as Element Call
/// sends it now, its stable name, and the older call.notify.
/// A call rings the phone only in a room of at most two joined members
/// (audit 0.8.4 NET-4, the author: "only in rooms of two"); in a bigger
/// room it is a quiet notification. A direct chat with more members counts
/// as a group here, unlike the rule for Rustrx's own calls (call_start).
/// Voip.cpp applies the same number to classic calls (Keys::kRingMaxMembers).
pub(crate) const RING_MAX_MEMBERS: u64 = 2;
pub(crate) fn rings_in(joined_members: u64) -> bool {
    joined_members <= RING_MAX_MEMBERS
}

const RING_TYPES: [&str; 3] = ["org.matrix.msc4075.rtc.notification", "m.rtc.notification", "org.matrix.msc4075.call.notify"];
/// The state events a call participant announces itself with (Element
/// Call's MatrixRTC membership, the unstable name and the stable one).
pub(crate) const CALL_MEMBER_TYPES: [&str; 2] = ["org.matrix.msc3401.call.member", "m.call.member"];
/// MSC4310, answering a ring with no.
const DECLINE_TYPE: &str = "org.matrix.msc4310.rtc.decline";

/// TEST ONLY (dev_ring): the file in the data folder that allows it, as
/// for the UI's dev commands (src/Keys.h, kDevCommandsFlag).
const DEV_COMMANDS_FILE: &str = "dev-commands";

/// TEST ONLY: dev_ring's invite, for `own` from `sender`, sent `now` (ms):
/// 60 s to ring, age 0, a fixed placeholder offer, ids that say what it is.
fn dev_invite(sender: &str, own: &str, now: u64) -> Value {
    const SDP: &str = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=rustrx-dev-ring\r\nt=0 0\r\n\
        m=audio 9 UDP/TLS/RTP/SAVPF 111\r\nc=IN IP4 0.0.0.0\r\na=mid:0\r\na=sendrecv\r\n\
        a=rtpmap:111 opus/48000/2\r\na=setup:actpass\r\n";
    json!({
        "type": "m.call.invite", "sender": sender, "event_id": format!("$rustrx-dev-ring-{now}"),
        "origin_server_ts": now, "unsigned": {"age": 0},
        "content": {
            "call_id": format!("rustrx-dev-ring-{now}"), "party_id": "rustrx-dev-ring", "version": "1",
            "lifetime": 60000, "invitee": own, "offer": {"type": "offer", "sdp": SDP},
        },
    })
}

/// How old an event is, in ms: the server's age where it gives one, as
/// the phone's clock can be off (a mainline phone boots with a wrong date
/// until NTP); else from its timestamp.
fn event_age_ms<T>(raw: &Raw<T>) -> u64 {
    let sent = raw.get_field::<u64>("origin_server_ts").ok().flatten().unwrap_or(0);
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0);
    raw.get_field::<Value>("unsigned")
        .ok()
        .flatten()
        .and_then(|u| u.get("age").and_then(Value::as_u64))
        .unwrap_or_else(|| now.saturating_sub(sent))
}

/// A 1:1 call's event (m.call.*) as it came, decrypted: what the "voip"
/// event carries besides the room's and the sender's names.
pub(crate) struct VoipEvent {
    pub(crate) kind: String,
    pub(crate) sender: String,
    pub(crate) content: Value,
    pub(crate) event_id: String,
    pub(crate) age: u64,
}

impl VoipEvent {
    /// None for any other type.
    pub(crate) fn of<T>(raw: &Raw<T>) -> Option<Self> {
        let kind = raw.get_field::<String>("type").ok().flatten()?;
        if !crate::protocol::voip_type_allowed(&kind) {
            return None;
        }
        Some(VoipEvent {
            kind,
            sender: raw.get_field::<String>("sender").ok().flatten().unwrap_or_default(),
            content: raw.get_field::<Value>("content").ok().flatten().unwrap_or(Value::Null),
            event_id: raw.get_field::<String>("event_id").ok().flatten().unwrap_or_default(),
            age: event_age_ms(raw),
        })
    }

    pub(crate) fn call_id(&self) -> &str {
        self.content.get("call_id").and_then(Value::as_str).unwrap_or("")
    }

    pub(crate) fn is_invite(&self) -> bool {
        self.kind == "m.call.invite"
    }

    fn json(&self, room_id: &str, room_name: &str, sender_name: &str, own: bool, is_dm: bool, joined: u64) -> Value {
        json!({
            "ev": "voip", "room_id": room_id, "room_name": room_name,
            "type": self.kind, "sender": self.sender, "sender_name": sender_name, "own": own,
            "content": self.content, "event_id": self.event_id, "age_ms": self.age, "is_dm": is_dm,
            "joined_members": joined,
        })
    }
}

/// The call events this process passed on, so that one seen by the sync
/// and by a push (the classic sync of a push-woken process sees both) goes
/// to the UI once; and the call that rings from a push, whose end comes
/// by push only as an event the push rules filter out (push.rs). A
/// push-woken process keeps its ids in VOIP_SEEN_FILE: the app's first
/// sync after it brings the same invite and hangup again, which rang a
/// second time and left a second missed call.
#[derive(Default)]
pub(crate) struct VoipSeen {
    ids: std::collections::VecDeque<String>,
    ring: Option<(String, String, std::time::Instant)>,
}

const VOIP_SEEN_KEPT: usize = 64;
pub(crate) const VOIP_SEEN_FILE: &str = "voip-seen.json";

impl VoipSeen {
    /// What a push-woken process left; nothing on anything unexpected.
    pub(crate) fn load(path: &std::path::Path) -> Self {
        let ids: Vec<String> = std::fs::read(path)
            .ok()
            .filter(|b| b.len() <= 64 * 1024)
            .and_then(|b| serde_json::from_slice(&b).ok())
            .unwrap_or_default();
        let skip = ids.len().saturating_sub(VOIP_SEEN_KEPT);
        VoipSeen { ids: ids.into_iter().skip(skip).filter(|i| i.len() <= 255).collect(), ring: None }
    }

    fn save(&self, path: &std::path::Path) {
        let tmp = path.with_extension("tmp");
        let ok = serde_json::to_vec(&self.ids).ok().is_some_and(|b| std::fs::write(&tmp, b).is_ok())
            && std::fs::rename(&tmp, path).is_ok();
        if !ok {
            tracing::info!("voip: the seen call events not saved");
        }
    }

    /// false: passed on already
    fn first(&mut self, ev: &VoipEvent, room_id: &str, own: bool) -> bool {
        if !ev.event_id.is_empty() {
            if self.ids.contains(&ev.event_id) {
                return false;
            }
            if self.ids.len() >= VOIP_SEEN_KEPT {
                self.ids.pop_front();
            }
            self.ids.push_back(ev.event_id.clone());
        }
        // the lifetime as Voip takes it (none: too old already)
        let lifetime = ev.content.get("lifetime").and_then(Value::as_u64).unwrap_or(0).min(RING_LIFETIME_MAX_MS);
        if ev.is_invite() && !own && ev.age < lifetime {
            let until = std::time::Instant::now() + Duration::from_millis(lifetime - ev.age);
            self.ring = Some((room_id.to_owned(), ev.call_id().to_owned(), until));
        } else if !ev.is_invite() && ev.kind != "m.call.candidates" && self.ringing(room_id) == Some(ev.call_id()) {
            self.ring = None;
        }
        true
    }

    /// The call id of the call that may still ring in the room.
    fn ringing(&self, room_id: &str) -> Option<&str> {
        self.ring
            .as_ref()
            .filter(|(r, _, until)| r == room_id && *until > std::time::Instant::now())
            .map(|(_, c, _)| c.as_str())
    }
}

impl Core {
    /// Incoming calls: a ring (or a group call's notification) from someone
    /// else that is still fresh becomes "call_ring"; the UI rings until it
    /// expires, the call ends, or it is answered or declined. The initial
    /// sync's old ones are past their lifetime and stay quiet.
    pub(crate) fn install_call_handlers(&'static self, client: &Client) {
        client.add_event_handler(move |raw: Raw<AnySyncMessageLikeEvent>, room: Room| async move {
            let Ok(Some(kind)) = raw.get_field::<String>("type") else { return };
            if !RING_TYPES.contains(&kind.as_str()) {
                return;
            }
            let sender = raw.get_field::<String>("sender").ok().flatten().unwrap_or_default();
            if sender == room.own_user_id().as_str() {
                return;
            }
            let content = raw.get_field::<Value>("content").ok().flatten().unwrap_or(Value::Null);
            let lifetime = content.get("lifetime").and_then(Value::as_u64).unwrap_or(RING_LIFETIME_DEFAULT_MS).min(RING_LIFETIME_MAX_MS);
            let age = event_age_ms(&raw);
            let event_id = raw.get_field::<String>("event_id").ok().flatten().unwrap_or_default();
            if age >= lifetime {
                tracing::info!(room = %room.room_id(), age, lifetime, "call: ring too old");
                return;
            }
            let asked_ring = content
                .get("notification_type")
                .or_else(|| content.get("notify_type"))
                .and_then(Value::as_str)
                == Some("ring");
            // rings only in a room of two (the author, audit 0.8.4 NET-4):
            // anyone in a group, a public room too, could ring the phone;
            // there it is the quiet line of a group call
            let ring = asked_ring && rings_in(room.joined_members_count());
            let sender_name = match UserId::parse(sender.as_str()) {
                Ok(u) => member_name(&room, &u).await,
                Err(_) => sender.clone(),
            };
            let room_name = room_name(&room);
            tracing::info!(room = %room.room_id(), ring, asked_ring, age, "call: incoming");
            self.emit(json!({
                "ev": "call_ring", "room_id": room.room_id().as_str(), "room_name": room_name,
                "sender": sender, "sender_name": sender_name, "ring": ring,
                "event_id": event_id, "expires_ms": lifetime - age,
            }));
        });

        // 1:1 calls (m.call.*): every event goes to the UI as it came, our
        // own too; the call's state machine there needs our echo (and
        // another device of ours answering) to tell its party from ours.
        // Handlers get the decrypted event in an encrypted room.
        client.add_event_handler(move |raw: Raw<AnySyncMessageLikeEvent>, room: Room| async move {
            self.voip_in_room(&raw, &room).await;
        });
    }

    /// An m.call.* event of the room to the UI as "voip"; None for any
    /// other type. The sync handler's and a push-woken process's.
    pub(crate) async fn voip_in_room<T>(&'static self, raw: &Raw<T>, room: &Room) -> Option<VoipEvent> {
        let ev = VoipEvent::of(raw)?;
        let sender_name = match UserId::parse(ev.sender.as_str()) {
            Ok(u) => member_name(room, &u).await,
            Err(_) => ev.sender.clone(),
        };
        let own = ev.sender == room.own_user_id().as_str();
        // whether the room is a direct chat: an invite for nobody in
        // particular rings only there (anyone in a big room could ring
        // every member's phone otherwise)
        let is_dm = room.is_direct().await.unwrap_or(false);
        self.emit_voip(&ev, room.room_id().as_str(), &room_name(room), &sender_name, own, is_dm, room.joined_members_count());
        Some(ev)
    }

    /// "voip", once per event.
    /// joined: the room's joined members, for the UI's room-of-two rule (rings_in)
    pub(crate) fn emit_voip(&self, ev: &VoipEvent, room_id: &str, room_name: &str, sender_name: &str, own: bool, is_dm: bool,
                            joined: u64) {
        {
            let mut seen = self.voip_seen.lock().unwrap_or_else(|e| e.into_inner());
            if !seen.first(ev, room_id, own) {
                return;
            }
            if super::push::push_wake() {
                seen.save(&self.data_dir.join(VOIP_SEEN_FILE));
            }
        }
        tracing::info!(room = %room_id, kind = %ev.kind, call_id = %ev.call_id(), own, age = ev.age, is_dm, joined, "voip: event");
        self.emit(ev.json(room_id, room_name, sender_name, own, is_dm, joined));
    }

    /// The call id of a call that a push made ring in the room, while it may.
    pub(crate) fn push_ringing(&self, room_id: &str) -> Option<String> {
        self.voip_seen.lock().unwrap_or_else(|e| e.into_inner()).ringing(room_id).map(str::to_owned)
    }

    /// One m.call.* event from the UI's call, sent like any event (so
    /// encrypted in an encrypted room).
    pub(crate) async fn voip_send(&'static self, room_id: &str, kind: &str, content: serde_json::Map<String, Value>) -> Result<()> {
        if !crate::protocol::voip_type_allowed(kind) {
            bail!("not a call event: {kind}");
        }
        let room = self.room(room_id).await?;
        let call_id = content.get("call_id").and_then(Value::as_str).unwrap_or("").to_owned();
        let sent = room.send_raw(kind, Value::Object(content)).await?;
        let event_id = sent.response.event_id;
        tracing::info!(room = %room.room_id(), %kind, %call_id, "voip: sent");
        self.emit(json!({
            "ev": "voip_sent", "room_id": room.room_id().as_str(), "type": kind,
            "call_id": call_id, "event_id": event_id.as_str(),
        }));
        Ok(())
    }

    /// The homeserver's TURN server for a call. None (no TURN set up, an
    /// error) is an answer too, with no uris: on a LAN a call works without.
    pub(crate) async fn turn_server(&'static self) -> Result<()> {
        use matrix_sdk::ruma::api::client::voip::get_turn_server_info;
        let client = self.client().await?;
        match client.send(get_turn_server_info::v3::Request::new()).await {
            Ok(r) => {
                tracing::info!(uris = r.uris.len(), ttl = r.ttl.as_secs(), "voip: turn server");
                self.emit(json!({
                    "ev": "turn_server", "username": r.username, "password": r.password,
                    "uris": r.uris, "ttl": r.ttl.as_secs(),
                }));
            }
            Err(e) => {
                tracing::info!("voip: no turn server: {e}");
                self.emit(json!({"ev": "turn_server", "username": "", "password": "", "uris": [], "ttl": 0}));
            }
        }
        Ok(())
    }

    /// TEST ONLY: a ring with nobody to call (the phones under test reach no
    /// one overnight). A made-up m.call.invite from the room's other member
    /// goes the sync handler's way (voip_in_room: the names, VoipSeen,
    /// "voip"), in the app, the stand-in and a push-woken process alike.
    /// Its SDP is a placeholder: Answer may fail to connect. Refused
    /// without the dev-commands file in the data folder (the first
    /// account's; another account's folder is two below it).
    pub(crate) async fn dev_ring(&'static self, room_id: &str) -> Result<()> {
        let flag = |d: &std::path::Path| d.join(DEV_COMMANDS_FILE).exists();
        let root = self.data_dir.parent().and_then(std::path::Path::parent);
        if !flag(&self.data_dir) && !root.is_some_and(flag) {
            bail!("dev_ring: dev commands are off");
        }
        let room = self.room(room_id).await?;
        let own = room.own_user_id().to_owned();
        let sender = room
            .members(RoomMemberships::JOIN)
            .await?
            .into_iter()
            .map(|m| m.user_id().to_owned())
            .find(|u| *u != own)
            .ok_or_else(|| anyhow!("dev_ring: no other member in the room"))?;
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis() as u64)
            .unwrap_or(0);
        let raw: Raw<AnySyncMessageLikeEvent> = Raw::new(&dev_invite(sender.as_str(), own.as_str(), now))?.cast_unchecked();
        tracing::info!(room = %room.room_id(), "voip: TEST ONLY made-up invite (dev_ring)");
        self.voip_in_room(&raw, &room).await;
        Ok(())
    }

    /// No to a ring (MSC4310): the caller's clients stop ringing for us.
    pub(crate) async fn call_decline(&'static self, room_id: &str, event_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        EventId::parse(event_id)?;
        let content = json!({"m.relates_to": {"rel_type": "m.reference", "event_id": event_id}});
        room.send_raw(DECLINE_TYPE, content).await?;
        Ok(())
    }

    /// A call needs each participant to post a membership state event; a
    /// room made without calls in mind asks moderator level for that, and
    /// Element Call then fails with "Connection lost". Someone who may
    /// change the room's permissions opens calls to everyone (as Element X
    /// sets up new rooms); anyone else gets told why.
    async fn ensure_call_permission(&'static self, room: &Room) -> Result<()> {
        let own = room.own_user_id().to_owned();
        // through the room's write lock, on top of a change still on its
        // way (moderation.rs): sent on the stale state, it reverted that
        let opened = self.write_levels(room, |levels| {
            // the others have to be able to join too, not only the one who
            // starts the call (an admin starting it is never blocked)
            let members_blocked = CALL_MEMBER_TYPES.iter().any(|t| {
                let needed = levels.events.get(&TimelineEventType::from(*t)).copied().unwrap_or(levels.state_default);
                needed > levels.users_default
            });
            let own_blocked = CALL_MEMBER_TYPES
                .iter()
                .any(|t| !levels.user_can_send_state(&own, StateEventType::from(*t)));
            if !members_blocked && !own_blocked {
                return Ok(false);
            }
            if !levels.user_can_send_state(&own, StateEventType::RoomPowerLevels) {
                if own_blocked {
                    bail!("only moderators can take part in calls in this room; a room admin can allow calls for everyone");
                }
                // we may, others may not: the call goes ahead, they will be told
                return Ok(false);
            }
            for t in CALL_MEMBER_TYPES {
                levels.events.insert(TimelineEventType::from(t), Int::from(0u8));
            }
            Ok(true)
        })
        .await?;
        if !opened {
            return Ok(());
        }
        tracing::info!(room = %room.room_id(), "call: opened calls to everyone in the room");
        self.emit(json!({"ev": "calls_allowed", "room_id": room.room_id().as_str()}));
        Ok(())
    }

    pub(crate) async fn call_start(&'static self, room_id: &str, voice: bool, skip_lobby: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        self.call_end().await;
        let seq = {
            let mut st = self.state.lock().await;
            st.call_seq += 1;
            st.call_seq
        };
        // a membership of this device left over from a call that did not
        // end cleanly (a crash, a killed app) goes first
        Self::leave_own_memberships(&room).await;
        self.ensure_call_permission(&room).await?;

        let dm = room.is_direct().await.unwrap_or(false);
        let ongoing = room.has_active_room_call();
        let intent = match (dm, ongoing, voice) {
            (true, true, _) => Intent::JoinExistingDm,
            (true, false, true) => Intent::StartCallDmVoice,
            (true, false, false) => Intent::StartCallDm,
            (false, true, _) => Intent::JoinExisting,
            (false, false, _) => Intent::StartCall,
        };
        let intent_name = format!("{intent:?}");
        let encryption = if room.encryption_state().is_encrypted() {
            EncryptionSystem::PerParticipantKeys
        } else {
            EncryptionSystem::Unencrypted
        };
        let encryption_name = format!("{encryption:?}");
        let props = VirtualElementCallWidgetProperties {
            element_call_url: ELEMENT_CALL_URL.to_owned(),
            widget_id: WIDGET_ID.to_owned(),
            encryption,
            ..Default::default()
        };
        let config = VirtualElementCallWidgetConfig {
            intent: Some(intent),
            // the phone's audio routing (earpiece, speaker, headset) stays with the system
            controlled_audio_devices: Some(true),
            hide_screensharing: Some(true),
            skip_lobby: skip_lobby.then_some(true),
            ..Default::default()
        };
        let settings = WidgetSettings::new_virtual_element_call_widget(props, config)
            .map_err(|e| anyhow!("call widget: {e}"))?;
        let url = settings
            .generate_webview_url(&room, ClientProperties::new(APP_ID, None, Some("dark".to_owned())))
            .await
            .map_err(|e| anyhow!("call url: {e}"))?;
        // a new call rings the other side in a direct chat and notifies a
        // group; Element Call sends that event itself when told so (the
        // SDK's own setting for it is not exported in 0.19), joining sends
        // nothing
        // (two people in a room that is not marked direct are a call too)
        let one_to_one = dm || room.joined_members_count() <= 2;
        let notify = match (ongoing, one_to_one) {
            (true, _) => None,
            (false, true) => Some("ring"),
            (false, false) => Some("notification"),
        };
        let mut url = url;
        if let Some(kind) = notify {
            let fragment = url.fragment().unwrap_or("?").to_owned();
            if !fragment.contains("sendNotificationType=") {
                let sep = if fragment.ends_with('?') { "" } else { "&" };
                url.set_fragment(Some(&format!("{fragment}{sep}sendNotificationType={kind}")));
            }
        }

        let rid_room = room.clone();
        let capabilities = CallCapabilities::new(room.own_user_id().as_str(), room.client().device_id().map(|d| d.as_str()).unwrap_or(""));
        let (driver, handle) = WidgetDriver::new(settings);
        let handle = Arc::new(handle);
        let rid = room.room_id().to_owned();
        let run = self.rt.spawn({
            let rid = rid.clone();
            async move {
                let _ = driver.run(room, capabilities).await;
                tracing::info!(room = %rid, "call widget driver ended");
                self.emit(json!({"ev": "call_ended", "room_id": rid.as_str()}));
            }
        });
        let forward = self.rt.spawn({
            let handle = handle.clone();
            async move {
                while let Some(msg) = handle.recv().await {
                    self.emit(json!({"ev": "call_message", "data": msg}));
                }
            }
        });
        let mut st = self.state.lock().await;
        if st.call_seq != seq {
            // hung up while it was starting
            drop(st);
            run.abort();
            forward.abort();
            Self::leave_own_memberships(&rid_room).await;
            return Ok(());
        }
        if let Some(old) = st.call.replace(CallSession { handle, tasks: vec![run, forward], room: rid_room }) {
            old.stop();
        }
        drop(st);
        tracing::info!(room = %rid, ?intent_name, ?encryption_name, skip_lobby, "call widget started");
        self.emit(json!({"ev": "call_url", "room_id": rid.as_str(), "url": url.as_str()}));
        Ok(())
    }

    /// A postMessage from the page, for the driver.
    pub(crate) async fn call_message(&self, data: &str) -> Result<()> {
        let st = self.state.lock().await;
        let call = st.call.as_ref().context("no call")?;
        if !call.handle.send(data.to_owned()) {
            bail!("the call has ended");
        }
        Ok(())
    }

    pub(crate) async fn call_end(&self) {
        let call = {
            let mut st = self.state.lock().await;
            st.call_seq += 1;
            st.call.take()
        };
        if let Some(call) = call {
            let room = call.room.clone();
            call.stop();
            // Element Call leaves by emptying its membership; closed from
            // outside (the page swiped away, the app quit) it had no time
            // to, and the room showed a call in progress for hours
            Self::leave_own_memberships(&room).await;
        }
    }

    /// This device's call memberships in the room, emptied (the way Element
    /// Call leaves a call); others' and already empty ones are left alone.
    async fn leave_own_memberships(room: &Room) {
        let own = room.own_user_id().to_owned();
        let device = room.client().device_id().map(|d| d.to_string()).unwrap_or_default();
        if device.is_empty() {
            return;
        }
        for kind in CALL_MEMBER_TYPES {
            let Ok(events) = room.get_state_events(StateEventType::from(kind)).await else { continue };
            for ev in events {
                let RawAnySyncOrStrippedState::Sync(raw) = ev else { continue };
                let v = raw_value(&raw);
                let key = v.get("state_key").and_then(Value::as_str).unwrap_or("");
                let live = v.get("content").and_then(Value::as_object).is_some_and(|c| !c.is_empty());
                let mine = v.get("sender").and_then(Value::as_str) == Some(own.as_str()) && key.contains(&device);
                if live && mine {
                    match room.send_state_event_raw(kind, key, json!({})).await {
                        Ok(_) => tracing::info!(room = %room.room_id(), "call: left a membership of this device"),
                        Err(e) => tracing::info!(room = %room.room_id(), "call: leaving a membership: {e}"),
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn caps(list: &[&str]) -> Capabilities {
        serde_json::from_value(json!(list)).unwrap()
    }

    fn names(c: &Capabilities) -> Vec<String> {
        let mut v: Vec<String> = serde_json::from_value(serde_json::to_value(c).unwrap()).unwrap();
        v.sort();
        v
    }

    /// What Element Call asks for a call is granted as asked; the room's
    /// messages, other state, others' memberships and internal to-device
    /// types are refused; a send of the member type for any key becomes
    /// the own keys only.
    /// Planted-bug: return `asked` from `grant` (GrantAll, as up to 0.8.3).
    #[test]
    fn element_call_gets_only_what_a_call_needs() {
        let (me, dev) = ("@me:example.org", "DEV");
        let provider = CallCapabilities::new(me, dev);
        let call = [
            "io.element.requires_client",
            "org.matrix.msc4157.send.delayed_event",
            "org.matrix.msc4157.update_delayed_event",
            "org.matrix.msc2762.receive.state_event:m.room.member",
            "org.matrix.msc2762.receive.state_event:org.matrix.msc3401.call.member",
            "org.matrix.msc2762.receive.state_event:m.room.create",
            "org.matrix.msc2762.receive.state_event:m.room.encryption",
            "org.matrix.msc2762.receive.event:io.element.call.encryption_keys",
            "org.matrix.msc2762.receive.event:m.reaction",
            "org.matrix.msc2762.receive.event:org.matrix.msc4310.rtc.decline",
            "org.matrix.msc3819.receive.to_device:io.element.call.encryption_keys",
            "org.matrix.msc2762.send.state_event:org.matrix.msc3401.call.member#_@me:example.org_DEV_m.call",
            "org.matrix.msc2762.send.event:org.matrix.msc4075.rtc.notification",
            "org.matrix.msc2762.send.event:org.matrix.msc4075.call.notify",
            "org.matrix.msc2762.send.event:io.element.call.encryption_keys",
            "org.matrix.msc3819.send.to_device:io.element.call.encryption_keys",
        ];
        let (granted, refused) = provider.grant(caps(&call));
        assert!(refused.is_empty(), "{refused:?}");
        assert_eq!(names(&granted), names(&caps(&call)));

        let hostile = [
            "org.matrix.msc2762.receive.event:m.room.message",
            "org.matrix.msc2762.receive.event:m.room.message#m.text",
            "org.matrix.msc2762.receive.event:m.room.encrypted",
            "org.matrix.msc2762.receive.state_event:m.room.power_levels",
            "org.matrix.msc2762.send.state_event:m.room.topic",
            "org.matrix.msc2762.send.state_event:m.room.power_levels#",
            "org.matrix.msc2762.send.state_event:org.matrix.msc3401.call.member#@other:example.org",
            "org.matrix.msc2762.send.event:m.room.message",
            "org.matrix.msc3819.receive.to_device:m.room_key",
            "org.matrix.msc3819.send.to_device:m.secret.send",
        ];
        let (granted, refused) = provider.grant(caps(&hostile));
        assert!(names(&granted).is_empty(), "{:?}", names(&granted));
        assert_eq!(refused.len(), hostile.len(), "{refused:?}");

        // the member type for any key: the own keys, nobody else's
        let (granted, _) = provider.grant(caps(&["org.matrix.msc2762.send.state_event:org.matrix.msc3401.call.member"]));
        let got = names(&granted);
        assert!(got.contains(&"org.matrix.msc2762.send.state_event:org.matrix.msc3401.call.member#_@me:example.org_DEV".to_owned()));
        assert!(got.iter().all(|n| n.contains("#@me:example.org") || n.contains("#_@me:example.org_DEV")), "{got:?}");
        // a flag not asked for is not given
        assert!(!provider.grant(caps(&[])).0.requires_client);
    }

    fn voip(v: Value) -> Option<VoipEvent> {
        let raw: Raw<matrix_sdk::ruma::events::AnySyncTimelineEvent> = serde_json::from_value(v).unwrap();
        VoipEvent::of(&raw)
    }

    fn invite(id: &str, call: &str, lifetime: u64, age: u64) -> VoipEvent {
        voip(json!({"type": "m.call.invite", "event_id": id, "sender": "@them:example.org", "origin_server_ts": 1,
                    "unsigned": {"age": age}, "content": {"call_id": call, "lifetime": lifetime, "version": "1"}})).unwrap()
    }

    /// A pushed event and the sync handler's make one "voip" event: only the
    /// seven m.call.* types, the server's age before the clock, the fields
    /// the UI reads.
    /// Planted-bug: drop the voip_type_allowed check in `VoipEvent::of`.
    #[test]
    fn voip_event_is_the_call_types_only() {
        assert!(voip(json!({"type": "m.room.message", "event_id": "$m", "sender": "@a:b", "content": {"body": "x"}})).is_none());
        assert!(voip(json!({"type": "m.call.member", "event_id": "$m", "sender": "@a:b", "content": {}})).is_none());
        let ev = invite("$i", "c1", 60000, 1234);
        assert_eq!(ev.age, 1234);
        assert_eq!(ev.call_id(), "c1");
        assert!(ev.is_invite());
        let j = ev.json("!r:b", "Room", "Them", false, true, 2);
        for k in ["ev", "room_id", "room_name", "type", "sender", "sender_name", "own", "content", "event_id", "age_ms", "is_dm",
                  "joined_members"] {
            assert!(j.get(k).is_some(), "{k}");
        }
        assert_eq!(j["ev"], "voip");
        assert_eq!(j["age_ms"], 1234);
        assert_eq!(j["joined_members"], 2);
        // no server age: from its timestamp, which is long ago here
        let old = voip(json!({"type": "m.call.hangup", "event_id": "$h", "sender": "@a:b", "origin_server_ts": 1,
                              "content": {"call_id": "c1"}})).unwrap();
        assert!(old.age > 1_000_000);
    }

    /// A ring rings only in a room of two; three or more joined members
    /// make it the quiet group line (NET-4).
    /// Planted-bug: RING_MAX_MEMBERS = 3, or rings_in returning true.
    #[test]
    fn rings_only_in_a_room_of_two() {
        assert!(rings_in(1));
        assert!(rings_in(2));
        assert!(!rings_in(3));
        assert!(!rings_in(500));
    }

    /// TEST ONLY dev_ring's invite rings as a real one would: an invite, age
    /// 0, the full lifetime, for us, with an offer.
    /// Planted-bug: put "age": 60000 in dev_invite's unsigned.
    #[test]
    fn dev_invite_is_a_fresh_invite_for_us() {
        let ev = voip(dev_invite("@them:example.org", "@me:example.org", 5)).unwrap();
        assert!(ev.is_invite());
        assert_eq!(ev.age, 0);
        assert_eq!(ev.sender, "@them:example.org");
        assert_eq!(ev.call_id(), "rustrx-dev-ring-5");
        assert_eq!(ev.content["lifetime"], 60000);
        assert_eq!(ev.content["invitee"], "@me:example.org");
        assert!(ev.content["offer"]["sdp"].as_str().unwrap().starts_with("v=0\r\n"));
    }

    /// Each event once (the classic sync of a woken process and its push
    /// fetch both see it); a fresh invite from someone else is the ringing
    /// call until its lifetime is over or its hangup, reject or answer
    /// came; candidates and other calls leave it ringing.
    /// Planted-bug: clear `ring` on m.call.candidates too.
    #[test]
    fn voip_seen_passes_each_event_once_and_tracks_the_ring() {
        let mut seen = VoipSeen::default();
        let inv = invite("$i", "c1", 60000, 100);
        assert!(seen.first(&inv, "!r:b", false));
        assert!(!seen.first(&inv, "!r:b", false));
        assert_eq!(seen.ringing("!r:b"), Some("c1"));
        assert_eq!(seen.ringing("!other:b"), None);
        let cand = voip(json!({"type": "m.call.candidates", "event_id": "$c", "sender": "@them:example.org",
                               "content": {"call_id": "c1", "candidates": []}})).unwrap();
        assert!(seen.first(&cand, "!r:b", false));
        assert_eq!(seen.ringing("!r:b"), Some("c1"));
        let other = voip(json!({"type": "m.call.hangup", "event_id": "$o", "sender": "@them:example.org",
                                "content": {"call_id": "c2"}})).unwrap();
        assert!(seen.first(&other, "!r:b", false));
        assert_eq!(seen.ringing("!r:b"), Some("c1"));
        let hangup = voip(json!({"type": "m.call.hangup", "event_id": "$h", "sender": "@them:example.org",
                                 "content": {"call_id": "c1"}})).unwrap();
        assert!(seen.first(&hangup, "!r:b", false));
        assert_eq!(seen.ringing("!r:b"), None);

        // too old, our own, or without a lifetime: no ring
        let mut seen = VoipSeen::default();
        seen.first(&invite("$a", "c1", 60000, 60000), "!r:b", false);
        seen.first(&invite("$b", "c2", 60000, 0), "!r:b", true);
        let none = voip(json!({"type": "m.call.invite", "event_id": "$n", "sender": "@them:example.org",
                               "unsigned": {"age": 0}, "content": {"call_id": "c3"}})).unwrap();
        seen.first(&none, "!r:b", false);
        assert_eq!(seen.ringing("!r:b"), None);

        // only the last VOIP_SEEN_KEPT ids are kept
        let mut seen = VoipSeen::default();
        for n in 0..=VOIP_SEEN_KEPT {
            assert!(seen.first(&invite(&format!("${n}"), "c", 0, 0), "!r:b", false));
        }
        assert!(seen.first(&invite("$0", "c", 0, 0), "!r:b", false));
        assert!(!seen.first(&invite(&format!("${VOIP_SEEN_KEPT}"), "c", 0, 0), "!r:b", false));
    }

    /// What a push-woken process passed on is not passed on again by the
    /// app's sync (the same invite rang twice, the same hangup left a
    /// second missed call); a damaged file is nothing.
    /// Planted-bug: `load` returns the default.
    #[test]
    fn voip_seen_outlives_the_woken_process() {
        let dir = std::env::temp_dir().join(format!("rustrx-voip-seen-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join(VOIP_SEEN_FILE);
        let mut woken = VoipSeen::default();
        woken.first(&invite("$i", "c1", 60000, 0), "!r:b", false);
        woken.save(&path);
        let mut app = VoipSeen::load(&path);
        assert!(!app.first(&invite("$i", "c1", 60000, 0), "!r:b", false));
        assert!(app.first(&invite("$j", "c2", 60000, 0), "!r:b", false));
        std::fs::write(&path, b"{not json").unwrap();
        assert!(VoipSeen::load(&path).ids.is_empty());
        assert!(VoipSeen::load(&dir.join("none")).ids.is_empty());
        let _ = std::fs::remove_dir_all(&dir);
    }
}
