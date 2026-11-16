// SPDX-License-Identifier: GPL-3.0-or-later
//! Requests to join (knocking): a room or space whose join rule is "knock"
//! lets people ask to come in; whoever may invite there answers them.
//! (part of `impl Core`; spaces are not listed as rooms, their requests
//! are answered all the same)

use super::*;
use matrix_sdk::ruma::events::room::join_rules::JoinRule;

impl Core {
    /// Watches the requests of every joined room and space that takes them
    /// and where we may invite; called with every room list update, so a
    /// room that starts taking requests is picked up. Each watched room
    /// reports its open requests as "knock_requests" (also when none are
    /// left).
    pub(super) async fn watch_knocks(
        &'static self,
        client: &Client,
        only: Option<&std::collections::HashSet<OwnedRoomId>>,
        seen: &mut super::sync::ListSeen,
    ) {
        for room in client.rooms() {
            if only.is_some_and(|o| !o.contains(room.room_id())) {
                continue;
            }
            if room.state() != RoomState::Joined {
                continue;
            }
            if !matches!(room.join_rule(), Some(JoinRule::Knock) | Some(JoinRule::KnockRestricted(_))) {
                continue;
            }
            let can_invite = match room.power_levels().await {
                Ok(levels) => levels.user_can_invite(room.own_user_id()),
                Err(_) => false,
            };
            if !can_invite {
                continue;
            }
            let id = room.room_id().to_owned();
            if !seen.knocks_watched.insert(id.clone()) {
                continue;
            }
            let (stream, cleanup) = match room.subscribe_to_knock_requests().await {
                Ok(s) => s,
                Err(e) => {
                    tracing::info!(room = %id, "knock: cannot watch: {e}");
                    seen.knocks_watched.remove(&id);
                    continue;
                }
            };
            tracing::info!(room = %id, space = room.is_space(), "knock: watching requests");
            let task = self.rt.spawn(async move {
                let mut stream = std::pin::pin!(stream);
                while let Some(requests) = stream.next().await {
                    let name = room_name(&room);
                    let list: Vec<Value> = requests
                        .iter()
                        .map(|r| {
                            json!({
                                "event_id": r.event_id.as_str(),
                                "user_id": r.member_info.user_id.as_str(),
                                "name": r.member_info.display_name.clone()
                                    .unwrap_or_else(|| r.member_info.user_id.localpart().to_owned()),
                                "reason": r.member_info.reason,
                                "ts": r.timestamp.map(u64::from),
                                "seen": r.is_seen,
                            })
                        })
                        .collect();
                    tracing::info!(room = %room.room_id(), open = list.len(), "knock: requests");
                    crate::instance().emit(json!({
                        "ev": "knock_requests", "room_id": room.room_id().as_str(), "room_name": name,
                        "is_space": room.is_space(), "requests": list,
                    }));
                }
            });
            let mut st = self.state.lock().await;
            st.login_tasks.push(task);
            st.login_tasks.push(cleanup);
        }
    }

    /// Answers a request: accept invites them, decline removes the request
    /// (a kick), ban also keeps them out.
    pub(crate) async fn knock_answer(
        &'static self,
        room_id: &str,
        user_id: &str,
        accept: bool,
        ban: bool,
        reason: Option<&str>,
    ) -> Result<()> {
        let room = self.room(room_id).await?;
        let user = UserId::parse(user_id.trim())?;
        if accept {
            room.invite_user_by_id(&user).await?;
        } else if ban {
            room.ban_user(&user, reason).await?;
        } else {
            room.kick_user(&user, reason).await?;
        }
        Ok(())
    }

    /// The requests the user has looked at: no notification for them again.
    pub(crate) async fn knock_seen(&'static self, room_id: &str, user_ids: &[String]) -> Result<()> {
        let room = self.room(room_id).await?;
        let users: Vec<OwnedUserId> = user_ids.iter().filter_map(|u| UserId::parse(u.trim()).ok()).collect();
        room.mark_knock_requests_as_seen(&users).await?;
        Ok(())
    }
}
