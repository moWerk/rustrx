// SPDX-License-Identifier: GPL-3.0-or-later
//! Push (UnifiedPush through a distributor such as Foghorn): the pusher on
//! the homeserver, and one pushed message fetched by a process woken for it.
//! The UI side (registering, the endpoint, the gateway, the D-Bus connector)
//! is src/UnifiedPush.cpp; the plan is docs/PUSH.md. (part of `impl Core`)

use super::*;
use matrix_sdk::ruma::api::client::push::{Pusher, PusherIds, PusherInit, PusherKind};
use matrix_sdk::ruma::events::{AnySyncMessageLikeEvent, AnySyncTimelineEvent};
use matrix_sdk::ruma::push::{HttpPusherData, PushFormat};
use matrix_sdk_ui::notification_client::{
    NotificationClient, NotificationEvent, NotificationProcessSetup, NotificationStatus, RawNotificationEvent,
};

use super::call::VoipEvent;

/// A process started by the distributor for a push: no sync, no room list,
/// only the pushed message (RUSTRX_PUSH_WAKE, set by its D-Bus service file).
pub(crate) fn push_wake() -> bool {
    std::env::var("RUSTRX_PUSH_WAKE").map(|v| v == "1").unwrap_or(false)
}

/// What a notification shows of a message: its text, or what it is.
fn body_of(event: &AnySyncTimelineEvent) -> Option<String> {
    match event {
        AnySyncTimelineEvent::MessageLike(AnySyncMessageLikeEvent::RoomMessage(m)) => {
            m.as_original().map(|o| o.content.msgtype.body().to_owned())
        }
        AnySyncTimelineEvent::MessageLike(AnySyncMessageLikeEvent::RoomEncrypted(_)) => None,
        AnySyncTimelineEvent::MessageLike(AnySyncMessageLikeEvent::Sticker(s)) => {
            s.as_original().map(|o| o.content.body.clone())
        }
        // a call that does not ring (too old, or a call going on), as the room list says it
        AnySyncTimelineEvent::MessageLike(AnySyncMessageLikeEvent::CallInvite(_)) => Some("Call".to_owned()),
        _ => None,
    }
}

impl Core {
    /// The homeserver pushes to this gateway for this endpoint (the push
    /// address, a secret: never logged); a changed endpoint replaces the
    /// old one. Answered by "pusher_set".
    pub(crate) async fn push_set(&'static self, endpoint: &str, gateway: &str, old: Option<&str>, lang: Option<&str>) -> Result<()> {
        let client = self.client().await?;
        let mut data = HttpPusherData::new(gateway.to_owned());
        data.format = Some(PushFormat::EventIdOnly);
        let pusher: Pusher = PusherInit {
            ids: PusherIds::new(endpoint.to_owned(), APP_ID.to_owned()),
            kind: PusherKind::Http(data),
            app_display_name: "Rustrx".to_owned(),
            device_display_name: "Rustrx on Sailfish OS".to_owned(),
            profile_tag: None,
            lang: lang.unwrap_or("en").to_owned(),
        }
        .into();
        client.pusher().set(pusher, false).await?;
        if let Some(old) = old.filter(|o| !o.is_empty() && *o != endpoint) {
            if let Err(e) = client.pusher().delete(PusherIds::new(old.to_owned(), APP_ID.to_owned())).await {
                tracing::info!("push: removing the old pusher: {e}");
            }
        }
        tracing::info!(gateway, "push: pusher set");
        self.emit(json!({"ev": "pusher_set", "endpoint": endpoint}));
        Ok(())
    }

    /// Whether the homeserver still has our pusher for this endpoint (a
    /// password change, an admin or an expiry can drop it): "pusher_checked".
    pub(crate) async fn push_check(&'static self, endpoint: &str) -> Result<()> {
        use matrix_sdk::ruma::api::client::push::get_pushers;
        let client = self.client().await?;
        let pushers = client.send(get_pushers::v3::Request::new()).await?.pushers;
        let present = pushers.iter().any(|p| p.ids.pushkey == endpoint && p.ids.app_id == APP_ID);
        tracing::info!(present, "push: pusher checked");
        self.emit(json!({"ev": "pusher_checked", "endpoint": endpoint, "present": present}));
        Ok(())
    }

    pub(crate) async fn push_remove(&'static self, endpoint: &str) -> Result<()> {
        let client = self.client().await?;
        client.pusher().delete(PusherIds::new(endpoint.to_owned(), APP_ID.to_owned())).await?;
        tracing::info!("push: pusher removed");
        Ok(())
    }

