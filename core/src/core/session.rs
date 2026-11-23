// SPDX-License-Identifier: GPL-3.0-or-later
//! Logging in and out, restoring and saving the session, the server probe.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

/// In the store: its to-device position was checked for one the classic
/// sync left (drop_classic_to_device_token).
const TODEVICE_CHECKED_MARKER: &str = "rustrx-todevice-checked";

/// The check is due: a crypto store without the marker.
fn todevice_check_needed(store: &std::path::Path) -> bool {
    store.join(CRYPTO_DB).exists() && !store.join(TODEVICE_CHECKED_MARKER).exists()
}

/// A classic sync ran on this store: checked again at the next sliding start.
fn todevice_check_rearm(store: &std::path::Path) {
    std::fs::remove_file(store.join(TODEVICE_CHECKED_MARKER)).ok();
}

/// How the store's key opens the store. Until 0.8.2 it went in as a
/// passphrase, and every open of each database ran PBKDF2 (200,000 rounds)
/// over it, 300 to 390 ms per start on the phones (CB3, ST5). The key is 32
/// random bytes from sailfish-secrets as 64 hex characters, which are also
/// valid unpadded base64 (48 bytes): the SDK's high-entropy route takes
/// those bytes, opens a database whose cipher is still under the PBKDF2 of
/// their base64 (the hex string again) one last time, and rewraps it under
/// the HKDF of the bytes in one row write; every later open is the HKDF.
/// Each database does this for itself at its own open, so a crash between
/// two leaves a mix that the next start opens and finishes.
/// Security: what encrypts the data are each database's own random
/// encryption key and MAC seed (XChaCha20-Poly1305 for the values, keyed
/// BLAKE3 for the lookup keys), unchanged. Only their wrapping changes:
/// PBKDF2 with a salt per database becomes HKDF-SHA256 without salt, so
/// the four databases (and every account, which share the key) are
/// wrapped under one key, each export with a fresh random 192-bit nonce.
/// Stretching adds nothing to 256 random bits. session.json's seal is an HKDF of the hex text itself,
/// so its wrapping key differs from the stores'.
/// A build before this one opens a rewrapped database with "KdfMismatch":
/// a failed restore (Retry), the store left alone; nothing in those builds
/// removes it on that error, and this build opens it again.
enum StoreSecret {
    /// the key's bytes as the SDK's high-entropy passphrase
    Key(Vec<u8>),
    /// a key of another shape (not from SecretStore): as before
    Passphrase(String),
}

impl StoreSecret {
    fn of(key: &str) -> StoreSecret {
        use matrix_sdk_base::crypto::vodozemac::{base64_decode, base64_encode};
        // only SecretStore's shape (256 random bits as hex) is known to be
        // random; the SDK turns the bytes back into the passphrase with
        // unpadded standard base64, so they must come back unchanged
        if key.len() != 64 || !key.bytes().all(|b| b.is_ascii_hexdigit()) {
            return StoreSecret::Passphrase(key.to_owned());
        }
        match base64_decode(key) {
            Ok(bytes) if base64_encode(&bytes) == key => StoreSecret::Key(bytes),
            _ => StoreSecret::Passphrase(key.to_owned()),
        }
    }
}

/// The SDK's low-memory settings, 500 KB of page cache per connection, and
/// STORE_POOL_SIZE connections per database (the low-memory default is one
/// per physical core, per database: 32 on an 8-core phone if all opened).
/// Every opener of the store goes through here, so all of them open it
/// the same way.
fn store_config(dir: &std::path::Path, secret: Option<&StoreSecret>) -> matrix_sdk::SqliteStoreConfig {
    let config = matrix_sdk::SqliteStoreConfig::with_low_memory_config(dir).pool_max_size(STORE_POOL_SIZE);
    match secret {
        Some(StoreSecret::Key(bytes)) => {
            config.high_entropy_passphrase(Some(bytes), matrix_sdk_sqlite::Base64Variant::Unpadded)
        }
        Some(StoreSecret::Passphrase(p)) => config.passphrase(Some(p)),
        None => config,
    }
}

/// The store's four databases (the builder opens them all).
const STORE_DBS: [&str; 4] = [matrix_sdk::STATE_STORE_DATABASE_NAME, EVENT_CACHE_DB, MEDIA_DB, CRYPTO_DB];

