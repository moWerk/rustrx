// SPDX-License-Identifier: GPL-3.0-or-later
//! Calls through Element Call (MatrixRTC with a LiveKit server), the way
//! Element X does them: Element Call is a web app shown in a WebView, and
//! the SDK's widget driver answers its Matrix requests (MSC2762, MSC3819).
//! The UI only carries the widget API's postMessage strings: "call_message"
//! commands go to the driver, "call_message" events go to the page.
//!
//! Spike: one call at a time, every capability the widget asks for is
//! granted (Element Call asks only for what a call needs in this room).

use super::*;
use matrix_sdk::ruma::events::room::power_levels::RoomPowerLevelsEventContent;
use matrix_sdk::ruma::events::{AnySyncMessageLikeEvent, StateEventType, TimelineEventType};
use matrix_sdk::ruma::Int;
use matrix_sdk::deserialized_responses::RawAnySyncOrStrippedState;
use matrix_sdk::ruma::serde::Raw;
use matrix_sdk::widget::{
    Capabilities, CapabilitiesProvider, ClientProperties, EncryptionSystem, Intent,
    VirtualElementCallWidgetConfig, VirtualElementCallWidgetProperties, WidgetDriver,
    WidgetDriverHandle, WidgetSettings,
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

struct GrantAll;

impl CapabilitiesProvider for GrantAll {
    async fn acquire_capabilities(&self, capabilities: Capabilities) -> Capabilities {
        capabilities
    }
}

/// The event types a starting call rings with: MSC4075 as Element Call
/// sends it now, its stable name, and the older call.notify.
const RING_TYPES: [&str; 3] = ["org.matrix.msc4075.rtc.notification", "m.rtc.notification", "org.matrix.msc4075.call.notify"];
/// The state events a call participant announces itself with (Element
/// Call's MatrixRTC membership, the unstable name and the stable one).
pub(crate) const CALL_MEMBER_TYPES: [&str; 2] = ["org.matrix.msc3401.call.member", "m.call.member"];
/// MSC4310, answering a ring with no.
const DECLINE_TYPE: &str = "org.matrix.msc4310.rtc.decline";

/// How old an event is, in ms: the server's age where it gives one, as
/// the phone's clock can be off (a mainline phone boots with a wrong date
/// until NTP); else from its timestamp.
fn event_age_ms(raw: &Raw<AnySyncMessageLikeEvent>) -> u64 {
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

impl Core {
    /// Incoming calls: a ring (or a group call's notification) from someone
    /// else that is still fresh becomes "call_ring"; the UI rings until it
    /// expires, the call ends, or it is answered or declined. The initial
    /// sync's old ones are past their lifetime and stay quiet.
    pub(crate) fn install_call_handlers(&'static self, client: &Client) {
        client.add_event_handler(|raw: Raw<AnySyncMessageLikeEvent>, room: Room| async move {
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
            let ring = content
                .get("notification_type")
                .or_else(|| content.get("notify_type"))
                .and_then(Value::as_str)
                == Some("ring");
            let sender_name = match UserId::parse(sender.as_str()) {
                Ok(u) => member_name(&room, &u).await,
                Err(_) => sender.clone(),
            };
            let room_name = room_name(&room);
            tracing::info!(room = %room.room_id(), ring, age, "call: incoming");
            crate::instance().emit(json!({
                "ev": "call_ring", "room_id": room.room_id().as_str(), "room_name": room_name,
                "sender": sender, "sender_name": sender_name, "ring": ring,
                "event_id": event_id, "expires_ms": lifetime - age,
            }));
        });

        // 1:1 calls (m.call.*): every event goes to the UI as it came, our
        // own too; the call's state machine there needs our echo (and
        // another device of ours answering) to tell its party from ours.
        // Handlers get the decrypted event in an encrypted room.
        client.add_event_handler(|raw: Raw<AnySyncMessageLikeEvent>, room: Room| async move {
            let Ok(Some(kind)) = raw.get_field::<String>("type") else { return };
            if !crate::protocol::voip_type_allowed(&kind) {
                return;
            }
            let sender = raw.get_field::<String>("sender").ok().flatten().unwrap_or_default();
            let own = sender == room.own_user_id().as_str();
            let content = raw.get_field::<Value>("content").ok().flatten().unwrap_or(Value::Null);
            let event_id = raw.get_field::<String>("event_id").ok().flatten().unwrap_or_default();
            let age = event_age_ms(&raw);
            let sender_name = match UserId::parse(sender.as_str()) {
                Ok(u) => member_name(&room, &u).await,
                Err(_) => sender.clone(),
            };
            let call_id = content.get("call_id").and_then(Value::as_str).unwrap_or("").to_owned();
            // whether the room is a direct chat: an invite for nobody in
            // particular rings only there (anyone in a big room could ring
            // every member's phone otherwise)
            let is_dm = room.is_direct().await.unwrap_or(false);
            tracing::info!(room = %room.room_id(), %kind, %call_id, own, age, is_dm, "voip: event");
            crate::instance().emit(json!({
                "ev": "voip", "room_id": room.room_id().as_str(), "room_name": room_name(&room),
                "type": kind, "sender": sender, "sender_name": sender_name, "own": own,
                "content": content, "event_id": event_id, "age_ms": age, "is_dm": is_dm,
            }));
        });
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
        let mut levels = room.power_levels().await?;
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
            return Ok(());
        }
        if !levels.user_can_send_state(&own, StateEventType::RoomPowerLevels) {
            if own_blocked {
                bail!("only moderators can take part in calls in this room; a room admin can allow calls for everyone");
            }
            // we may, others may not: the call goes ahead, they will be told
            return Ok(());
        }
        for t in CALL_MEMBER_TYPES {
            levels.events.insert(TimelineEventType::from(t), Int::from(0u8));
        }
        room.send_state_event(RoomPowerLevelsEventContent::try_from(levels)?).await?;
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
        let (driver, handle) = WidgetDriver::new(settings);
        let handle = Arc::new(handle);
        let rid = room.room_id().to_owned();
        let run = self.rt.spawn({
            let rid = rid.clone();
            async move {
                let _ = driver.run(room, GrantAll).await;
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
