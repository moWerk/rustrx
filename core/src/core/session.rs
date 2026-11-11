//! Logging in and out, restoring and saving the session, the server probe.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

impl Core {
    pub(crate) fn session_path(&self) -> PathBuf {
        self.data_dir.join("session.json")
    }

    /// Restore a saved session, or report that a login is needed.
    pub(crate) fn boot(&'static self) {
        self.rt.spawn(async move {
            std::fs::create_dir_all(self.data_dir.join("media")).ok();
            std::fs::create_dir_all(self.data_dir.join("avatars")).ok();
            match self.restore().await {
                Ok(true) => {}
                Ok(false) => self.emit(json!({"ev": "ready", "logged_in": false})),
                Err(e) => {
                    self.error("restore", &e);
                    self.emit(json!({"ev": "ready", "logged_in": false}));
                }
            }
        });
    }

    pub(crate) async fn restore(&'static self) -> Result<bool> {
        let path = self.session_path();
        if !path.exists() {
            return Ok(false);
        }
        let saved: SavedSession = serde_json::from_slice(&std::fs::read(&path)?)?;
        let t = std::time::Instant::now();
        let client = match saved.sliding_sync {
            Some(native) => self.build_client_with(&saved.homeserver, true, Some(native)).await?,
            None => self.build_client(&saved.homeserver, true).await?,
        };
        tracing::info!(ms = t.elapsed().as_millis() as u64, "startup: client built");
        client.restore_session(saved.session).await?;
        tracing::info!(ms = t.elapsed().as_millis() as u64, "startup: session restored");
        self.after_login(client).await?;
        tracing::info!(ms = t.elapsed().as_millis() as u64, "startup: sync started");
        Ok(true)
    }

    pub(crate) async fn build_client(&self, server: &str, with_store: bool) -> Result<Client> {
        self.build_client_with(server, with_store, None).await
    }

    /// `known`: the sync flavour from a previous login, which skips discovery.
    pub(crate) async fn build_client_with(&self, server: &str, with_store: bool, known: Option<bool>) -> Result<Client> {
        let make = |version: VersionBuilder| {
            let mut b = Client::builder()
                .sliding_sync_version_builder(version)
                .with_encryption_settings(EncryptionSettings {
                    auto_enable_cross_signing: true,
                    backup_download_strategy: BackupDownloadStrategy::AfterDecryptionFailure,
                    auto_enable_backups: true,
                })
                .handle_refresh_tokens()
                .user_agent(USER_AGENT);
            if with_store {
                b = b.sqlite_store(self.data_dir.join("store"), None);
            }
            if server.starts_with("http://") || server.starts_with("https://") {
                b.homeserver_url(server)
            } else {
                b.server_name_or_homeserver_url(server)
            }
        };
        if let Some(native) = known {
            let version = if native { VersionBuilder::Native } else { VersionBuilder::None };
            return make(version).build().await.map_err(|e| anyhow!("{e}"));
        }
        // Native sliding sync where the server has it, the classic sync otherwise.
        match make(VersionBuilder::DiscoverNative).build().await {
            Ok(c) => Ok(c),
            Err(first) => {
                tracing::info!("no native sliding sync ({first}), using the classic sync");
                make(VersionBuilder::None).build().await.map_err(|e| anyhow!("{e}"))
            }
        }
    }

    pub(crate) fn save_session(&self, client: &Client) -> Result<()> {
        let session = client.matrix_auth().session().context("no Matrix session after login")?;
        let saved = SavedSession {
            homeserver: client.homeserver().to_string(),
            session,
            sliding_sync: Some(!matches!(client.sliding_sync_version(), Version::None)),
        };
        // the tokens: owner-only, and complete or absent (a crash mid-write
        // must not leave a half file for the next start)
        use std::io::Write;
        use std::os::unix::fs::OpenOptionsExt;
        let path = self.session_path();
        let tmp = path.with_extension("json.tmp");
        {
            let mut f = std::fs::OpenOptions::new()
                .write(true)
                .create(true)
                .truncate(true)
                .mode(0o600)
                .open(&tmp)?;
            f.write_all(&serde_json::to_vec(&saved)?)?;
            f.sync_all()?;
        }
        std::fs::rename(&tmp, &path)?;
        Ok(())
    }

