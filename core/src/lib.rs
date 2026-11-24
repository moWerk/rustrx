// SPDX-License-Identifier: GPL-3.0-or-later
//! Rustrx core: matrix-rust-sdk behind a C ABI small enough to call from a
//! Qt 5.6 application. Commands go in as JSON, events come back as JSON
//! through one callback, which fires on tokio worker threads; the caller
//! hops to its own thread before touching UI state.
//!
//!   rustrx_start(data_dir, callback, ctx)  once per account
//!   rustrx_command("{\"cmd\": ..., ...}")   any thread, returns at once
//!
//! Every account's core runs on the one tokio runtime of the process,
//! made by the first rustrx_start and stopped by rustrx_shutdown.
//!
//! Replies are events (`"ev"`), never return values, so the UI stays
//! asynchronous throughout.

// the SDK's async fns nest deeply; the Send proof for the sync task overflows the default
#![recursion_limit = "256"]

mod consts;
mod core;
mod protocol;
mod serialize;

use std::ffi::{c_char, c_void, CStr, CString};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::OnceLock;

pub type EventCallback = extern "C" fn(ctx: *mut c_void, json: *const c_char);

/// The caller's context pointer, carried into tokio tasks. The caller keeps
/// it valid for the life of the process (the core is never torn down).
struct Ctx(*mut c_void);
unsafe impl Send for Ctx {}
unsafe impl Sync for Ctx {}

pub(crate) struct Emitter {
    cb: EventCallback,
    ctx: Ctx,
}

/// Cleared by rustrx_stop: the callback's receiver is going away, no event
/// may reach it any more (tokio tasks run on until rustrx_shutdown).
static LIVE: AtomicBool = AtomicBool::new(true);

impl Emitter {
    pub(crate) fn emit(&self, value: serde_json::Value) {
        if !LIVE.load(Ordering::Acquire) {
            return;
        }
        // perf: the big events' size, their serialising and the UI's parse
        // (never "log": its line would come back here)
        let perf = tracing::enabled!(target: "rustrx::perf", tracing::Level::DEBUG)
            && matches!(value["ev"].as_str(), Some("timeline" | "thread_timeline" | "rooms" | "image_packs" | "members"));
        let t = perf.then(std::time::Instant::now);
        // serde escapes a NUL in a string as \u0000, so the text has none
        // and needs no scan for one (CString::new scanned and copied it)
        if let Ok(bytes) = serde_json::to_vec(&value) {
            let s = unsafe { CString::from_vec_unchecked(bytes) };
            let ser = t.map(|t| t.elapsed());
            (self.cb)(self.ctx.0, s.as_ptr());
            if let (Some(t), Some(ser)) = (t, ser) {
                // a list's length, or the items a delta carries (an object by id)
                let n = ["items", "rooms", "packs", "members"]
                    .iter()
                    .find_map(|k| value[*k].as_array().map(Vec::len).or_else(|| value[*k].as_object().map(|o| o.len())))
                    .unwrap_or(0);
                tracing::debug!(target: "rustrx::perf", ev = value["ev"].as_str().unwrap_or(""), n,
                                bytes = s.as_bytes().len(), ser_us = ser.as_micros() as u64,
                                callback_us = (t.elapsed() - ser).as_micros() as u64, "emit");
            }
        }
    }
}

/// Stop delivering events. Called by the UI before its receiver is destroyed.
#[no_mangle]
pub extern "C" fn rustrx_stop() {
    LIVE.store(false, Ordering::Release);
}

/// Stop syncing and let go of the stores, waiting at most `timeout_ms`.
/// Called before the process gives up its bus name, which is the only lock
/// on the store: the next Rustrx opens it as soon as the name is gone.
#[no_mangle]
pub extern "C" fn rustrx_shutdown(timeout_ms: u32) {
    // a panic cannot leave an extern "C" fn: caught, the process ends normally
    // every account's core at once, each on a thread of its own: all of
    // them within the one limit
    let done = std::panic::catch_unwind(|| {
        let limit = std::time::Duration::from_millis(timeout_ms.into());
        let runtime = RUNTIME.lock().unwrap_or_else(|e| e.into_inner()).take();
        shutdown_all(cores(), runtime, limit);
    });
    if done.is_err() {
        eprintln!("rustrx: shutdown panicked");
    }
}

/// Every core's stop at once, each on a thread of its own, within the
/// limit less a reserve; then the shared runtime stops with what is left,
/// so no task outlives the shutdown. A second call finds the cores stopped
/// and no runtime.
fn shutdown_all(cores: Vec<&'static core::Core>, runtime: Option<tokio::runtime::Runtime>, limit: std::time::Duration) {
    let start = std::time::Instant::now();
    let reserve = (limit / 5).min(consts::SHUTDOWN_RUNTIME_RESERVE);
    let each = limit.saturating_sub(reserve);
    let stops: Vec<_> = cores.into_iter().map(|core| std::thread::spawn(move || core.shutdown(each))).collect();
    for stop in stops {
        let _ = stop.join();
    }
    if let Some(runtime) = runtime {
        // tasks still running are dropped at their next wait, blocking ones
        // are not waited for longer
        runtime.shutdown_timeout(limit.saturating_sub(start.elapsed()));
    }
}

