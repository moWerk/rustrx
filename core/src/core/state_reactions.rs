// SPDX-License-Identifier: GPL-3.0-or-later
//! Reactions to state rows (a join, a profile change, any other state), as
//! nheko shows them. Matrix allows an annotation on any event, but the SDK's
//! timeline keeps reactions only for messages (`TimelineItemContent::
//! reactions()` is None for state), so the open room counts them here: the
//! state events in the loaded timeline get their annotations fetched once,
//! the sync adds new ones and takes redacted ones away, and the timeline is
//! sent again when that changes anything.
//! The counts, and the reply originals fetched for the room's timeline,
//! are kept for the last KEPT_ROOMS rooms opened: a room opened again
//! shows them at once and asks nothing. Fetched longer than KEPT_FRESH
//! ago (nothing watches a closed room), they show while the counts are
//! fetched again and replaced. (part of `impl Core`)

use super::*;
use matrix_sdk::event_handler::EventHandlerDropGuard;
use matrix_sdk::room::{IncludeRelations, RelationsOptions};
use matrix_sdk::ruma::events::relation::RelationType;
use matrix_sdk::ruma::events::AnySyncTimelineEvent;
use matrix_sdk::ruma::serde::Raw;
use matrix_sdk_ui::timeline::TimelineItemContent;
use std::collections::HashSet;

/// How many annotations one state event gets fetched with: one page. A wave
/// on a join gets a few; a row with more shows the first ones.
const STATE_REACTIONS_FETCH: u32 = 100;
/// Fetches running at once: paging back through a room full of joins would
/// otherwise ask the server for all of them in the same moment.
const STATE_REACTIONS_PARALLEL: usize = 4;
/// Rooms whose counts and reply originals stay after their close.
const KEPT_ROOMS: usize = 10;
/// How long after their fetch those are taken as they are at an open;
/// later, the counts are fetched again and the reply originals asked again.
pub(crate) const KEPT_FRESH: std::time::Duration = std::time::Duration::from_secs(600);
/// Reply originals kept per room, the oldest fetched going first.
const KEPT_REPLIES: usize = 200;

/// One m.reaction (m.annotation), read from an event's JSON.
#[derive(Clone, Debug, PartialEq)]
pub(crate) struct Reaction {
    pub(crate) id: String,
    pub(crate) sender: String,
    pub(crate) target: String,
    pub(crate) key: String,
}

/// The reaction an event is, if it is one. A redacted reaction has lost its
/// relation and is none.
pub(crate) fn reaction_of(ev: &Value) -> Option<Reaction> {
    if ev["type"].as_str() != Some("m.reaction") || !ev["unsigned"]["redacted_because"].is_null() {
        return None;
    }
    let rel = &ev["content"]["m.relates_to"];
    if rel["rel_type"].as_str() != Some("m.annotation") {
        return None;
    }
    let text = |v: &Value| v.as_str().filter(|s| !s.is_empty()).map(str::to_owned);
    Some(Reaction {
        id: text(&ev["event_id"])?,
        sender: text(&ev["sender"])?,
        target: text(&rel["event_id"])?,
        key: text(&rel["key"])?,
    })
}

/// The event an m.room.redaction removes: in its content since room
/// version 11, at the top before.
pub(crate) fn redacted_by(ev: &Value) -> Option<String> {
    if ev["type"].as_str() != Some("m.room.redaction") {
        return None;
    }
    ev["content"]["redacts"].as_str().or_else(|| ev["redacts"].as_str()).map(str::to_owned)
}

/// The reactions on the state events of one open room.
#[derive(Default)]
pub(crate) struct StateReactions {
    /// target -> key -> (sender, reaction id), keys in the order they came
    targets: HashMap<String, Vec<(String, Vec<(String, String)>)>>,
    /// reactions redacted while the room was open: a fetch answered late
    /// does not bring them back
    redacted: HashSet<String>,
    /// "target key" of our own reactions being sent or redacted: a second
    /// tap meanwhile would send the same annotation twice
    busy: HashSet<String>,
    /// tracked targets to fetch again (kept past KEPT_FRESH): their counts
    /// show meanwhile, and the answer replaces them
    refetch: HashSet<String>,
    /// targets whose fetch failed: asked again at the next open, not on
    /// every pass (a server answering 429 would get them all again)
    failed: HashSet<String>,
}