    pub(crate) async fn after_login(&'static self, client: Client) -> Result<()> {
        self.save_session(&client)?;
        let user_id = client.user_id().map(|u| u.to_string());
        let device_id = client.device_id().map(|d| d.to_string());
        self.emit(json!({
            "ev": "logged_in",
            "user_id": user_id,
            "device_id": device_id,
            "homeserver": client.homeserver().to_string(),
            "sliding_sync": !matches!(client.sliding_sync_version(), Version::None),
        }));
        self.install_handlers(&client);
        {
            let mut st = self.state.lock().await;
            st.client = Some(client.clone());
        }
        let watch = self.rt.spawn(self.session_watch(client.clone()));
        // the previews come from the event cache; the sync service subscribes
        // it itself, the classic sync loop does not
        let _ = client.event_cache().subscribe();
        // the cached room list first, before anything waits for the network
        let rooms = self.rt.spawn(self.rooms_task(client.clone()));
        self.state.lock().await.login_tasks.extend([watch, rooms]);
        self.start_sync(client).await
    }

    /// Servers with refresh tokens rotate them while the app runs; the
    /// rotated pair has to reach session.json or the next start restores a
    /// dead session ("Invalid refresh token", 0.2.0 after a day).
    pub(crate) async fn session_watch(&'static self, client: Client) {
        let mut changes = client.subscribe_to_session_changes();
        loop {
            match changes.recv().await {
                Ok(SessionChange::TokensRefreshed) => match self.save_session(&client) {
                    Ok(()) => tracing::info!("tokens refreshed, session saved"),
                    Err(e) => tracing::warn!("saving the refreshed session: {e}"),
                },
                // on a task of its own: the teardown aborts this one
                Ok(SessionChange::UnknownToken(data)) => {
                    self.rt.spawn(self.session_expired(data.soft_logout));
                    break;
                }
                Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => continue,
                Err(tokio::sync::broadcast::error::RecvError::Closed) => break,
            }
        }
    }

    /// Local side of a logout the server already did: stop syncing, drop
    /// the saved session, tell the UI. A soft logout keeps the crypto store
    /// for a new login on the same device.
    pub(crate) async fn session_expired(&'static self, soft_logout: bool) {
        let teardown = {
            let mut st = self.state.lock().await;
            if st.client.is_none() {
                return; // already handled; every failing request reports it once more
            }
            st.take_login()
        };
        tracing::warn!(soft_logout, "the server no longer accepts this session");
        let client = teardown.stop().await;
        std::fs::remove_file(self.session_path()).ok();
        if soft_logout {
            // the crypto store belongs to this device: the next login asks for it again
            if let Some(device) = client.as_ref().and_then(|c| c.device_id()) {
                std::fs::write(self.data_dir.join("device_id"), device.as_str()).ok();
            }
        } else {
            self.wipe_local_data();
        }
        self.emit(json!({"ev": "logged_out", "reason": "session_expired", "soft_logout": soft_logout}));
    }

    /// Everything of the account on this phone: store, decrypted media, avatars.
    pub(crate) fn wipe_local_data(&self) {
        std::fs::remove_dir_all(self.data_dir.join("store")).ok();
        std::fs::remove_file(self.data_dir.join("device_id")).ok();
        for dir in ["media", "avatars"] {
            std::fs::remove_dir_all(self.data_dir.join(dir)).ok();
            std::fs::create_dir_all(self.data_dir.join(dir)).ok();
        }
    }

    /// Server from a full user id, or the homeserver field.
    pub(crate) fn server_of(v: &Value) -> Result<String> {
        if let Some(h) = opt(v, "homeserver").filter(|h| !h.trim().is_empty()) {
            return Ok(h.trim().to_owned());
        }
        let user = s(v, "user")?;
        let user = user.trim();
        match user.strip_prefix('@').and_then(|u| u.split_once(':')) {
            Some((_, server)) if !server.is_empty() => Ok(server.to_owned()),
            _ => bail!("enter the full Matrix ID, like @name:server.org, or a homeserver"),
        }
    }

