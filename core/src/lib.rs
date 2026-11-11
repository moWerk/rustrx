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

mod core;
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
        .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("warn,rustrx=info"));
    let _ = tracing_subscriber::fmt()
        .with_env_filter(filter)
        .with_ansi(false)
        .without_time()
        .with_writer(|| LogWriter::default())
        .try_init();
    let rt = match tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .thread_name("rustrx")
        .enable_all()
        .build()
    {
        Ok(rt) => rt,
        Err(_) => return 3,
    };
    // a panicking task would otherwise vanish: the sandbox drops stderr
    std::panic::set_hook(Box::new(|info| {
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
    let text = unsafe { CStr::from_ptr(json) }.to_string_lossy();
    match serde_json::from_str::<serde_json::Value>(&text) {
        Ok(value) => {
            core.dispatch(value);
            0
        }
        Err(_) => 2,
    }
}