impl StateReactions {
    /// The state event is counted from now on; true when it is new here.
    pub(crate) fn track(&mut self, target: &str) -> bool {
        if self.targets.contains_key(target) {
            return false;
        }
        self.targets.insert(target.to_owned(), Vec::new());
        true
    }

    pub(crate) fn tracks(&self, target: &str) -> bool {
        self.targets.contains_key(target)
    }

    /// Whether the target's annotations are to be fetched now: new here,
    /// or kept too long. `Some(true)`: the answer replaces its counts.
    pub(crate) fn to_fetch(&mut self, target: &str) -> Option<bool> {
        if self.track(target) {
            return Some(false);
        }
        self.refetch.remove(target).then_some(true)
    }

    /// A fetch failed: asked again at the next open (the counts are kept
    /// now, so a failure would last).
    pub(crate) fn fetch_failed(&mut self, target: &str) {
        self.failed.insert(target.to_owned());
    }

    /// An open: the failed targets are fetched again when next seen.
    pub(crate) fn reopened(&mut self) {
        self.refetch.extend(self.failed.drain());
    }

    /// Every tracked target is fetched again when next seen.
    pub(crate) fn stale(&mut self) {
        self.refetch = self.targets.keys().cloned().collect();
        self.failed.clear();
    }

    /// The fetched annotations of a target in place of its counts; true
    /// when that changed them.
    pub(crate) fn replace(&mut self, target: &str, found: &[Reaction]) -> bool {
        let Some(before) = self.targets.get(target).cloned() else { return false };
        self.targets.insert(target.to_owned(), Vec::new());
        for r in found {
            self.add(r);
        }
        self.targets.get(target) != Some(&before)
    }

    /// True when it changed the counts. Reactions to untracked events
    /// (messages: the SDK counts those) are left alone, and each sender
    /// counts once per key, as the SDK counts.
    pub(crate) fn add(&mut self, r: &Reaction) -> bool {
        if self.redacted.contains(&r.id) {
            return false;
        }
        let Some(keys) = self.targets.get_mut(&r.target) else { return false };
        let pos = match keys.iter().position(|(k, _)| *k == r.key) {
            Some(p) => p,
            None => {
                keys.push((r.key.clone(), Vec::new()));
                keys.len() - 1
            }
        };
        let senders = &mut keys[pos].1;
        if senders.iter().any(|(s, id)| *s == r.sender || *id == r.id) {
            return false;
        }
        senders.push((r.sender.clone(), r.id.clone()));
        true
    }

    /// A redacted reaction goes; true when it was counted.
    pub(crate) fn remove(&mut self, reaction_id: &str) -> bool {
        self.redacted.insert(reaction_id.to_owned());
        for keys in self.targets.values_mut() {
            for i in 0..keys.len() {
                let before = keys[i].1.len();
                keys[i].1.retain(|(_, id)| id != reaction_id);
                if keys[i].1.len() != before {
                    if keys[i].1.is_empty() {
                        keys.remove(i);
                    }
                    return true;
                }
            }
        }
        false
    }

    /// The id of our own reaction with this key, for taking it back.
    pub(crate) fn own(&self, target: &str, key: &str, own: &str) -> Option<String> {
        let keys = self.targets.get(target)?;
        let (_, senders) = keys.iter().find(|(k, _)| k == key)?;
        senders.iter().find(|(s, _)| s == own).map(|(_, id)| id.clone())
    }

