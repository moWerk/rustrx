// SPDX-License-Identifier: GPL-3.0-or-later
//! Commands from the UI: parsing and dispatch. Each command is one call
//! into the file of its domain; the arms follow protocol.rs's sections.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

impl Core {
    pub(crate) fn dispatch(&'static self, v: Value) {
        let cmd = v.get("cmd").and_then(Value::as_str).unwrap_or("").to_owned();
        let room = v.get("room_id").and_then(Value::as_str).unwrap_or("").to_owned();
        tracing::info!(%cmd, %room, "command");
        // the shape is checked before anything runs (protocol.rs)
        let command: Command = match crate::protocol::parse(v) {
            Ok(c) => c,
            Err(e) => {
                self.error(&cmd, &anyhow!("{e}"));
                return;
            }
        };
        self.rt.spawn(async move {
            if let Err(e) = self.handle(command).await {
                self.error(&cmd, &e);
            }
        });
    }

    pub(crate) async fn handle(&'static self, command: Command) -> Result<()> {
        use Command::*;
        match command {
            // --- session (session.rs) ---
            ProbeServer { user, homeserver } => self.probe_server(user, homeserver).await,
            LoginPassword { user, password, homeserver } => self.login_password(user, password, homeserver).await,
            LoginSso { user, homeserver, idp } => self.login_sso(user, homeserver, idp).await,
            LoginCancel => {
                self.login_cancel();
                Ok(())
            }
            Logout => self.logout().await,

            // --- the open room (timeline.rs, threads.rs, push.rs, packs.rs) ---
            OpenRoom { room_id } => self.open_room(&room_id).await,
            CloseRoom => {
                self.close_room().await;
                Ok(())
            }
            PaginateBack { room_id, count } => self.paginate_back(&room_id, count).await,
            SendText { room_id, body, reply_to, markdown, emotes, thread_root } => {
                self.send_text(&room_id, &body, given(&reply_to), markdown.unwrap_or(true), emotes.as_ref(), given(&thread_root))
                    .await
            }
            SendSticker { room_id, url, body, info, thread_root } => {
                self.send_sticker(&room_id, &url, &body, info, given(&thread_root)).await
            }
            OpenThread { room_id, root } => self.open_thread(&room_id, &root).await,
            RetrySend { room_id, txn_id } => self.retry_send(&room_id, &txn_id).await,
            CloseThread => {
                self.close_thread().await;
                Ok(())
            }
            ThreadList { room_id } => self.thread_list(&room_id).await,
            CloseThreadList => {
                self.close_thread_list().await;
                Ok(())
            }
            MarkThreadRead => self.mark_thread_read().await,
            PushSet { endpoint, gateway, old, lang } => self.push_set(&endpoint, &gateway, given(&old), given(&lang)).await,
            PushRemove { endpoint } => self.push_remove(&endpoint).await,
            PushHandle { room_id, event_id } => self.push_handle(&room_id, &event_id).await,
            SendFile { room_id, path, mime, caption, w, h, thumb_path, thumb_w, thumb_h, voice, duration_ms, thread_root } => {
                let voice = voice.unwrap_or(false);
                self.send_file(&room_id, path, given(&mime), given(&caption), w, h, thumb_path, thumb_w, thumb_h, voice, duration_ms, given(&thread_root))
                    .await
            }
            React { room_id, event_id, key } => self.react(&room_id, &event_id, &key).await,
            Redact { room_id, event_id, txn_id, reason } => self.redact(&room_id, event_id, txn_id, given(&reason)).await,
            Edit { room_id, event_id, txn_id, body } => self.edit(&room_id, event_id, txn_id, &body).await,
            PollVote { room_id, event_id, answers } => self.poll_vote(&room_id, &event_id, answers).await,
            Report { room_id, event_id, reason } => self.report(&room_id, &event_id, given(&reason)).await,
            EventSource { room_id, event_id } => self.event_source(&room_id, &event_id).await,
            MarkRead { room_id, private } => self.mark_read(&room_id, private.unwrap_or(false)).await,
            Typing { room_id, typing } => self.typing(&room_id, typing.unwrap_or(false)).await,
            GetMedia { room_id, event_id, thumb, dest_dir } => {
                self.get_media(&room_id, &event_id, thumb.unwrap_or(false), dest_dir).await
            }
            FetchEmote { mxc } => self.fetch_emote(&mxc).await,
            ImagePacks { room_id } => self.image_packs(&room_id).await,
            AddUserEmote { shortcode, url } => self.add_user_emote(&shortcode, &url).await,

            // --- rooms (rooms.rs, spaces.rs) ---
            GetNotificationMode { room_id } => self.notification_mode(&room_id).await,
            SetNotificationMode { room_id, mode } => self.set_notification_mode(&room_id, &mode).await,
            SetFavourite { room_id, value } => self.set_favourite(&room_id, value.unwrap_or(true)).await,
            SetLowPriority { room_id, value } => self.set_low_priority(&room_id, value.unwrap_or(true)).await,
            JoinRoom { room_id } => self.join_room(&room_id).await,
            LeaveRoom { room_id } => self.leave_room(&room_id).await,
            JoinByIdOrAlias { id, via } => self.join_by_id_or_alias(&id, &via).await,
            RoomPreview { id, via } => self.room_preview(&id, &via).await,
            Knock { id, reason, via } => self.knock(&id, given(&reason), &via).await,
            RoomLink { room_id } => self.room_link(&room_id).await,
            SetJoinRule { room_id, rule } => self.set_join_rule(&room_id, &rule).await,
            SpaceOpen { space_id } => self.space_open(&space_id).await,
            SpaceClose { space_id } => {
                self.space_close(&space_id).await;
                Ok(())
            }
            SpaceAddChild { space_id, room_id } => self.space_add_child(&space_id, &room_id).await,
            SpaceRemoveChild { space_id, room_id } => self.space_remove_child(&space_id, &room_id).await,
            SpaceLeave { space_id, with_rooms } => self.space_leave(&space_id, with_rooms.unwrap_or(false)).await,
            CreateDm { user_id } => self.create_dm(&user_id).await,
            CreateRoom { name, topic, public, alias, encrypted, space, parent, join_rule } => {
                self.create_room(
                    given(&name),
                    given(&topic),
                    public.unwrap_or(false),
                    given(&alias),
                    encrypted,
                    space.unwrap_or(false),
                    given(&parent),
                    given(&join_rule),
                )
                .await
            }
            RoomMembers { room_id } => self.room_members(&room_id).await,
            SetRoomName { room_id, name } => self.set_room_name(&room_id, name).await,
            SetRoomAvatar { room_id, path, w, h } => self.set_room_avatar(&room_id, &path, w, h).await,
            SetOwnAvatar { path } => self.set_own_avatar(&path).await,
            SetRoomTopic { room_id, topic } => self.set_room_topic(&room_id, &topic).await,
            Invite { room_id, user_id } => self.invite(&room_id, &user_id).await,
            Search { term } => self.search(&term).await,
            RefreshRooms => self.refresh_rooms().await,

            // --- encryption (verification.rs, account.rs) ---
            VerificationRequestSelf => self.verification_request_self().await,
            VerificationAccept { flow_id } => self.verification_accept(&flow_id).await,
            VerificationStartSas { flow_id } => self.verification_start_sas(&flow_id).await,
            VerificationConfirm { flow_id } => self.verification_confirm(&flow_id).await,
            VerificationMismatch { flow_id } => self.verification_mismatch(&flow_id).await,
            VerificationCancel { flow_id } => self.verification_cancel(&flow_id).await,
            RecoveryRecover { key } => self.recovery_recover(&key).await,
            RecoveryEnable => self.recovery_enable().await,
            RecoveryReset => self.recovery_reset().await,
            IdentityReset => self.identity_reset().await,
            ExportKeys { path, passphrase } => self.export_keys(path, &passphrase).await,
            ImportKeys { path, passphrase } => self.import_keys(path, &passphrase).await,

            // --- account and devices (account.rs, sync.rs) ---
            Devices => self.devices().await,
            DeleteDevice { device_id, password } => self.delete_device(device_id, password).await,
            Profile => self.profile().await,
            SetDisplayName { name } => self.set_display_name(&name).await,
            IgnoreUser { user_id } => self.ignore_user(&user_id).await,
            UnignoreUser { user_id } => self.unignore_user(&user_id).await,
            StorageInfo => self.storage_info().await,
            ClearMedia => self.clear_media().await,

            // --- calls (call.rs) and requests to join (knock.rs) ---
            CallStart { room_id, voice, skip_lobby } => {
                self.call_start(&room_id, voice.unwrap_or(false), skip_lobby.unwrap_or(false)).await
            }
            CallMessage { data } => self.call_message(&data).await,
            CallEnd => {
                self.call_end().await;
                Ok(())
            }
            KnockAnswer { room_id, user_id, accept, ban, reason } => {
                self.knock_answer(&room_id, &user_id, accept, ban.unwrap_or(false), given(&reason)).await
            }
            KnockSeen { room_id, user_ids } => self.knock_seen(&room_id, &user_ids).await,
            CallDecline { room_id, event_id } => self.call_decline(&room_id, &event_id).await,
        }
    }
}
