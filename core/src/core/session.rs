// SPDX-License-Identifier: GPL-3.0-or-later
//! Logging in and out, restoring and saving the session, the server probe.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

impl Core {
    pub(crate) fn session_path(&self) -> PathBuf {
        self.data_dir.join(SESSION_FILE)
    }

    /// Restore a saved session, or report that a login is needed.
    pub(crate) fn boot(&'static self) {
        self.rt.spawn(async move {
            std::fs::create_dir_all(self.data_dir.join(MEDIA_DIR)).ok();
            std::fs::create_dir_all(self.data_dir.join(AVATAR_DIR)).ok();
            match self.restore().await {
                Ok(true) => {}
                Ok(false) => {
                    // a session the server ended earlier: whose store is kept
                    let kept = std::fs::read_to_string(self.relogin_path()).ok()
                        .and_then(|raw| serde_json::from_str::<Value>(&raw).ok())
                        .and_then(|v| v["user_id"].as_str().map(str::to_owned));
                    self.emit(json!({"ev": "ready", "logged_in": false, "proto": PROTO, "kept_user": kept}))
                }
                Err(e) => {
                    // a saved session that could not be opened (the store's
                    // key not there yet, before the first unlock, say): not
                    // a logout, so the stand-in tries again later
                    self.error("restore", &e);
                    self.emit(json!({"ev": "ready", "logged_in": false, "proto": PROTO, "restore_failed": true}));
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
        match saved.client_id {
            Some(id) => {
                use matrix_sdk::authentication::oauth::{ClientId, OAuthSession, UserSession};
                let user = UserSession { meta: saved.session.meta, tokens: saved.session.tokens };
                client.restore_session(OAuthSession { client_id: ClientId::new(id), user }).await?
            }
            None => client.restore_session(saved.session).await?,
        }
        tracing::info!(ms = t.elapsed().as_millis() as u64, "startup: session restored");
        self.after_login(client).await?;
        tracing::info!(ms = t.elapsed().as_millis() as u64, "startup: sync started");
        Ok(true)
    }

    pub(crate) async fn build_client(&self, server: &str, with_store: bool) -> Result<Client> {
        self.build_client_with(server, with_store, None).await
    }

    /// `known`: the sync flavour from a previous login, which skips discovery.
    /// The passphrase for the store, if it is (to be) encrypted. A new store
    /// is encrypted when sailfish-secrets gave a key; a file in it marks it,
    /// with a fingerprint of the key (a hash; the key cannot be read from
    /// it). A store from before stays as it is.
    /// - no key now (sailfish-secrets locked before the first unlock, or not
    ///   reachable): nothing is removed; the store cannot open this time
    /// - a key that is not the store's (the secret was lost and made anew):
    ///   the store can never be read again and goes
    fn store_passphrase(&self) -> Result<Option<String>> {
        let dir = self.data_dir.join(STORE_DIR);
        let marker = dir.join(ENCRYPTED_MARKER);
        let existing = std::fs::read_dir(&dir).map(|mut d| d.next().is_some()).unwrap_or(false);
        let key = crate::store_key();
        let fingerprint = |k: &str| {
            use std::hash::{Hash, Hasher};
            let mut h = std::collections::hash_map::DefaultHasher::new();
            k.hash(&mut h);
            format!("{:016x}", h.finish())
        };
        if existing {
            let Ok(mark) = std::fs::read_to_string(&marker) else {
                tracing::info!("store: unencrypted (made before sailfish-secrets was used)");
                return Ok(None);
            };
            let Some(key) = key else {
                bail!("the encrypted store's key is not available (sailfish-secrets locked or unreachable)");
            };
            if mark.trim() == fingerprint(key) {
                tracing::info!("store: encrypted, key from sailfish-secrets");
                return Ok(Some(key.to_owned()));
            }
            tracing::warn!("store: encrypted with a key that is gone; it cannot be read and goes");
            std::fs::remove_dir_all(&dir).ok();
        }
        let Some(key) = key else { return Ok(None) };
        std::fs::create_dir_all(&dir).ok();
        std::fs::write(&marker, fingerprint(key)).ok();
        tracing::info!("store: new and encrypted, key from sailfish-secrets");
        Ok(Some(key.to_owned()))
    }

    pub(crate) async fn build_client_with(&self, server: &str, with_store: bool, known: Option<bool>) -> Result<Client> {
        tracing::info!(%server, with_store, ?known, "client: building");
        let passphrase = if with_store { self.store_passphrase()? } else { None };
        let make = |version: VersionBuilder| {
            let mut b = Client::builder()
                .sliding_sync_version_builder(version)
                .with_encryption_settings(EncryptionSettings {
                    auto_enable_cross_signing: true,
                    backup_download_strategy: BackupDownloadStrategy::AfterDecryptionFailure,
                    auto_enable_backups: true,
                })
                .handle_refresh_tokens()
                // one process uses the store at a time (the D-Bus name is the
                // lock between the app and the stand-in): no cross-process
                // lock, whose lease the SDK renewed 20 times a second for as
                // long as the encryption sync ran (the idle CPU users saw)
                .cross_process_store_config(matrix_sdk::cross_process_lock::CrossProcessLockConfig::SingleProcess)
                .user_agent(USER_AGENT);
            if with_store {
                b = b.sqlite_store(self.data_dir.join(STORE_DIR), passphrase.as_deref());
            }
            if server.starts_with("http://") || server.starts_with("https://") {
                b.homeserver_url(server)
            } else {
                b.server_name_or_homeserver_url(server)
            }
        };
        let client = if let Some(native) = known {
            let version = if native { VersionBuilder::Native } else { VersionBuilder::None };
            make(version).build().await.map_err(|e| anyhow!("{e}"))?
        } else {
            // Native sliding sync where the server has it, the classic sync otherwise.
            match make(VersionBuilder::DiscoverNative).build().await {
                Ok(c) => c,
                Err(first) => {
                    tracing::info!("no native sliding sync ({first}), using the classic sync");
                    make(VersionBuilder::None).build().await.map_err(|e| anyhow!("{e}"))?
                }
            }
        };
        tracing::info!("client: built");
        // a picture or file on its way shows how much of it is uploaded
        client.send_queue().enable_upload_progress(true);
        Ok(client)
    }

    pub(crate) fn save_session(&self, client: &Client) -> Result<()> {
        use matrix_sdk::AuthSession;
        // an OAuth session is the same user session and tokens, plus the
        // client id the account service gave Rustrx
        let (session, client_id) = match client.session().context("no session after login")? {
            AuthSession::Matrix(s) => (s, None),
            AuthSession::OAuth(o) => (
                MatrixSession { meta: o.user.meta, tokens: o.user.tokens },
                Some(o.client_id.as_str().to_owned()),
            ),
            _ => bail!("a session of an unknown kind"),
        };
        let saved = SavedSession {
            homeserver: client.homeserver().to_string(),
            session,
            sliding_sync: Some(!matches!(client.sliding_sync_version(), Version::None)),
            client_id,
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
            "proto": PROTO,
            "user_id": user_id,
            "device_id": device_id,
            "homeserver": client.homeserver().to_string(),
            "sliding_sync": !matches!(client.sliding_sync_version(), Version::None),
        }));
        self.install_handlers(&client);
        self.install_call_handlers(&client);
        let generation = {
            let mut st = self.state.lock().await;
            st.client = Some(client.clone());
            st.generation += 1;
            st.generation
        };
        // in every process, the one woken for a push too: a token refresh
        // there rotates the refresh token, and unsaved, the next start
        // restores the spent one and the server ends the session
        // (invalid_grant on the Prawn, 2026-11-17 01:52)
        let watch = self.rt.spawn(self.session_watch(client.clone()));
        // woken for one push: the session only (no sync, no room list); the
        // pushed message is fetched by push_handle
        if push::push_wake() {
            tracing::info!("push: woken, no sync");
            let mut st = self.state.lock().await;
            if st.generation == generation {
                st.login_tasks.push(watch);
            } else {
                watch.abort();
            }
            return Ok(());
        }
        // the previews come from the event cache; the sync service subscribes
        // it itself, the classic sync loop does not
        let _ = client.event_cache().subscribe();
        // the cached room list first, before anything waits for the network
        let rooms = self.rt.spawn(self.rooms_task(client.clone()));
        let sends = self.rt.spawn(Self::send_queue_watch(client.clone()));
        {
            let mut st = self.state.lock().await;
            if st.generation != generation {
                // ended already (an expired session's first request)
                for t in [watch, rooms, sends] {
                    t.abort();
                }
                return Ok(());
            }
            st.login_tasks.retain(|t| !t.is_finished());
            st.login_tasks.extend([watch, rooms, sends]);
        }
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
                    if !Self::session_gone(&client).await {
                        tracing::warn!("token refresh failed without an answer from the server; the session stays");
                        continue;
                    }
                    self.rt.spawn(self.session_expired(data.soft_logout));
                    break;
                }
                Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => continue,
                Err(tokio::sync::broadcast::error::RecvError::Closed) => break,
            }
        }
    }

    /// Whether the server really ended the session. The SDK reports an unknown
    /// token whenever a refresh fails, also when the refresh never reached the
    /// server (no network after a sleep, a proxy error): that logged people
    /// out. Refreshing again tells: gone only if the server refuses the
    /// refresh token, or there is none. Asked more than once, spread out: a
    /// phone waking from sleep had its refresh cut by a connection reset,
    /// and the one retry a second later was refused (tchncs.de, 2026-11-16),
    /// although the spec keeps an old refresh token valid for that retry.
    async fn session_gone(client: &Client) -> bool {
        for (attempt, wait) in SESSION_GONE_WAITS.iter().enumerate() {
            tokio::time::sleep(*wait).await;
            match Self::refresh_refused(client).await {
                Some(false) => return false,
                Some(true) => tracing::warn!(attempt, "session: the refresh token was refused"),
                // no answer from the server: not a verdict, the session stays
                None => return false,
            }
        }
        true
    }

    /// One refresh: Some(true) refused by the server, Some(false) refreshed,
    /// None no answer (no network, a timeout).
    async fn refresh_refused(client: &Client) -> Option<bool> {
        match client.refresh_access_token().await {
            Ok(_) => Some(false),
            Err(matrix_sdk::RefreshTokenError::MatrixAuth(e)) => e.client_api_error_kind().is_some().then_some(true),
            Err(matrix_sdk::RefreshTokenError::RefreshTokenRequired) => Some(true),
            // an OAuth refresh fails for no network too (the metadata fetch):
            // gone only when the account service refuses the refresh token
            Err(matrix_sdk::RefreshTokenError::OAuth(e)) => {
                use matrix_sdk::authentication::oauth::error::{BasicErrorResponseType, RequestTokenError};
                use matrix_sdk::authentication::oauth::OAuthError;
                matches!(&*e, OAuthError::RefreshToken(RequestTokenError::ServerResponse(r))
                    if *r.error() == BasicErrorResponseType::InvalidGrant)
                    .then_some(true)
            }
        }
    }

    /// What a session that ended on the server's side leaves for the next
    /// login: who it was and on which device ("relogin.json").
    fn relogin_path(&self) -> PathBuf {
        self.data_dir.join(RELOGIN_FILE)
    }

    /// Before a login: the device to log in to again, if the kept store
    /// belongs to this user (the store, keys and history then stay). A store
    /// of another user, or one that cannot be matched (single sign-on with
    /// only a server given), is wiped first, so accounts never share one.
    fn kept_device(&self, user: Option<&str>) -> Option<String> {
        let wanted = user.map(|u| u.trim().to_lowercase()).filter(|u| u.starts_with('@'));
        if let Ok(raw) = std::fs::read_to_string(self.relogin_path()) {
            let kept: Value = serde_json::from_str(&raw).unwrap_or(Value::Null);
            let kept_user = kept["user_id"].as_str().unwrap_or("").to_lowercase();
            let device = kept["device_id"].as_str().map(str::to_owned);
            if wanted.as_deref() == Some(kept_user.as_str()) && device.is_some() {
                tracing::info!("login: the kept store and device of this user");
                return device;
            }
            tracing::info!("login: another user or none given, the kept store goes");
            self.wipe_local_data();
            return None;
        }
        // after a soft logout of an earlier version: the device only
        std::fs::read_to_string(self.data_dir.join(DEVICE_ID_FILE)).ok().map(|d| d.trim().to_owned())
    }

    /// Local side of a logout the server already did: stop syncing, drop
    /// the saved session, tell the UI. The store, the media and the device
    /// stay: logging in again as the same user goes on where it was, keys
    /// and history included (a lost refresh answer ended sessions on
    /// servers that rotate their tokens).
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
        let user = client.as_ref().and_then(|c| c.user_id()).map(|u| u.to_string());
        if let (Some(user), Some(device)) = (user.as_deref(), client.as_ref().and_then(|c| c.device_id())) {
            let kept = json!({"user_id": user, "device_id": device.as_str()});
            std::fs::write(self.relogin_path(), kept.to_string()).ok();
        } else {
            self.wipe_local_data();
        }
        self.emit(json!({"ev": "logged_out", "reason": "session_expired", "soft_logout": soft_logout, "user_id": user}));
    }

    /// Everything of the account on this phone: store, decrypted media, avatars.
    pub(crate) fn wipe_local_data(&self) {
        std::fs::remove_dir_all(self.data_dir.join(STORE_DIR)).ok();
        std::fs::remove_file(self.data_dir.join(DEVICE_ID_FILE)).ok();
        std::fs::remove_file(self.relogin_path()).ok();
        for dir in [MEDIA_DIR, AVATAR_DIR] {
            std::fs::remove_dir_all(self.data_dir.join(dir)).ok();
            std::fs::create_dir_all(self.data_dir.join(dir)).ok();
        }
    }

    /// Server from a full user id, or the homeserver field.
    pub(crate) fn server_of(user: Option<&str>, homeserver: &Option<String>) -> Result<String> {
        if let Some(h) = given(homeserver) {
            return Ok(h.to_owned());
        }
        let user = user.unwrap_or("").trim();
        match user.strip_prefix('@').and_then(|u| u.split_once(':')) {
            Some((_, server)) if !server.is_empty() => Ok(server.to_owned()),
            _ => bail!("enter the full Matrix ID, like @name:server.org, or a homeserver"),
        }
    }

    pub(crate) async fn probe_server(&'static self, user: Option<String>, homeserver: Option<String>) -> Result<()> {
        let server = Self::server_of(user.as_deref(), &homeserver)?;
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
        let meta = client.oauth().server_metadata().await;
        // an account service: logins belong in the browser (single sign-on);
        // tchncs.de refused a correct password over m.login.password
        let account_service = meta.is_ok();
        // QR login needs the rendezvous of MSC4108 (tchncs.de has it,
        // matrix.org not yet)
        let qr = account_service
            && client.unstable_features().await
                .map(|f| f.iter().any(|x| x.as_str() == "org.matrix.msc4108"))
                .unwrap_or(false);
        let signup = match meta {
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
            "account_service": account_service,
            "qr_login": qr,
            "idps": idps,
            "sliding_sync": !matches!(client.sliding_sync_version(), Version::None),
        }));
        Ok(())
    }

    pub(crate) async fn login_password(&'static self, user: String, password: String, homeserver: Option<String>) -> Result<()> {
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        let result: Result<Client> = async {
            let server = Self::server_of(Some(&user), &homeserver)?;
            // before the store opens: this user's kept store, or a clean one
            let device = self.kept_device(Some(&user));
            tracing::info!(%server, "login: building the client");
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            tracing::info!("login: client built, sending the password");
            // the same device again, so the kept crypto store fits
            let mut login = client
                .matrix_auth()
                .login_username(user.trim(), &password)
                .initial_device_display_name(DEVICE_NAME)
                .request_refresh_token();
            if let Some(d) = device.as_deref() {
                login = login.device_id(d.trim());
            }
            login.send().await.map_err(|e| anyhow!("{e}"))?;
            std::fs::remove_file(self.data_dir.join(DEVICE_ID_FILE)).ok();
            std::fs::remove_file(self.relogin_path()).ok();
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

    pub(crate) async fn login_sso(&'static self, user: Option<String>, homeserver: Option<String>, idp: Option<String>) -> Result<()> {
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        // made before anything awaits, so a cancel from now on reaches it
        let cancelled = self.sso_cancel.notified();
        tokio::pin!(cancelled);
        let result: Result<Client> = async {
            let server = Self::server_of(user.as_deref(), &homeserver)?;
            // before the store opens: this user's kept store, or a clean one
            let device = self.kept_device(user.as_deref());
            tracing::info!(%server, "sso: building the client");
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            tracing::info!("sso: client built, asking for the sign-on page");
            let mut builder = client
                .matrix_auth()
                .login_sso(|url| async move {
                    tracing::info!("sso: sign-on page ready, waiting for the browser");
                    crate::instance().emit(json!({"ev": "open_url", "url": url}));
                    Ok(())
                })
                .initial_device_display_name(DEVICE_NAME)
                .request_refresh_token();
            if let Some(idp) = given(&idp) {
                builder = builder.identity_provider_id(idp);
            }
            if let Some(d) = device.as_deref() {
                builder = builder.device_id(d);
            }
            // the SDK waits for the browser's redirect without end; the
            // login page could only be left by restarting the app
            tokio::select! {
                sent = tokio::time::timeout(SSO_TIMEOUT, builder.send()) => match sent {
                    Ok(r) => {
                        r.map_err(|e| anyhow!("{e}"))?;
                    }
                    Err(_) => bail!("no login from the browser within {} minutes", SSO_TIMEOUT.as_secs() / 60),
                },
                _ = &mut cancelled => bail!("login cancelled"),
            }
            std::fs::remove_file(self.data_dir.join(DEVICE_ID_FILE)).ok();
            std::fs::remove_file(self.relogin_path()).ok();
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

    /// At the process's end: the login's tasks and the sync stop and the
    /// client goes, so nothing writes to the store once the bus name (the
    /// store's lock) is released. Blocks the caller up to `limit`.
    pub(crate) fn shutdown(&'static self, limit: Duration) {
        let done = self.rt.block_on(tokio::time::timeout(limit, async {
            let teardown = self.state.lock().await.take_login();
            // the tokens as they are now: a refresh that ended while this
            // process was leaving may not have reached session.json yet
            if let Some(client) = teardown.client.as_ref() {
                if client.session().is_some() {
                    if let Err(e) = self.save_session(client) {
                        tracing::warn!("shutdown: session not saved: {e}");
                    }
                }
            }
            // the sync stopped first, then the stores closed: running
            // writes finish, connections and file locks are let go
            if let Some(client) = teardown.stop().await {
                if let Err(e) = client.pause().await {
                    tracing::warn!("shutdown: stores not closed: {e}");
                }
            }
        }));
        if done.is_err() {
            tracing::warn!("shutdown: not finished in time");
        }
    }

    /// Logging in with a QR code (MSC4108): this device shows a code, a
    /// logged-in one (Element) scans it and shows two digits, which the
    /// user types here ("qr_check_code"); the account service then signs
    /// this device in and the other one hands over the keys, so it comes
    /// in verified. Progress goes out as "qr_login" (state: qr, scanned,
    /// waiting, syncing, done).
    pub(crate) async fn login_qr(&'static self, user: Option<String>, homeserver: Option<String>) -> Result<()> {
        use matrix_sdk::authentication::oauth::qrcode::{GeneratedQrProgress, LoginProgress};
        use matrix_sdk::authentication::oauth::registration::{ApplicationType, ClientMetadata, Localized, OAuthGrantType};
        use matrix_sdk::authentication::oauth::ClientRegistrationData;
        use matrix_sdk::reqwest::Url;
        use std::future::IntoFuture;
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        let cancelled = self.sso_cancel.notified();
        tokio::pin!(cancelled);
        let result: Result<Client> = async {
            let server = Self::server_of(user.as_deref(), &homeserver)?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            let mut meta = ClientMetadata::new(
                ApplicationType::Native,
                vec![OAuthGrantType::DeviceCode],
                Localized::new(Url::parse(CLIENT_URI)?, []),
            );
            meta.client_name = Some(Localized::new(DEVICE_NAME.to_owned(), []));
            let registration = ClientRegistrationData::new(matrix_sdk::ruma::serde::Raw::new(&meta)?);
            let oauth = client.oauth();
            let login = oauth.login_with_qr_code(Some(&registration)).generate();
            let mut progress = login.subscribe_to_progress();
            let done = login.into_future();
            tokio::pin!(done);
            let deadline = tokio::time::sleep(SSO_TIMEOUT);
            tokio::pin!(deadline);
            loop {
                tokio::select! {
                    r = &mut done => {
                        r.map_err(|e| anyhow!("{e}"))?;
                        break;
                    }
                    Some(p) = progress.next() => match p {
                        LoginProgress::EstablishingSecureChannel(GeneratedQrProgress::QrReady(data)) => {
                            let code = qrcode::QrCode::new(data.to_bytes())?;
                            let modules: String = code.to_colors().iter()
                                .map(|c| if *c == qrcode::Color::Dark { '1' } else { '0' }).collect();
                            self.emit(json!({"ev": "qr_login", "state": "qr", "width": code.width(), "modules": modules}));
                        }
                        LoginProgress::EstablishingSecureChannel(GeneratedQrProgress::QrScanned(sender)) => {
                            *self.qr_check.lock().await = Some(sender);
                            self.emit(json!({"ev": "qr_login", "state": "scanned"}));
                        }
                        LoginProgress::WaitingForToken { user_code } => {
                            self.emit(json!({"ev": "qr_login", "state": "waiting", "user_code": user_code}));
                        }
                        LoginProgress::SyncingSecrets => self.emit(json!({"ev": "qr_login", "state": "syncing"})),
                        LoginProgress::Done => self.emit(json!({"ev": "qr_login", "state": "done"})),
                        _ => {}
                    },
                    _ = &mut cancelled => bail!("login cancelled"),
                    _ = &mut deadline => bail!("no login from the other device within {} minutes", SSO_TIMEOUT.as_secs() / 60),
                }
            }
            std::fs::remove_file(self.data_dir.join(DEVICE_ID_FILE)).ok();
            std::fs::remove_file(self.relogin_path()).ok();
            Ok(client)
        }
        .await;
        self.qr_check.lock().await.take();
        match result {
            Ok(client) => self.after_login(client).await,
            Err(e) => {
                self.emit(json!({"ev": "login_error", "message": format!("{e:#}")}));
                Ok(())
            }
        }
    }

    /// The two digits the other device of a QR login shows.
    pub(crate) async fn qr_check_code(&self, code: u8) -> Result<()> {
        let sender = self.qr_check.lock().await.take().context("no QR login waits for a code")?;
        sender.send(code).await.map_err(|e| anyhow!("{e}"))?;
        Ok(())
    }

    /// Ends a single sign-on that waits for the browser ("login_error").
    pub(crate) fn login_cancel(&self) {
        self.sso_cancel.notify_waiters();
    }

    /// Logging in through the server's account service (OAuth 2.0, MAS):
    /// its own page in the browser, so the password is never typed into
    /// Rustrx. Rustrx registers itself with the service, opens the page, and
    /// takes the answer on a port of its own on 127.0.0.1. With `signup`
    /// the page offers to make an account first.
    pub(crate) async fn login_oauth(&'static self, user: Option<String>, homeserver: Option<String>, signup: bool) -> Result<()> {
        use matrix_sdk::authentication::oauth::registration::{ApplicationType, ClientMetadata, Localized, OAuthGrantType};
        use matrix_sdk::authentication::oauth::ClientRegistrationData;
        use matrix_sdk::reqwest::Url;
        use matrix_sdk::utils::local_server::{LocalServerBuilder, LocalServerIpAddress};
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        let cancelled = self.sso_cancel.notified();
        tokio::pin!(cancelled);
        let result: Result<Client> = async {
            let server = Self::server_of(user.as_deref(), &homeserver)?;
            let device = self.kept_device(user.as_deref());
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            let (redirect, answer) = LocalServerBuilder::new()
                .ip_address(LocalServerIpAddress::Localhostv4)
                .spawn()
                .await?;
            let mut meta = ClientMetadata::new(
                ApplicationType::Native,
                vec![OAuthGrantType::AuthorizationCode { redirect_uris: vec![redirect.clone()] }],
                Localized::new(Url::parse(CLIENT_URI)?, []),
            );
            meta.client_name = Some(Localized::new(DEVICE_NAME.to_owned(), []));
            let registration = ClientRegistrationData::new(matrix_sdk::ruma::serde::Raw::new(&meta)?);
            let device = device.map(|d| matrix_sdk::ruma::OwnedDeviceId::from(d.trim()));
            let mut login = client.oauth().login(redirect, device, Some(registration), None);
            if signup {
                login = login.prompt(vec![matrix_sdk::ruma::api::client::discovery::get_authorization_server_metadata::v1::Prompt::Create]);
            }
            let data = login.build().await?;
            tracing::info!("oauth: the account service's page ready, waiting for the browser");
            self.emit(json!({"ev": "open_url", "url": data.url.as_str()}));
            let query = tokio::select! {
                q = tokio::time::timeout(SSO_TIMEOUT, answer) => match q {
                    Ok(Some(q)) => q,
                    Ok(None) => bail!("the browser came back without an answer"),
                    Err(_) => {
                        client.oauth().abort_login(&data.state).await;
                        bail!("no login from the browser within {} minutes", SSO_TIMEOUT.as_secs() / 60)
                    }
                },
                _ = &mut cancelled => {
                    client.oauth().abort_login(&data.state).await;
                    bail!("login cancelled")
                }
            };
            client.oauth().finish_login(query.into()).await.map_err(|e| anyhow!("{e}"))?;
            std::fs::remove_file(self.data_dir.join(DEVICE_ID_FILE)).ok();
            std::fs::remove_file(self.relogin_path()).ok();
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
        // a call in progress is left while the client can still say so;
        // the teardown below only stops it, and the room would show this
        // device in the call until its membership expired
        self.call_end().await;
        // out of the state first: nothing below may hold the lock, or every
        // other command waits for the server (or its timeout) too
        let (teardown, generation) = {
            let mut st = self.state.lock().await;
            let teardown = st.take_login();
            (teardown, st.generation)
        };
        // the server's side, bounded: the SDK retries for up to 15 minutes
        if let Some(client) = teardown.stop().await {
            match tokio::time::timeout(LOGOUT_SERVER_TIMEOUT, client.logout()).await {
                Ok(Ok(())) => {}
                Ok(Err(e)) => tracing::warn!("logout: {e}"),
                Err(_) => tracing::warn!("logout: the server did not answer, logged out here only"),
            }
        }
        // a login (or another logout) since: its store and session are
        // not this logout's to remove
        if self.state.lock().await.generation != generation {
            tracing::warn!("logout: a newer login or logout came first, nothing removed");
            return Ok(());
        }
        std::fs::remove_file(self.session_path()).ok();
        self.wipe_local_data();
        self.emit(json!({"ev": "logged_out"}));
        Ok(())
    }
}