/// The process's runtime, shared by every account's core: made by the
/// first rustrx_start, taken and stopped by rustrx_shutdown. `HANDLE` stays
/// after that, so a start after the shutdown is refused, not given a
/// runtime that no longer runs.
static RUNTIME: std::sync::Mutex<Option<tokio::runtime::Runtime>> = std::sync::Mutex::new(None);
static HANDLE: OnceLock<tokio::runtime::Handle> = OnceLock::new();

/// The shared runtime's handle; made on the first call. None when it could
/// not be made, or after the shutdown took it.
fn shared_runtime() -> Option<tokio::runtime::Handle> {
    let mut slot = RUNTIME.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(handle) = HANDLE.get() {
        return slot.is_some().then(|| handle.clone());
    }
    let rt = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(consts::WORKER_THREADS)
        .max_blocking_threads(consts::MAX_BLOCKING_THREADS)
        .thread_keep_alive(consts::BLOCKING_KEEP_ALIVE)
        .thread_name("rustrx")
        .enable_all()
        .build()
        .ok()?;
    let handle = HANDLE.get_or_init(|| rt.handle().clone()).clone();
    *slot = Some(rt);
    Some(handle)
}

/// One core per account, in the order they were started; a core lives as
/// long as the process (its number is its index here).
static CORES: std::sync::Mutex<Vec<&'static core::Core>> = std::sync::Mutex::new(Vec::new());

fn cores() -> Vec<&'static core::Core> {
    CORES.lock().map(|c| c.clone()).unwrap_or_default()
}

fn core_at(index: i32) -> Option<&'static core::Core> {
    let index = usize::try_from(index).ok()?;
    CORES.lock().ok()?.get(index).copied()
}

/// Collects formatted log lines and emits each as `{"ev":"log",...}`.
#[derive(Default)]
struct LogWriter(Vec<u8>);

impl std::io::Write for LogWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.0.extend_from_slice(buf);
        while let Some(pos) = self.0.iter().position(|&b| b == b'\n') {
            let line: Vec<u8> = self.0.drain(..=pos).collect();
            let text = String::from_utf8_lossy(&line).trim_end().to_owned();
            if is_expected_token_expiry(&text) {
                continue;
            }
            // the process's one log, through the first account's core
            if let Some(core) = core_at(0) {
                core.emit(serde_json::json!({"ev": "log", "line": text}));
            } else {
                eprintln!("{text}");
            }
        }
        Ok(buf.len())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

/// An access token from an account service runs out every few minutes; the
/// SDK logs the server's 401 as an ERROR and then refreshes the token, which
/// "tokens refreshed" records (a failed refresh is logged on its own). The
/// expected 401 lines only filled the log, about 250 a day per phone.
fn is_expected_token_expiry(line: &str) -> bool {
    line.contains("matrix_sdk::http_client") && line.contains("UnknownToken")
}

/// The key for the local store from sailfish-secrets (the app sets it before
/// rustrx_start; none when sailfish-secrets is missing or failed).
static STORE_KEY: OnceLock<String> = OnceLock::new();

pub(crate) fn store_key() -> Option<&'static str> {
    STORE_KEY.get().map(String::as_str).filter(|k| !k.is_empty())
}

/// Set the store key; call before rustrx_start, or later, before the
/// "restore" command, when the key came late (Secrets' prompt answered on a
/// second try). Returns 0, or 1 when it was set already, 2 on a bad argument.
#[no_mangle]
pub extern "C" fn rustrx_set_store_key(key: *const c_char) -> i32 {
    if key.is_null() {
        return 2;
    }
    let Ok(key) = unsafe { std::ffi::CStr::from_ptr(key) }.to_str() else { return 2 };
    if STORE_KEY.set(key.to_owned()).is_err() {
        return 1;
    }
    0
}

#[no_mangle]
pub extern "C" fn rustrx_version() -> *const c_char {
    static VERSION: OnceLock<CString> = OnceLock::new();
    VERSION.get_or_init(|| CString::new(env!("CARGO_PKG_VERSION")).unwrap()).as_ptr()
}

