// SPDX-License-Identifier: GPL-3.0-or-later
//! session.json, which holds the access and refresh tokens. Where
//! sailfish-secrets gives the store's key, the file is sealed with it (the
//! SDK's StoreCipher: XChaCha20-Poly1305, its key a fresh one per write,
//! wrapped under an HKDF of the store's key); without a key it stays plain
//! JSON, as before. Either way it is owner-only, and complete or absent.
//! Every reader and writer of the file goes through `read_session_file`
//! and `write_session_file`. (part of `impl Core`)

use std::path::Path;

use matrix_sdk_store_encryption::{EncryptedValueBase64, StoreCipher};

use super::*;

/// The sealed form of the file. `rustrx_sealed` is what tells it from a
/// plain SavedSession, which never has that field.
#[derive(Serialize, Deserialize)]
struct Sealed {
    rustrx_sealed: u32,
    /// the cipher of this write, exported under the store's key (hex)
    cipher: String,
    /// the SavedSession's JSON, encrypted with that cipher
    value: EncryptedValueBase64,
}

const SEALED_VERSION: u32 = 1;

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn unhex(text: &str) -> Result<Vec<u8>> {
    if !text.len().is_multiple_of(2) || !text.is_ascii() {
        bail!("not hex");
    }
    (0..text.len()).step_by(2).map(|i| u8::from_str_radix(&text[i..i + 2], 16).map_err(|e| anyhow!("{e}"))).collect()
}

/// The saved session, and whether the file was sealed. None when there is
/// no file. Err (never None) for a sealed file without the key or with
/// another one: that is not a logout, the session stays and the next start
/// tries again (`restore_failed`, as for a locked store).
pub(crate) fn read_session_file(path: &Path, key: Option<&str>) -> Result<Option<(SavedSession, bool)>> {
    let raw = match std::fs::read(path) {
        Ok(raw) => raw,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(e.into()),
    };
    let value: Value = serde_json::from_slice(&raw)?;
    if value.get("rustrx_sealed").is_none() {
        return Ok(Some((serde_json::from_value(value)?, false)));
    }
    let sealed: Sealed = serde_json::from_value(value)?;
    if sealed.rustrx_sealed != SEALED_VERSION {
        bail!("the session file has an unknown format ({})", sealed.rustrx_sealed);
    }
    let Some(key) = key else {
        bail!("the session is sealed and the store's key is not available (sailfish-secrets locked or unreachable)");
    };
    let cipher = StoreCipher::import_with_key(key.as_bytes(), &unhex(&sealed.cipher)?)
        .map_err(|e| anyhow!("the session's seal does not fit the store's key: {e}"))?;
    let plain = cipher.decrypt_value_base64_data(sealed.value).map_err(|e| anyhow!("the session cannot be unsealed: {e}"))?;
    Ok(Some((serde_json::from_slice(&plain)?, true)))
}

/// Writes the session, sealed when there is a key: to a temporary file,
/// owner-only, synced, then renamed over the old one (a crash mid-write
/// must not leave a half file for the next start).
pub(crate) fn write_session_file(path: &Path, saved: &SavedSession, key: Option<&str>) -> Result<()> {
    let plain = serde_json::to_vec(saved)?;
    let bytes = match key {
        Some(key) => {
            let cipher = StoreCipher::new().map_err(|e| anyhow!("{e}"))?;
            let sealed = Sealed {
                rustrx_sealed: SEALED_VERSION,
                cipher: hex(&cipher.export_with_key(key.as_bytes()).map_err(|e| anyhow!("{e}"))?),
                value: cipher.encrypt_value_base64_data(plain).map_err(|e| anyhow!("{e}"))?,
            };
            serde_json::to_vec(&sealed)?
        }
        None => plain,
    };
    use std::io::Write;
    use std::os::unix::fs::OpenOptionsExt;
    let tmp = path.with_extension("json.tmp");
    {
        let mut f = std::fs::OpenOptions::new().write(true).create(true).truncate(true).mode(0o600).open(&tmp)?;
        f.write_all(&bytes)?;
        f.sync_all()?;
    }
    std::fs::rename(&tmp, path)?;
    Ok(())
}

/// Reads the session; a plain file is sealed at once when there is a key
/// now (a session saved before sailfish-secrets was used, or by 0.5.0 and
/// earlier). A failed rewrite leaves the plain file as it was.
pub(crate) fn load_session_file(path: &Path, key: Option<&str>) -> Result<Option<SavedSession>> {
    let Some((saved, sealed)) = read_session_file(path, key)? else { return Ok(None) };
    if !sealed && key.is_some() {
        match write_session_file(path, &saved, key) {
            Ok(()) => tracing::info!("session: sealed with the store's key"),
            Err(e) => tracing::warn!("session: not sealed: {e}"),
        }
    }
    Ok(Some(saved))
}

impl Core {
    pub(crate) fn session_path(&self) -> PathBuf {
        self.data_dir.join(SESSION_FILE)
    }

    /// The saved session, if any (sealed with the store's key, or plain).
    pub(crate) fn load_session(&self) -> Result<Option<SavedSession>> {
        load_session_file(&self.session_path(), crate::store_key())
    }

    pub(crate) fn write_session(&self, saved: &SavedSession) -> Result<()> {
        write_session_file(&self.session_path(), saved, crate::store_key())
    }

