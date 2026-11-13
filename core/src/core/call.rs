//! Calls through Element Call (MatrixRTC with a LiveKit server), the way
//! Element X does them: Element Call is a web app shown in a WebView, and
//! the SDK's widget driver answers its Matrix requests (MSC2762, MSC3819).
//! The UI only carries the widget API's postMessage strings: "call_message"
//! commands go to the driver, "call_message" events go to the page.
//!
//! Spike: one call at a time, every capability the widget asks for is
//! granted (Element Call asks only for what a call needs in this room).

use super::*;
use matrix_sdk::widget::{
    Capabilities, CapabilitiesProvider, ClientProperties, EncryptionSystem, Intent,
    VirtualElementCallWidgetConfig, VirtualElementCallWidgetProperties, WidgetDriver,
    WidgetDriverHandle, WidgetSettings,
};

/// The hosted Element Call; the embedded package could be bundled later.
const ELEMENT_CALL_URL: &str = "https://call.element.io";
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

impl Core {
    pub(crate) async fn call_start(&'static self, room_id: &str, voice: bool) -> Result<()> {
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
        let encryption = if room.encryption_state().is_encrypted() {
            EncryptionSystem::PerParticipantKeys
        } else {
            EncryptionSystem::Unencrypted
        };
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
            ..Default::default()
        };
        let settings = WidgetSettings::new_virtual_element_call_widget(props, config)
            .map_err(|e| anyhow!("call widget: {e}"))?;
        let url = settings
            .generate_webview_url(&room, ClientProperties::new("net.mowerk.rustrx", None, Some("dark".to_owned())))
            .await
            .map_err(|e| anyhow!("call url: {e}"))?;

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
        tracing::info!(room = %rid, "call widget started");
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
