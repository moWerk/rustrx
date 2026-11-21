// SPDX-License-Identifier: GPL-3.0-or-later
//! The open room: its timeline, sending, receipts, typing, media.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;
use matrix_sdk::room::Receipts;
use matrix_sdk::send_queue::LocalEchoContent;

impl Core {
    /// A reaction the server refuses for good (sent twice, say) stays in its
    /// room's send queue, and the SDK holds back everything sent to that room
    /// after it, across restarts, to keep the order. Such reactions are
    /// dropped: at the start and whenever a send is refused. A refused
    /// message stays, shown as not sent, for the user to retry or delete.
    ///
    /// Any send error also switches the room's queue off, and only
    /// set_enabled switches it on again: after a network error everything
    /// sent to that room stayed "sending" until a restart. Such rooms are
    /// switched on again after SEND_RETRY_DELAY, and all of them when the
    /// sync is running again (sync.rs).
    pub(crate) async fn send_queue_watch(client: Client) {
        use tokio::sync::broadcast::error::RecvError;
        let mut errors = client.send_queue().subscribe_errors();
        Self::drop_refused_reactions(&client).await;
        let mut asleep: HashMap<OwnedRoomId, tokio::time::Instant> = HashMap::new();
        loop {
            let next = asleep.values().min().copied();
            tokio::select! {
                e = errors.recv() => match e {
                    Ok(e) if !e.is_recoverable => Self::drop_refused_reactions(&client).await,
                    Ok(e) => {
                        tracing::info!(room_id = %e.room_id, "send queue: off after a network error, on again later");
                        asleep.entry(e.room_id).or_insert_with(|| tokio::time::Instant::now() + SEND_RETRY_DELAY);
                    }
                    Err(RecvError::Lagged(_)) => {
                        Self::drop_refused_reactions(&client).await;
                        client.send_queue().set_enabled(true).await;
                    }
                    Err(RecvError::Closed) => break,
                },
                _ = tokio::time::sleep_until(next.unwrap_or_else(tokio::time::Instant::now)), if next.is_some() => {
                    let now = tokio::time::Instant::now();
                    asleep.retain(|room_id, at| {
                        if *at > now {
                            return true;
                        }
                        if let Some(room) = client.get_room(room_id) {
                            room.send_queue().set_enabled(true);
                        }
                        false
                    });
                }
            }
        }
    }

    /// A message the server did not take, sent again as it was (its reply,
    /// formatting, picture and all): the echo is unwedged and its room's
    /// queue switched on. "Send again" used to discard it and send the
    /// text anew, into the same switched-off queue.
    pub(crate) async fn retry_send(&self, room_id: &str, txn_id: &str) -> Result<()> {
        let client = self.client().await?;
        let room = self.room(room_id).await?;
        let echoes = client.send_queue().local_echoes().await?;
        let echo = echoes
            .get(room.room_id())
            .and_then(|list| list.iter().find(|e| e.transaction_id.as_str() == txn_id))
            .ok_or_else(|| anyhow!("no unsent message {txn_id}"))?;
        match &echo.content {
            LocalEchoContent::Event { send_handle, .. } => send_handle.unwedge().await?,
            _ => bail!("only a message is sent again"),
        }
        room.send_queue().set_enabled(true);
        Ok(())
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
        mut threads_read: tokio::sync::watch::Receiver<u64>,
    ) {
        let mut names = HashMap::new();
        let mut replies_asked = std::collections::HashSet::new();
        let mut unread = HashMap::new();
        let (mut items, mut stream) = timeline.subscribe().await;
        let mut started = Some(started);
        loop {
            Self::fetch_replied(timeline, &items, &mut replies_asked);
            Self::receipt_names(room, &items, &mut names).await;
            let mut json = items_json(&items, own, &names);
            // the message our own read receipt is on (the SDK keeps our
            // receipts off the items): the room's unread count runs from
            // there, where the unread line (m.fully_read) can lag far behind
            let own_read = timeline.latest_user_read_receipt_timeline_event_id(own).await;
            if let Value::Array(list) = &mut json {
                for item in list.iter_mut() {
                    if own_read.as_ref().is_some_and(|id| item["event_id"].as_str() == Some(id.as_str())) {
                        item["own_read"] = Value::Bool(true);
                    }
                    let root = item["event_id"].as_str().map(str::to_owned);
                    if let (Some(root), Some(thread)) = (root, item.get_mut("thread")) {
                        Self::mark_thread_unread(room, own, &root, thread, &mut unread).await;
                    }
                }
            }
            emit(json);
            if let Some(started) = started.take() {
                started();
            }
            tokio::select! {
                next = stream.next() => {
                    if next.is_none() {
                        break;
                    }
                }
                // a thread read here: its receipt is new, nothing else
                changed = threads_read.changed() => {
                    if changed.is_err() {
                        std::future::pending::<()>().await;
                    }
                    unread.clear();
                }
            }
            tokio::time::sleep(TIMELINE_COALESCE).await;
            while let Some(Some(_)) = stream.next().now_or_never() {}
            items = timeline.items().await;
        }
    }

