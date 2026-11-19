// SPDX-License-Identifier: GPL-3.0-or-later
//! Rustrx core: matrix-rust-sdk behind a C ABI small enough to call from a
//! Qt 5.6 application. Commands go in as JSON, events come back as JSON
//! through one callback, which fires on tokio worker threads; the caller
//! hops to its own thread before touching UI state.
//!
//!   rustrx_start(data_dir, callback, ctx)  once per process
//!   rustrx_command("{\"cmd\": ..., ...}")   any thread, returns at once
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
/// may reach it any more (tokio tasks keep running until the process ends).
static LIVE: AtomicBool = AtomicBool::new(true);

impl Emitter {
    pub(crate) fn emit(&self, value: serde_json::Value) {
        if !LIVE.load(Ordering::Acquire) {
            return;
        }
        if let Ok(s) = CString::new(value.to_string()) {
            (self.cb)(self.ctx.0, s.as_ptr());
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
    let done = std::panic::catch_unwind(|| {
        if let Some(core) = CORE.get() {
            core.shutdown(std::time::Duration::from_millis(timeout_ms.into()));
        }
    });
    if done.is_err() {
        eprintln!("rustrx: shutdown panicked");
    }
}

static CORE: OnceLock<core::Core> = OnceLock::new();

/// Collects formatted log lines and emits each as `{"ev":"log",...}`.
#[derive(Default)]
struct LogWriter(Vec<u8>);

impl std::io::Write for LogWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.0.extend_from_slice(buf);
        while let Some(pos) = self.0.iter().position(|&b| b == b'\n') {
            let line: Vec<u8> = self.0.drain(..=pos).collect();
            let text = String::from_utf8_lossy(&line).trim_end().to_owned();
            if let Some(core) = CORE.get() {
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

pub(crate) fn instance() -> &'static core::Core {
    CORE.get().expect("rustrx_start was called")
}

/// The key for the local store from sailfish-secrets (the app sets it before
/// rustrx_start; none when sailfish-secrets is missing or failed).
static STORE_KEY: OnceLock<String> = OnceLock::new();

pub(crate) fn store_key() -> Option<&'static str> {
    STORE_KEY.get().map(String::as_str).filter(|k| !k.is_empty())
}

/// Set the store key; call before rustrx_start. Returns 0, or 1 when it was
/// set already, 2 on a bad argument.
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

/// Start the core. `data_dir` holds the session, the store and cached media.
/// Returns 0, or 1 when already started, 2 on a bad argument, 3 when the
/// runtime could not be created.
#[no_mangle]
pub extern "C" fn rustrx_start(data_dir: *const c_char, cb: EventCallback, ctx: *mut c_void) -> i32 {
    if CORE.get().is_some() {
        return 1;
    }
    if data_dir.is_null() {
        return 2;
    }
    let dir = unsafe { CStr::from_ptr(data_dir) }.to_string_lossy().into_owned();
    // The sandbox drops stderr, so log lines travel as events and the UI
    // hands them to the journal.
    let filter = tracing_subscriber::EnvFilter::try_from_env("RUSTRX_LOG")
        .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("warn,rustrx=info,matrix_sdk_crypto::backups=error"));
    let _ = tracing_subscriber::fmt()
        .with_env_filter(filter)
        .with_ansi(false)
        .without_time()
        .with_writer(|| LogWriter::default())
        .try_init();
    let rt = match tokio::runtime::Builder::new_multi_thread()
        .worker_threads(consts::WORKER_THREADS)
        .thread_name("rustrx")
        .enable_all()
        .build()
    {
        Ok(rt) => rt,
        Err(_) => return 3,
    };
    // a panicking task would otherwise vanish: into the debug log, and to
    // stderr (the journal), which also reaches it when the Qt loop that
    // writes the debug log has ended (a panic at exit was invisible there)
    std::panic::set_hook(Box::new(|info| {
        eprintln!("rustrx: panic: {info}");
        tracing::error!("panic: {info}");
    }));
    let core = core::Core::new(rt, dir.into(), Emitter { cb, ctx: Ctx(ctx) });
    if CORE.set(core).is_err() {
        return 1;
    }
    instance().boot();
    0
}

/// Queue one command. Returns 0, 2 for unparsable JSON, 4 before start.
#[no_mangle]
pub extern "C" fn rustrx_command(json: *const c_char) -> i32 {
    let Some(core) = CORE.get() else { return 4 };
    if json.is_null() {
        return 2;
    }
    command(core, unsafe { CStr::from_ptr(json) })
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
        let sink: &'static Sink = Box::leak(Box::new(Mutex::new(Vec::new())));
        let rt = tokio::runtime::Builder::new_multi_thread().worker_threads(1).enable_all().build().unwrap();
        let emitter = Emitter { cb: collect, ctx: Ctx(sink as *const Sink as *mut c_void) };
        let dir = std::env::temp_dir().join("rustrx-tests-never-written");
        (Box::leak(Box::new(core::Core::new(rt, dir, emitter))), sink)
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

    /// Before rustrx_start every command, even a null pointer, is refused
    /// with 4 and nothing panics (no test here starts the global core).
    /// Planted-bug: check `json.is_null()` before `CORE.get()`.
    #[test]
    fn commands_before_start_are_refused() {
        let c = CString::new(r#"{"cmd":"logout"}"#).unwrap();
        assert_eq!(rustrx_command(c.as_ptr()), 4);
        assert_eq!(rustrx_command(std::ptr::null()), 4);
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
    /// (`value.to_string().replace("\\u0000", "\0")`): CString::new refuses
    /// it and the event is lost.
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
        let core: &'static core::Core = Box::leak(Box::new(core::Core::new(rt, dir.clone(), emitter)));
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