    pub(crate) async fn probe_server(&'static self, v: &Value) -> Result<()> {
        let server = Self::server_of(v)?;
        let client = self.build_client(&server, false).await.map_err(|e| anyhow!("{server}: {e}"))?;
        let flows = client.matrix_auth().get_login_types().await?.flows;
        let mut password = false;
        let mut idps: Vec<String> = Vec::new();
        let mut sso = false;
        for flow in &flows {
            match flow {
                LoginType::Password(_) => password = true,
                LoginType::Sso(s) => {
                    sso = true;
                    idps.extend(s.identity_providers.iter().map(|p| p.name.clone()));
                }
                _ => {}
            }
        }
        // a server with an account service (MAS, matrix.org since 2025) signs
        // new people up in the browser; the password login works afterwards
        let signup = match client.oauth().server_metadata().await {
            Ok(meta) if meta.prompt_values_supported.iter().any(|p| p.as_str() == "create") => {
                meta.issuer.join("register").ok().map(|u| u.to_string())
            }
            _ => None,
        };
        self.emit(json!({
            "ev": "server_info",
            "signup_url": signup,
            "server": server,
            "homeserver": client.homeserver().to_string(),
            "password": password,
            "sso": sso,
            "idps": idps,
            "sliding_sync": !matches!(client.sliding_sync_version(), Version::None),
        }));
        Ok(())
    }

    pub(crate) async fn login_password(&'static self, v: &Value) -> Result<()> {
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        let result: Result<Client> = async {
            let server = Self::server_of(v)?;
            let user = s(v, "user")?;
            let password = s(v, "password")?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            // after a soft logout: the same device again, so the kept crypto store fits
            let device = std::fs::read_to_string(self.data_dir.join("device_id")).ok();
            let mut login = client
                .matrix_auth()
                .login_username(user.trim(), &password)
                .initial_device_display_name(DEVICE_NAME)
                .request_refresh_token();
            if let Some(d) = device.as_deref() {
                login = login.device_id(d.trim());
            }
            login.send().await.map_err(|e| anyhow!("{e}"))?;
            std::fs::remove_file(self.data_dir.join("device_id")).ok();
            Ok(client)
        }
        .await;
        match result {
            Ok(client) => self.after_login(client).await,
            Err(e) => {
                self.emit(json!({"ev": "login_error", "message": format!("{e:#}")}));
                Ok(())
            }
        }
    }

    pub(crate) async fn login_sso(&'static self, v: &Value) -> Result<()> {
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        let result: Result<Client> = async {
            let server = Self::server_of(v)?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            let mut builder = client
                .matrix_auth()
                .login_sso(|url| async move {
                    crate::instance().emit(json!({"ev": "open_url", "url": url}));
                    Ok(())
                })
                .initial_device_display_name(DEVICE_NAME)
                .request_refresh_token();
            if let Some(idp) = opt(v, "idp") {
                builder = builder.identity_provider_id(&idp);
            }
            builder.send().await.map_err(|e| anyhow!("{e}"))?;
            Ok(client)
        }
        .await;
        match result {
            Ok(client) => self.after_login(client).await,
            Err(e) => {
                self.emit(json!({"ev": "login_error", "message": format!("{e:#}")}));
                Ok(())
            }
        }
    }

    pub(crate) async fn logout(&'static self) -> Result<()> {
        // out of the state first: nothing below may hold the lock, or every
        // other command waits for the server (or its timeout) too
        let teardown = self.state.lock().await.take_login();
        if let Some(client) = teardown.stop().await {
            if let Err(e) = client.logout().await {
                tracing::warn!("logout: {e}");
            }
        }
        std::fs::remove_file(self.session_path()).ok();
        self.wipe_local_data();
        self.emit(json!({"ev": "logged_out"}));
        Ok(())
    }
}