    /// `reactions` as messages have it (`{key, count, me}`), None when none.
    pub(crate) fn json(&self, target: &str, own: &str) -> Option<Value> {
        let keys = self.targets.get(target)?;
        let list: Vec<Value> = keys
            .iter()
            .filter(|(_, senders)| !senders.is_empty())
            .map(|(key, senders)| json!({"key": key, "count": senders.len(), "me": senders.iter().any(|(s, _)| s == own)}))
            .collect();
        (!list.is_empty()).then(|| Value::Array(list))
    }

    /// False when this toggle is already on its way.
    pub(crate) fn start(&mut self, target: &str, key: &str) -> bool {
        self.busy.insert(format!("{target} {key}"))
    }

    pub(crate) fn done(&mut self, target: &str, key: &str) {
        self.busy.remove(&format!("{target} {key}"));
    }
}

/// The originals of replies whose original is not loaded, as `reply_to`
/// had them once fetched (serialize.rs), by the original's event id. Only
/// in memory; the SDK's own details win whenever it has them.
#[derive(Default)]
pub(crate) struct Replies {
    found: HashMap<String, (Value, std::time::Instant)>,
}

impl Replies {
    /// A fetched original (`reply_to` with its sender), kept.
    pub(crate) fn put(&mut self, reply: &Value) {
        let Some(id) = reply["event_id"].as_str() else { return };
        if reply["sender"].is_null() {
            return;
        }
        if self.found.get(id).map_or(false, |(v, _)| v == reply) {
            return;
        }
        if self.found.len() >= KEPT_REPLIES && !self.found.contains_key(id) {
            if let Some(oldest) = self.found.iter().min_by_key(|(_, (_, at))| *at).map(|(k, _)| k.clone()) {
                self.found.remove(&oldest);
            }
        }
        self.found.insert(id.to_owned(), (reply.clone(), std::time::Instant::now()));
    }

    /// The kept original, however old.
    pub(crate) fn get(&self, event_id: &str) -> Option<&Value> {
        self.found.get(event_id).map(|(v, _)| v)
    }

    /// Kept and fresh: not asked again.
    pub(crate) fn fresh(&self, event_id: &str) -> bool {
        self.found.get(event_id).map_or(false, |(_, at)| at.elapsed() < KEPT_FRESH)
    }
}

/// A room's counts, kept beside its timeline (see the top), with a signal
/// to send the timeline again; and for the open room's timeline stream
/// the reply originals and the UI's ask for everything again
/// (`timeline_resync`).
pub(crate) struct SharedStateReactions {
    pub(crate) counts: std::sync::Mutex<StateReactions>,
    changed: tokio::sync::watch::Sender<u64>,
    fetches: Arc<tokio::sync::Semaphore>,
    pub(crate) replies: Arc<std::sync::Mutex<Replies>>,
    pub(crate) resync: Arc<tokio::sync::Notify>,
    /// when the counts were last fetched all (made, or gone stale)
    fetched: std::sync::Mutex<std::time::Instant>,
}

type KeptRooms = Vec<(String, OwnedRoomId, Arc<SharedStateReactions>)>;

/// The kept rooms, the latest opened first, by own user id and room.
static KEPT: std::sync::Mutex<KeptRooms> = std::sync::Mutex::new(Vec::new());

fn kept_rooms() -> std::sync::MutexGuard<'static, KeptRooms> {
    KEPT.lock().unwrap_or_else(|e| e.into_inner())
}

impl SharedStateReactions {
    pub(crate) fn new() -> Arc<Self> {
        Arc::new(SharedStateReactions {
            counts: std::sync::Mutex::new(StateReactions::default()),
            changed: tokio::sync::watch::channel(0).0,
            fetches: Arc::new(tokio::sync::Semaphore::new(STATE_REACTIONS_PARALLEL)),
            replies: Arc::default(),
            resync: Arc::default(),
            fetched: std::sync::Mutex::new(std::time::Instant::now()),
        })
    }

