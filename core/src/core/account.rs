// SPDX-License-Identifier: GPL-3.0-or-later
//! The account: recovery and the encryption identity, room keys, devices,
//! the profile, ignored users and the media kept on the phone.
//! (part of `impl Core`; verification is in verification.rs)

use super::*;

impl Core {
    pub(crate) async fn recovery_recover(&self, key: &str) -> Result<()> {
        let client = self.client().await?;
        client.encryption().recovery().recover(key.trim()).await?;
        self.emit(json!({"ev": "recovery_done"}));
        Ok(())
    }

    pub(crate) async fn recovery_enable(&self) -> Result<()> {
        let client = self.client().await?;
        let key = client.encryption().recovery().enable().wait_for_backups_to_upload().await?;
        self.emit(json!({"ev": "recovery_key", "recovery_key": key}));
        Ok(())
    }

    pub(crate) async fn recovery_reset(&self) -> Result<()> {
        let client = self.client().await?;
        let key = client.encryption().recovery().reset_key().await?;
        self.emit(json!({"ev": "recovery_key", "key": key}));
        Ok(())
    }

    /// No other device and no recovery key: new cross-signing keys, a new
    /// backup and recovery key. An account service asks for approval in
    /// the browser ("identity_reset_approval").
    pub(crate) async fn identity_reset(&self) -> Result<()> {
        use matrix_sdk::encryption::CrossSigningResetAuthType;
        let client = self.client().await?;
        let recovery = client.encryption().recovery();
        if let Some(handle) = recovery.reset_identity().await? {
            match handle.auth_type() {
                CrossSigningResetAuthType::OAuth(info) => {
                    let url = info.approval_url.to_string();
                    tracing::info!("identity reset: waiting for the approval in the browser");
                    self.emit(json!({"ev": "identity_reset_approval", "url": url}));
                    self.emit(json!({"ev": "open_url", "url": url}));
                    // the SDK polls for two minutes
                    handle.reset(None).await?;
                }
                CrossSigningResetAuthType::Uiaa(_) => {
                    handle.cancel().await;
                    bail!("this server asks for the password to reset the encryption, which Rustrx cannot do yet");
                }
            }
        }
        tracing::info!("identity reset: done, setting up the backup");
        let key = recovery.enable().wait_for_backups_to_upload().await?;
        self.emit(json!({"ev": "recovery_key", "key": key}));
        Ok(())
    }

    pub(crate) async fn export_keys(&self, path: String, passphrase: &str) -> Result<()> {
        let client = self.client().await?;
        let path = PathBuf::from(path);
        client.encryption().export_room_keys(path.clone(), passphrase, |_| true).await?;
        self.emit(json!({"ev": "keys_exported", "path": path.to_string_lossy()}));
        Ok(())
    }

    pub(crate) async fn import_keys(&self, path: String, passphrase: &str) -> Result<()> {
        let client = self.client().await?;
        let result = client.encryption().import_room_keys(PathBuf::from(path), passphrase).await?;
        self.emit(json!({"ev": "keys_imported", "imported": result.imported_count, "total": result.total_count}));
        Ok(())
    }

    /// The account's devices: "devices". A server whose logins an account
    /// service (OAuth 2.0, MAS) keeps takes no password to remove a device:
    /// its page for that device goes with each (`manage_url`), and its
    /// device list as `account_url` (0.8 audit: the password prompt was a
    /// dead end on matrix.org).
    pub(crate) async fn devices(&self) -> Result<()> {
        use matrix_sdk::ruma::api::client::discovery::get_authorization_server_metadata::v1::{
            AccountManagementActionData, DeviceDeleteData,
        };
        let client = self.client().await?;
        let own = client.device_id().map(|d| d.to_string());
        let user = client.user_id().context("not logged in")?.to_owned();
        // also for a password login on such a server (an older session
        // carried over): the homeserver then asks for no password either
        let meta = match client.oauth().cached_server_metadata().await {
            Ok(m) if m.account_management_uri.is_some() => Some(m),
            Ok(_) => None,
            Err(e) => {
                tracing::info!("devices: no account service ({e})");
                None
            }
        };
        let account_url = meta
            .as_ref()
            .and_then(|m| m.account_management_url_with_action(AccountManagementActionData::DevicesList))
            .map(|u| u.to_string());
        let mut devices: Vec<Value> = Vec::new();
        for d in client.devices().await?.devices.iter() {
            // verified: cross-signed by us; keys: it has encryption keys at
            // all (a device without them is no risk to the account's keys)
            let crypto = client.encryption().get_device(&user, &d.device_id).await.ok().flatten();
            let manage_url = meta
                .as_ref()
                .and_then(|m| m.account_management_url_with_action(AccountManagementActionData::DeviceDelete(DeviceDeleteData::new(&d.device_id))))
                .map(|u| u.to_string());
            devices.push(json!({
                "manage_url": manage_url,
                "device_id": d.device_id.as_str(),
                "name": d.display_name,
                "last_seen_ts": d.last_seen_ts.map(|t| u64::from(t.0)),
                "last_seen_ip": d.last_seen_ip,
                "current": Some(d.device_id.to_string()) == own,
                "keys": crypto.is_some(),
                "verified": crypto.map(|c| c.is_verified()).unwrap_or(false),
            }));
        }
        self.emit(json!({"ev": "devices", "devices": devices, "account_url": account_url}));
        Ok(())
    }