/// The databases opened key-based that have no marker yet, with whether
/// each was there before (rewrapped) or not (made key-based).
fn key_based_unmarked(store: &std::path::Path, dbs: &[&'static str]) -> Vec<(&'static str, bool)> {
    dbs.iter()
        .filter(|db| !store.join(format!("{KEY_BASED_MARKER}{db}")).exists())
        .map(|db| (*db, store.join(db).exists()))
        .collect()
}

/// After an open succeeded: each of `dbs` is key-based now. The markers
/// only record that (and log it once); the open never depends on them, as
/// the high-entropy route handles either state.
fn mark_key_based(store: &std::path::Path, dbs: &[(&'static str, bool)]) {
    for (db, existed) in dbs {
        if !store.join(db).exists() {
            continue;
        }
        if std::fs::write(store.join(format!("{KEY_BASED_MARKER}{db}")), b"").is_ok() {
            if *existed {
                tracing::info!("store: key migrated, {db} wrapped under the key, no more PBKDF2");
            } else {
                tracing::info!("store: {db} made key-based");
            }
        }
    }
}

/// The SDK's media cache had no limit (its default policy is empty): it
/// grows to MEDIA_CACHE_MAX_BYTES, the longest unused go first, and the
/// rest of the policy is the SDK's own default (files over 20 MiB are not
/// cached, none kept unused for 60 days, a clean-up a day at the next
/// media call). Uploads waiting in the send queue are kept whatever the
/// policy (IgnoreMediaRetentionPolicy::Yes, matrix-sdk send_queue/upload.rs).
/// The policy is kept in the media database: written only when it differs.
async fn limit_media_cache(client: &Client) {
    let policy = matrix_sdk::media::MediaRetentionPolicy::new().with_max_cache_size(Some(MEDIA_CACHE_MAX_BYTES));
    let media = client.media();
    if media.media_retention_policy().await.ok() == Some(policy) {
        return;
    }
    match media.set_media_retention_policy(policy).await {
        Ok(()) => tracing::info!(max = MEDIA_CACHE_MAX_BYTES, "media cache: limit set"),
        Err(e) => tracing::warn!("media cache: limit not set: {e}"),
    }
}

/// The fingerprint of builds before 0.8.2, in ENCRYPTED_MARKER: std's
/// DefaultHasher, not stable across Rust releases.
fn legacy_fingerprint(key: &str) -> String {
    use std::hash::{Hash, Hasher};
    let mut h = std::collections::hash_map::DefaultHasher::new();
    key.hash(&mut h);
    format!("{:016x}", h.finish())
}

/// The stable fingerprint (KEY_FINGERPRINT_FILE): FNV-1a 64 of the key,
/// fixed here, so no toolchain changes it. It tells one key from another;
/// 64 bits of hash cannot give back a 256-bit key.
fn stable_fingerprint(key: &str) -> String {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for b in b"rustrx-store-key\0".iter().chain(key.as_bytes()) {
        h ^= u64::from(*b);
        h = h.wrapping_mul(0x0000_0100_0000_01b3);
    }
    format!("fnv1a64:{h:016x}")
}

/// What the store's marks say of the key.
#[derive(Debug, PartialEq)]
enum KeyCheck {
    /// the stable fingerprint fits
    Fits,
    /// no stable one yet, the old one fits
    OldFits,
    /// no well-formed stable one (none, cut short, of a form this build
    /// does not know), and the old one does not fit: not a proof, the old
    /// hash may have changed
    Unproven,
    /// the stable fingerprint, well-formed, is another key's (still
    /// proven by the open before the store goes: prove_store_key)
    Lost,
}

impl KeyCheck {
    fn of(store: &std::path::Path, old_mark: &str, key: &str) -> KeyCheck {
        if let Some(stable) = read_stable_fingerprint(store) {
            return if stable == stable_fingerprint(key) { KeyCheck::Fits } else { KeyCheck::Lost };
        }
        if old_mark.trim() == legacy_fingerprint(key) {
            KeyCheck::OldFits
        } else {
            KeyCheck::Unproven
        }
    }
}

/// The store's cipher did not decrypt under the key: its AEAD tag proves
/// another key (not a busy or broken database, nor an unknown format).
fn key_refused(e: &matrix_sdk_sqlite::OpenStoreError) -> bool {
    matches!(e, matrix_sdk_sqlite::OpenStoreError::InitCipher(matrix_sdk_store_encryption::Error::Encryption(_)))
}

/// The stable fingerprint when the file holds one in this build's form
/// (the prefix and 16 hex digits); anything else counts as none.
fn read_stable_fingerprint(store: &std::path::Path) -> Option<String> {
    let raw = std::fs::read_to_string(store.join(KEY_FINGERPRINT_FILE)).ok()?;
    let mark = raw.trim();
    let digits = mark.strip_prefix("fnv1a64:")?;
    (digits.len() == 16 && digits.bytes().all(|b| b.is_ascii_hexdigit())).then(|| mark.to_owned())
}

/// After the store opened with `key`: the key is the store's, and the
/// stable fingerprint is written where it does not say so (written whole
/// or not at all: a temporary file, then renamed).
fn confirm_store_key(store: &std::path::Path, key: &str) {
    if !store.join(ENCRYPTED_MARKER).exists() || read_stable_fingerprint(store).as_deref() == Some(&stable_fingerprint(key)) {
        return;
    }
    let tmp = store.join(format!("{KEY_FINGERPRINT_FILE}.tmp"));
    if std::fs::write(&tmp, stable_fingerprint(key)).is_ok() && std::fs::rename(&tmp, store.join(KEY_FINGERPRINT_FILE)).is_ok() {
        tracing::info!("store: the key opened it, stable fingerprint written");
    }
}

/// For a store whose stable fingerprint does not say the key fits
/// (KeyCheck other than Fits): one database opened with the secret
/// settles it. It opens: the key is the store's, the stable fingerprint
/// is written. Its cipher's seal (AEAD) refuses the key: proof of another
/// key, the store goes and false is returned (the caller asks for the
/// secret again, which makes a new store). Any other failure is an error
/// and removes nothing. No fingerprint alone ever removes a store.
async fn prove_store_key(dir: &std::path::Path, secret: &StoreSecret, key: &str) -> Result<bool> {
    if !dir.join(ENCRYPTED_MARKER).exists() || read_stable_fingerprint(dir).as_deref() == Some(&stable_fingerprint(key)) {
        return Ok(true);
    }
    let config = store_config(dir, Some(secret));
    let opened = if dir.join(matrix_sdk::STATE_STORE_DATABASE_NAME).exists() {
        matrix_sdk::SqliteStateStore::open_with_config(&config).await.map(drop)
    } else if dir.join(CRYPTO_DB).exists() {
        matrix_sdk::SqliteCryptoStore::open_with_config(&config).await.map(drop)
    } else {
        Ok(())
    };
    match opened {
        Ok(()) => {
            confirm_store_key(dir, key);
            Ok(true)
        }
        Err(e) if key_refused(&e) => {
            tracing::warn!("store: encrypted with a key that is gone (its seal refuses this one); it cannot be read and goes");
            std::fs::remove_dir_all(dir).ok();
            Ok(false)
        }
        Err(e) => Err(anyhow!("the store did not open: {e}")),
    }
}

/// One login at a time: held from a login's start to its end (after_login
/// included), let go on every way out.
pub(crate) struct LoginGuard(&'static Core);

impl Drop for LoginGuard {
    fn drop(&mut self) {
        self.0.login_busy.store(false, std::sync::atomic::Ordering::Release);
    }
}

impl Core {
    /// The login's guard, or None when another login is running.
    pub(crate) fn begin_login(&'static self) -> Option<LoginGuard> {
        use std::sync::atomic::Ordering;
        self.login_busy.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).ok().map(|_| LoginGuard(self))
    }

    /// The guard for a login that may start; when another runs, this one
    /// is refused with "login_error" (two at once wrote one store from two
    /// clients, and the later after_login replaced the first's client)
    pub(crate) fn login_or_refuse(&'static self) -> Option<LoginGuard> {
        let guard = self.begin_login();
        if guard.is_none() {
            tracing::info!("login: another login is running, refused");
            self.emit(json!({"ev": "login_error", "message": "another login is still running"}));
        }
        guard
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
        // before the client and its store: a session that cannot be
        // unsealed is an error here, and the store is left alone
        let Some(saved) = self.load_session()? else { return Ok(false) };
        let t = std::time::Instant::now();
        if saved.sliding_sync != Some(false) {
            self.drop_classic_to_device_token().await;
        }
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

    /// Both syncs keep their to-device position in one place in the crypto
    /// store, and sliding sync sends it on: after the classic sync (the
    /// Classic sync switch, then back) Synapse refused it ("should look like
    /// an int"), and the sync service retried at once, every 18 ms, for
    /// hours (the Prawn, 2026-11-21). A position that is not a number is
    /// dropped before the client opens the store; sliding sync then starts
    /// its to-device stream afresh, and the server sends what is left.
    /// Checked once per store (CB7, 0.8.2): the marker says so, and goes
    /// whenever a classic sync runs on the store (after_login), so the
    /// next sliding start looks again. A store from a build before the
    /// marker is checked once, then no more.
    async fn drop_classic_to_device_token(&self) {
        use matrix_sdk_base::crypto::store::CryptoStore;
        let dir = self.data_dir.join(STORE_DIR);
        if !todevice_check_needed(&dir) {
            return;
        }
        let secret = match self.store_secret() {
            Ok(s) => s,
            Err(_) => return, // the client's build says why
        };
        let unmarked = if secret.is_some() { key_based_unmarked(&dir, &[CRYPTO_DB]) } else { Vec::new() };
        let config = store_config(&dir, secret.as_ref());
        let store = match matrix_sdk::SqliteCryptoStore::open_with_config(&config).await {
            Ok(s) => {
                if let (true, Some(key)) = (secret.is_some(), crate::store_key()) {
                    confirm_store_key(&dir, key);
                }
                mark_key_based(&dir, &unmarked);
                s
            }
            Err(e) => {
                tracing::warn!("to-device position: the store did not open: {e}");
                return;
            }
        };
        let done = match store.next_batch_token().await {
            Ok(Some(token)) if !token.is_empty() && !token.bytes().all(|b| b.is_ascii_digit()) => {
                match store.remove_custom_value(NEXT_BATCH_TOKEN_KEY).await {
                    Ok(()) => {
                        tracing::info!("to-device position from the classic sync dropped for sliding sync");
                        true
                    }
                    Err(e) => {
                        tracing::warn!("to-device position not dropped: {e}");
                        false
                    }
                }
            }
            Ok(_) => true,
            Err(e) => {
                tracing::warn!("to-device position not read: {e}");
                false
            }
        };
        // only after the store opened and the position was read (or
        // dropped): the store is there, so the file cannot make an empty
        // folder look like an old unencrypted store (store_passphrase)
        if done {
            std::fs::write(dir.join(TODEVICE_CHECKED_MARKER), b"").ok();
        }
    }

    /// The store's secret (store_passphrase), in the form the store opens with.
    fn store_secret(&self) -> Result<Option<StoreSecret>> {
        Ok(self.store_passphrase()?.as_deref().map(StoreSecret::of))
    }

    pub(crate) async fn build_client(&self, server: &str, with_store: bool) -> Result<Client> {
        self.build_client_with(server, with_store, None).await
    }

    /// The passphrase for the store, if it is (to be) encrypted. A new store
    /// is encrypted when sailfish-secrets gave a key; files in it mark it,
    /// with fingerprints of the key (hashes; the key cannot be read from
    /// them). A store from before stays as it is.
    /// - no key now (sailfish-secrets locked before the first unlock, or not
    ///   reachable): nothing is removed; the store cannot open this time
    /// - a key that is not the store's (the secret was lost and made anew):
    ///   the store can never be read again and goes, but only when a
    ///   database's seal refuses the key (prove_store_key, before the
    ///   client is built); the fingerprints only say whether to look. The
    ///   stable one is KEY_FINGERPRINT_FILE; the old one in
    ///   ENCRYPTED_MARKER is std's DefaultHasher, which Rust may change with
    ///   any toolchain: a match there is taken and the stable one written; a
    ///   mismatch there proves nothing either.
    ///   ENCRYPTED_MARKER keeps the old form: builds before 0.8.2 remove the
    ///   store when that file is anything else.
    fn store_passphrase(&self) -> Result<Option<String>> {
        let dir = self.data_dir.join(STORE_DIR);
        let marker = dir.join(ENCRYPTED_MARKER);
        let existing = std::fs::read_dir(&dir).map(|mut d| d.next().is_some()).unwrap_or(false);
        let key = crate::store_key();
        if existing {
            let Ok(mark) = std::fs::read_to_string(&marker) else {
                tracing::info!("store: unencrypted (made before sailfish-secrets was used)");
                return Ok(None);
            };
            let Some(key) = key else {
                bail!("the encrypted store's key is not available (sailfish-secrets locked or unreachable)");
            };
            match KeyCheck::of(&dir, &mark, key) {
                KeyCheck::Fits => tracing::info!("store: encrypted, key from sailfish-secrets"),
                KeyCheck::OldFits => {
                    tracing::info!("store: encrypted, key from sailfish-secrets (old fingerprint)");
                    confirm_store_key(&dir, key);
                }
                KeyCheck::Unproven => {
                    tracing::warn!("store: encrypted, its old fingerprint does not fit (another toolchain's hash?); opened with the key, nothing removed");
                }
                KeyCheck::Lost => {
                    tracing::warn!("store: encrypted, its fingerprint is another key's; the open proves it (prove_store_key)");
                }
            }
            return Ok(Some(key.to_owned()));
        }
        let Some(key) = key else { return Ok(None) };
        Ok(Some(self.new_encrypted_store(&dir, key)))
    }

    /// Marks a new store as encrypted with `key`; its passphrase.
    fn new_encrypted_store(&self, dir: &std::path::Path, key: &str) -> String {
        std::fs::create_dir_all(dir).ok();
        std::fs::write(dir.join(ENCRYPTED_MARKER), legacy_fingerprint(key)).ok();
        confirm_store_key(dir, key);
        tracing::info!("store: new and encrypted, key from sailfish-secrets");
        key.to_owned()
    }

    /// How the local store is kept, for the account's security status:
    /// "encrypted" (key from sailfish-secrets), "unencrypted" (made before
    /// sailfish-secrets was used; a new login encrypts it) or "no_secrets"
    /// (no key to be had on this phone). Checked before the key: an old
    /// plain store on a phone without a key is "no_secrets", not fixable.
    pub(crate) fn store_state(&self) -> &'static str {
        if crate::store_key().is_none() {
            return "no_secrets";
        }
        let dir = self.data_dir.join(STORE_DIR);
        let existing = std::fs::read_dir(&dir).map(|mut d| d.next().is_some()).unwrap_or(false);
        if existing && !dir.join(ENCRYPTED_MARKER).exists() {
            "unencrypted"
        } else {
            "encrypted"
        }
    }

    /// `known`: the sync flavour from a previous login, which skips discovery.
    pub(crate) async fn build_client_with(&self, server: &str, with_store: bool, known: Option<bool>) -> Result<Client> {
        tracing::info!(%server, with_store, ?known, "client: building");
        let mut secret = if with_store { self.store_secret()? } else { None };
        let store_dir = self.data_dir.join(STORE_DIR);
        if let (Some(s), Some(key)) = (secret.as_ref(), crate::store_key()) {
            if !prove_store_key(&store_dir, s, key).await? {
                secret = self.store_secret()?;
            }
        }
        let unmarked = if secret.is_some() { key_based_unmarked(&store_dir, &STORE_DBS) } else { Vec::new() };
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
                b = b.sqlite_store_with_config_and_cache_path(store_config(&store_dir, secret.as_ref()), None::<&std::path::Path>);
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
        if with_store {
            // the builder opened all four databases with the secret
            if let (true, Some(key)) = (secret.is_some(), crate::store_key()) {
                confirm_store_key(&store_dir, key);
            }
            mark_key_based(&store_dir, &unmarked);
            limit_media_cache(&client).await;
        }
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
        // the tokens: sealed with the store's key where there is one,
        // owner-only, complete or absent (session_file.rs)
        self.write_session(&saved)
    }

    pub(crate) async fn after_login(&'static self, client: Client) -> Result<()> {
        self.save_session(&client)?;
        // a classic sync keeps a to-device position sliding sync refuses:
        // the next sliding start checks the store again
        if matches!(client.sliding_sync_version(), Version::None) {
            todevice_check_rearm(&self.data_dir.join(STORE_DIR));
        }
        // offline until the UI says otherwise ("Show me as online"): the
        // syncs carry it, and none has run yet
        client.set_presence(matrix_sdk::ruma::presence::PresenceState::Offline, None, false).await.ok();
        let user_id = client.user_id().map(|u| u.to_string());
        let device_id = client.device_id().map(|d| d.to_string());
        self.emit(json!({
            "ev": "logged_in",
            "proto": PROTO,
            "user_id": user_id,
            "device_id": device_id,
            "homeserver": client.homeserver().to_string(),
            "sliding_sync": !matches!(client.sliding_sync_version(), Version::None),
            "store": self.store_state(),
        }));
        self.install_handlers(&client);
        self.install_call_handlers(&client);
        self.install_join_handler(&client);
        self.report_server_features(client.clone());
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
        let rules = self.rt.spawn(self.account_notifications_task(client.clone()));
        {
            let mut st = self.state.lock().await;
            if st.generation != generation {
                // ended already (an expired session's first request)
                for t in [watch, rooms, sends, rules] {
                    t.abort();
                }
                return Ok(());
            }
            st.login_tasks.retain(|t| !t.is_finished());
            st.login_tasks.extend([watch, rooms, sends, rules]);
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
    pub(crate) fn relogin_path(&self) -> PathBuf {
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
        self.remove_session();
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
        // a server that delegates its logins to an account service (MAS, or
        // Synapse's delegated auth with another OpenID provider) may have no
        // /login at all: M_UNRECOGNIZED there means "the browser only", not
        // a server that cannot be used (doctorflynt's Casdoor, 0.5.0)
        let meta = client.oauth().server_metadata().await;
        if let Err(e) = &meta {
            tracing::info!("probe: no account service ({e})");
        }
        let flows = match client.matrix_auth().get_login_types().await {
            Ok(types) => types.flows,
            Err(e) if meta.is_ok() => {
                tracing::info!("probe: no /login beside the account service ({e})");
                Vec::new()
            }
            Err(e) => return Err(e.into()),
        };
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
        // new people up in the browser; the password login works afterwards.
        // An account service: logins belong in the browser (single sign-on);
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
        let Some(_login) = self.login_or_refuse() else { return Ok(()) };
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
        let Some(_login) = self.login_or_refuse() else { return Ok(()) };
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
                .login_sso(move |url| async move {
                    tracing::info!("sso: sign-on page ready, waiting for the browser");
                    self.emit(json!({"ev": "open_url", "url": url}));
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
    /// The timeout is made inside the runtime: built on the caller's thread
    /// (Qt's main thread, no runtime there) its timer panicked, the panic
    /// could not leave the extern "C" fn, and every quit since 0.4.7 ended
    /// in abort() before any of this ran.
    /// In this order, all within `limit`: the running commands are aborted
    /// (none may write after the stores closed, nor hold the state's lock),
    /// the login's tasks and the sync stop, the session is saved, the
    /// client pauses (its stores close), and last the runtime stops, so no
    /// tokio thread is left running into the process's exit handlers.
    pub(crate) fn shutdown(&'static self, limit: Duration) {
        let start = std::time::Instant::now();
        let Some(runtime) = self.runtime.lock().ok().and_then(|mut r| r.take()) else {
            return; // stopped already
        };
        // the runtime's own stop gets what the rest leaves, and at least this
        let reserve = (limit / 5).min(SHUTDOWN_RUNTIME_RESERVE);
        let commands = self.take_commands();
        let done = self.rt.block_on(async { tokio::time::timeout(limit.saturating_sub(reserve), async {
            for t in &commands {
                t.abort();
            }
            for t in commands {
                let _ = t.await;
            }
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
        })
        .await });
        if done.is_err() {
            tracing::warn!("shutdown: not finished in time");
        }
        // what is left of the budget: tasks still running are dropped at
        // their next wait, blocking ones are not waited for longer
        runtime.shutdown_timeout(limit.saturating_sub(start.elapsed()));
    }

    /// Logging in with a QR code (MSC4108): this device shows a code, a
    /// logged-in one (Element) scans it and shows two digits, which the
    /// user types here ("qr_check_code"); the account service then signs
    /// this device in and the other one hands over the keys, so it comes
    /// in verified. Progress goes out as "qr_login" (state: qr, scanned,
    /// waiting, syncing, done).
    pub(crate) async fn login_qr(&'static self, user: Option<String>, homeserver: Option<String>) -> Result<()> {
        use matrix_sdk::authentication::oauth::qrcode::{GeneratedQrProgress, LoginProgress};
        use std::future::IntoFuture;
        let Some(_login) = self.login_or_refuse() else { return Ok(()) };
        if self.state.lock().await.client.is_some() {
            bail!("already logged in");
        }
        let cancelled = self.sso_cancel.notified();
        tokio::pin!(cancelled);
        let result: Result<Client> = async {
            let server = Self::server_of(user.as_deref(), &homeserver)?;
            self.fresh_store_for_qr()?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            let registration = super::qrlink::device_code_registration()?;
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
                        // the keys are in and the device is verified; the
                        // SDK's own Done comes with the end and was lost
                        self.qr_step(json!({"ev": "qr_login", "state": "done"}));
                        break;
                    }
                    Some(p) = progress.next() => match p {
                        LoginProgress::EstablishingSecureChannel(GeneratedQrProgress::QrReady(data)) => {
                            let mut ev = super::qrlink::qr_modules(&data)?;
                            ev["ev"] = json!("qr_login");
                            ev["state"] = json!("qr");
                            self.qr_step(ev);
                        }
                        LoginProgress::EstablishingSecureChannel(GeneratedQrProgress::QrScanned(sender)) => {
                            *self.qr_check.lock().await = Some(sender);
                            self.qr_step(json!({"ev": "qr_login", "state": "scanned"}));
                        }
                        LoginProgress::WaitingForToken { user_code } => {
                            self.qr_step(json!({"ev": "qr_login", "state": "waiting", "user_code": user_code}));
                        }
                        LoginProgress::SyncingSecrets => self.qr_step(json!({"ev": "qr_login", "state": "syncing"})),
                        LoginProgress::Done => self.qr_step(json!({"ev": "qr_login", "state": "done"})),
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
        let Some(_login) = self.login_or_refuse() else { return Ok(()) };
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
            // registered without the port: loopback ports are free for every
            // login (RFC 8252 §7.3), and some services refuse one with a port
            // ("invalid_client_metadata"); the browser still comes back to it
            let mut registered = redirect.clone();
            registered.set_port(None).map_err(|_| anyhow!("redirect {redirect} has no port to drop"))?;
            let mut meta = ClientMetadata::new(
                ApplicationType::Native,
                vec![OAuthGrantType::AuthorizationCode { redirect_uris: vec![registered] }],
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

    /// The classic /sync for this account instead of sliding sync, or back
    /// to asking the server (`classic` false). For servers that advertise
    /// sliding sync and send only part of the rooms through it (Beeper: one
    /// room, dodoradio's log); that cannot be told from outside, so it is a
    /// switch. The session stops and is restored from the same store with
    /// the other flavour: no logout, the rooms come again ("logged_in",
    /// then "rooms").
    pub(crate) async fn set_classic_sync(&'static self, classic: bool) -> Result<()> {
        let Some(mut saved) = self.load_session()? else { bail!("not logged in") };
        tracing::info!(classic, "sync: switching the flavour, restoring the session");
        let teardown = self.state.lock().await.take_login();
        teardown.stop().await;
        saved.sliding_sync = if classic { Some(false) } else { None };
        self.write_session(&saved)?;
        if !self.restore().await? {
            bail!("the session could not be restored");
        }
        Ok(())
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
            if let Some(own) = st.client.as_ref().and_then(|c| c.user_id()) {
                crate::core::state_reactions::SharedStateReactions::forget_account(own);
            }
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
        self.remove_session();
        self.wipe_local_data();
        self.emit(json!({"ev": "logged_out"}));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Every opener of the store gets STORE_POOL_SIZE connections per
    /// database (one writer, two readers), not one per core.
    /// Planted-bug: drop `.pool_max_size(STORE_POOL_SIZE)`.
    #[test]
    fn the_store_pool_is_small() {
        let config = format!("{:?}", store_config(std::path::Path::new("/nowhere"), None));
        assert!(config.contains(&format!("max_size: {STORE_POOL_SIZE},")), "{config}");
    }

    // Planted-bug: todevice_check_needed ignores the marker (always true),
    // or todevice_check_rearm removes nothing.
    #[test]
    fn todevice_check_once_until_classic() {
        let d = std::env::temp_dir().join(format!("rustrx-todevice-test-{}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(&d).unwrap();
        let store = d.as_path();
        assert!(!todevice_check_needed(store), "no crypto store, nothing to check");
        std::fs::write(store.join(CRYPTO_DB), b"").unwrap();
        assert!(todevice_check_needed(store), "a store from before the marker is checked");
        std::fs::write(store.join(TODEVICE_CHECKED_MARKER), b"").unwrap();
        assert!(!todevice_check_needed(store), "checked once");
        todevice_check_rearm(store);
        assert!(todevice_check_needed(store), "a classic sync ran: checked again");
        // re-arming without a marker is harmless
        todevice_check_rearm(store);
        assert!(todevice_check_needed(store));
        std::fs::remove_dir_all(&d).ok();
    }

    const HEX_KEY: &str = "00112233445566778899aabbccddeeff0123456789abcdeffedcba9876543210";

    fn temp_store(name: &str) -> std::path::PathBuf {
        let d = std::env::temp_dir().join(format!("rustrx-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    // Planted-bug: StoreSecret::of takes every key as Key (a key the SDK
    // would base64 into another passphrase), or none.
    #[test]
    fn store_secret_shape() {
        use matrix_sdk_base::crypto::vodozemac::base64_encode;
        let StoreSecret::Key(bytes) = StoreSecret::of(HEX_KEY) else { panic!("hex from SecretStore is a key") };
        assert_eq!(bytes.len(), 48);
        assert_eq!(base64_encode(&bytes), HEX_KEY, "the SDK's passphrase is the hex again");
        let upper = HEX_KEY.to_uppercase();
        assert!(matches!(StoreSecret::of(&upper), StoreSecret::Key(ref b) if base64_encode(b) == upper));
        // shapes that would not come back unchanged stay passphrases
        let short = &HEX_KEY[..60];
        let not_hex = HEX_KEY.replace('0', "g");
        for other in ["", "abc", "not-base64!", short, not_hex.as_str()] {
            assert!(matches!(StoreSecret::of(other), StoreSecret::Passphrase(ref p) if p == other), "{other:?}");
        }
    }

    // A store made with the passphrase (every build before 0.8.2), two of
    // its databases rewrapped (a crash after them), then all four: each
    // opens whatever state it is in, the data stays readable, and the
    // passphrase no longer opens it (an older build: KdfMismatch, the store
    // left as it is). Planted-bug: store_config gives the Key secret as
    // .key(bytes) (the PBKDF2 store does not open), or as .passphrase.
    #[test]
    fn store_key_migration() {
        use matrix_sdk::{SqliteCryptoStore, SqliteEventCacheStore, SqliteMediaStore, SqliteStateStore};
        use matrix_sdk_base::crypto::store::CryptoStore;
        use matrix_sdk_base::StateStore;
        let dir = temp_store("keymig");
        let rt = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
        rt.block_on(async {
            let old = matrix_sdk::SqliteStoreConfig::with_low_memory_config(&dir).passphrase(Some(HEX_KEY));
            let t = std::time::Instant::now();
            let crypto = SqliteCryptoStore::open_with_config(&old).await.unwrap();
            crypto.set_custom_value("rustrx-test", b"crypto".to_vec()).await.unwrap();
            drop(crypto);
            let state = SqliteStateStore::open_with_config(&old).await.unwrap();
            state.set_custom_value(b"rustrx-test", b"state".to_vec()).await.unwrap();
            drop(state);
            drop(SqliteEventCacheStore::open_with_config(&old).await.unwrap());
            drop(SqliteMediaStore::open_with_config(&old).await.unwrap());
            eprintln!("four PBKDF2 opens: {} ms", t.elapsed().as_millis());
            for db in STORE_DBS {
                assert!(dir.join(db).exists(), "{db} made");
            }

            let secret = StoreSecret::of(HEX_KEY);
            let new = store_config(&dir, Some(&secret));
            // the crash in between: only crypto and state rewrapped
            let unmarked = key_based_unmarked(&dir, &[CRYPTO_DB, matrix_sdk::STATE_STORE_DATABASE_NAME]);
            assert_eq!(unmarked.len(), 2);
            assert!(unmarked.iter().all(|(_, existed)| *existed));
            drop(SqliteCryptoStore::open_with_config(&new).await.unwrap());
            drop(SqliteStateStore::open_with_config(&new).await.unwrap());
            mark_key_based(&dir, &unmarked);
            // the next start: two rewrapped, two still under the PBKDF2
            let unmarked = key_based_unmarked(&dir, &STORE_DBS);
            assert_eq!(unmarked.iter().map(|(db, _)| *db).collect::<Vec<_>>(), vec![EVENT_CACHE_DB, MEDIA_DB]);
            let t = std::time::Instant::now();
            let crypto = SqliteCryptoStore::open_with_config(&new).await.unwrap();
            let state = SqliteStateStore::open_with_config(&new).await.unwrap();
            drop(SqliteEventCacheStore::open_with_config(&new).await.unwrap());
            drop(SqliteMediaStore::open_with_config(&new).await.unwrap());
            eprintln!("mixed state, two rewrapped now: {} ms", t.elapsed().as_millis());
            mark_key_based(&dir, &unmarked);
            assert!(key_based_unmarked(&dir, &STORE_DBS).is_empty(), "all four marked");
            assert_eq!(crypto.get_custom_value("rustrx-test").await.unwrap().as_deref(), Some(&b"crypto"[..]));
            assert_eq!(state.get_custom_value(b"rustrx-test").await.unwrap().as_deref(), Some(&b"state"[..]));
            drop(crypto);
            drop(state);

            // every later open: no derivation, the same data
            let t = std::time::Instant::now();
            let crypto = SqliteCryptoStore::open_with_config(&new).await.unwrap();
            drop(SqliteStateStore::open_with_config(&new).await.unwrap());
            drop(SqliteEventCacheStore::open_with_config(&new).await.unwrap());
            drop(SqliteMediaStore::open_with_config(&new).await.unwrap());
            eprintln!("four key-based opens: {} ms", t.elapsed().as_millis());
            assert_eq!(crypto.get_custom_value("rustrx-test").await.unwrap().as_deref(), Some(&b"crypto"[..]));
            drop(crypto);

            // an older build (the passphrase): refused, nothing changed
            assert!(SqliteCryptoStore::open_with_config(&old).await.is_err());
            assert!(SqliteMediaStore::open_with_config(&old).await.is_err());
            let crypto = SqliteCryptoStore::open_with_config(&new).await.unwrap();
            assert_eq!(crypto.get_custom_value("rustrx-test").await.unwrap().as_deref(), Some(&b"crypto"[..]));
            // a wrong key opens nothing, and its refusal is the proof
            // prove_store_key removes a store on
            let other = StoreSecret::of("ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100");
            let Err(e) = SqliteCryptoStore::open_with_config(&store_config(&dir, Some(&other))).await else { panic!("opened") };
            assert!(key_refused(&e), "{e}");
            let wrong_old = matrix_sdk::SqliteStoreConfig::with_low_memory_config(&dir).passphrase(Some("ffee"));
            let Err(e) = SqliteCryptoStore::open_with_config(&wrong_old).await else { panic!("opened") };
            assert!(!key_refused(&e), "KdfMismatch (an older build's way) is no proof: {e}");
        });
        std::fs::remove_dir_all(&dir).ok();
    }

    // Planted-bug: stable_fingerprint changes (every store's stable mark
    // stops fitting and the store goes), KeyCheck::of reads an old
    // mismatch or an unknown form as Lost.
    #[test]
    fn store_key_fingerprints() {
        assert_eq!(stable_fingerprint(HEX_KEY), "fnv1a64:65c236ea15dec421", "fixed for good");
        assert_ne!(stable_fingerprint(HEX_KEY), stable_fingerprint(&HEX_KEY.replace('0', "1")));
        let dir = temp_store("keyfp");
        let old = legacy_fingerprint(HEX_KEY);
        assert_eq!(KeyCheck::of(&dir, &old, HEX_KEY), KeyCheck::OldFits);
        assert_eq!(KeyCheck::of(&dir, &format!("{old}\n"), HEX_KEY), KeyCheck::OldFits);
        // another toolchain's DefaultHasher: no proof either way
        assert_eq!(KeyCheck::of(&dir, "0123456789abcdef", HEX_KEY), KeyCheck::Unproven);
        std::fs::write(dir.join(KEY_FINGERPRINT_FILE), "sha3:whatever").unwrap();
        assert_eq!(KeyCheck::of(&dir, "0123456789abcdef", HEX_KEY), KeyCheck::Unproven, "an unknown form is no proof");
        std::fs::write(dir.join(KEY_FINGERPRINT_FILE), stable_fingerprint(HEX_KEY)).unwrap();
        assert_eq!(KeyCheck::of(&dir, "0123456789abcdef", HEX_KEY), KeyCheck::Fits, "the stable one decides");
        std::fs::write(dir.join(KEY_FINGERPRINT_FILE), stable_fingerprint("another key")).unwrap();
        assert_eq!(KeyCheck::of(&dir, &old, HEX_KEY), KeyCheck::Lost, "the same form, another key");
        std::fs::remove_dir_all(&dir).ok();
    }

    // The one way a store goes: a database's seal refusing the key.
    // Planted-bug: prove_store_key removes on any open error, or on a
    // fingerprint alone; confirm_store_key keeps a cut-short file.
    #[test]
    fn store_key_proof() {
        use matrix_sdk::SqliteStateStore;
        let rt = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
        let secret = StoreSecret::of(HEX_KEY);
        let other_key = "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100";
        async fn make(name: &str, secret: &StoreSecret) -> std::path::PathBuf {
            let dir = temp_store(name);
            std::fs::write(dir.join(ENCRYPTED_MARKER), "0123456789abcdef").unwrap(); // another toolchain's hash
            drop(SqliteStateStore::open_with_config(&store_config(&dir, Some(secret))).await.unwrap());
            dir
        }
        rt.block_on(async {
            // the right key, no stable fingerprint: kept, the stable one written
            let dir = make("proof-right", &secret).await;
            assert!(prove_store_key(&dir, &secret, HEX_KEY).await.unwrap());
            assert_eq!(read_stable_fingerprint(&dir), Some(stable_fingerprint(HEX_KEY)));
            assert!(dir.join(matrix_sdk::STATE_STORE_DATABASE_NAME).exists());
            // a cut-short or empty stable file: no proof either way, rewritten
            for bad in ["fnv1a64:65c2", "", "fnv1a64:zzzzzzzzzzzzzzzz"] {
                std::fs::write(dir.join(KEY_FINGERPRINT_FILE), bad).unwrap();
                assert_eq!(KeyCheck::of(&dir, "0123456789abcdef", HEX_KEY), KeyCheck::Unproven, "{bad:?}");
                assert!(prove_store_key(&dir, &secret, HEX_KEY).await.unwrap(), "{bad:?}");
                assert_eq!(read_stable_fingerprint(&dir), Some(stable_fingerprint(HEX_KEY)), "{bad:?}");
            }
            std::fs::remove_dir_all(&dir).ok();

            // another key: the seal refuses it, the store goes
            let dir = make("proof-wrong", &secret).await;
            let other = StoreSecret::of(other_key);
            assert!(!prove_store_key(&dir, &other, other_key).await.unwrap());
            assert!(!dir.exists(), "removed");

            // a well-formed stable fingerprint of another key: still opened
            // first; the right key behind it keeps the store
            let dir = make("proof-lost-mark", &secret).await;
            std::fs::write(dir.join(KEY_FINGERPRINT_FILE), stable_fingerprint(other_key)).unwrap();
            assert_eq!(KeyCheck::of(&dir, "x", HEX_KEY), KeyCheck::Lost);
            assert!(prove_store_key(&dir, &secret, HEX_KEY).await.unwrap());
            assert!(dir.join(matrix_sdk::STATE_STORE_DATABASE_NAME).exists());
            assert_eq!(read_stable_fingerprint(&dir), Some(stable_fingerprint(HEX_KEY)));
            std::fs::remove_dir_all(&dir).ok();

            // a broken database is no proof: an error, nothing removed
            let dir = temp_store("proof-broken");
            std::fs::write(dir.join(ENCRYPTED_MARKER), "0123456789abcdef").unwrap();
            std::fs::write(dir.join(matrix_sdk::STATE_STORE_DATABASE_NAME), b"not a database, not at all, just bytes").unwrap();
            assert!(prove_store_key(&dir, &secret, HEX_KEY).await.is_err());
            assert!(dir.join(matrix_sdk::STATE_STORE_DATABASE_NAME).exists() && dir.join(ENCRYPTED_MARKER).exists());
            std::fs::remove_dir_all(&dir).ok();
        });
    }

    // A new store with the key: made key-based at once, no PBKDF2 ever.
    #[test]
    fn store_key_new_store() {
        use matrix_sdk::SqliteCryptoStore;
        let dir = temp_store("keynew");
        let rt = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
        rt.block_on(async {
            let unmarked = key_based_unmarked(&dir, &[CRYPTO_DB]);
            assert_eq!(unmarked, vec![(CRYPTO_DB, false)]);
            let new = store_config(&dir, Some(&StoreSecret::of(HEX_KEY)));
            drop(SqliteCryptoStore::open_with_config(&new).await.unwrap());
            mark_key_based(&dir, &unmarked);
            assert!(dir.join(format!("{KEY_BASED_MARKER}{CRYPTO_DB}")).exists());
            let old = matrix_sdk::SqliteStoreConfig::with_low_memory_config(&dir).passphrase(Some(HEX_KEY));
            assert!(SqliteCryptoStore::open_with_config(&old).await.is_err(), "never under the PBKDF2");
        });
        std::fs::remove_dir_all(&dir).ok();
    }
}
