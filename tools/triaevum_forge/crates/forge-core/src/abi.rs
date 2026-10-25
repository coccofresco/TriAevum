// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Forge ABI v1: the stable C interface the Android installer and the game's
//! host use. Declarations live in `include/triaevum_forge.h`.
//!
//! Requests, events and results are versioned UTF-8 JSON in byte spans.
//! Buffers Rust returns are released only by `TriAevumForgeFreeBuffer`.
//! Events are polled (never called back from worker threads), so a JNI or UI
//! host decides on which thread it handles them. Panics are caught here and
//! become `internal` errors; nothing unwinds across the boundary.

// The safety contract of every exported function is the C/Java interface
// documented above and in include/triaevum_forge.h; it is not repeated per item.
#![allow(clippy::missing_safety_doc)]

use std::collections::VecDeque;
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;

use serde_json::{json, Value};

use crate::cancel::CancelToken;
use crate::error::{ErrorKind, ForgeError};
use crate::jobs;

pub const ABI_VERSION: u32 = 1;

#[repr(C)]
pub struct TriAevumForgeBuffer {
    pub data: *mut u8,
    pub len: usize,
}

impl TriAevumForgeBuffer {
    fn empty() -> Self {
        Self { data: std::ptr::null_mut(), len: 0 }
    }

    fn from_vec(bytes: Vec<u8>) -> Self {
        let mut boxed = bytes.into_boxed_slice();
        let buffer = Self { data: boxed.as_mut_ptr(), len: boxed.len() };
        std::mem::forget(boxed);
        buffer
    }
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TriAevumForgeStatus {
    Ok = 0,
    InvalidArgument = 1,
    NoEvent = 2,
    Busy = 3,
    Failed = 4,
}

/// Terminal and progress events of one job, in order. The terminal event
/// (`succeeded`, `failed` or `cancelled`) is queued exactly once.
#[derive(Default)]
struct EventQueue {
    events: VecDeque<Value>,
    terminal: bool,
}

pub struct Job {
    queue: Arc<(Mutex<EventQueue>, Condvar)>,
    cancel: CancelToken,
    thread: Option<JoinHandle<()>>,
}

/// Sink a running job reports through. Progress may be coalesced by the
/// consumer; stage and terminal events are always kept.
#[derive(Clone)]
pub struct EventSink {
    queue: Arc<(Mutex<EventQueue>, Condvar)>,
}

impl EventSink {
    pub fn emit(&self, event: Value) {
        let (lock, signal) = &*self.queue;
        let mut queue = lock.lock().unwrap_or_else(|poison| poison.into_inner());
        queue.events.push_back(event);
        signal.notify_all();
    }

    pub fn stage(&self, stage: &str, message: &str) {
        self.emit(json!({"event": "stage", "stage": stage, "message": message}));
    }

    pub fn progress(&self, stage: &str, done: u64, total: Option<u64>) {
        self.emit(json!({"event": "progress", "stage": stage, "done": done, "total": total}));
    }

    fn finish(&self, event: Value) {
        let (lock, signal) = &*self.queue;
        let mut queue = lock.lock().unwrap_or_else(|poison| poison.into_inner());
        if !queue.terminal {
            queue.terminal = true;
            queue.events.push_back(event);
        }
        signal.notify_all();
    }
}

pub fn error_event(error: &ForgeError) -> Value {
    if error.kind == ErrorKind::Cancelled {
        return json!({"event": "cancelled"});
    }
    json!({
        "event": "failed",
        "error": {
            "code": error.kind.code(),
            "stage": error.stage,
            "message": error.message,
            "os_code": error.os_code,
            "retryable": matches!(error.kind, ErrorKind::Busy | ErrorKind::StorageFull),
        },
    })
}

impl Job {
    /// Runs `work` on a worker thread. Its result becomes the terminal event.
    pub fn spawn(work: impl FnOnce(&EventSink, &CancelToken) -> crate::Result<Value> + Send + 'static) -> Self {
        let queue = Arc::new((Mutex::new(EventQueue::default()), Condvar::new()));
        let cancel = CancelToken::new();
        let sink = EventSink { queue: Arc::clone(&queue) };
        let token = cancel.clone();
        let thread = std::thread::spawn(move || {
            let outcome = catch_unwind(AssertUnwindSafe(|| work(&sink, &token)));
            let event = match outcome {
                Ok(Ok(result)) => json!({"event": "succeeded", "result": result}),
                Ok(Err(error)) => error_event(&error),
                Err(_) => error_event(&ForgeError::internal("Forge stopped on an internal error")),
            };
            sink.finish(event);
        });
        Self { queue, cancel, thread: Some(thread) }
    }

    pub fn cancel(&self) {
        self.cancel.cancel();
    }

    /// Pops the next event, waiting up to `timeout_ms` (0 = do not wait).
    pub fn poll(&self, timeout_ms: u32) -> Option<Value> {
        let (lock, signal) = &*self.queue;
        let mut queue = lock.lock().unwrap_or_else(|poison| poison.into_inner());
        if queue.events.is_empty() && timeout_ms > 0 && !queue.terminal {
            let duration = std::time::Duration::from_millis(u64::from(timeout_ms));
            queue = signal
                .wait_timeout_while(queue, duration, |q| q.events.is_empty() && !q.terminal)
                .unwrap_or_else(|poison| poison.into_inner())
                .0;
        }
        queue.events.pop_front()
    }