    /// Logs a device out; the server asks for the password.
    pub(crate) async fn delete_device(&self, device_id: String, password: String) -> Result<()> {
        let client = self.client().await?;
        let id: OwnedDeviceId = device_id.into();
        let ids = [id];
        if let Err(e) = client.delete_devices(&ids, None).await {
            let info = e.as_uiaa_response().context(format!("{e}"))?;
            // a single sign-on account may have no password the server takes
            // here: said plainly, not as the server's refusal of a wrong one
            let password_flow = info.flows.iter().any(|f| f.stages.iter().any(|s| s.as_str() == "m.login.password"));
            if !password_flow {
                bail!("this server does not take a password to remove a device; remove it from another client or the server's own page");
            }
            let localpart = client.user_id().context("no user id")?.localpart().to_owned();
            let mut auth = Password::new(UserIdentifier::Matrix(MatrixUserIdentifier::new(localpart)), password);
            auth.session = info.session.clone();
            client.delete_devices(&ids, Some(AuthData::Password(auth))).await?;
        }
        self.emit(json!({"ev": "device_deleted", "device_id": ids[0].as_str()}));
        Ok(())
    }

    /// The own name and picture: "profile".
    pub(crate) async fn profile(&self) -> Result<()> {
        let client = self.client().await?;
        let name = client.account().get_display_name().await?;
        // one's own picture, cached beside the rooms' as "_own.img"
        let file = self.data_dir.join(AVATAR_DIR).join(OWN_AVATAR_FILE);
        let format = MediaFormat::Thumbnail(MediaThumbnailSettings::new(OWN_AVATAR_PX.into(), OWN_AVATAR_PX.into()));
        let avatar = match client.account().get_avatar(format).await {
            Ok(Some(bytes)) if std::fs::write(&file, &bytes).is_ok() => Some(file.to_string_lossy().into_owned()),
            Ok(None) => {
                std::fs::remove_file(&file).ok();
                None
            }
            _ => file.exists().then(|| file.to_string_lossy().into_owned()),
        };
        let version = std::fs::metadata(&file)
            .and_then(|m| m.modified())
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map(|d| d.as_millis() as u64);
        self.emit(json!({"ev": "profile", "display_name": name,
                         "user_id": client.user_id().map(|u| u.to_string()),
                         "avatar_path": avatar, "avatar_v": version}));
        Ok(())
    }

    /// One's own picture from a file (prepared by the UI as for a room).
    pub(crate) async fn set_own_avatar(&self, path: &str) -> Result<()> {
        let client = self.client().await?;
        let data = tokio::fs::read(path).await?;
        let mime = mime_guess::from_path(path).first_or(mime::IMAGE_JPEG);
        client.account().upload_avatar(&mime, data).await?;
        tokio::fs::remove_file(path).await.ok();
        // the new picture to the settings page
        self.profile().await
    }

    pub(crate) async fn set_display_name(&self, name: &str) -> Result<()> {
        let client = self.client().await?;
        client.account().set_display_name(Some(name)).await?;
        Ok(())
    }

    pub(crate) async fn ignore_user(&self, user_id: &str) -> Result<()> {
        let client = self.client().await?;
        client.account().ignore_user(&UserId::parse(user_id.trim())?).await?;
        self.ignored_users().await
    }

    pub(crate) async fn unignore_user(&self, user_id: &str) -> Result<()> {
        let client = self.client().await?;
        client.account().unignore_user(&UserId::parse(user_id.trim())?).await?;
        self.ignored_users().await
    }

    /// The users this account ignores, with the name their profile gives
    /// where the server tells it: "ignored_users".
    pub(crate) async fn ignored_users(&self) -> Result<()> {
        let client = self.client().await?;
        let ids = client.subscribe_to_ignore_user_list_changes().get();
        let mut users = Vec::new();
        for id in ids {
            let name = match UserId::parse(&id) {
                Ok(user) => client.account().fetch_user_profile_of(&user).await.ok()
                    .and_then(|p| p.get("displayname").and_then(Value::as_str).map(str::to_owned)),
                Err(_) => None,
            };
            users.push(json!({"user_id": id, "name": name}));
        }
        self.emit(json!({"ev": "ignored_users", "users": users}));
        Ok(())
    }

    /// The decrypted copies this app keeps, and the SDK's media cache;
    /// answered with the new sizes ("storage").
    pub(crate) async fn clear_media(&self) -> Result<()> {
        let media = self.data_dir.join(MEDIA_DIR);
        std::fs::remove_dir_all(&media).ok();
        std::fs::create_dir_all(&media).ok();
        let client = self.client().await?;
        client.media().clean().await?;
        self.storage_info().await
    }
}
