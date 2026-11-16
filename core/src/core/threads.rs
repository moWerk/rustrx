// SPDX-License-Identifier: GPL-3.0-or-later
//! Threads: replies gathered under a root message. The room's timeline
//! hides them (its root carries a summary: how many, the latest); a
//! thread opened shows them in a timeline of its own, where sending goes
//! into the thread by itself (the SDK adds the relation). One thread is
//! open at a time, beside the open room. (part of `impl Core`)

use super::*;
use matrix_sdk_ui::timeline::thread_list_service::{ThreadListItemEvent, ThreadListPaginationState, ThreadListService};

fn list_event_json(e: &ThreadListItemEvent) -> Value {
    let name = profile_name(&e.sender_profile, &e.sender);
    let body = e.content.as_ref().and_then(|c| c.as_message()).map(|m| m.body().to_owned());
    json!({
        "event_id": e.event_id.as_str(), "sender": e.sender.as_str(), "sender_name": name,
        "is_own": e.is_own, "ts": u64::from(e.timestamp.0), "body": body,
    })
}

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
        let seq = self.state.lock().await.thread_seq;
        let timeline = Self::build_timeline(&room, TimelineFocus::Thread { root_event_id: root.clone() }, true).await?;
        let tl = timeline.clone();
        let room_id_owned = room.room_id().to_owned();
        let room_id_kept = room_id_owned.clone();
        let root_owned = root.clone();
        let task = self.rt.spawn(async move {
            let emit = |items: Value| {
                self.emit(json!({"ev": "thread_timeline", "room_id": room_id_owned.as_str(),
                                 "thread_root": root_owned.as_str(), "items": items}));
            };
            // the whole thread: older replies page in after the first items
            let back = tl.clone();
            let page_in = move || {
                tokio::spawn(async move {
                    let _ = back.paginate_backwards(THREAD_FILL).await;
                });
            };
            Self::stream_timeline(&room, &tl, &own, emit, page_in, crate::instance().threads_read.subscribe()).await;
        });
        tracing::info!(room = %room_id, %root, "thread: open");
        // a close or another open during the build: this one is not wanted
        let mut st = self.state.lock().await;
        if st.thread_seq != seq {
            task.abort();
            return Ok(());
        }
        st.thread_seq += 1;
        if let Some(old) = st.thread.replace(OpenThread { room_id: room_id_kept, root, timeline, task }) {
            old.task_handle().abort();
        }
        Ok(())
    }

    /// The room's threads, newest activity first: "thread_list", live until
    /// close_thread_list.
    pub(crate) async fn thread_list(&'static self, room_id: &str) -> Result<()> {
        let room = self.room(room_id).await?;
        self.close_thread_list().await;
        let seq = self.state.lock().await.thread_list_seq;
        let service = Arc::new(ThreadListService::new(room.clone()));
        let rid = room.room_id().to_owned();
        let own = self.client().await?.user_id().context("no user id")?.to_owned();
        let mut threads_read = self.threads_read.subscribe();
        let task = self.rt.spawn(async move {
            let mut known = HashMap::new();
            let emit = |service: &ThreadListService, loading: bool, known: &mut HashMap<String, bool>| {
                let mut items: Vec<Value> = service
                    .items()
                    .iter()
                    .map(|i| {
                        json!({
                            "root": list_event_json(&i.root_event),
                            "latest": i.latest_event.as_ref().map(list_event_json),
                            "count": i.num_replies,
                        })
                    })
                    .collect();
                // the latest activity first
                let ts = |v: &Value| {
                    v["latest"]["ts"].as_u64().or_else(|| v["root"]["ts"].as_u64()).unwrap_or(0)
                };
                items.sort_by(|a, b| ts(b).cmp(&ts(a)));
                let room = room.clone();
                let own = own.clone();
                let rid = rid.clone();
                let mut known = std::mem::take(known);
                async move {
                    // unread: as for a thread line in the room (timeline.rs)
                    for item in items.iter_mut() {
                        let (Some(root), Some(latest)) = (item["root"]["event_id"].as_str().map(str::to_owned),
                                                         item["latest"]["event_id"].as_str().map(str::to_owned)) else { continue };
                        let key = format!("{root} {latest}");
                        let unread = match known.get(&key) {
                            Some(u) => *u,
                            None => {
                                let u = Self::thread_unread(&room, &own, &root, &latest,
                                                            item["latest"]["ts"].as_u64().unwrap_or(0),
                                                            item["latest"]["is_own"].as_bool().unwrap_or(false)).await;
                                known.insert(key, u);
                                u
                            }
                        };
                        item["unread"] = Value::Bool(unread);
                    }
                    crate::instance().emit(json!({"ev": "thread_list", "room_id": rid.as_str(), "threads": items, "loading": loading}));
                    known
                }
            };
            let (_, mut updates) = service.subscribe_to_items_updates();
            for _ in 0..10 {
                if let ThreadListPaginationState::Idle { end_reached: true } = service.pagination_state() {
                    break;
                }
                if let Err(e) = service.paginate().await {
                    tracing::info!(room = %rid, "threads: list: {e}");
                    break;
                }
                known = emit(&service, true, &mut known).await;
            }
            known = emit(&service, false, &mut known).await;
            tracing::info!(room = %rid, threads = service.items().len(), "threads: list");
            loop {
                tokio::select! {
                    next = updates.next() => if next.is_none() { break },
                    // a thread read here: its receipt is new
                    changed = threads_read.changed() => {
                        if changed.is_err() {
                            break;
                        }
                        known.clear();
                    }
                }
                known = emit(&service, false, &mut known).await;
            }
        });
        let mut st = self.state.lock().await;
        if st.thread_list_seq != seq {
            task.abort();
            return Ok(());
        }
        st.thread_list_seq += 1;
        if let Some(old) = st.thread_list.replace(task) {
            old.abort();
        }
        Ok(())
    }

    pub(crate) async fn close_thread_list(&self) {
        let mut st = self.state.lock().await;
        st.thread_list_seq += 1;
        if let Some(task) = st.thread_list.take() {
            task.abort();
        }
    }

    /// The open thread read up to its newest message (a threaded receipt,
    /// so other clients know too).
    pub(crate) async fn mark_thread_read(&self) -> Result<()> {
        let tl = self.state.lock().await.thread.as_ref().map(|t| t.timeline.clone());
        let Some(tl) = tl else { return Ok(()) };
        // only the root yet: the server takes no threaded receipt on it
        let events = tl.items().await.iter().filter(|i| i.as_event().is_some()).count();
        if events < 2 {
            return Ok(());
        }
        // a receipt is a courtesy to the other clients: refused, it is only logged
        if let Err(e) = tl.mark_as_read(ReceiptType::Read).await {
            tracing::info!("thread: read receipt: {e}");
        }
        // the room's timeline and the thread list show it read
        self.threads_read.send_modify(|n| *n += 1);
        Ok(())
    }

    pub(crate) async fn close_thread(&self) {
        let mut st = self.state.lock().await;
        st.thread_seq += 1;
        if let Some(open) = st.thread.take() {
            open.task.abort();
        }
    }

    /// The timeline to send into: the open thread's when it is this one,
    /// else a thread timeline made for the send.
    pub(crate) async fn thread_timeline(&self, room: &Room, root: &str) -> Result<Arc<Timeline>> {
        let root = EventId::parse(root)?;
        let open = {
            let st = self.state.lock().await;
            st.thread.as_ref().filter(|t| t.room_id == room.room_id() && t.root == root).map(|t| t.timeline.clone())
        };
        if let Some(tl) = open {
            return Ok(tl);
        }
        Self::build_timeline(room, TimelineFocus::Thread { root_event_id: root }, false).await
    }

    /// The timeline to send into: the thread's when a root is given, else
    /// the room's.
    pub(crate) async fn timeline_target(&self, room: &Room, thread_root: Option<&str>) -> Result<Arc<Timeline>> {
        match thread_root {
            Some(root) => self.thread_timeline(room, root).await,
            None => self.timeline_for(room).await,
        }
    }

    /// The timeline that holds an event: the open thread's when it is there,
    /// else the room's. A local echo (no event id) is looked up in the
    /// room's timeline: the thread's echoes are discarded there as well.
    pub(crate) async fn timeline_holding(&self, room: &Room, event_id: Option<&str>) -> Result<Arc<Timeline>> {
        let thread = {
            let st = self.state.lock().await;
            st.thread
                .as_ref()
                .filter(|t| t.room_id == room.room_id())
                .map(|t| (t.timeline.clone(), event_id == Some(t.root.as_str())))
        };
        let event = match event_id.filter(|s| !s.is_empty()).map(EventId::parse) {
            Some(Ok(e)) => Some(e),
            _ => None,
        };
        let Some((tl, is_root)) = thread else { return self.timeline_for(room).await };
        let Some(event) = event else { return self.timeline_for(room).await };
        // the thread's root is the room's when the room has it loaded: the
        // thread's timeline does not see all of the root's reactions, and a
        // reaction toggled there was sent a second time (refused by the
        // server, which then held back everything after it in the room)
        if is_root {
            let room_tl = self.timeline_for(room).await?;
            if room_tl.item_by_event_id(&event).await.is_some() {
                return Ok(room_tl);
            }
            return Ok(tl);
        }
        if tl.item_by_event_id(&event).await.is_some() {
            return Ok(tl);
        }
        self.timeline_for(room).await
    }
}
