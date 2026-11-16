//! The open room: its timeline, sending, receipts, typing, media.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;
use matrix_sdk::send_queue::LocalEchoContent;

impl Core {
    /// A reaction the server refuses for good (sent twice, say) stays in its
    /// room's send queue, and the SDK holds back everything sent to that room
    /// after it, across restarts, to keep the order. Such reactions are
    /// dropped: at the start and whenever a send is refused. A refused
    /// message stays, shown as not sent, for the user to retry or delete.
    pub(crate) async fn send_queue_watch(client: Client) {
        let mut errors = client.send_queue().subscribe_errors();
        Self::drop_refused_reactions(&client).await;
        loop {
            match errors.recv().await {
                Ok(e) if !e.is_recoverable => Self::drop_refused_reactions(&client).await,
                Ok(_) => {}
                Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => Self::drop_refused_reactions(&client).await,
                Err(tokio::sync::broadcast::error::RecvError::Closed) => break,
            }
        }
    }

    async fn drop_refused_reactions(client: &Client) {
        let echoes = match client.send_queue().local_echoes().await {
            Ok(echoes) => echoes,
            Err(e) => {
                tracing::warn!("send queue: no local echoes: {e}");
                return;
            }
        };
        let mut woken = std::collections::HashSet::new();
        for (room_id, list) in echoes {
            for echo in list {
                let LocalEchoContent::Event { serialized_event, send_handle, send_error: Some(error) } = echo.content else {
                    continue;
                };
                if serialized_event.raw().1 != "m.reaction" {
                    continue;
                }
                let dropped = send_handle.abort().await;
                tracing::warn!(%room_id, txn_id = %echo.transaction_id, ?error, ?dropped, "send queue: refused reaction dropped");
                woken.insert(room_id.clone());
            }
        }
        // the abort wakes the room's sending task; once in a test run the
        // room still slept until the next start, so it is woken once more
        for room_id in woken {
            if let Some(room) = client.get_room(&room_id) {
                room.send_queue().set_enabled(true);
            }
        }
    }

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
                names.insert(user.clone(), member_name(room, user).await);
            }
        }
    }

    /// A reply whose original is not loaded (older than the loaded history,
    /// or not yet decrypted when the reply came) gets it fetched, once; the
    /// timeline then updates with the quote.
    pub(crate) fn fetch_replied<'a>(
        timeline: &Arc<Timeline>,
        items: impl IntoIterator<Item = &'a Arc<TimelineItem>>,
        asked: &mut std::collections::HashSet<OwnedEventId>,
    ) {
        for item in items {
            let Some(event) = item.as_event() else { continue };
            let Some(msg) = event.content().as_msglike() else { continue };
            let Some(reply) = &msg.in_reply_to else { continue };
            if !matches!(reply.event, TimelineDetails::Unavailable) {
                continue;
            }
            let Some(id) = event.event_id().map(ToOwned::to_owned) else { continue };
            if !asked.insert(id.clone()) {
                continue;
            }
            let tl = timeline.clone();
            tokio::spawn(async move {
                if let Err(e) = tl.fetch_details_for_event(&id).await {
                    tracing::info!(event = %id, "reply: fetching the original: {e}");
                }
            });
        }
    }

    /// A timeline's items on every change, coalesced per update burst, until
    /// its stream ends. Replies get their originals fetched, read receipts
    /// their names. `started` runs once, after the first items are out.
    pub(crate) async fn stream_timeline(
        room: &Room,
        timeline: &Arc<Timeline>,
        own: &UserId,
        emit: impl Fn(Value) + Send,
        started: impl FnOnce() + Send,
    ) {
        let mut names = HashMap::new();
        let mut replies_asked = std::collections::HashSet::new();
        let (mut items, mut stream) = timeline.subscribe().await;
        let mut started = Some(started);
        loop {
            Self::fetch_replied(timeline, &items, &mut replies_asked);
            Self::receipt_names(room, &items, &mut names).await;
            emit(items_json(&items, own, &names));
            if let Some(started) = started.take() {
                started();
            }
            if stream.next().await.is_none() {
                break;
            }
            tokio::time::sleep(TIMELINE_COALESCE).await;
            while let Some(Some(_)) = stream.next().now_or_never() {}
            items = timeline.items().await;
        }
    }

    /// Timeline snapshots for the open room, and who is typing there (the
    /// own user left out). Ends when the timeline's stream does.
    pub(crate) async fn timeline_task(&'static self, room: Room, timeline: Arc<Timeline>, own: OwnedUserId) {
        let room_id = room.room_id().to_owned();
        let emit = |items: Value| self.emit(json!({"ev": "timeline", "room_id": room_id.as_str(), "items": items}));
        let items = Self::stream_timeline(&room, &timeline, &own, emit, || {});
        // the guard keeps the typing handler registered while this task lives
        let (_typing_guard, mut typing) = room.subscribe_to_typing_notifications();
        let typing = async {
            loop {
                let users = match typing.recv().await {
                    Ok(users) => users,
                    Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => continue,
                    // no more typing news; the timeline goes on
                    Err(tokio::sync::broadcast::error::RecvError::Closed) => std::future::pending().await,
                };
                let mut who = Vec::new();
                for u in users.iter().filter(|u| **u != own) {
                    who.push(member_name(&room, u).await);
                }
                self.emit(json!({"ev": "typing", "room_id": room_id.as_str(), "names": who}));
            }
        };
        tokio::select! {
            _ = items => {}
            _ = typing => {}
        }
    }

    /// A timeline with this focus; `track` follows the read marker and the
    /// receipts (a timeline made only to send into does not need them).
    pub(crate) async fn build_timeline(room: &Room, focus: TimelineFocus, track: bool) -> Result<Arc<Timeline>> {
        let mut builder = room.timeline_builder().with_focus(focus);
        if track {
            builder = builder.track_read_marker_and_receipts(TimelineReadReceiptTracking::MessageLikeEvents);
        }
        Ok(Arc::new(builder.build().await?))
    }

    pub(crate) async fn open_room(&'static self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        let own = client.user_id().context("no user id")?.to_owned();
        let room_id = room.room_id().to_owned();
        // bound first: the lock is not held across the subscription
        let service = self.state.lock().await.sync_service.clone();
        if let Some(service) = service {
            service.room_list_service().set_room_subscriptions(&[&room_id]).await;
        }
        // every message, thread replies too (marked by their thread_root);
        // the thread's page shows a thread alone, its root a summary
        let timeline = Self::build_timeline(&room, TimelineFocus::Live { hide_threaded_events: false }, true).await?;
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
            let _ = tl.paginate_backwards(PAGINATE_DEFAULT).await;
        });
        Ok(())
    }

    pub(crate) async fn close_room(&self) {
        let open = self.state.lock().await.open.take();
        if let Some(open) = open {
            open.task.abort();
        }
    }

    /// A timeline item by event id, or by transaction id for a local echo.
    fn item_id(event_id: Option<String>, txn_id: Option<String>) -> Result<TimelineEventItemId> {
        if let Some(txn) = given(&txn_id) {
            return Ok(TimelineEventItemId::TransactionId(txn.into()));
        }
        let id = given(&event_id).context("missing field `event_id`")?;
        Ok(TimelineEventItemId::EventId(EventId::parse(id)?))
    }

    /// The open room's timeline when it matches, else a throwaway one.
    pub(crate) async fn timeline_for(&self, room: &Room) -> Result<Arc<Timeline>> {
        let open = {
            let st = self.state.lock().await;
            st.open.as_ref().filter(|o| o.room_id == room.room_id()).map(|o| o.timeline.clone())
        };
        match open {
            Some(tl) => Ok(tl),
            None => Ok(Arc::new(room.timeline().await?)),
        }
    }

    /// Older history; answered by "paginated".
    pub(crate) async fn paginate_back(&self, room_id: &str, count: Option<u16>) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_for(&room).await?;
        let more = tl.paginate_backwards(count.unwrap_or(PAGINATE_DEFAULT)).await?;
        self.emit(json!({"ev": "paginated", "room_id": room.room_id().as_str(), "at_start": more}));
        Ok(())
    }

    pub(crate) async fn send_text(
        &self,
        room_id: &str,
        body: &str,
        reply_to: Option<&str>,
        markdown: bool,
        emotes: Option<&HashMap<String, String>>,
        thread_root: Option<&str>,
    ) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_target(&room, thread_root).await?;
        let mut content = if markdown {
            RoomMessageEventContent::text_markdown(body)
        } else {
            RoomMessageEventContent::text_plain(body)
        };
        if let Some(emotes) = emotes {
            packs::with_emotes(&mut content, emotes);
        }
        match reply_to {
            Some(reply) => {
                let without: RoomMessageEventContentWithoutRelation = content.into();
                tl.send_reply(without, EventId::parse(reply)?).await?;
            }
            None => {
                tl.send(content.into()).await?;
            }
        }
        Ok(())
    }

    /// A file; a picture carries its size and a thumbnail made by the UI,
    /// a voice message its duration.
    #[allow(clippy::too_many_arguments)]
    pub(crate) async fn send_file(
        &self,
        room_id: &str,
        path: String,
        caption: Option<&str>,
        w: Option<u64>,
        h: Option<u64>,
        thumb_path: Option<String>,
        thumb_w: Option<u64>,
        thumb_h: Option<u64>,
        voice: bool,
        duration_ms: Option<u64>,
        thread_root: Option<&str>,
    ) -> Result<()> {
        let room = self.room(room_id).await?;
        let path = PathBuf::from(path);
        let mime = mime_guess::from_path(&path).first_or_octet_stream();
        let tl = self.timeline_target(&room, thread_root).await?;
        let dim = |n: Option<u64>| n.and_then(|n| UInt::try_from(n).ok());
        let mut config = AttachmentConfig::default();
        if let (Some(w), Some(h)) = (dim(w), dim(h)) {
            let size = std::fs::metadata(&path).ok().and_then(|m| UInt::try_from(m.len()).ok());
            config.info = Some(AttachmentInfo::Image(BaseImageInfo {
                width: Some(w),
                height: Some(h),
                size,
                ..Default::default()
            }));
        }
        if let (Some(tp), Some(w), Some(h)) = (thumb_path, dim(thumb_w), dim(thumb_h)) {
            if let Ok(data) = std::fs::read(&tp) {
                let size = UInt::try_from(data.len() as u64).unwrap_or_default();
                config.thumbnail = Some(Thumbnail {
                    data,
                    content_type: mime::IMAGE_JPEG,
                    width: w,
                    height: h,
                    size,
                });
            }
            let _ = std::fs::remove_file(&tp);
        }
        if let Some(caption) = caption {
            config.caption = Some(TextMessageEventContent::markdown(caption));
        }
        // a voice message: other clients show it as one (a player, not a file)
        if voice {
            let duration = duration_ms.map(Duration::from_millis);
            let size = std::fs::metadata(&path).ok().and_then(|m| UInt::try_from(m.len()).ok());
            config.info = Some(AttachmentInfo::Voice(BaseAudioInfo { duration, size, ..Default::default() }));
        }
        // through the send queue: the message shows at once as a local
        // echo with its upload progress, instead of only once uploaded
        tl.send_attachment(path, mime, config).use_send_queue().await?;
        Ok(())
    }

    pub(crate) async fn react(&self, room_id: &str, event_id: &str, key: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_holding(&room, Some(event_id)).await?;
        let id = TimelineEventItemId::EventId(EventId::parse(event_id)?);
        tl.toggle_reaction(&id, key).await?;
        Ok(())
    }

    /// A sent event is redacted; a local echo (txn_id) is discarded.
    pub(crate) async fn redact(
        &self,
        room_id: &str,
        event_id: Option<String>,
        txn_id: Option<String>,
        reason: Option<&str>,
    ) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_holding(&room, event_id.as_deref()).await?;
        tl.redact(&Self::item_id(event_id, txn_id)?, reason).await?;
        Ok(())
    }

    pub(crate) async fn edit(&self, room_id: &str, event_id: Option<String>, txn_id: Option<String>, body: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_holding(&room, event_id.as_deref()).await?;
        let content: RoomMessageEventContentWithoutRelation = RoomMessageEventContent::text_markdown(body).into();
        tl.edit(&Self::item_id(event_id, txn_id)?, EditedContent::RoomMessage(content)).await?;
        Ok(())
    }

    pub(crate) async fn poll_vote(&self, room_id: &str, event_id: &str, answers: Vec<String>) -> Result<()> {
        let room = self.room(room_id).await?;
        let poll = EventId::parse(event_id)?;
        room.send(UnstablePollResponseEventContent::new(answers, poll)).await?;
        Ok(())
    }

    pub(crate) async fn report(&self, room_id: &str, event_id: &str, reason: Option<&str>) -> Result<()> {
        let room = self.room(room_id).await?;
        let event_id = EventId::parse(event_id)?;
        room.report_content(event_id, reason.map(str::to_owned)).await?;
        Ok(())
    }

    /// The event's JSON as received, and its latest edit: "event_source".
    pub(crate) async fn event_source(&self, room_id: &str, event_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_holding(&room, Some(event_id)).await?;
        let item = tl
            .item_by_event_id(&EventId::parse(event_id)?)
            .await
            .context("the message is not in the loaded timeline")?;
        // as received where it cannot be read
        let pretty = |raw: &matrix_sdk::ruma::serde::Raw<_>| match raw_value(raw) {
            Value::Null => raw.json().get().to_owned(),
            v => serde_json::to_string_pretty(&v).unwrap_or_else(|_| raw.json().get().to_owned()),
        };
        let original = item.original_json().map(&pretty).context("no source for this message yet")?;
        let edit = item.latest_edit_json().map(&pretty);
        self.emit(json!({"ev": "event_source", "event_id": event_id, "json": original, "edit": edit}));
        Ok(())
    }

    pub(crate) async fn mark_read(&self, room_id: &str, private: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        // the open room's timeline knows its newest item; any other room
        // gets the receipt on its latest event (a throwaway timeline is
        // empty and marked nothing, so "Mark as read" in the list did nothing)
        let is_open = {
            let st = self.state.lock().await;
            st.open.as_ref().map(|o| o.room_id == room.room_id()).unwrap_or(false)
        };
        // a private receipt moves our marker without telling the room
        let kind = if private { ReceiptType::ReadPrivate } else { ReceiptType::Read };
        // The fully-read marker is where a room opens: only our own
        // account sees it, so it moves with "receipts off" too.
        // Without it a room opened where another client left it.
        if is_open {
            let tl = self.timeline_for(&room).await?;
            tl.mark_as_read(kind).await?;
            tl.mark_as_read(ReceiptType::FullyRead).await?;
        } else if let Some(event_id) = crate::serialize::latest_event_id(&room) {
            room.send_single_receipt(kind, ReceiptThread::Unthreaded, event_id.clone()).await?;
            room.send_single_receipt(ReceiptType::FullyRead, ReceiptThread::Unthreaded, event_id).await?;
        }
        let _ = room.set_unread_flag(false).await;
        Ok(())
    }

    pub(crate) async fn typing(&self, room_id: &str, typing: bool) -> Result<()> {
        let room = self.room(room_id).await?;
        room.typing_notice(typing).await?;
        Ok(())
    }

    pub(crate) async fn fetch_media<C: MediaEventContent>(
        client: &Client,
        content: &C,
        thumb: bool,
    ) -> Result<Option<Vec<u8>>> {
        if thumb {
            let settings = MediaThumbnailSettings::new(MEDIA_THUMB_PX.into(), MEDIA_THUMB_PX.into());
            Ok(client.media().get_thumbnail(content, settings, true).await?)
        } else {
            Ok(client.media().get_file(content, true).await?)
        }
    }

    pub(crate) async fn get_media(&'static self, room_id: &str, event_id: &str, thumb: bool, dest_dir: Option<String>) -> Result<()> {
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        let tl = self.timeline_holding(&room, Some(event_id)).await?;
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
        // the extension from what the file is: a thumbnail is a JPEG or PNG
        // whatever the original was, and Qt picks its image reader by the
        // extension (a JPEG named .webp or .mp4 showed nothing); else the
        // message's type
        let ext = sniff_extension(&bytes)
            .or_else(|| mime.as_deref().and_then(|m| mime_guess::get_mime_extensions_str(m).and_then(|e| e.first().copied())))
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
                    .filter(|b| !b.is_empty() && b.len() < MAX_FILENAME_FROM_BODY)
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
            None => self.data_dir.join(MEDIA_DIR).join(format!("{safe}{}.{ext}", if thumb { "-thumb" } else { "" })),
        };
        tokio::fs::write(&path, &bytes).await?;
        self.emit(json!({"ev": "media", "room_id": room.room_id().as_str(), "event_id": event_id,
                         "thumb": thumb, "exported": exported,
                         "path": path.to_string_lossy()}));
        Ok(())
    }
}