/// Start a core on its account's data folder (the session, the store and
/// cached media). Returns its number (0 for the first, then 1, 2, ...), or
/// below 0: -2 no folder, -3 no runtime (not made, or after the shutdown).
#[no_mangle]
pub extern "C" fn rustrx_start(data_dir: *const c_char, cb: EventCallback, ctx: *mut c_void) -> i32 {
    if data_dir.is_null() {
        return -2;
    }
    let dir = unsafe { CStr::from_ptr(data_dir) }.to_string_lossy().into_owned();
    // The sandbox drops stderr, so log lines travel as events and the UI
    // hands them to the journal. Once per process.
    // RUSTRX_PERF=1 (set by the app for its perf-log flag too): the perf: lines
    let perf = if std::env::var("RUSTRX_PERF").as_deref() == Ok("1") { ",rustrx::perf=debug" } else { "" };
    let rules = std::env::var("RUSTRX_LOG").unwrap_or_else(|_| "warn,rustrx=info,matrix_sdk_crypto::backups=error".into());
    let filter = tracing_subscriber::EnvFilter::new(format!("{rules}{perf}"));
    let _ = tracing_subscriber::fmt()
        .with_env_filter(filter)
        .with_ansi(false)
        .without_time()
        .with_writer(|| LogWriter::default())
        .try_init();
    // the first account's start makes the runtime, the others share it
    let Some(rt) = shared_runtime() else { return -3 };
    // a panicking task would otherwise vanish: into the debug log, and to
    // stderr (the journal), which also reaches it when the Qt loop that
    // writes the debug log has ended (a panic at exit was invisible there)
    std::panic::set_hook(Box::new(|info| {
        eprintln!("rustrx: panic: {info}");
        tracing::error!("panic: {info}");
    }));
    let core: &'static core::Core = Box::leak(Box::new(core::Core::new(rt, None, dir.into(), Emitter { cb, ctx: Ctx(ctx) })));
    let index = {
        let Ok(mut all) = CORES.lock() else { return -3 };
        all.push(core);
        all.len() as i32 - 1
    };
    core.boot();
    index
}

/// Queue one command for the first account. Returns 0, 2 for unparsable
/// JSON, 4 before start.
#[no_mangle]
pub extern "C" fn rustrx_command(json: *const c_char) -> i32 {
    rustrx_command_to(0, json)
}

/// Queue one command for the account `index` (rustrx_start's number).
/// Returns 0, 2 for unparsable JSON, 4 for no such core.
#[no_mangle]
pub extern "C" fn rustrx_command_to(index: i32, json: *const c_char) -> i32 {
    let Some(core) = core_at(index) else { return 4 };
    if json.is_null() {
        return 2;
    }
    command(core, unsafe { CStr::from_ptr(json) })
}

/// Reads a QR code in a greyscale picture (`width` x `height` bytes, one
/// per pixel, rows `stride` bytes apart) into `out`: its length, 0 for no
/// code, -1 for bad arguments, -2 for a code longer than `cap`. Needs no
/// core; the camera page calls it for each picture it takes.
#[no_mangle]
pub extern "C" fn rustrx_decode_qr(grey: *const u8, width: i32, height: i32, stride: i32, out: *mut u8, cap: i32) -> i32 {
    if grey.is_null() || out.is_null() || width <= 0 || height <= 0 || stride < width || cap <= 0 {
        return -1;
    }
    let (w, h, stride) = (width as usize, height as usize, stride as usize);
    let pixels = unsafe { std::slice::from_raw_parts(grey, stride * (h - 1) + w) };
    match decode_qr(pixels, w, h, stride) {
        None => 0,
        Some(data) if data.len() > cap as usize => -2,
        Some(data) => {
            unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), out, data.len()) };
            data.len() as i32
        }
    }
}

/// The bytes of the first QR code that decodes, as they are (a login code
/// is binary, not text).
fn decode_qr(pixels: &[u8], w: usize, h: usize, stride: usize) -> Option<Vec<u8>> {
    let mut image = rqrr::PreparedImage::prepare_from_greyscale(w, h, |x, y| pixels[y * stride + x]);
    image.detect_grids().into_iter().find_map(|grid| {
        let mut data = Vec::new();
        grid.decode_to(&mut data).ok().map(|_| data)
    })
}