    /// One pushed message: fetched and decrypted on its own (the SDK's
    /// notification client, small timeouts, made for this), or after one
    /// short sync where the server has no sliding sync. Answered by
    /// "push_notification" unless the account's rules keep it quiet. In a
    /// process woken for it, which has no sync, a classic call's events go
    /// to the UI as "voip" too, as the sync handler sends them (call.rs):
    /// an invite rings there; the other call events get no line.
    pub(crate) async fn push_handle(&'static self, room_id: &str, event_id: &str) -> Result<()> {
        let _one_at_a_time = self.push_lock.lock().await;
        let client = self.client().await?;
        let room_id = OwnedRoomId::try_from(room_id)?;
        let event_id = OwnedEventId::try_from(event_id)?;
        let t = std::time::Instant::now();
        let sliding = !matches!(client.sliding_sync_version(), Version::None);
        let wake = push_wake();
        // while a pushed call rings: the caller's hangup and the rest of the
        // call's events, encrypted, are pushed, but the push rules filter
        // them out once decrypted; the event fetched first, without waiting
        // for a notification client per candidates event
        if wake && sliding {
            if let Some(call_id) = self.push_ringing(room_id.as_str()) {
                if self.push_call_event(&client, &room_id, &event_id, &call_id).await {
                    tracing::info!(ms = t.elapsed().as_millis() as u64, "push: call event fetched");
                    return Ok(());
                }
            }
        }
        let (room_name, sender_name, body, is_dm, noisy) = if sliding {
            // In the app (or the stand-in) the sync service runs in this
            // process: the notification client works with it, not beside
            // it (two encryption syncs over one crypto store otherwise).
            // Only the process a push woke has none: there the SDK's
            // cross-process lock guards the store.
            let setup = match self.state.lock().await.sync_service.clone() {
                Some(sync_service) => NotificationProcessSetup::SingleProcess { sync_service },
                None => NotificationProcessSetup::MultipleProcesses,
            };
            let nc = NotificationClient::new(client.clone(), setup).await?;
            match nc.get_notification(&room_id, &event_id).await? {
                NotificationStatus::Event(item) => {
                    if let RawNotificationEvent::Timeline(raw) = &item.raw_event {
                        if let Some(ev) = VoipEvent::of(raw) {
                            if wake {
                                let sender_name = item.sender_display_name.clone().unwrap_or_else(|| ev.sender.clone());
                                let own = client.user_id().is_some_and(|u| u.as_str() == ev.sender);
                                self.emit_voip(&ev, room_id.as_str(), &item.room_computed_display_name, &sender_name, own,
                                               item.is_direct_message_room);
                            }
                            if !ev.is_invite() {
                                return Ok(());
                            }
                        }
                    }
                    let body = match &item.event {
                        NotificationEvent::Timeline(ev) => body_of(ev),
                        NotificationEvent::Invite(_) => None,
                    };
                    (
                        item.room_computed_display_name.clone(),
                        item.sender_display_name.clone().unwrap_or_default(),
                        body,
                        item.is_direct_message_room,
                        item.is_noisy.unwrap_or(true),
                    )
                }
                other => {
                    tracing::info!(ms = t.elapsed().as_millis() as u64, "push: nothing to show ({})",
                        match other {
                            NotificationStatus::EventFilteredOut => "filtered out",
                            NotificationStatus::EventRedacted => "redacted",
                            _ => "not found",
                        });
                    return Ok(());
                }
            }
        } else {
            // the classic sync: one round, which brings the keys too
            let settings = matrix_sdk::config::SyncSettings::default().timeout(Duration::from_secs(0));
            client.sync_once(settings).await?;
            let room = client.get_room(&room_id).context("push: unknown room")?;
            let event = room.event(&event_id, None).await?;
            // (the sync above passed it on already when it was in its batch)
            let call = if wake { self.voip_in_room(event.raw(), &room).await } else { VoipEvent::of(event.raw()) };
            if call.is_some_and(|ev| !ev.is_invite()) {
                return Ok(());
            }
            if matches!(room.notification_mode().await, Some(RoomNotificationMode::Mute)) {
                return Ok(());
            }
            let parsed: Option<AnySyncTimelineEvent> = event.raw().deserialize().ok();
            let sender = parsed.as_ref().map(|e| e.sender().to_owned());
            let sender_name = match &sender {
                Some(u) => member_name(&room, u).await,
                None => String::new(),
            };
            let name = room_name(&room);
            (name, sender_name, parsed.as_ref().and_then(body_of), room.is_direct().await.unwrap_or(false), true)
        };
        tracing::info!(ms = t.elapsed().as_millis() as u64, sliding, "push: message fetched");
        self.emit(json!({
            "ev": "push_notification", "room_id": room_id.as_str(), "event_id": event_id.as_str(),
            "room_name": room_name, "sender_name": sender_name, "body": body,
            "is_dm": is_dm, "noisy": noisy,
        }));
        Ok(())
    }

    /// A pushed event of the call ringing in the room (its hangup, say),
    /// to the UI; false when it is something else, or still encrypted, and
    /// the notification client is to have it.
    async fn push_call_event(&'static self, client: &Client, room_id: &RoomId, event_id: &EventId, call_id: &str) -> bool {
        let Some(room) = client.get_room(room_id) else { return false };
        let event = match room.event(event_id, None).await {
            Ok(e) => e,
            Err(e) => {
                tracing::info!("push: fetching the event during a ring: {e}");
                return false;
            }
        };
        match VoipEvent::of(event.raw()) {
            Some(ev) if !ev.is_invite() => {
                if ev.call_id() == call_id {
                    self.voip_in_room(event.raw(), &room).await;
                }
                true
            }
            _ => false,
        }
    }
}
