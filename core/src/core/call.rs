//! Calls through Element Call (MatrixRTC with a LiveKit server), the way
//! Element X does them: Element Call is a web app shown in a WebView, and
//! the SDK's widget driver answers its Matrix requests (MSC2762, MSC3819).
//! The UI only carries the widget API's postMessage strings: "call_message"
//! commands go to the driver, "call_message" events go to the page.
//!
//! Spike: one call at a time, every capability the widget asks for is
//! granted (Element Call asks only for what a call needs in this room).

use super::*;
use matrix_sdk::ruma::events::AnySyncMessageLikeEvent;
use matrix_sdk::ruma::serde::Raw;
use matrix_sdk::widget::{
    Capabilities, CapabilitiesProvider, ClientProperties, EncryptionSystem, Intent,
    VirtualElementCallWidgetConfig, VirtualElementCallWidgetProperties, WidgetDriver,
    WidgetDriverHandle, WidgetSettings,
};

/// The hosted Element Call, at its room route: the bare address is its
/// own start page ("Name of call") and ignores the widget's room. The
/// embedded package could be bundled later.
const ELEMENT_CALL_URL: &str = "https://call.element.io/room";
const WIDGET_ID: &str = "rustrx-element-call";

pub(crate) struct CallSession {
    handle: Arc<WidgetDriverHandle>,
    tasks: Vec<JoinHandle<()>>,
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
/// MSC4310, answering a ring with no.
const DECLINE_TYPE: &str = "org.matrix.msc4310.rtc.decline";

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
            let sent = raw.get_field::<u64>("origin_server_ts").ok().flatten().unwrap_or(0);
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_millis() as u64)
                .unwrap_or(0);
            let lifetime = content.get("lifetime").and_then(Value::as_u64).unwrap_or(30_000).min(120_000);
            // the server's age where it gives one: the phone's clock can be
            // off (a mainline phone boots with a wrong date until NTP)
            let age = raw
                .get_field::<Value>("unsigned")
                .ok()
                .flatten()
                .and_then(|u| u.get("age").and_then(Value::as_u64))
                .unwrap_or_else(|| now.saturating_sub(sent));
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
                Ok(u) => match room.get_member_no_sync(&u).await {
                    Ok(Some(m)) => m.name().to_owned(),
                    _ => u.localpart().to_owned(),
                },
                Err(_) => sender.clone(),
            };
            let room_name = room.cached_display_name().map(|n| n.to_string()).unwrap_or_else(|| room.room_id().to_string());
            tracing::info!(room = %room.room_id(), ring, age, "call: incoming");
            crate::instance().emit(json!({
                "ev": "call_ring", "room_id": room.room_id().as_str(), "room_name": room_name,
                "sender": sender, "sender_name": sender_name, "ring": ring,
                "event_id": event_id, "expires_ms": lifetime - age,
            }));
        });
    }

    /// No to a ring (MSC4310): the caller's clients stop ringing for us.
    pub(crate) async fn call_decline(&'static self, room_id: &str, event_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        EventId::parse(event_id)?;
        let content = json!({"m.relates_to": {"rel_type": "m.reference", "event_id": event_id}});
        room.send_raw(DECLINE_TYPE, content).await?;
        Ok(())
    }

    pub(crate) async fn call_start(&'static self, room_id: &str, voice: bool, skip_lobby: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        self.call_end().await;

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
            .generate_webview_url(&room, ClientProperties::new("net.mowerk.rustrx", None, Some("dark".to_owned())))
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
        self.state.lock().await.call = Some(CallSession { handle, tasks: vec![run, forward] });
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
        if let Some(call) = self.state.lock().await.call.take() {
            call.stop();
        }
    }
}