    pub fn join(&mut self) {
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

impl Drop for Job {
    fn drop(&mut self) {
        self.cancel();
        self.join();
    }
}

fn guarded<T>(fallback: T, body: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(body)).unwrap_or(fallback)
}

fn span<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
    if data.is_null() {
        return (len == 0).then_some(&[][..]);
    }
    // SAFETY: the caller passes a readable span of `len` bytes for the call.
    Some(unsafe { std::slice::from_raw_parts(data, len) })
}

#[no_mangle]
pub extern "C" fn TriAevumForgeGetAbiVersion() -> u32 {
    ABI_VERSION
}

/// Capabilities as JSON: target, accepted input formats and stage support.
#[no_mangle]
pub extern "C" fn TriAevumForgeGetCapabilities() -> TriAevumForgeBuffer {
    guarded(TriAevumForgeBuffer::empty(), || {
        TriAevumForgeBuffer::from_vec(crate::legacy_json::compact_line_bytes(&jobs::capabilities()))
    })
}

#[no_mangle]
pub unsafe extern "C" fn TriAevumForgeFreeBuffer(buffer: TriAevumForgeBuffer) {
    if buffer.data.is_null() {
        return;
    }
    // SAFETY: the buffer was produced by `from_vec` and is freed once.
    unsafe {
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(buffer.data, buffer.len)));
    }
}

/// Starts a job from a JSON request (`{"format":"triaevum_forge_request_v1",
/// "job":..., ...}`). On success `*job_out` owns the job until
/// `TriAevumForgeDestroyJob`. Invalid requests fail without a job.
#[no_mangle]
pub unsafe extern "C" fn TriAevumForgeStartJob(
    request: *const u8,
    request_len: usize,
    job_out: *mut *mut c_void,
    error_out: *mut TriAevumForgeBuffer,
) -> TriAevumForgeStatus {
    guarded(TriAevumForgeStatus::Failed, || {
        if job_out.is_null() {
            return TriAevumForgeStatus::InvalidArgument;
        }
        let Some(bytes) = span(request, request_len) else {
            return TriAevumForgeStatus::InvalidArgument;
        };
        match jobs::start(bytes) {
            Ok(job) => {
                // SAFETY: checked non-null above.
                unsafe { *job_out = Box::into_raw(Box::new(job)).cast() };
                TriAevumForgeStatus::Ok
            }
            Err(error) => {
                if !error_out.is_null() {
                    let bytes = crate::legacy_json::compact_line_bytes(&error_event(&error));
                    // SAFETY: the caller passed a writable buffer slot.
                    unsafe { *error_out = TriAevumForgeBuffer::from_vec(bytes) };
                }
                TriAevumForgeStatus::Failed
            }
        }
    })
}

fn with_job<T>(job: *mut c_void, fallback: T, body: impl FnOnce(&mut Job) -> T) -> T {
    if job.is_null() {
        return fallback;
    }
    // SAFETY: `job` came from `TriAevumForgeStartJob` and is not yet destroyed.
    let job = unsafe { &mut *job.cast::<Job>() };
    guarded(fallback, || body(job))
}

/// Writes the next event into `*event_out`, or returns `NoEvent`.
#[no_mangle]
pub unsafe extern "C" fn TriAevumForgePollEvent(
    job: *mut c_void,
    timeout_ms: u32,
    event_out: *mut TriAevumForgeBuffer,
) -> TriAevumForgeStatus {
    if event_out.is_null() {
        return TriAevumForgeStatus::InvalidArgument;
    }
    with_job(job, TriAevumForgeStatus::InvalidArgument, |job| match job.poll(timeout_ms) {
        Some(event) => {
            let bytes = crate::legacy_json::compact_line_bytes(&event);
            // SAFETY: checked non-null above.
            unsafe { *event_out = TriAevumForgeBuffer::from_vec(bytes) };
            TriAevumForgeStatus::Ok
        }
        None => TriAevumForgeStatus::NoEvent,
    })
}

#[no_mangle]
pub unsafe extern "C" fn TriAevumForgeCancelJob(job: *mut c_void) -> TriAevumForgeStatus {
    with_job(job, TriAevumForgeStatus::InvalidArgument, |job| {
        job.cancel();
        TriAevumForgeStatus::Ok
    })
}

/// Waits for the worker to finish. Remaining events stay pollable.
#[no_mangle]
pub unsafe extern "C" fn TriAevumForgeJoinJob(job: *mut c_void) -> TriAevumForgeStatus {
    with_job(job, TriAevumForgeStatus::InvalidArgument, |job| {
        job.join();
        TriAevumForgeStatus::Ok
    })
}

/// Cancels, joins and frees a job. The pointer is invalid afterwards.
#[no_mangle]
pub unsafe extern "C" fn TriAevumForgeDestroyJob(job: *mut c_void) {
    if job.is_null() {
        return;
    }
    // SAFETY: `job` came from `TriAevumForgeStartJob`; destroyed once.
    let job = unsafe { Box::from_raw(job.cast::<Job>()) };
    let _ = catch_unwind(AssertUnwindSafe(move || drop(job)));
}
