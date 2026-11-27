// SPDX-License-Identifier: GPL-3.0-or-later
//! The decrypted copies in media/: found again by name instead of being
//! fetched and written anew, written through a temporary name so a broken
//! write is never served, and kept under a size limit (oldest first),
//! media/emotes included. The sweep runs before the first file of a run
//! goes out, and again after every SWEEP_AGAIN_BYTES written (the app and
//! the stand-in live for days); that one spares what this run may have
//! given out.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, SystemTime};

/// What media/ may hold per account before the oldest files go. The SDK
/// keeps its own copy (up to SDK_MEDIA_CACHE_MAX_BYTES, 100 MiB); a file
/// swept here comes from there, or from the server.
pub(crate) const MEDIA_CACHE_MAX_BYTES: u64 = 300 * 1024 * 1024;
/// A temporary file older than this was left by a write that never ended.
const PART_MAX_AGE: Duration = Duration::from_secs(60 * 60);
/// A file served again gets a new date at most this often (the sweep goes
/// by date: what is looked at stays).
const TOUCH_AFTER: Duration = Duration::from_secs(24 * 60 * 60);
const PART_SUFFIX: &str = ".part";
/// Written since the last sweep before the next one (audit FFI-3).
pub(crate) const SWEEP_AGAIN_BYTES: u64 = 50 * 1024 * 1024;

/// The cached file `<stem>.<ext>` for the first extension that has one:
/// not empty, and of the size the event gives where it gives one (a file
/// cut short by an older version's write is fetched again).
pub(crate) fn find_existing(dir: &Path, stem: &str, exts: &[&str], expected_len: Option<u64>) -> Option<PathBuf> {
    for ext in exts {
        let p = dir.join(format!("{stem}.{ext}"));
        let Ok(meta) = std::fs::metadata(&p) else { continue };
        if !meta.is_file() || meta.len() == 0 {
            continue;
        }
        if expected_len.is_some_and(|n| n != meta.len()) {
            continue;
        }
        return Some(p);
    }
    None
}

/// The name a file is written under before it is renamed into place: one
/// per write (a thumbnail row, the picture page and a GIF can ask for the
/// same file at once), hidden, and never one `find_existing` looks for.
pub(crate) fn temp_path(path: &Path) -> PathBuf {
    static N: AtomicU64 = AtomicU64::new(0);
    let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    let n = N.fetch_add(1, Ordering::Relaxed);
    path.with_file_name(format!(".{name}.{}-{n}{PART_SUFFIX}", std::process::id()))
}

/// A served file's date moved to now, once a day at most.
pub(crate) fn touch(path: &Path) {
    let Ok(f) = std::fs::File::open(path) else { return };
    let old = f.metadata().and_then(|m| m.modified()).ok()
        .and_then(|t| SystemTime::now().duration_since(t).ok())
        .is_some_and(|age| age >= TOUCH_AFTER);
    if old {
        let _ = f.set_modified(SystemTime::now());
    }
}

/// media/ brought down to `cap` bytes, oldest files first, its emotes/
/// folder counted in, and temporary files of writes that never ended
/// removed. A file dated `spare` or later stays whatever the size.
/// Returns the files removed and their bytes.
pub(crate) fn sweep(dir: &Path, cap: u64, now: SystemTime, spare: Option<SystemTime>) -> (usize, u64) {
    let Ok(entries) = std::fs::read_dir(dir) else { return (0, 0) };
    let emotes = std::fs::read_dir(dir.join(crate::consts::EMOTE_DIR)).into_iter().flatten();
    let mut files = Vec::new();
    let (mut removed, mut freed, mut total) = (0, 0, 0u64);
    for e in entries.chain(emotes).flatten() {
        let Ok(meta) = e.metadata() else { continue };
        if !meta.is_file() {
            continue;
        }
        let modified = meta.modified().unwrap_or(SystemTime::UNIX_EPOCH);
        if e.file_name().to_string_lossy().ends_with(PART_SUFFIX) {
            // a write going on now (another process on this folder) is young
            if now.duration_since(modified).is_ok_and(|age| age >= PART_MAX_AGE) && std::fs::remove_file(e.path()).is_ok() {
                removed += 1;
                freed += meta.len();
            }
            continue;
        }
        total += meta.len();
        files.push((modified, meta.len(), e.path()));
    }
    if total <= cap {
        return (removed, freed);
    }
    files.sort_by_key(|f| f.0);
    for (modified, len, path) in files {
        if total <= cap || spare.is_some_and(|s| modified >= s) {
            break;
        }
        if std::fs::remove_file(&path).is_ok() {
            total -= len;
            removed += 1;
            freed += len;
        }
    }
    (removed, freed)
}

/// The sweep, once per process and folder, finished before the first
/// path from that folder goes out: a file the UI was given is never swept
/// under it in the same run.
pub(crate) async fn swept(dir: &Path) {
    run_start();
    static DONE: OnceLock<Mutex<HashMap<PathBuf, Arc<tokio::sync::OnceCell<()>>>>> = OnceLock::new();
    let cell = {
        let Ok(mut map) = DONE.get_or_init(Default::default).lock() else { return };
        map.entry(dir.to_owned()).or_default().clone()
    };
    cell.get_or_init(|| async {
        let dir = dir.to_owned();
        let t = std::time::Instant::now();
        let (n, bytes) = tokio::task::spawn_blocking(move || sweep(&dir, MEDIA_CACHE_MAX_BYTES, SystemTime::now(), None))
            .await
            .unwrap_or((0, 0));
        if n > 0 {
            tracing::info!(files = n, kb = bytes / 1024, ms = t.elapsed().as_millis() as u64, "media: swept");
        }
    })
    .await;
}