/// rustrx_command past its pointer checks; apart so tests reach it without
/// the process-wide core.
fn command(core: &'static core::Core, json: &CStr) -> i32 {
    let text = json.to_string_lossy();
    match serde_json::from_str::<serde_json::Value>(&text) {
        Ok(value) => {
            core.dispatch(value);
            0
        }
        Err(_) => 2,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Only the SDK's 401 for an expired token is left out of the log; other
    /// errors, a 503 among them, stay. Planted-bug: match "401" alone.
    #[test]
    fn only_expected_token_expiry_is_hidden() {
        let expiry = "ERROR matrix_sdk::http_client: Error while sending request: Api(Server(MatrixError(Error { status_code: 401, body: Standard(StandardErrorBody { kind: UnknownToken(UnknownTokenErrorData { soft_logout: false }), message: \"Token is not active\" }) })))";
        assert!(is_expected_token_expiry(expiry));
        let unavailable = "ERROR matrix_sdk::http_client: Error while sending request: Api(Server(MatrixError(Error { status_code: 503, body: Standard(StandardErrorBody { kind: Unknown, message: \"Unable to introspect the access token\" }) })))";
        assert!(!is_expected_token_expiry(unavailable));
        assert!(!is_expected_token_expiry("WARN rustrx::core::session: token refresh failed: UnknownToken"));
    }

    /// A binary code, drawn as the login page draws one, reads back byte
    /// for byte: the scanner and the QR login speak the same bytes.
    #[test]
    fn qr_reads_back() {
        let data: Vec<u8> = (0..=255u8).chain(b"MATRIX".iter().copied()).collect();
        let code = qrcode::QrCode::new(&data).unwrap();
        let (n, cell, quiet) = (code.width(), 4, 16);
        let side = n * cell + 2 * quiet;
        let colors = code.to_colors();
        let pixels: Vec<u8> = (0..side * side)
            .map(|i| {
                let (x, y) = (i % side, i / side);
                if x < quiet || y < quiet || x >= quiet + n * cell || y >= quiet + n * cell {
                    return 255;
                }
                if colors[(y - quiet) / cell * n + (x - quiet) / cell] == qrcode::Color::Dark { 0 } else { 255 }
            })
            .collect();
        assert_eq!(decode_qr(&pixels, side, side, side), Some(data.clone()));
        let mut out = vec![0u8; 1024];
        let len = rustrx_decode_qr(pixels.as_ptr(), side as i32, side as i32, side as i32, out.as_mut_ptr(), out.len() as i32);
        assert_eq!(&out[..len as usize], &data[..]);
        assert_eq!(rustrx_decode_qr(pixels.as_ptr(), side as i32, side as i32, side as i32, out.as_mut_ptr(), 4), -2);
        assert_eq!(decode_qr(&vec![255; 100 * 100], 100, 100, 100), None);
    }
    use serde_json::{json, Value};
    use std::sync::Mutex;
    use std::time::{Duration, Instant};

    type Sink = Mutex<Vec<String>>;

    /// The test's callback. It must not panic: a panic cannot unwind out of
    /// an extern "C" fn, it aborts the whole test run.
    extern "C" fn collect(ctx: *mut c_void, json: *const c_char) {
        let sink = unsafe { &*(ctx as *const Sink) };
        let text = unsafe { CStr::from_ptr(json) }.to_string_lossy().into_owned();
        if let Ok(mut events) = sink.lock() {
            events.push(text);
        }
    }

    /// A core of the test's own, never logged in, its events collected. Not
    /// the process-wide CORE: tests run in parallel and in any order.
    fn test_core() -> (&'static core::Core, &'static Sink) {
        test_core_in(std::env::temp_dir().join("rustrx-tests-never-written"))
    }

    /// A QR login starts from an empty store: a device left by a failed
    /// attempt made the next one fail ("the account in the store doesn't
    /// match", jp26), but a store kept for a relogin stays and the QR login
    /// is refused. Planted-bug: drop the remove_dir_all in
    /// fresh_store_for_qr.
    #[test]
    fn qr_login_starts_from_an_empty_store() {
        let dir = std::env::temp_dir().join(format!("rustrx-qr-store-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(dir.join("store")).unwrap();
        std::fs::write(dir.join("store").join("matrix-sdk-crypto.sqlite3"), b"left by an earlier attempt").unwrap();
        let (core, _) = test_core_in(dir.clone());
        core.fresh_store_for_qr().unwrap();
        assert!(!dir.join("store").exists() || std::fs::read_dir(dir.join("store")).unwrap().next().is_none());

        std::fs::create_dir_all(dir.join("store")).unwrap();
        std::fs::write(dir.join("store").join("matrix-sdk-crypto.sqlite3"), b"kept for a relogin").unwrap();
        std::fs::write(dir.join("relogin.json"), b"{}").unwrap();
        assert!(core.fresh_store_for_qr().is_err());
        assert!(dir.join("store").join("matrix-sdk-crypto.sqlite3").exists());
        std::fs::remove_dir_all(&dir).ok();
    }

    fn test_core_in(dir: std::path::PathBuf) -> (&'static core::Core, &'static Sink) {
        let sink: &'static Sink = Box::leak(Box::new(Mutex::new(Vec::new())));
        let rt = tokio::runtime::Builder::new_multi_thread().worker_threads(1).enable_all().build().unwrap();
        let emitter = Emitter { cb: collect, ctx: Ctx(sink as *const Sink as *mut c_void) };
        (Box::leak(Box::new(core::Core::new(rt.handle().clone(), Some(rt), dir, emitter))), sink)
    }

    /// A core on a runtime it does not own, as every core of the app is.
    fn test_core_on(rt: &tokio::runtime::Runtime) -> (&'static core::Core, &'static Sink) {
        let sink: &'static Sink = Box::leak(Box::new(Mutex::new(Vec::new())));
        let emitter = Emitter { cb: collect, ctx: Ctx(sink as *const Sink as *mut c_void) };
        let dir = std::env::temp_dir().join("rustrx-tests-never-written");
        (Box::leak(Box::new(core::Core::new(rt.handle().clone(), None, dir, emitter))), sink)
    }

    fn wait_for_event(sink: &Sink) -> Vec<Value> {
        let deadline = Instant::now() + Duration::from_secs(5);
        while sink.lock().unwrap().is_empty() && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(10));
        }
        events(sink)
    }

    /// A custom emoji in the cache is answered from it by its name (no
    /// folder listing, no network: this core is not logged in), whatever
    /// type it was saved as.
    /// Planted-bug: leave "gif" out of MEDIA_EXTENSIONS: the cached gif is
    /// not found and the fetch fails with "not logged in".
    #[test]
    fn a_cached_emote_is_found_by_name() {
        let dir = std::env::temp_dir().join(format!("rustrx-emote-test-{}", std::process::id()));
        let emotes = dir.join(consts::MEDIA_DIR).join(consts::EMOTE_DIR);
        std::fs::create_dir_all(&emotes).unwrap();
        let file = emotes.join("example_org_AbC123.gif");
        std::fs::write(&file, b"GIF89a").unwrap();
        let (core, sink) = test_core_in(dir.clone());
        assert_eq!(send(core, r#"{"cmd":"fetch_emote","mxc":"mxc://example.org/AbC123"}"#), 0);
        let got = wait_for_event(sink);
        assert_eq!(got, vec![json!({"ev": "emote", "mxc": "mxc://example.org/AbC123", "path": file.to_string_lossy()})]);
        std::fs::remove_dir_all(dir).ok();
    }

    fn events(sink: &Sink) -> Vec<Value> {
        sink.lock().unwrap().iter().map(|s| serde_json::from_str(s).expect("every event is JSON")).collect()
    }

    fn send(core: &'static core::Core, text: &str) -> i32 {
        command(core, &CString::new(text).unwrap())
    }

    /// Shutting down from a thread outside the runtime, as Qt's main thread
    /// calls it at the process's end, finishes instead of panicking.
    /// Planted-bug: `block_on(tokio::time::timeout(..))` (the timer made
    /// outside the runtime: "there is no reactor running"), as in 0.4.7 to
    /// 0.5.0, where every quit aborted.
    #[test]
    fn shutdown_outside_the_runtime_finishes() {
        let (core, _sink) = test_core();
        let done = std::thread::spawn(move || core.shutdown(Duration::from_millis(500))).join();
        assert!(done.is_ok(), "shutdown panicked");
    }

    /// The shutdown aborts a running command, stops the runtime (no task
    /// outlives it into the process's exit), and stays in its budget; a
    /// command after it does not run.
    /// Planted-bugs: drop the `t.abort()` loop in `shutdown` (the command,
    /// mark_read waiting 22 s between retries, is awaited until the
    /// timeout: too slow); put the runtime back instead of stopping it
    /// (`*self.runtime.lock().unwrap() = runtime`: the sleeping task still
    /// holds its sentinel).
    #[test]
    fn shutdown_aborts_commands_and_stops_the_runtime() {
        let (core, sink) = test_core();
        // "not logged in", retried after 2, 5 and 15 s: a long command
        assert_eq!(send(core, r#"{"cmd":"mark_read","room_id":"!a:b"}"#), 0);
        let sentinel = std::sync::Arc::new(());
        let held = sentinel.clone();
        core.runtime_handle().spawn(async move {
            tokio::time::sleep(Duration::from_secs(60)).await;
            drop(held);
        });
        std::thread::sleep(Duration::from_millis(200));
        let started = Instant::now();
        let done = std::thread::spawn(move || core.shutdown(Duration::from_secs(3))).join();
        assert!(done.is_ok(), "shutdown panicked");
        assert!(started.elapsed() < Duration::from_secs(1), "took {:?}", started.elapsed());
        assert_eq!(std::sync::Arc::strong_count(&sentinel), 1, "a task outlived the runtime");
        assert!(events(sink).is_empty(), "{:?}", events(sink));
        assert_eq!(send(core, r#"{"cmd":"open_room","room_id":"!a:b"}"#), 0);
        std::thread::sleep(Duration::from_millis(300));
        assert!(events(sink).is_empty(), "a command ran after the shutdown: {:?}", events(sink));
        // a second shutdown has nothing left to do and returns
        core.shutdown(Duration::from_millis(100));
    }

    /// Two accounts' cores on one runtime they do not own: the shared stop
    /// aborts both cores' running commands, stops the runtime last (a task
    /// spawned on it outlives neither), stays in its budget, and both cores
    /// refuse commands afterwards. A second stop has nothing left to do.
    /// Planted-bugs: in `shutdown_all`, leave out the runtime's
    /// `shutdown_timeout` (the sentinel is still held); in `Core::shutdown`,
    /// return early when the core owns no runtime, as before the shared
    /// runtime (the cores are not stopped).
    #[test]
    fn the_shared_runtime_stops_after_every_core() {
        let rt = tokio::runtime::Builder::new_multi_thread().worker_threads(2).enable_all().build().unwrap();
        let (a, sink_a) = test_core_on(&rt);
        let (b, sink_b) = test_core_on(&rt);
        for core in [a, b] {
            assert_eq!(send(core, r#"{"cmd":"mark_read","room_id":"!a:b"}"#), 0);
        }
        let sentinel = std::sync::Arc::new(());
        let held = sentinel.clone();
        rt.spawn(async move {
            tokio::time::sleep(Duration::from_secs(60)).await;
            drop(held);
        });
        std::thread::sleep(Duration::from_millis(200));
        let started = Instant::now();
        let done = std::thread::spawn(move || shutdown_all(vec![a, b], Some(rt), Duration::from_secs(3))).join();
        assert!(done.is_ok(), "shutdown panicked");
        assert!(started.elapsed() < Duration::from_secs(1), "took {:?}", started.elapsed());
        assert_eq!(std::sync::Arc::strong_count(&sentinel), 1, "a task outlived the shared runtime");
        for (core, sink) in [(a, sink_a), (b, sink_b)] {
            assert!(core.is_stopped(), "a core was not stopped");
            assert!(events(sink).is_empty(), "{:?}", events(sink));
            assert_eq!(send(core, r#"{"cmd":"open_room","room_id":"!a:b"}"#), 0);
        }
        std::thread::sleep(Duration::from_millis(300));
        assert!(events(sink_a).is_empty() && events(sink_b).is_empty(), "a command ran after the shutdown");
        shutdown_all(vec![a, b], None, Duration::from_millis(100));
    }

    /// The rooms held at shutdown: a timeline made again moves its room to
    /// the newest, a room and a thread in it are two entries, and only the
    /// newest TIMELINE_ROOMS_KEPT stay.
    /// Planted-bug: in `note_timeline`, pop the newest (`pop_back`) when
    /// over the cap: the room just opened is the one left out.
    #[test]
    fn the_newest_timeline_rooms_are_kept() {
        use matrix_sdk::ruma::{EventId, RoomId};
        let (core, _) = test_core();
        let room = |i: usize| RoomId::parse(format!("!r{i}:example.org")).unwrap();
        let root = EventId::parse("$root:example.org").unwrap();
        core.note_timeline(&room(0), None);
        core.note_timeline(&room(0), Some(&root));
        // full: the two of room 0 and six more
        for i in 1..consts::TIMELINE_ROOMS_KEPT - 1 {
            core.note_timeline(&room(i), None);
        }
        // again: newest, not twice
        core.note_timeline(&room(0), None);
        let kept = core.timeline_rooms();
        assert_eq!(kept.len(), consts::TIMELINE_ROOMS_KEPT);
        assert_eq!(kept.last().unwrap(), &(room(0), None));
        assert_eq!(kept.first().unwrap(), &(room(0), Some(root.clone())));
        core.note_timeline(&room(99), None);
        let kept = core.timeline_rooms();
        assert_eq!(kept.len(), consts::TIMELINE_ROOMS_KEPT);
        assert!(!kept.contains(&(room(0), Some(root))), "the oldest stayed");
        assert_eq!(kept.last().unwrap(), &(room(99), None));
    }

    /// While a login runs, a second one of any kind is refused at once
    /// with login_error (before any network), and the guard is let go.
    /// Planted-bug: in `login_or_refuse`, drop the emit (the second login
    /// ends silently: no event) or return a guard anyway (`begin_login`
    /// always Some: the second login goes on to the network).
    #[test]
    fn a_second_login_is_refused() {
        let (core, sink) = test_core();
        let first = core.begin_login().expect("no login runs yet");
        assert!(core.begin_login().is_none());
        let logins = [
            r#"{"cmd":"login_password","user":"@a:example.invalid","password":"x"}"#,
            r#"{"cmd":"login_sso","homeserver":"example.invalid"}"#,
            r#"{"cmd":"login_oauth","homeserver":"example.invalid"}"#,
            r#"{"cmd":"login_qr","homeserver":"example.invalid"}"#,
        ];
        for login in logins {
            sink.lock().unwrap().clear();
            assert_eq!(send(core, login), 0);
            let deadline = Instant::now() + Duration::from_secs(5);
            while sink.lock().unwrap().is_empty() && Instant::now() < deadline {
                std::thread::sleep(Duration::from_millis(10));
            }
            let got = events(sink);
            assert_eq!(got, vec![json!({"ev": "login_error", "message": "another login is still running"})], "{login}");
        }
        drop(first);
        assert!(core.begin_login().is_some(), "the guard was not let go");
    }

    /// Before rustrx_start every command, even a null pointer, is refused
    /// with 4 and nothing panics (no test here starts the global core).
    /// Planted-bug: check `json.is_null()` before `core_at`.
    #[test]
    fn commands_before_start_are_refused() {
        let c = CString::new(r#"{"cmd":"logout"}"#).unwrap();
        assert_eq!(rustrx_command(c.as_ptr()), 4);
        assert_eq!(rustrx_command(std::ptr::null()), 4);
        assert_eq!(rustrx_command_to(3, c.as_ptr()), 4);
        assert_eq!(rustrx_command_to(-1, c.as_ptr()), 4);
    }

    /// Two accounts in one process: each core starts on its own folder,
    /// says "ready" to its own receiver only, and a command for one is
    /// answered by that one alone.
    /// Planted-bug: give the second core the first one's receiver (its
    /// events then land in the first sink and the counts differ).
    #[test]
    fn two_accounts_keep_apart() {
        let base = std::env::temp_dir().join(format!("rustrx-two-accounts-{}", std::process::id()));
        let (a, sink_a) = test_core_in(base.join("a"));
        let (b, sink_b) = test_core_in(base.join("accounts").join("1"));
        a.boot();
        b.boot();
        let ready = |sink: &Sink| {
            let deadline = Instant::now() + Duration::from_secs(10);
            loop {
                let n = events(sink).iter().filter(|e| e["ev"] == "ready").count();
                if n > 0 || Instant::now() > deadline {
                    return n;
                }
                std::thread::sleep(Duration::from_millis(20));
            }
        };
        assert_eq!(ready(sink_a), 1);
        assert_eq!(ready(sink_b), 1);
        assert!(base.join("a").join(consts::MEDIA_DIR).is_dir() && base.join("accounts/1").join(consts::MEDIA_DIR).is_dir());
        let before_a = events(sink_a).len();
        assert_eq!(send(b, r#"{"cmd":"open_room","room_id":"!nowhere:example.org"}"#), 0);
        let deadline = Instant::now() + Duration::from_secs(5);
        while !events(sink_b).iter().any(|e| e["ev"] == "error") && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(20));
        }
        assert!(events(sink_b).iter().any(|e| e["ev"] == "error"), "{:?}", events(sink_b));
        std::thread::sleep(Duration::from_millis(200));
        assert_eq!(events(sink_a).len(), before_a, "{:?}", events(sink_a));
        assert_eq!(events(sink_a).iter().filter(|e| e["ev"] == "ready").count(), 1);
        std::fs::remove_dir_all(base).ok();
    }

    /// Text that is not JSON is answered with 2 and no event; deep nesting
    /// hits serde_json's recursion limit instead of the stack's.
    /// Planted-bug: answer `Err(_) => 0` in `command`.
    #[test]
    fn not_json_is_refused_with_2() {
        let (core, sink) = test_core();
        let deep = format!("{}{}", "[".repeat(100_000), "]".repeat(100_000));
        let deep_object = format!("{}1{}", r#"{"a":"#.repeat(50_000), "}".repeat(50_000));
        let cases = [
            "", " ", "{", r#"{"cmd":"#, r#"{"cmd":"logout""#, "nul", "logout",
            r#"{"cmd":"logout"} trailing"#, r#"{'cmd':'logout'}"#, r#"{"cmd":"lo\qout"}"#,
            r#"{"cmd":"\ud800"}"#, &deep, &deep_object,
        ];
        for case in cases {
            assert_eq!(send(core, case), 2, "{}", &case[..case.len().min(60)]);
        }
        // bytes that are not UTF-8 become U+FFFD, and that is no JSON either
        let bad = CStr::from_bytes_with_nul(b"\xff\xfe{\0").unwrap();
        assert_eq!(command(core, bad), 2);
        assert!(events(sink).is_empty());
    }

    /// JSON that is no command: refused before anything runs, with one
    /// error event at once (no task, no wait) naming the cmd and serde's
    /// reason.
    /// Planted-bug: in `Core::dispatch`, `return` on a parse error without
    /// `self.error`.
    #[test]
    fn bad_commands_give_an_error_event_at_once() {
        let (core, sink) = test_core();
        let long = "z".repeat(1 << 20);
        let long_cmd = json!({"cmd": long}).to_string();
        let cases: [(&str, &str, &str); 9] = [
            ("{}", "", "missing field `cmd`"),
            ("[]", "", ""),
            (r#"{"cmd":5}"#, "", "invalid type"),
            (r#"{"cmd":"nope"}"#, "nope", "unknown variant `nope`"),
            (r#"{"cmd":"send_text","room_id":"!a:b"}"#, "send_text", "missing field `body`"),
            (r#"{"cmd":"paginate_back","room_id":"!a:b","count":70000}"#, "paginate_back", "70000"),
            (r#"{"cmd":"poll_vote","room_id":"!a:b","event_id":"$e","answers":[1]}"#, "poll_vote", "invalid type"),
            (r#"{"cmd":"knock_answer","room_id":"!a:b","user_id":"@u:b"}"#, "knock_answer", "missing field `accept`"),
            (&long_cmd, &long, "unknown variant"),
        ];
        for (text, cmd, reason) in cases {
            sink.lock().unwrap().clear();
            assert_eq!(send(core, text), 0);
            let got = events(sink);
            assert_eq!(got.len(), 1, "{}: {got:?}", &text[..text.len().min(60)]);
            assert_eq!(got[0]["ev"], "error");
            assert_eq!(got[0]["cmd"], cmd);
            let message = got[0]["message"].as_str().unwrap();
            assert!(message.contains(reason), "{message}");
        }
    }

    /// A NUL inside a command still yields its error event: the event is
    /// JSON, which escapes it, so the C string is not cut or dropped.
    /// Planted-bug: in `Emitter::emit`, hand on the text with the NUL raw
    /// (`value.to_string().replace("\\u0000", "\0").into_bytes()`): the C
    /// string ends at the NUL, the UI gets cut JSON (the test cannot parse it).
    #[test]
    fn nul_in_a_command_still_reaches_the_ui() {
        let (core, sink) = test_core();
        assert_eq!(send(core, r#"{"cmd":"log\u0000out"}"#), 0);
        let got = events(sink);
        assert_eq!(got.len(), 1, "{got:?}");
        assert_eq!(got[0]["cmd"], "log\u{0}out");
    }

    /// A well-formed command fails in its handler when nobody is logged in,
    /// and that error comes back from the task as an event.
    /// Planted-bug: in `Core::dispatch`, drop the handler's error
    /// (`let _ = self.handle(command).await;`).
    #[test]
    fn handler_errors_come_back_as_events() {
        let (core, sink) = test_core();
        assert_eq!(send(core, r#"{"cmd":"open_room","room_id":"!a:b"}"#), 0);
        let deadline = Instant::now() + Duration::from_secs(10);
        while sink.lock().unwrap().is_empty() && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(10));
        }
        let got = events(sink);
        assert_eq!(got, vec![json!({"ev": "error", "cmd": "open_room", "message": "not logged in"})]);
    }

    /// Against matrix.org's account service, so ignored by default (run with
    /// `cargo test -- --ignored`): Rustrx registers itself, gets the page
    /// for the browser, and a cancel ends the wait with login_error. Nothing
    /// is logged in; the store goes into a temporary folder.
    /// Planted-bug: drop the cancel arm's notification (login_cancel then
    /// waits for the 10 minute timeout and this fails at its deadline).
    #[test]
    #[ignore = "network: matrix.org"]
    fn oauth_login_reaches_the_page_and_cancels() {
        let sink: &'static Sink = Box::leak(Box::new(Mutex::new(Vec::new())));
        let rt = tokio::runtime::Builder::new_multi_thread().worker_threads(2).enable_all().build().unwrap();
        let emitter = Emitter { cb: collect, ctx: Ctx(sink as *const Sink as *mut c_void) };
        let dir = std::env::temp_dir().join(format!("rustrx-oauth-test-{}", std::process::id()));
        let core: &'static core::Core = Box::leak(Box::new(core::Core::new(rt.handle().clone(), Some(rt), dir.clone(), emitter)));
        assert_eq!(send(core, r#"{"cmd":"login_oauth","homeserver":"matrix.org"}"#), 0);
        let wait_for = |ev: &str| {
            let deadline = Instant::now() + Duration::from_secs(60);
            loop {
                if let Some(e) = events(sink).into_iter().find(|e| e["ev"] == ev) {
                    return e;
                }
                assert!(Instant::now() < deadline, "no {ev}: {:?}", events(sink));
                std::thread::sleep(Duration::from_millis(50));
            }
        };
        let page = wait_for("open_url");
        let url = page["url"].as_str().unwrap();
        assert!(url.starts_with("https://account.matrix.org/"), "{url}");
        assert!(url.contains("redirect_uri=http%3A%2F%2F127.0.0.1%3A"), "{url}");
        assert_eq!(send(core, r#"{"cmd":"login_cancel"}"#), 0);
        let error = wait_for("login_error");
        assert_eq!(error["message"], "login cancelled");
        std::fs::remove_dir_all(dir).ok();
    }
}