    /// Puts "unread" into a thread summary (serialize.rs): its latest reply
    /// is someone else's and no read receipt of ours, threaded or not,
    /// reaches it. The receipts are the server's, so a thread read in
    /// another client counts as read here too. Remembered per latest reply.
    pub(crate) async fn mark_thread_unread(room: &Room, own: &UserId, root: &str, thread: &mut Value,
                                           known: &mut HashMap<String, bool>) {
        let Some(latest) = thread["latest_id"].as_str().map(str::to_owned) else { return };
        let unread = Self::cached_thread_unread(room, own, root, &latest,
                                                thread["latest_ts"].as_u64().unwrap_or(0),
                                                thread["latest_own"].as_bool().unwrap_or(false), known).await;
        thread["unread"] = Value::Bool(unread);
    }

    /// `thread_unread`, remembered per thread and latest reply in `known`
    /// (the room's thread lines and the thread list each keep their own).
    pub(crate) async fn cached_thread_unread(room: &Room, own: &UserId, root: &str, latest: &str, latest_ts: u64,
                                             latest_own: bool, known: &mut HashMap<String, bool>) -> bool {
        let key = format!("{root} {latest}");
        if let Some(u) = known.get(&key) {
            return *u;
        }
        let u = Self::thread_unread(room, own, root, latest, latest_ts, latest_own).await;
        known.insert(key, u);
        u
    }

    pub(crate) async fn thread_unread(room: &Room, own: &UserId, root: &str, latest: &str, latest_ts: u64, latest_own: bool) -> bool {
        if latest_own {
            return false;
        }
        let Ok(root) = EventId::parse(root) else { return false };
        use matrix_sdk::ruma::events::receipt::ReceiptType as Kind;
        for kind in [Kind::Read, Kind::ReadPrivate] {
            for thread in [ReceiptThread::Thread(root.clone()), ReceiptThread::Unthreaded] {
                if let Ok(Some((id, receipt))) = room.load_user_receipt(kind.clone(), &thread, own).await {
                    if id.as_str() == latest || receipt.ts.map_or(false, |t| u64::from(t.0) >= latest_ts) {
                        return false;
                    }
                }
            }
        }
        true
    }

