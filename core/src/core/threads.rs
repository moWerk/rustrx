//! Threads: replies gathered under a root message. The room's timeline
//! hides them (its root carries a summary: how many, the latest); a
//! thread opened shows them in a timeline of its own, where sending goes
//! into the thread by itself (the SDK adds the relation). One thread is
//! open at a time, beside the open room. (part of `impl Core`)

use super::*;
use matrix_sdk_ui::timeline::TimelineFocus;

pub(crate) struct OpenThread {
    pub(crate) room_id: OwnedRoomId,
    pub(crate) root: OwnedEventId,
    pub(crate) timeline: Arc<Timeline>,
    task: JoinHandle<()>,
}

impl OpenThread {
    pub(crate) fn task_handle(self) -> JoinHandle<()> {
        self.task
    }
}

impl Core {
    /// A thread's timeline, sent as "thread_timeline" (the root first, then
    /// the replies) on every change, until close_thread.
    pub(crate) async fn open_thread(&'static self, room_id: &str, root: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        let client = self.client().await?;
        let own = client.user_id().context("no user id")?.to_owned();
        let root = EventId::parse(root)?;
        self.close_thread().await;
        let timeline = Arc::new(
            room.timeline_builder()
                .with_focus(TimelineFocus::Thread { root_event_id: root.clone() })
                .track_read_marker_and_receipts(TimelineReadReceiptTracking::MessageLikeEvents)
                .build()
                .await?,
        );
        let tl = timeline.clone();
        let room_id_owned = room.room_id().to_owned();
        let room_id_kept = room_id_owned.clone();
        let root_owned = root.clone();
        let task = self.rt.spawn(async move {
            let mut names = HashMap::new();
            let (items, mut stream) = tl.subscribe().await;
            let items: Vec<Arc<TimelineItem>> = items.into_iter().collect();
            Self::receipt_names(&room, &items, &mut names).await;
            let emit = |items: Value| {
                crate::instance().emit(json!({"ev": "thread_timeline", "room_id": room_id_owned.as_str(),
                                              "root": root_owned.as_str(), "items": items}));
            };
            emit(items_json(&items, &own, &names));
            // the whole thread: older replies page in
            let back = tl.clone();
            tokio::spawn(async move {
                let _ = back.paginate_backwards(50).await;
            });
            while stream.next().await.is_some() {
                tokio::time::sleep(Duration::from_millis(60)).await;
                while let Some(Some(_)) = stream.next().now_or_never() {}
                let items = tl.items().await;
                Self::receipt_names(&room, &items, &mut names).await;
                emit(items_json(&items, &own, &names));
            }
        });
        tracing::info!(room = %room_id, %root, "thread: open");
        self.state.lock().await.thread = Some(OpenThread { room_id: room_id_kept, root, timeline, task });
        Ok(())
    }

    pub(crate) async fn close_thread(&self) {
        if let Some(open) = self.state.lock().await.thread.take() {
            open.task.abort();
        }
    }

    /// The timeline to send into: the open thread's when it is this one,
    /// else a thread timeline made for the send.
    pub(crate) async fn thread_timeline(&self, room: &Room, root: &str) -> Result<Arc<Timeline>> {
        let root = EventId::parse(root)?;
        if let Some(open) = &self.state.lock().await.thread {
            if open.room_id == room.room_id() && open.root == root {
                return Ok(open.timeline.clone());
            }
        }
        Ok(Arc::new(
            room.timeline_builder().with_focus(TimelineFocus::Thread { root_event_id: root }).build().await?,
        ))
    }

    /// The timeline that holds an event (or a local echo): the open thread's
    /// when it is there, else the room's.
    pub(crate) async fn timeline_holding(
        &self,
        room: &Room,
        event_id: Option<&str>,
        txn_id: Option<&str>,
    ) -> Result<Arc<Timeline>> {
        let thread = {
            let st = self.state.lock().await;
            st.thread.as_ref().filter(|t| t.room_id == room.room_id()).map(|t| t.timeline.clone())
        };
        if let Some(tl) = thread {
            // a local echo (txn_id) is looked up in the room's timeline: the
            // thread's echoes are discarded there as well
            let _ = txn_id;
            let found = match event_id.filter(|s| !s.is_empty()).map(EventId::parse) {
                Some(Ok(e)) => tl.item_by_event_id(&e).await.is_some(),
                _ => false,
            };
            if found {
                return Ok(tl);
            }
        }
        self.timeline_for(room).await
    }
}