    /// The room's kept counts and replies for an open, else new ones.
    /// Fetched longer than KEPT_FRESH ago: the counts are fetched again.
    pub(crate) fn kept(own: &UserId, room_id: &RoomId) -> Arc<Self> {
        let mut kept = kept_rooms();
        let found = kept.iter().position(|(u, r, _)| u == own.as_str() && r == room_id);
        let shared = match found {
            Some(i) => kept.remove(i).2,
            None => Self::new(),
        };
        kept.insert(0, (own.to_string(), room_id.to_owned(), shared.clone()));
        kept.truncate(KEPT_ROOMS);
        drop(kept);
        let mut fetched = shared.fetched.lock().unwrap_or_else(|e| e.into_inner());
        if fetched.elapsed() >= KEPT_FRESH {
            *fetched = std::time::Instant::now();
            shared.lock().stale();
        } else {
            shared.lock().reopened();
        }
        drop(fetched);
        shared
    }

    /// The room's kept reply originals (a thread of an open room), without
    /// counting as an open.
    pub(crate) fn kept_replies(own: &UserId, room_id: &RoomId) -> Arc<std::sync::Mutex<Replies>> {
        let kept = kept_rooms();
        match kept.iter().find(|(u, r, _)| u == own.as_str() && r == room_id) {
            Some((_, _, s)) => s.replies.clone(),
            None => Arc::default(),
        }
    }

    /// An account's kept rooms go (logout).
    pub(crate) fn forget_account(own: &UserId) {
        kept_rooms().retain(|(u, _, _)| u != own.as_str());
    }

    pub(crate) fn subscribe(&self) -> tokio::sync::watch::Receiver<u64> {
        self.changed.subscribe()
    }

    pub(crate) fn lock(&self) -> std::sync::MutexGuard<'_, StateReactions> {
        self.counts.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn bump(&self) {
        self.changed.send_modify(|n| *n = n.wrapping_add(1));
    }

    fn add(&self, r: &Reaction) {
        if self.lock().add(r) {
            self.bump();
        }
    }

    fn remove(&self, reaction_id: &str) {
        if self.lock().remove(reaction_id) {
            self.bump();
        }
    }

    pub(crate) fn tracks(&self, target: &str) -> bool {
        self.lock().tracks(target)
    }

}

impl StateReactions {
    /// A state row's `reactions` (its timeline JSON given), None for others.
    pub(crate) fn of_row(&self, item: &Value, own: &str) -> Option<Value> {
        if !matches!(item["type"].as_str(), Some("membership") | Some("state")) {
            return None;
        }
        self.json(item["event_id"].as_str()?, own)
    }
}