    /// Timeline snapshots for the open room, and who is typing there (the
    /// own user left out). Ends when the timeline's stream does.
    pub(crate) async fn timeline_task(&'static self, room: Room, timeline: Arc<Timeline>, own: OwnedUserId) {
        let room_id = room.room_id().to_owned();
        let emit = |items: Value| self.emit(json!({"ev": "timeline", "room_id": room_id.as_str(), "items": items}));
        let items = Self::stream_timeline(&room, &timeline, &own, emit, || {}, self.threads_read.subscribe());
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
        let seq = {
            let mut st = self.state.lock().await;
            st.open_seq += 1;
            st.open_seq
        };
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        // the room's draft, from the encrypted store: "draft"
        match room.load_composer_draft(None).await {
            Ok(draft) => self.emit(json!({"ev": "draft", "room_id": room.room_id().as_str(),
                                          "text": draft.map(|d| d.plain_text).unwrap_or_default()})),
            Err(e) => tracing::warn!("draft: not loaded: {e}"),
        }
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
        let mut st = self.state.lock().await;
        if st.open_seq != seq {
            // a close or another open came while this one was building
            return Ok(());
        }
        let task = self.rt.spawn(self.timeline_task(room.clone(), timeline.clone(), own));
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

    /// The text being written in a room, kept in the SDK's store, which is
    /// encrypted where the store is; empty text removes it.
    pub(crate) async fn save_draft(&self, room_id: &str, text: &str, thread_root: Option<&str>) -> Result<()> {
        let room = self.room(room_id).await?;
        let root = thread_root.map(EventId::parse).transpose()?;
        let root = root.as_deref();
        if text.trim().is_empty() {
            room.clear_composer_draft(root).await?;
        } else {
            let draft = matrix_sdk::ComposerDraft {
                plain_text: text.to_owned(),
                html_text: None,
                draft_type: matrix_sdk::ComposerDraftType::NewMessage,
                attachments: Vec::new(),
            };
            room.save_composer_draft(draft, root).await?;
        }
        Ok(())
    }

    pub(crate) async fn close_room(&self) {
        let open = {
            let mut st = self.state.lock().await;
            st.open_seq += 1;
            st.open.take()
        };
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
        emote: bool,
        mentions: &[crate::protocol::MentionArg],
    ) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_target(&room, thread_root).await?;
        let content = message_content(body, markdown, emote, emotes, mentions);
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
    /// a video its size, duration and a frame as thumbnail, a voice message
    /// its duration.
    #[allow(clippy::too_many_arguments)]
    pub(crate) async fn send_file(
        &self,
        room_id: &str,
        path: String,
        mime: Option<&str>,
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
        // the UI's type (from the content) first: a shared photo whose name had
        // no known extension went out as an unknown file, not as a picture
        let mime = mime
            .and_then(|m| m.parse::<mime::Mime>().ok())
            .filter(|m| *m != mime::APPLICATION_OCTET_STREAM)
            .unwrap_or_else(|| mime_guess::from_path(&path).first_or_octet_stream());
        let tl = self.timeline_target(&room, thread_root).await?;
        let dim = |n: Option<u64>| n.and_then(|n| UInt::try_from(n).ok());
        let mut config = AttachmentConfig::default();
        // a file the app may not read (a memory card outside its sandbox)
        // or an empty one goes out as nothing anyone can open: said here
        let len = async { tokio::fs::File::open(&path).await?.metadata().await }
            .await
            .with_context(|| format!("cannot read {}", path.display()))?
            .len();
        if len == 0 {
            bail!("{} is empty", path.display());
        }
        let size = UInt::try_from(len).ok();
        if mime.type_() == mime::VIDEO {
            // other clients size the player and show the length before loading
            config.info = Some(AttachmentInfo::Video(BaseVideoInfo {
                width: dim(w),
                height: dim(h),
                duration: duration_ms.map(Duration::from_millis),
                size,
                ..Default::default()
            }));
        } else if let (Some(w), Some(h)) = (dim(w), dim(h)) {
            config.info = Some(AttachmentInfo::Image(BaseImageInfo {
                width: Some(w),
                height: Some(h),
                size,
                ..Default::default()
            }));
        }
        if let (Some(tp), Some(w), Some(h)) = (thumb_path, dim(thumb_w), dim(thumb_h)) {
            if let Ok(data) = tokio::fs::read(&tp).await {
                let size = UInt::try_from(data.len() as u64).unwrap_or_default();
                config.thumbnail = Some(Thumbnail {
                    data,
                    content_type: mime::IMAGE_JPEG,
                    width: w,
                    height: h,
                    size,
                });
            }
            let _ = tokio::fs::remove_file(&tp).await;
        }
        if let Some(caption) = caption {
            config.caption = Some(TextMessageEventContent::markdown(caption));
        }
        // a voice message: other clients show it as one (a player, not a file)
        if voice {
            let duration = duration_ms.map(Duration::from_millis);
            config.info = Some(AttachmentInfo::Voice(BaseAudioInfo { duration, size, ..Default::default() }));
        }
        // through the send queue: the message shows at once as a local
        // echo with its upload progress, instead of only once uploaded
        tl.send_attachment(path, mime, config).use_send_queue().await?;
        Ok(())
    }

    pub(crate) async fn react(&self, room_id: &str, event_id: &str, key: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let event = EventId::parse(event_id)?;
        let id = TimelineEventItemId::EventId(event.clone());
        let toggled = match self.timeline_holding(&room, Some(event_id)).await {
            Ok(tl) => tl.toggle_reaction(&id, key).await.map_err(|e| anyhow!("{e}")),
            Err(e) => Err(e),
        };
        if let Err(e) = toggled {
            // not in a timeline here: a notification's reaction, perhaps in
            // the process woken for a push, which syncs nothing. Sent as it
            // is, through the room's send queue.
            tracing::info!("react: {e}; sent without the timeline");
            use matrix_sdk::ruma::events::{reaction::ReactionEventContent, relation::Annotation};
            let content = ReactionEventContent::new(Annotation::new(event, key.to_owned()));
            room.send_queue().send(content.into()).await?;
        }
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
        let echo = given(&txn_id).is_some();
        tl.redact(&Self::item_id(event_id, txn_id)?, reason).await?;
        // a discarded echo may be the one its room's queue stopped at
        if echo {
            room.send_queue().set_enabled(true);
        }
        Ok(())
    }

    /// A message changed. Made as a new message is (an edited "||secret||"
    /// lost its spoiler and other clients showed it in the clear; pills
    /// and custom emoji fell back to plain text): with the custom emoji
    /// and people the UI names, and those of the message as it was whose
    /// shortcode or name is still in the text.
    pub(crate) async fn edit(&self, room_id: &str, event_id: Option<String>, txn_id: Option<String>, body: &str,
                             emotes: Option<&HashMap<String, String>>, mentions: &[crate::protocol::MentionArg]) -> Result<()> {
        let room = self.room(room_id).await?;
        let tl = self.timeline_holding(&room, event_id.as_deref()).await?;
        let id = Self::item_id(event_id, txn_id)?;
        let old_html = match &id {
            TimelineEventItemId::EventId(e) => tl.item_by_event_id(e).await.and_then(|item| {
                match item.content().as_message().map(|m| m.msgtype()) {
                    Some(MessageType::Text(t)) => t.formatted.as_ref().map(|f| f.body.clone()),
                    Some(MessageType::Emote(t)) => t.formatted.as_ref().map(|f| f.body.clone()),
                    _ => None,
                }
            }),
            TimelineEventItemId::TransactionId(_) => None,
        };
        let content: RoomMessageEventContentWithoutRelation = edit_content(body, old_html.as_deref(), emotes, mentions).into();
        tl.edit(&id, EditedContent::RoomMessage(content)).await?;
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

    pub(crate) async fn mark_read(&self, room_id: &str, private: bool, marker: bool, event_id: Option<&str>) -> Result<()> {
        let room = self.room(room_id).await?;
        // read up to one message, the one the reader saw last
        if let Some(event_id) = event_id {
            let receipts = read_receipts(EventId::parse(event_id)?, private, marker);
            // the open room's timeline refuses a receipt older than ours;
            // a flush can arrive after the room was closed (commands run
            // side by side): then straight to the room, never the whole
            // room's newest, which would mark unseen messages read
            let tl = {
                let st = self.state.lock().await;
                st.open.as_ref().filter(|o| o.room_id == room.room_id()).map(|o| o.timeline.clone())
            };
            match tl {
                Some(tl) => tl.send_multiple_receipts(receipts).await?,
                // the timeline's order check, by the events' times: a late
                // flush must not move the receipt back past what was read
                // since (in another client, or in this room reopened)
                None => {
                    let target = EventId::parse(event_id)?;
                    if receipt_moves_back(Self::own_receipt_ts(&room).await, Self::event_ts(&room, &target).await) {
                        tracing::info!(room = %room.room_id(), "mark_read: older than the receipt there, not sent");
                        return Ok(());
                    }
                    room.send_multiple_receipts(receipts).await?
                }
            }
            return Ok(());
        }
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
        // Without `marker` (read on opening) it stays where the messages
        // were last shown, so the unread line is still there to read from.
        if is_open {
            let tl = self.timeline_for(&room).await?;
            tl.mark_as_read(kind).await?;
            if marker {
                tl.mark_as_read(ReceiptType::FullyRead).await?;
            }
        } else if let Some(event_id) = crate::serialize::latest_event_id(&room) {
            room.send_single_receipt(kind, ReceiptThread::Unthreaded, event_id.clone()).await?;
            if marker {
                room.send_single_receipt(ReceiptType::FullyRead, ReceiptThread::Unthreaded, event_id).await?;
            }
        }
        let _ = room.set_unread_flag(false).await;
        Ok(())
    }

    /// When an event was sent (ms), from the event cache or the server.
    async fn event_ts(room: &Room, event_id: &EventId) -> Option<u64> {
        let config = matrix_sdk::config::RequestConfig::short_retry();
        let event = room.load_or_fetch_event(event_id, Some(config)).await.ok()?;
        event.timestamp().map(|t| u64::from(t.0))
    }

    /// When the event under our newest read receipt (public or private,
    /// not in a thread) was sent; None when there is none or it is unknown.
    async fn own_receipt_ts(room: &Room) -> Option<u64> {
        use matrix_sdk::ruma::events::receipt::ReceiptType as Kind;
        let own = room.own_user_id().to_owned();
        let mut newest = None;
        for kind in [Kind::Read, Kind::ReadPrivate] {
            if let Ok(Some((id, _))) = room.load_user_receipt(kind, &ReceiptThread::Unthreaded, &own).await {
                newest = newest.max(Self::event_ts(room, &id).await);
            }
        }
        newest
    }

    /// Online or offline for others; also the state every later sync
    /// request carries (the SDK's default was online, around the clock
    /// with the stand-in syncing every five minutes)
    pub(crate) async fn set_presence(&self, online: bool) -> Result<()> {
        use matrix_sdk::ruma::presence::PresenceState;
        let client = self.client().await?;
        let state = if online { PresenceState::Online } else { PresenceState::Offline };
        client.set_presence(state, None, true).await?;
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
        let generation = self.generation().await;
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
                // Downloads is open to other apps: a link put there in the
                // folder's place would lead the copy into Rustrx's own files
                if tokio::fs::symlink_metadata(&dir).await?.file_type().is_symlink() {
                    bail!("{} is a link, not a folder", dir.display());
                }
                let mut path = dir.join(&original);
                if path.exists() {
                    path = dir.join(format!("{safe}-{original}"));
                }
                path
            }
            None => self.data_dir.join(MEDIA_DIR).join(format!("{safe}{}.{ext}", if thumb { "-thumb" } else { "" })),
        };
        self.same_login(generation).await?;
        if exported {
            // a new file only: never over one, never through a link left there
            use tokio::io::AsyncWriteExt;
            let mut f = tokio::fs::OpenOptions::new().write(true).create_new(true).open(&path).await?;
            f.write_all(&bytes).await?;
            f.flush().await?;
        } else {
            tokio::fs::write(&path, &bytes).await?;
        }
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
        // the cached file has one of the extensions sniff_extension gives:
        // looked up by name, without listing the folder per emoji
        let mut cached = None;
        for ext in MEDIA_EXTENSIONS {
            let p = dir.join(format!("{safe}.{ext}"));
            if tokio::fs::try_exists(&p).await.unwrap_or(false) {
                cached = Some(p);
                break;
            }
        }
        let path = match cached {
            Some(p) => p,
            None => {
                let generation = self.generation().await;
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
                self.same_login(generation).await?;
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

/// Every extension sniff_extension gives (a custom emoji without a known
/// type is saved as png).
const MEDIA_EXTENSIONS: [&str; 7] = ["png", "jpg", "gif", "webp", "mp4", "webm", "ogg"];

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

/// A message as the composer's text makes it: markdown or not, an emote
/// (/me) or not, its custom emoji, pills and spoilers.
fn message_content(body: &str, markdown: bool, emote: bool, emotes: Option<&HashMap<String, String>>,
                   mentions: &[crate::protocol::MentionArg]) -> RoomMessageEventContent {
    let mut content = match (emote, markdown) {
        (true, true) => RoomMessageEventContent::emote_markdown(body),
        (true, false) => RoomMessageEventContent::emote_plain(body),
        (false, true) => RoomMessageEventContent::text_markdown(body),
        (false, false) => RoomMessageEventContent::text_plain(body),
    };
    if let Some(emotes) = emotes {
        packs::with_emotes(&mut content, emotes);
    }
    with_mentions(&mut content, body, mentions);
    if markdown {
        with_spoilers(&mut content, body);
    }
    content
}

/// An edit's new content: as a new message, with the custom emoji and
/// people given, plus those of the message as it was (`old_html`) whose
/// shortcode or name the new text still has. The edit field picks no
/// people, so without this every pill of the message was lost.
fn edit_content(body: &str, old_html: Option<&str>, emotes: Option<&HashMap<String, String>>,
                mentions: &[crate::protocol::MentionArg]) -> RoomMessageEventContent {
    let mut all_emotes: HashMap<String, String> = old_html.map(emotes_in_html).unwrap_or_default();
    if let Some(given) = emotes {
        all_emotes.extend(given.iter().map(|(k, v)| (k.clone(), v.clone())));
    }
    let mut all_mentions = mentions.to_vec();
    for pill in old_html.map(pills_in_html).unwrap_or_default() {
        if body.contains(&pill.name) && !all_mentions.iter().any(|m| m.user_id == pill.user_id) {
            all_mentions.push(pill);
        }
    }
    message_content(body, true, false, Some(&all_emotes), &all_mentions)
}

/// An attribute's value in one HTML tag (double or single quotes).
fn html_attr<'a>(tag: &'a str, name: &str) -> Option<&'a str> {
    for quote in ['"', '\''] {
        let key = format!("{name}={quote}");
        if let Some(start) = tag.find(&key).map(|i| i + key.len()) {
            return tag[start..].find(quote).map(|end| &tag[start..start + end]);
        }
    }
    None
}

/// Text from HTML: the entities escape_html writes, and the apostrophe.
fn unescape_html(text: &str) -> String {
    text.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&#39;", "'").replace("&amp;", "&")
}

/// The custom emoji in a message's HTML, shortcode to mxc address
/// (`<img data-mx-emoticon src="mxc://..." alt=":code:">`).
fn emotes_in_html(html: &str) -> HashMap<String, String> {
    let mut out = HashMap::new();
    for part in html.split("<img").skip(1) {
        let tag = &part[..part.find('>').unwrap_or(part.len())];
        if !tag.contains("data-mx-emoticon") {
            continue;
        }
        let (Some(src), Some(alt)) = (html_attr(tag, "src"), html_attr(tag, "alt")) else { continue };
        let code = unescape_html(alt.trim().trim_matches(':'));
        if src.starts_with("mxc://") && !code.is_empty() {
            out.insert(code, src.to_owned());
        }
    }
    out
}

/// The people a message's HTML links as pills (`<a
/// href="https://matrix.to/#/@user:server">name</a>`), with their names.
fn pills_in_html(html: &str) -> Vec<crate::protocol::MentionArg> {
    let mut out = Vec::new();
    for part in html.split("<a ").skip(1) {
        let Some(close) = part.find('>') else { continue };
        let Some(href) = html_attr(&part[..close], "href") else { continue };
        let Some(target) = href.strip_prefix("https://matrix.to/#/") else { continue };
        let target = target.split('?').next().unwrap_or("").replace("%40", "@").replace("%3A", ":").replace("%3a", ":");
        let Ok(user) = UserId::parse(target.as_str()) else { continue };
        let Some(end) = part[close + 1..].find("</a>") else { continue };
        let name = unescape_html(&part[close + 1..close + 1 + end]);
        if !name.trim().is_empty() && !name.contains('<') {
            out.push(crate::protocol::MentionArg { user_id: user.to_string(), name });
        }
    }
    out
}

/// The people picked while typing: the first place each one's name is
/// written becomes a pill (a matrix.to link, as Element writes them), and
/// the message says whom it mentions, "@room" the whole room (m.mentions:
/// their clients notify on it). The plain body keeps the names.
fn with_mentions(content: &mut RoomMessageEventContent, body: &str, mentions: &[crate::protocol::MentionArg]) {
    use matrix_sdk::ruma::events::room::message::FormattedBody;
    use matrix_sdk::ruma::events::Mentions;
    let room = body.contains("@room");
    if mentions.is_empty() && !room {
        return;
    }
    let escape = crate::serialize::escape_html;
    let mut ids = Vec::new();
    let link = |formatted: &mut Option<FormattedBody>, ids: &mut Vec<OwnedUserId>| {
        let mut html = formatted.take().map(|f| f.body).unwrap_or_else(|| escape(body).replace('\n', "<br>"));
        for m in mentions {
            let Ok(user) = UserId::parse(m.user_id.trim()) else { continue };
            let name = escape(m.name.trim());
            if !name.is_empty() && html.contains(&name) {
                let pill = format!("<a href=\"https://matrix.to/#/{}\">{}</a>", user, name);
                html = html.replacen(&name, &pill, 1);
            }
            ids.push(user);
        }
        *formatted = Some(FormattedBody::html(html));
    };
    match &mut content.msgtype {
        MessageType::Text(t) => link(&mut t.formatted, &mut ids),
        MessageType::Emote(t) => link(&mut t.formatted, &mut ids),
        _ => {}
    }
    let mut m = Mentions::with_user_ids(ids);
    m.room = room;
    content.mentions = Some(m);
}

/// Text between two "||" in one paragraph (line breaks allowed) becomes a spoiler in the HTML
/// (not inside code); the plain body keeps the "||" as it was typed.
fn with_spoilers(content: &mut RoomMessageEventContent, body: &str) {
    use matrix_sdk::ruma::events::room::message::FormattedBody;
    if !body.contains("||") {
        return;
    }
    let escape = crate::serialize::escape_html;
    let mark =|formatted: &mut Option<FormattedBody>| {
        let html = formatted.as_ref().map(|f| f.body.clone()).unwrap_or_else(|| escape(body).replace('\n', "<br>"));
        if let Some(out) = spoilers_in_html(&html) {
            *formatted = Some(FormattedBody::html(out));
        }
    };
    match &mut content.msgtype {
        MessageType::Text(t) => mark(&mut t.formatted),
        MessageType::Emote(t) => mark(&mut t.formatted),
        _ => {}
    }
}

/// The HTML with each pair of "||" in the text made a spoiler span, or
/// None when there is no pair. A pair must be in one paragraph (no block
/// tag between; line breaks are fine) and hold something; code is left alone.
fn spoilers_in_html(html: &str) -> Option<String> {
    const BLOCK: [&str; 12] = ["p", "li", "ul", "ol", "blockquote", "h1", "h2", "h3", "h4", "h5", "h6", "hr"];
    // the "||" outside code, each with the paragraph it is in
    let mut marks: Vec<(usize, usize)> = Vec::new();
    let (mut para, mut code, mut i) = (0usize, 0usize, 0usize);
    // bytes only: every index kept is an ASCII "<", ">" or "|", so the
    // slices below always fall on a character boundary (string slicing
    // at byte steps panicked on "ä" or an emoji)
    let b = html.as_bytes();
    while i < b.len() {
        if b[i] == b'<' {
            let end = b[i..].iter().position(|&c| c == b'>').map_or(b.len(), |e| i + e + 1);
            let closing = b.get(i + 1) == Some(&b'/');
            let start = if closing { i + 2 } else { i + 1 };
            let name: String = b[start.min(b.len())..]
                .iter()
                .take_while(|c| c.is_ascii_alphanumeric())
                .map(|&c| (c as char).to_ascii_lowercase())
                .collect();
            if name == "code" || name == "pre" {
                if closing { code = code.saturating_sub(1) } else { code += 1 }
            }
            if BLOCK.contains(&name.as_str()) || name == "pre" {
                para += 1;
            }
            i = end;
        } else if code == 0 && b[i..].starts_with(b"||") {
            marks.push((i, para));
            i += 2;
        } else {
            i += 1;
        }
    }
    let mut pairs = Vec::new();
    let mut k = 0;
    while k + 1 < marks.len() {
        let (a, pa) = marks[k];
        let (z, pz) = marks[k + 1];
        if pa == pz && z > a + 2 {
            pairs.push((a, z));
            k += 2;
        } else {
            k += 1;
        }
    }
    if pairs.is_empty() {
        return None;
    }
    let mut out = String::with_capacity(html.len() + pairs.len() * 32);
    let mut at = 0;
    for (a, z) in pairs {
        out.push_str(&html[at..a]);
        out.push_str("<span data-mx-spoiler>");
        out.push_str(&html[a + 2..z]);
        out.push_str("</span>");
        at = z + 2;
    }
    out.push_str(&html[at..]);
    Some(out)
}

/// Whether a receipt on an event sent at `target` would move ours (on an
/// event sent at `current`) back. Only when both are known: a receipt that
/// cannot be placed goes out, as it did before.
fn receipt_moves_back(current: Option<u64>, target: Option<u64>) -> bool {
    matches!((current, target), (Some(c), Some(t)) if t < c)
}

/// The receipts for "read up to this message": the read receipt, private
/// with "Send read receipts" off, and with `marker` the unread line too
fn read_receipts(event_id: OwnedEventId, private: bool, marker: bool) -> Receipts {
    let read = Some(event_id.clone());
    let receipts = if private {
        Receipts::new().private_read_receipt(read)
    } else {
        Receipts::new().public_read_receipt(read)
    };
    receipts.fully_read_marker(marker.then_some(event_id))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::protocol::MentionArg;

    fn mention(user_id: &str, name: &str) -> MentionArg {
        MentionArg { user_id: user_id.to_owned(), name: name.to_owned() }
    }

    fn html(c: &RoomMessageEventContent) -> String {
        match &c.msgtype {
            MessageType::Text(t) => t.formatted.as_ref().map(|f| f.body.clone()).unwrap_or_default(),
            MessageType::Emote(t) => t.formatted.as_ref().map(|f| f.body.clone()).unwrap_or_default(),
            _ => String::new(),
        }
    }

    /// The first place a picked name is written becomes a pill, the plain
    /// body keeps the name, the message mentions the person.
    /// Planted-bug: skip the replacement (`html.contains` never true).
    #[test]
    fn a_picked_name_becomes_a_pill() {
        let body = "hi Ann, and Ann again";
        let mut c = RoomMessageEventContent::text_markdown(body);
        with_mentions(&mut c, body, &[mention("@ann:example.org", "Ann")]);
        assert_eq!(html(&c), "hi <a href=\"https://matrix.to/#/@ann:example.org\">Ann</a>, and Ann again");
        assert_eq!(c.body(), body);
        let m = c.mentions.expect("mentions");
        assert!(m.user_ids.contains(&UserId::parse("@ann:example.org").unwrap()));
        assert!(!m.room);
    }

    /// "||" pairs become spoilers, in plain text and around markdown, one
    /// paragraph at a time, never in code; the body stays as typed.
    /// Planted-bug: pair across paragraphs (the second test's span opens
    /// in one <p> and closes in the next).
    #[test]
    fn spoilers() {
        let sp = |s: &str| spoilers_in_html(s);
        assert_eq!(sp("a ||b|| c").as_deref(), Some("a <span data-mx-spoiler>b</span> c"));
        assert_eq!(sp("<p>||a</p>\n<p>b||</p>"), None);
        assert_eq!(sp("||<strong>x</strong>|| and ||y||").as_deref(),
                   Some("<span data-mx-spoiler><strong>x</strong></span> and <span data-mx-spoiler>y</span>"));
        assert_eq!(sp("<code>||x||</code>"), None);
        assert_eq!(sp("a |||| b"), None);
        assert_eq!(sp("odd ||one|| ||"), Some("odd <span data-mx-spoiler>one</span> ||".to_string()));
        assert_eq!(sp("no pair || here"), None);
        // not ASCII, and broken tags: no panic, the right pairs
        // Planted-bug: slice the str at byte steps (panics on "ä").
        assert_eq!(sp("||x|| ä").as_deref(), Some("<span data-mx-spoiler>x</span> ä"));
        assert_eq!(sp("ä ||ü😀|| 😀").as_deref(), Some("ä <span data-mx-spoiler>ü😀</span> 😀"));
        assert_eq!(sp("<p>||a||</p><").as_deref(), Some("<p><span data-mx-spoiler>a</span></p><"));
        assert_eq!(sp("||a|| <ä"), Some("<span data-mx-spoiler>a</span> <ä".to_string()));
        assert_eq!(sp("<ä||a||"), None);
        assert_eq!(sp("||My first<br>Multiline<br />Spoiler||").as_deref(),
                   Some("<span data-mx-spoiler>My first<br>Multiline<br />Spoiler</span>"));

        let body = "look ||it was him||";
        let mut c = RoomMessageEventContent::text_markdown(body);
        with_spoilers(&mut c, body);
        assert_eq!(html(&c), "look <span data-mx-spoiler>it was him</span>");
        assert_eq!(c.body(), body);
        let body = "**loud** ||quiet||";
        let mut c = RoomMessageEventContent::text_markdown(body);
        with_spoilers(&mut c, body);
        assert!(html(&c).contains("<strong>loud</strong> <span data-mx-spoiler>quiet</span>"), "{}", html(&c));
        // as typed in the screenshot: three lines, one spoiler
        let body = "||My first\nMultilone\nSpoiler||";
        let mut c = RoomMessageEventContent::text_markdown(body);
        with_spoilers(&mut c, body);
        assert_eq!(html(&c), "<span data-mx-spoiler>My first<br>Multilone<br>Spoiler</span>");
    }

    /// Markdown keeps its formatting around the pill; new lines in a plain
    /// message become <br>. Planted-bug: build the HTML from the body
    /// always (the bold goes).
    #[test]
    fn formatting_stays() {
        let body = "**look** Bob";
        let mut c = RoomMessageEventContent::text_markdown(body);
        with_mentions(&mut c, body, &[mention("@bob:b.org", "Bob")]);
        assert!(html(&c).contains("<strong>look</strong>"), "{}", html(&c));
        assert!(html(&c).contains("<a href=\"https://matrix.to/#/@bob:b.org\">Bob</a>"));
        let body = "line one\nBob";
        let mut c = RoomMessageEventContent::text_plain(body);
        with_mentions(&mut c, body, &[mention("@bob:b.org", "Bob")]);
        assert_eq!(html(&c), "line one<br><a href=\"https://matrix.to/#/@bob:b.org\">Bob</a>");
    }

    /// A name with markup in it is escaped in the pill, an id that is no
    /// user id is left out, a name not in the text still mentions, "@room"
    /// mentions the room, and nothing picked and no @room changes nothing.
    /// Planted-bug: no escaping of the name.
    #[test]
    fn edges() {
        let body = "x <b>ad</b> y";
        let mut c = RoomMessageEventContent::text_plain(body);
        with_mentions(&mut c, body, &[mention("@a:b.org", "<b>ad</b>"), mention("not an id", "y")]);
        assert_eq!(html(&c), "x <a href=\"https://matrix.to/#/@a:b.org\">&lt;b&gt;ad&lt;/b&gt;</a> y");
        assert_eq!(c.mentions.as_ref().unwrap().user_ids.len(), 1);

        let body = "no name here";
        let mut c = RoomMessageEventContent::text_plain(body);
        with_mentions(&mut c, body, &[mention("@a:b.org", "Zed")]);
        assert_eq!(c.mentions.as_ref().unwrap().user_ids.len(), 1);

        let body = "@room look";
        let mut c = RoomMessageEventContent::text_plain(body);
        with_mentions(&mut c, body, &[]);
        assert!(c.mentions.as_ref().unwrap().room);

        let body = "plain";
        let mut c = RoomMessageEventContent::text_plain(body);
        with_mentions(&mut c, body, &[]);
        assert!(c.mentions.is_none());
        assert_eq!(html(&c), "");
    }

    /// An emote gets its pills too. Planted-bug: handle Text only.
    #[test]
    fn emotes_too() {
        let body = "waves at Ann";
        let mut c = RoomMessageEventContent::emote_plain(body);
        with_mentions(&mut c, body, &[mention("@ann:a.org", "Ann")]);
        assert!(html(&c).contains("matrix.to/#/@ann:a.org"));
    }

    /// One receipt, of the kind asked for; the unread line only with
    /// `marker`. Planted-bug: send the public receipt with receipts off.
    #[test]
    fn read_receipts_kinds() {
        let id = OwnedEventId::try_from("$a:x.org").unwrap();
        let r = read_receipts(id.clone(), true, false);
        assert_eq!(r.private_read_receipt.as_ref(), Some(&id));
        assert!(r.public_read_receipt.is_none() && r.fully_read.is_none());
        let r = read_receipts(id.clone(), false, true);
        assert_eq!(r.public_read_receipt.as_ref(), Some(&id));
        assert!(r.private_read_receipt.is_none());
        assert_eq!(r.fully_read.as_ref(), Some(&id));
    }

    /// An edit keeps the message's pills and custom emoji where the new
    /// text still has the name or shortcode, drops those it no longer has,
    /// and adds the ones the UI gives, as a new message would.
    /// Planted-bugs: skip the old pills in edit_content (`for pill in
    /// Vec::new()`): Ann is no longer linked or mentioned; skip the old
    /// emoji (`HashMap::new()` for all_emotes): :wave: stays text.
    #[test]
    fn an_edit_keeps_pills_and_custom_emoji() {
        let old = "hi <a href=\"https://matrix.to/#/@ann:a.org\">Ann</a> and \
                   <a href='https://matrix.to/#/%40bob%3Ab.org?via=b.org'>Bob &amp; co</a> \
                   <img data-mx-emoticon src=\"mxc://a.org/w\" alt=\":wave:\" title=\":wave:\" height=\"32\">";
        assert_eq!(emotes_in_html(old).get("wave").map(String::as_str), Some("mxc://a.org/w"));
        let pills = pills_in_html(old);
        assert_eq!(pills.iter().map(|p| (p.user_id.as_str(), p.name.as_str())).collect::<Vec<_>>(),
                   vec![("@ann:a.org", "Ann"), ("@bob:b.org", "Bob & co")]);

        let given: HashMap<String, String> = [("cat".to_owned(), "mxc://a.org/c".to_owned())].into();
        let c = edit_content("hi Ann :wave: :cat:", Some(old), Some(&given), &[]);
        let h = html(&c);
        assert!(h.contains("<a href=\"https://matrix.to/#/@ann:a.org\">Ann</a>"), "{h}");
        assert!(h.contains("src=\"mxc://a.org/w\""), "{h}");
        assert!(h.contains("src=\"mxc://a.org/c\""), "{h}");
        assert!(!h.contains("@bob:b.org"), "{h}");
        let m = c.mentions.expect("mentions");
        assert!(m.user_ids.contains(&UserId::parse("@ann:a.org").unwrap()));
        assert!(!m.user_ids.contains(&UserId::parse("@bob:b.org").unwrap()), "Bob is no longer in the text");
        // a message without HTML of its own is edited as a new one
        let plain = edit_content("plain ||x||", None, None, &[]);
        assert!(html(&plain).contains("data-mx-spoiler"));
        assert!(plain.mentions.is_none());
    }

    /// A receipt for a closed room is held back only when it would move
    /// ours back to an older message; one that cannot be placed goes out.
    /// Planted-bug: compare the other way (`t > c`): every newer receipt
    /// is held back and an older one moves ours back.
    #[test]
    fn closed_room_receipts_never_go_back() {
        assert!(receipt_moves_back(Some(2_000), Some(1_000)));
        assert!(!receipt_moves_back(Some(1_000), Some(2_000)));
        assert!(!receipt_moves_back(Some(1_000), Some(1_000)));
        assert!(!receipt_moves_back(None, Some(1_000)));
        assert!(!receipt_moves_back(Some(1_000), None));
        assert!(!receipt_moves_back(None, None));
    }
}
