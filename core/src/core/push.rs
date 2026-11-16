// SPDX-License-Identifier: GPL-3.0-or-later
//! Push (UnifiedPush through a distributor such as Foghorn): the pusher on
//! the homeserver, and one pushed message fetched by a process woken for it.
//! The UI side (registering, the endpoint, the gateway, the D-Bus connector)
//! is src/UnifiedPush.cpp; the plan is docs/PUSH.md. (part of `impl Core`)

use super::*;
use matrix_sdk::ruma::api::client::push::{Pusher, PusherIds, PusherInit, PusherKind};
use matrix_sdk::ruma::events::{AnySyncMessageLikeEvent, AnySyncTimelineEvent};
use matrix_sdk::ruma::push::{HttpPusherData, PushFormat};
use matrix_sdk_ui::notification_client::{NotificationClient, NotificationEvent, NotificationProcessSetup, NotificationStatus};

/// The pusher's app id on every homeserver: never to change once pushers exist.

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

    pub(crate) async fn push_remove(&'static self, endpoint: &str) -> Result<()> {
        let client = self.client().await?;
        client.pusher().delete(PusherIds::new(endpoint.to_owned(), APP_ID.to_owned())).await?;
        tracing::info!("push: pusher removed");
        Ok(())
    }

    /// One pushed message: fetched and decrypted on its own (the SDK's
    /// notification client, small timeouts, made for this), or after one
    /// short sync where the server has no sliding sync. Answered by
    /// "push_notification" unless the account's rules keep it quiet.
    pub(crate) async fn push_handle(&'static self, room_id: &str, event_id: &str) -> Result<()> {
        let _one_at_a_time = self.push_lock.lock().await;
        let client = self.client().await?;
        let room_id = OwnedRoomId::try_from(room_id)?;
        let event_id = OwnedEventId::try_from(event_id)?;
        let t = std::time::Instant::now();
        let sliding = !matches!(client.sliding_sync_version(), Version::None);
        let (room_name, sender_name, body, is_dm, noisy) = if sliding {
            let nc = NotificationClient::new(client.clone(), NotificationProcessSetup::MultipleProcesses).await?;
            match nc.get_notification(&room_id, &event_id).await? {
                NotificationStatus::Event(item) => {
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
            if matches!(room.notification_mode().await, Some(RoomNotificationMode::Mute)) {
                return Ok(());
            }
            let event = room.event(&event_id, None).await?;
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
}