    /// The saved session gone (a logout, or the server ended it).
    pub(crate) fn remove_session(&self) {
        let path = self.session_path();
        std::fs::remove_file(path.with_extension("json.tmp")).ok();
        std::fs::remove_file(path).ok();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const ACCESS: &str = "syt_access_token_never_in_the_clear";
    const REFRESH: &str = "syr_refresh_token_never_in_the_clear";

    fn saved() -> SavedSession {
        serde_json::from_value(json!({
            "homeserver": "https://matrix.example.org/",
            "session": {"user_id": "@ann:example.org", "device_id": "ABCDEF",
                        "access_token": ACCESS, "refresh_token": REFRESH},
            "sliding_sync": true,
            "client_id": "01JCLIENT",
        }))
        .unwrap()
    }

    /// A folder of the test's own under the system's temporary folder.
    fn dir(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("rustrx-session-test-{}-{name}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn holds_tokens(path: &Path) -> bool {
        let text = String::from_utf8_lossy(&std::fs::read(path).unwrap()).into_owned();
        text.contains(ACCESS) || text.contains(REFRESH)
    }

    fn tokens(s: &SavedSession) -> (String, Option<String>) {
        (s.session.tokens.access_token.clone(), s.session.tokens.refresh_token.clone())
    }

    /// With a key the file is sealed (no token readable in it), owner-only,
    /// and reads back whole.
    /// Planted-bug: in write_session_file, `None => plain` for every key
    /// (`let bytes = plain;`): the tokens stand in the file and it fails.
    #[test]
    fn sealed_round_trip() {
        let d = dir("round");
        let path = d.join(SESSION_FILE);
        write_session_file(&path, &saved(), Some("store-key-1")).unwrap();
        assert!(!holds_tokens(&path), "tokens in the clear");
        use std::os::unix::fs::PermissionsExt;
        assert_eq!(std::fs::metadata(&path).unwrap().permissions().mode() & 0o777, 0o600);
        let (back, sealed) = read_session_file(&path, Some("store-key-1")).unwrap().unwrap();
        assert!(sealed);
        assert_eq!(tokens(&back), (ACCESS.to_owned(), Some(REFRESH.to_owned())));
        assert_eq!(back.client_id.as_deref(), Some("01JCLIENT"));
        assert_eq!(back.homeserver, "https://matrix.example.org/");
        assert!(!d.join("session.json.tmp").exists());
        std::fs::remove_dir_all(d).ok();
    }

    /// A plain file of an earlier version is read as before and sealed at
    /// once when a key is there.
    /// Planted-bug: drop the rewrite in load_session_file (`if false`):
    /// the tokens stay in the clear.
    #[test]
    fn plain_is_migrated_to_sealed() {
        let d = dir("migrate");
        let path = d.join(SESSION_FILE);
        std::fs::write(&path, serde_json::to_vec(&saved()).unwrap()).unwrap();
        assert!(holds_tokens(&path));
        let back = load_session_file(&path, Some("store-key-1")).unwrap().unwrap();
        assert_eq!(tokens(&back), (ACCESS.to_owned(), Some(REFRESH.to_owned())));
        assert!(!holds_tokens(&path), "still in the clear after reading with a key");
        let (again, sealed) = read_session_file(&path, Some("store-key-1")).unwrap().unwrap();
        assert!(sealed);
        assert_eq!(tokens(&again), tokens(&back));
        std::fs::remove_dir_all(d).ok();
    }

    /// A sealed file with another key, or with none, is an error (the
    /// restore fails and is tried again), never "no session" (a logout),
    /// and the file stays.
    /// Planted-bug: in read_session_file, `let Some(key) = key else {
    /// return Ok(None) };` (a locked sailfish-secrets logged people out).
    #[test]
    fn wrong_or_missing_key_is_an_error_not_a_logout() {
        let d = dir("wrong");
        let path = d.join(SESSION_FILE);
        write_session_file(&path, &saved(), Some("store-key-1")).unwrap();
        assert!(read_session_file(&path, Some("store-key-2")).is_err());
        assert!(load_session_file(&path, Some("store-key-2")).is_err());
        assert!(read_session_file(&path, None).is_err());
        assert!(load_session_file(&path, None).is_err());
        assert!(path.exists());
        assert!(read_session_file(&path, Some("store-key-1")).unwrap().is_some());
        std::fs::remove_dir_all(d).ok();
    }

    /// Without a key on this phone (no sailfish-secrets) the file stays
    /// plain JSON, as before, and a missing file is no session.
    /// Planted-bug: seal with an empty key when there is none (`None =>`
    /// sealing with ""): the file is no plain SavedSession any more.
    #[test]
    fn no_key_stays_plain() {
        let d = dir("nokey");
        let path = d.join(SESSION_FILE);
        assert!(load_session_file(&path, None).unwrap().is_none());
        write_session_file(&path, &saved(), None).unwrap();
        let plain: SavedSession = serde_json::from_slice(&std::fs::read(&path).unwrap()).unwrap();
        assert_eq!(tokens(&plain), (ACCESS.to_owned(), Some(REFRESH.to_owned())));
        let back = load_session_file(&path, None).unwrap().unwrap();
        assert_eq!(tokens(&back), tokens(&plain));
        let (_, sealed) = read_session_file(&path, None).unwrap().unwrap();
        assert!(!sealed);
        std::fs::remove_dir_all(d).ok();
    }
}