impl Core {
    /// The state events among the items not asked about yet get their
    /// annotations fetched, each once while the room is open.
    pub(crate) fn fetch_state_reactions<'a>(
        room: &Room,
        shared: &Arc<SharedStateReactions>,
        items: impl IntoIterator<Item = &'a Arc<TimelineItem>>,
    ) {
        for item in items {
            let Some(event) = item.as_event() else { continue };
            if !matches!(
                event.content(),
                TimelineItemContent::MembershipChange(_) | TimelineItemContent::ProfileChange(_) | TimelineItemContent::OtherState(_)
            ) {
                continue;
            }
            let Some(id) = event.event_id().map(ToOwned::to_owned) else { continue };
            let Some(replace) = shared.lock().to_fetch(id.as_str()) else { continue };
            let (room, shared) = (room.clone(), shared.clone());
            tokio::spawn(async move {
                let Ok(_permit) = shared.fetches.clone().acquire_owned().await else { return };
                // by relation type only: in an encrypted room the server
                // sees m.room.encrypted, not m.reaction
                let opts = RelationsOptions {
                    include_relations: IncludeRelations::RelationsOfType(RelationType::Annotation),
                    limit: Some(UInt::from(STATE_REACTIONS_FETCH)),
                    ..Default::default()
                };
                match room.relations(id.clone(), opts).await {
                    Ok(found) => {
                        let found: Vec<Reaction> = found.chunk.iter().filter_map(|ev| reaction_of(&raw_value(ev.raw()))).collect();
                        if replace {
                            if shared.lock().replace(id.as_str(), &found) {
                                shared.bump();
                            }
                        } else {
                            for r in &found {
                                shared.add(r);
                            }
                        }
                    }
                    Err(e) => {
                        shared.lock().fetch_failed(id.as_str());
                        tracing::info!(event = %id, "state reactions: not fetched: {e}");
                    }
                }
            });
        }
    }

    /// New reactions to the tracked state events, and redactions, from the
    /// sync (decrypted where the room is encrypted), while the guard lives.
    pub(crate) fn watch_state_reactions(room: &Room, shared: &Arc<SharedStateReactions>) -> EventHandlerDropGuard {
        let shared = shared.clone();
        let handle = room.add_event_handler(move |ev: Raw<AnySyncTimelineEvent>| {
            let shared = shared.clone();
            async move {
                let v = raw_value(&ev);
                if let Some(r) = reaction_of(&v) {
                    shared.add(&r);
                } else if let Some(id) = redacted_by(&v) {
                    shared.remove(&id);
                }
            }
        });
        room.client().event_handler_drop_guard(handle)
    }

    /// The open room's counts, when the room is open.
    pub(crate) async fn open_state_reactions(&self, room: &Room) -> Option<Arc<SharedStateReactions>> {
        let st = self.state.lock().await;
        st.open.as_ref().filter(|o| o.room_id == room.room_id()).map(|o| o.state_reactions.clone())
    }

    /// Our reaction with this key on a state event: taken back (redacted)
    /// when there is one, else sent. Straight to the room, not through the
    /// timeline: it does not know reactions on state, so it never took one
    /// back and a second tap sent the same annotation again. The send's
    /// answer gives the reaction's id at once, so a tap right after takes
    /// it back.
    pub(crate) async fn toggle_state_reaction(room: &Room, shared: &Arc<SharedStateReactions>, target: &EventId, key: &str) -> Result<()> {
        use matrix_sdk::ruma::events::{reaction::ReactionEventContent, relation::Annotation};
        let own = room.own_user_id().to_string();
        let tid = target.as_str();
        if !shared.lock().start(tid, key) {
            tracing::info!(event = %target, "state reaction: already on its way");
            return Ok(());
        }
        let existing = shared.lock().own(tid, key, &own);
        let result = async {
            match existing {
                Some(id) => {
                    room.redact(&EventId::parse(&id)?, None, None).await?;
                    shared.remove(&id);
                }
                None => {
                    let content = ReactionEventContent::new(Annotation::new(target.to_owned(), key.to_owned()));
                    let sent = room.send(content).await?;
                    shared.add(&Reaction { id: sent.response.event_id.to_string(), sender: own.clone(),
                                           target: tid.to_owned(), key: key.to_owned() });
                }
            }
            Ok::<(), anyhow::Error>(())
        }
        .await;
        shared.lock().done(tid, key);
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reaction(id: &str, sender: &str, target: &str, key: &str) -> Value {
        json!({"type": "m.reaction", "event_id": id, "sender": sender,
               "content": {"m.relates_to": {"rel_type": "m.annotation", "event_id": target, "key": key}}})
    }

    fn counts() -> StateReactions {
        let mut c = StateReactions::default();
        assert!(c.track("$join"));
        assert!(!c.track("$join"));
        c
    }

    /// A reaction's fields from its JSON; other events and a redacted
    /// reaction are none.
    /// Planted-bug: drop the redacted_because check (a removed wave counts).
    #[test]
    fn reaction_is_read_from_the_event() {
        let r = reaction_of(&reaction("$r1", "@a:x", "$join", "👋")).unwrap();
        assert_eq!(r, Reaction { id: "$r1".into(), sender: "@a:x".into(), target: "$join".into(), key: "👋".into() });
        let mut redacted = reaction("$r1", "@a:x", "$join", "👋");
        redacted["unsigned"] = json!({"redacted_because": {"type": "m.room.redaction"}});
        assert_eq!(reaction_of(&redacted), None);
        let reply = json!({"type": "m.room.message", "event_id": "$m", "sender": "@a:x",
                           "content": {"m.relates_to": {"rel_type": "m.annotation", "event_id": "$join", "key": "x"}}});
        assert_eq!(reaction_of(&reply), None);
        let thread = json!({"type": "m.reaction", "event_id": "$t", "sender": "@a:x",
                            "content": {"m.relates_to": {"rel_type": "m.thread", "event_id": "$join"}}});
        assert_eq!(reaction_of(&thread), None);
    }

    /// The redacted event's id, from the content (v11) or the top.
    #[test]
    fn redaction_names_its_event() {
        assert_eq!(redacted_by(&json!({"type": "m.room.redaction", "content": {"redacts": "$r1"}})), Some("$r1".into()));
        assert_eq!(redacted_by(&json!({"type": "m.room.redaction", "redacts": "$r2", "content": {}})), Some("$r2".into()));
        assert_eq!(redacted_by(&json!({"type": "m.reaction", "redacts": "$r2"})), None);
    }

    /// Counted per key and sender, "me" for our own, keys in the order they came.
    /// Planted-bug: count every event instead of each sender once (the
    /// sync's copy of a fetched reaction counts twice).
    #[test]
    fn reactions_are_counted_per_key() {
        let mut c = counts();
        for (id, who, key) in [("$1", "@a:x", "👋"), ("$2", "@me:x", "👋"), ("$3", "@b:x", "🎉"), ("$1", "@a:x", "👋")] {
            c.add(&reaction_of(&reaction(id, who, "$join", key)).unwrap());
        }
        assert_eq!(c.json("$join", "@me:x"), Some(json!([
            {"key": "👋", "count": 2, "me": true},
            {"key": "🎉", "count": 1, "me": false},
        ])));
        assert_eq!(c.json("$join", "@b:x").unwrap()[1]["me"], json!(true));
        assert_eq!(c.own("$join", "👋", "@me:x"), Some("$2".into()));
        assert_eq!(c.own("$join", "🎉", "@me:x"), None);
    }

    /// Reactions to events not tracked (messages) are not counted here.
    #[test]
    fn untracked_targets_are_left_alone() {
        let mut c = counts();
        assert!(!c.add(&reaction_of(&reaction("$1", "@a:x", "$message", "👍")).unwrap()));
        assert_eq!(c.json("$message", "@a:x"), None);
        assert_eq!(c.json("$join", "@a:x"), None);
    }

    /// A redacted reaction disappears, its key too when it was the last,
    /// and a fetch answered after the redaction does not bring it back.
    /// Planted-bug: forget the redacted ids (the late fetch restores it).
    #[test]
    fn redacted_reactions_disappear() {
        let mut c = counts();
        let wave = reaction_of(&reaction("$1", "@a:x", "$join", "👋")).unwrap();
        c.add(&wave);
        c.add(&reaction_of(&reaction("$2", "@b:x", "$join", "🎉")).unwrap());
        assert!(c.remove("$1"));
        assert!(!c.remove("$1"));
        assert_eq!(c.json("$join", "@a:x"), Some(json!([{"key": "🎉", "count": 1, "me": false}])));
        assert!(!c.add(&wave));
        assert!(c.remove("$2"));
        assert_eq!(c.json("$join", "@a:x"), None);
    }

    /// The state rows of the timeline JSON get the counts, messages not.
    #[test]
    fn state_rows_get_their_reactions() {
        let shared = SharedStateReactions::new();
        shared.lock().track("$join");
        shared.lock().track("$msg");
        shared.add(&reaction_of(&reaction("$1", "@a:x", "$join", "👋")).unwrap());
        shared.add(&reaction_of(&reaction("$2", "@a:x", "$msg", "👋")).unwrap());
        let items = json!([
            {"kind": "event", "type": "membership", "event_id": "$join"},
            {"kind": "event", "type": "text", "event_id": "$msg"},
            {"kind": "date"},
        ]);
        let counts = shared.lock();
        assert_eq!(counts.of_row(&items[0], "@a:x"), Some(json!([{"key": "👋", "count": 1, "me": true}])));
        assert_eq!(counts.of_row(&items[1], "@a:x"), None);
        assert_eq!(counts.of_row(&items[2], "@a:x"), None);
    }

    /// A kept room's targets are not fetched again until they go stale;
    /// then the answer replaces the counts (a wave taken back while the
    /// room was closed goes); a failed fetch is asked again at the next
    /// open, not on every pass.
    /// Planted-bug: put a failed target back at once (fetched on every pass).
    /// Planted-bug: `add` instead of `replace` for a stale target (the
    /// taken-back wave stays).
    #[test]
    fn kept_counts_are_fetched_again_once_stale() {
        let mut c = StateReactions::default();
        assert_eq!(c.to_fetch("$join"), Some(false));
        assert_eq!(c.to_fetch("$join"), None);
        let wave = reaction_of(&reaction("$1", "@a:x", "$join", "👋")).unwrap();
        let party = reaction_of(&reaction("$2", "@b:x", "$join", "🎉")).unwrap();
        c.add(&wave);
        c.add(&party);
        c.stale();
        assert_eq!(c.to_fetch("$join"), Some(true));
        assert_eq!(c.to_fetch("$join"), None);
        assert!(c.replace("$join", &[party.clone()]));
        assert_eq!(c.json("$join", "@a:x"), Some(json!([{"key": "🎉", "count": 1, "me": false}])));
        assert!(!c.replace("$join", &[party]));
        // failed: not asked again in this open, at the next one
        assert_eq!(c.to_fetch("$new"), Some(false));
        c.fetch_failed("$new");
        assert_eq!(c.to_fetch("$new"), None);
        c.reopened();
        assert_eq!(c.to_fetch("$new"), Some(true));
        assert_eq!(c.to_fetch("$new"), None);
    }

    /// Reply originals: kept by id, fresh for a while, the oldest going
    /// when full; a quote without its sender (not fetched) is not kept.
    #[test]
    fn reply_originals_are_kept() {
        let mut r = Replies::default();
        r.put(&json!({"event_id": "$o", "pending": true}));
        assert!(r.get("$o").is_none());
        r.put(&json!({"event_id": "$o", "sender": "@a:x", "body": "hi"}));
        assert_eq!(r.get("$o").unwrap()["body"], json!("hi"));
        assert!(r.fresh("$o"));
        for i in 0..KEPT_REPLIES {
            r.put(&json!({"event_id": format!("$n{i}"), "sender": "@a:x"}));
        }
        assert_eq!(r.found.len(), KEPT_REPLIES);
        assert!(r.get("$o").is_none());
    }

    /// The kept rooms: the same entry for the same account and room, not
    /// another account's; the oldest go past KEPT_ROOMS.
    #[test]
    fn rooms_are_kept_per_account() {
        use matrix_sdk::ruma::{room_id, user_id};
        let (me, other) = (user_id!("@kept-me:x"), user_id!("@kept-other:x"));
        let a = SharedStateReactions::kept(me, room_id!("!a:x"));
        assert!(Arc::ptr_eq(&a, &SharedStateReactions::kept(me, room_id!("!a:x"))));
        assert!(!Arc::ptr_eq(&a, &SharedStateReactions::kept(other, room_id!("!a:x"))));
        assert!(Arc::ptr_eq(&a.replies, &SharedStateReactions::kept_replies(me, room_id!("!a:x"))));
        SharedStateReactions::forget_account(me);
        assert!(!Arc::ptr_eq(&a, &SharedStateReactions::kept(me, room_id!("!a:x"))));
        SharedStateReactions::forget_account(me);
        SharedStateReactions::forget_account(other);
    }

    /// A second toggle of the same key waits for the first.
    #[test]
    fn a_toggle_on_its_way_blocks_the_same_key() {
        let mut c = counts();
        assert!(c.start("$join", "👋"));
        assert!(!c.start("$join", "👋"));
        assert!(c.start("$join", "🎉"));
        c.done("$join", "👋");
        assert!(c.start("$join", "👋"));
    }
}