/// When this run began to give out files: set by the first sweep, which
/// comes before any path goes out.
fn run_start() -> SystemTime {
    static START: OnceLock<SystemTime> = OnceLock::new();
    *START.get_or_init(SystemTime::now)
}

/// `bytes` were written into the media folder `dir`: past
/// SWEEP_AGAIN_BYTES since the last time, it is swept again, sparing every
/// file this run may have given out. Those are dated from this run's start
/// less TOUCH_AFTER on: written in it, touched when served (`touch`), or
/// younger than TOUCH_AFTER when served.
pub(crate) fn written(dir: &Path, bytes: u64) {
    static SINCE: OnceLock<Mutex<HashMap<PathBuf, u64>>> = OnceLock::new();
    let start = run_start();
    let due = {
        let Ok(mut map) = SINCE.get_or_init(Default::default).lock() else { return };
        let n = map.entry(dir.to_owned()).or_default();
        *n += bytes;
        let due = *n >= SWEEP_AGAIN_BYTES;
        if due {
            *n = 0;
        }
        due
    };
    if !due {
        return;
    }
    let dir = dir.to_owned();
    let spare = start.checked_sub(TOUCH_AFTER).unwrap_or(SystemTime::UNIX_EPOCH);
    tokio::task::spawn_blocking(move || {
        let (n, bytes) = sweep(&dir, MEDIA_CACHE_MAX_BYTES, SystemTime::now(), Some(spare));
        if n > 0 {
            tracing::info!(files = n, kb = bytes / 1024, "media: swept again");
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dir(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("rustrx-media-cache-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn file(d: &Path, name: &str, len: usize, age_s: u64) -> PathBuf {
        let p = d.join(name);
        std::fs::write(&p, vec![1u8; len]).unwrap();
        let f = std::fs::File::options().write(true).open(&p).unwrap();
        f.set_modified(SystemTime::now() - Duration::from_secs(age_s)).unwrap();
        p
    }

    #[test]
    fn existing_file_by_extension_and_size() {
        let d = dir("find");
        file(&d, "abc-thumb.jpg", 10, 0);
        file(&d, "empty.png", 0, 0);
        assert_eq!(find_existing(&d, "abc-thumb", &["png", "jpg"], None), Some(d.join("abc-thumb.jpg")));
        // the full file is not the thumbnail
        assert_eq!(find_existing(&d, "abc", &["png", "jpg"], None), None);
        // empty, or not the size the event gives: fetched again
        assert_eq!(find_existing(&d, "empty", &["png"], None), None);
        assert_eq!(find_existing(&d, "abc-thumb", &["jpg"], Some(11)), None);
        assert_eq!(find_existing(&d, "abc-thumb", &["jpg"], Some(10)), Some(d.join("abc-thumb.jpg")));
        std::fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn temp_names_are_unique_and_never_found() {
        let d = dir("temp");
        let target = d.join("abc.jpg");
        let (a, b) = (temp_path(&target), temp_path(&target));
        assert_ne!(a, b);
        std::fs::write(&a, b"x").unwrap();
        assert_eq!(find_existing(&d, "abc", &["jpg", "bin"], None), None);
        std::fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn sweep_goes_oldest_first_with_emotes_and_keeps_young_parts() {
        let d = dir("sweep");
        file(&d, "old.jpg", 100, 300);
        file(&d, "mid.jpg", 100, 200);
        file(&d, "new.jpg", 100, 100);
        file(&d, ".x.jpg.1-0.part", 50, 2 * 60 * 60);
        file(&d, ".y.jpg.1-1.part", 50, 10);
        std::fs::create_dir_all(d.join("emotes")).unwrap();
        // Planted-bug: emotes/ left out of the sweep (FFI-2: it grew without end)
        file(&d.join("emotes"), "e.png", 500, 1000);
        file(&d.join("emotes"), "f.png", 10, 50);
        let (n, bytes) = sweep(&d, 150, SystemTime::now(), None);
        assert_eq!((n, bytes), (4, 750));
        assert!(!d.join("old.jpg").exists() && !d.join("mid.jpg").exists() && d.join("new.jpg").exists());
        assert!(!d.join(".x.jpg.1-0.part").exists() && d.join(".y.jpg.1-1.part").exists());
        assert!(!d.join("emotes/e.png").exists() && d.join("emotes/f.png").exists());
        // under the limit: nothing goes
        assert_eq!(sweep(&d, 1000, SystemTime::now(), None), (0, 0));
        std::fs::remove_dir_all(&d).ok();
    }

    // Planted-bug: the sweep after SWEEP_AGAIN_BYTES ignores `spare` and
    // removes a file this run gave out (the UI would lose a shown picture).
    #[test]
    fn sweep_again_spares_this_runs_files() {
        let d = dir("spare");
        file(&d, "old.jpg", 100, 3000);
        file(&d, "shown.jpg", 100, 100);
        let spare = SystemTime::now() - Duration::from_secs(1000);
        assert_eq!(sweep(&d, 0, SystemTime::now(), Some(spare)), (1, 100));
        assert!(!d.join("old.jpg").exists() && d.join("shown.jpg").exists());
        std::fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn touch_renews_only_old_dates() {
        let d = dir("touch");
        let old = file(&d, "old.jpg", 1, 3 * 24 * 60 * 60);
        let young = file(&d, "young.jpg", 1, 60);
        let before = std::fs::metadata(&young).unwrap().modified().unwrap();
        touch(&old);
        touch(&young);
        let age = SystemTime::now().duration_since(std::fs::metadata(&old).unwrap().modified().unwrap()).unwrap();
        assert!(age < Duration::from_secs(60));
        assert_eq!(std::fs::metadata(&young).unwrap().modified().unwrap(), before);
        std::fs::remove_dir_all(&d).ok();
    }
}