impl Core {
    /// A custom emoji's picture, cached by its address in media/emotes (one
    /// file per address, kept: an mxc address never changes its content).
    /// A cached one is answered without the network.
    pub(crate) async fn fetch_emote(&'static self, mxc: &str) -> Result<()> {
        let dir = self.data_dir.join(MEDIA_DIR).join(EMOTE_DIR);
        let safe: String = mxc.trim_start_matches("mxc://").chars()
            .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' }).collect();
        let cached = std::fs::read_dir(&dir).ok().and_then(|entries| {
            entries.flatten().map(|e| e.path()).find(|p| p.file_stem().map_or(false, |s| s == safe.as_str()))
        });
        let path = match cached {
            Some(p) => p,
            None => {
                let client = self.client().await?;
                let url = OwnedMxcUri::from(mxc);
                if url.validate().is_err() {
                    tracing::info!(%mxc, "emote: not an mxc address");
                    return Ok(());
                }
                // small: it is drawn at the height of a line
                let request = MediaRequestParameters {
                    source: MediaSource::Plain(url),
                    format: MediaFormat::Thumbnail(MediaThumbnailSettings::new(EMOTE_PX.into(), EMOTE_PX.into())),
                };
                // a picture that cannot be had stays its name: logged, no error
                // (a room with one would otherwise complain at every opening)
                let bytes = match client.media().get_media_content(&request, true).await {
                    Ok(b) => b,
                    Err(e) => {
                        tracing::info!(%mxc, "emote: {e}");
                        return Ok(());
                    }
                };
                let ext = sniff_extension(&bytes).unwrap_or("png");
                tokio::fs::create_dir_all(&dir).await?;
                let path = dir.join(format!("{safe}.{ext}"));
                tokio::fs::write(&path, &bytes).await?;
                path
            }
        };
        self.emit(json!({"ev": "emote", "mxc": mxc, "path": path.to_string_lossy()}));
        Ok(())
    }
}

/// The file type from its first bytes, for the types the UI shows or plays.
fn sniff_extension(bytes: &[u8]) -> Option<&'static str> {
    let b = bytes;
    if b.starts_with(&[0xFF, 0xD8, 0xFF]) {
        Some("jpg")
    } else if b.starts_with(&[0x89, b'P', b'N', b'G']) {
        Some("png")
    } else if b.starts_with(b"GIF8") {
        Some("gif")
    } else if b.len() >= 12 && &b[0..4] == b"RIFF" && &b[8..12] == b"WEBP" {
        Some("webp")
    } else if b.len() >= 12 && &b[4..8] == b"ftyp" {
        Some("mp4")
    } else if b.starts_with(&[0x1A, 0x45, 0xDF, 0xA3]) {
        Some("webm")
    } else if b.starts_with(b"OggS") {
        Some("ogg")
    } else {
        None
    }
}
