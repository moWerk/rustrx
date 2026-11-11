//! The open room: its timeline, typing, media, notification mode.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

impl Core {
    /// Display names for the people whose read receipts sit on these items;
    /// unknown members fall back to the localpart in the UI.
    pub(crate) async fn receipt_names<'a>(
        room: &Room,
        items: impl IntoIterator<Item = &'a Arc<TimelineItem>>,
        names: &mut HashMap<OwnedUserId, String>,
    ) {
        for item in items {
            let Some(event) = item.as_event() else { continue };
            for user in event.read_receipts().keys() {
                if names.contains_key(user) {
                    continue;
                }
                let name = match room.get_member_no_sync(user).await {
                    Ok(Some(m)) => m.name().to_owned(),
                    _ => user.localpart().to_owned(),
                };
                names.insert(user.clone(), name);
            }
        }
    }

    /// Timeline snapshots for the open room, coalesced per update burst, and
    /// who is typing there (the own user left out).
    pub(crate) async fn timeline_task(&'static self, room: Room, timeline: Arc<Timeline>, own: OwnedUserId) {
        let room_id = room.room_id().to_owned();
        let mut names = HashMap::new();
        let (items, mut stream) = timeline.subscribe().await;
        let items: Vec<Arc<TimelineItem>> = items.into_iter().collect();
        Self::receipt_names(&room, &items, &mut names).await;
        self.emit(json!({"ev": "timeline", "room_id": room_id.as_str(), "items": items_json(&items, &own, &names)}));
        // the guard keeps the typing handler registered while this task lives
        let (_typing_guard, mut typing) = room.subscribe_to_typing_notifications();
        loop {
            tokio::select! {
                update = stream.next() => {
                    if update.is_none() {
                        break;
                    }
                    tokio::time::sleep(Duration::from_millis(60)).await;
                    while let Some(Some(_)) = stream.next().now_or_never() {}
                    let items = timeline.items().await;
                    Self::receipt_names(&room, &items, &mut names).await;
                    self.emit(json!({"ev": "timeline", "room_id": room_id.as_str(), "items": items_json(&items, &own, &names)}));
                }
                users = typing.recv() => {
                    let Ok(users) = users else { continue };
                    let mut who = Vec::new();
                    for u in users.iter().filter(|u| **u != own) {
                        let name = match room.get_member_no_sync(u).await {
                            Ok(Some(m)) => m.name().to_owned(),
                            _ => u.localpart().to_owned(),
                        };
                        who.push(name);
                    }
                    self.emit(json!({"ev": "typing", "room_id": room_id.as_str(), "names": who}));
                }
            }
        }
    }


    pub(crate) async fn notification_mode(&self, room: &Room) -> Result<()> {
        let client = self.client().await?;
        let settings = client.notification_settings().await;
        let mode = match settings.get_user_defined_room_notification_mode(room.room_id()).await {
            Some(RoomNotificationMode::AllMessages) => "all",
            Some(RoomNotificationMode::MentionsAndKeywordsOnly) => "mentions",
            Some(RoomNotificationMode::Mute) => "mute",
            None => "default",
        };
        self.emit(json!({"ev": "notification_mode", "room_id": room.room_id().as_str(), "mode": mode}));
        Ok(())
    }

    pub(crate) async fn open_room(&'static self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        let own = client.user_id().context("no user id")?.to_owned();
        let room_id = room.room_id().to_owned();
        if let Some(service) = self.state.lock().await.sync_service.clone() {
            service.room_list_service().set_room_subscriptions(&[&room_id]).await;
        }
        let timeline = Arc::new(
            room.timeline_builder()
                .track_read_marker_and_receipts(TimelineReadReceiptTracking::MessageLikeEvents)
                .build()
                .await?,
        );
        let task = self.rt.spawn(self.timeline_task(room.clone(), timeline.clone(), own));
        let mut st = self.state.lock().await;
        if let Some(previous) = st.open.take() {
            previous.task.abort();
        }
        st.open = Some(OpenRoom { room_id, timeline: timeline.clone(), task });
        drop(st);
        // Sliding sync brings a short window; fill the first screen.
        let tl = timeline.clone();
        self.rt.spawn(async move {
            let _ = tl.paginate_backwards(30).await;
        });
        Ok(())
    }

    pub(crate) async fn fetch_media<C: MediaEventContent>(
        client: &Client,
        content: &C,
        thumb: bool,
    ) -> Result<Option<Vec<u8>>> {
        if thumb {
            let settings = MediaThumbnailSettings::new(uint!(640), uint!(640));
            Ok(client.media().get_thumbnail(content, settings, true).await?)
        } else {
            Ok(client.media().get_file(content, true).await?)
        }
    }

    pub(crate) async fn get_media(&'static self, room_id: &str, event_id: &str, thumb: bool, dest_dir: Option<String>) -> Result<()> {
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        let tl = self.timeline_for(&room).await?;
        let item = tl.item_by_event_id(&EventId::parse(&event_id)?).await.context("event not in timeline")?;
        let (bytes, mime) = match item.content().as_message().map(|m| m.msgtype()) {
            Some(MessageType::Image(c)) => (
                Self::fetch_media(&client, c, thumb).await?,
                c.info.as_ref().and_then(|i| i.mimetype.clone()),
            ),
            Some(MessageType::File(c)) => (
                Self::fetch_media(&client, c, false).await?,
                c.info.as_ref().and_then(|i| i.mimetype.clone()),
            ),
            Some(MessageType::Audio(c)) => (
                Self::fetch_media(&client, c, false).await?,
                c.info.as_ref().and_then(|i| i.mimetype.clone()),
            ),
            Some(MessageType::Video(c)) => (
                Self::fetch_media(&client, c, thumb).await?,
                c.info.as_ref().and_then(|i| i.mimetype.clone()),
            ),
            _ => match item.content().as_sticker() {
                Some(st) => (Self::fetch_media(&client, st.content(), false).await?, None),
                None => bail!("no media in this event"),
            },
        };
        let bytes = bytes.context("media not available")?;
        let ext = mime
            .as_deref()
            .and_then(|m| mime_guess::get_mime_extensions_str(m).and_then(|e| e.first().copied()))
            .unwrap_or("bin");
        let safe: String = event_id.chars().filter(|c| c.is_ascii_alphanumeric()).collect();
        // "dest_dir": a copy for other apps (the data dir is private to the sandbox),
        // named after the file where the message has a name
        let exported = dest_dir.is_some();
        let path = match dest_dir {
            Some(dir) => {
                let original = item
                    .content()
                    .as_message()
                    .map(|m| m.body().to_owned())
                    .filter(|b| !b.is_empty() && b.len() < 200)
                    .map(|b| b.chars().map(|c| if c == '/' || c == '\\' || c.is_control() { '_' } else { c }).collect::<String>())
                    .unwrap_or_else(|| format!("{safe}.{ext}"));
                let dir = PathBuf::from(dir);
                tokio::fs::create_dir_all(&dir).await?;
                let mut path = dir.join(&original);
                if path.exists() {
                    path = dir.join(format!("{safe}-{original}"));
                }
                path
            }
            None => self.data_dir.join("media").join(format!("{safe}{}.{ext}", if thumb { "-thumb" } else { "" })),
        };
        tokio::fs::write(&path, &bytes).await?;
        self.emit(json!({"ev": "media", "room_id": room.room_id().as_str(), "event_id": event_id,
                         "thumb": thumb, "exported": exported,
                         "path": path.to_string_lossy()}));
        Ok(())
    }
}
