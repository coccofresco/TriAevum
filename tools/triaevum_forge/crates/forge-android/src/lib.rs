// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! JNI bridge of the Android installer: `org.triaevum.android.forge.ForgeNative`.
//!
//! The same jobs as the C ABI, with Java byte arrays for the JSON spans. A
//! job handle is a `long` owned by the Java side until `destroyJob`. Events
//! are polled from a Java worker thread; no Java code runs on Forge threads.
//! Panics never cross into the JVM: they become an `internal` failure or an
//! exception.

// The safety contract of every exported function is the C/Java interface
// documented above and in include/triaevum_forge.h; it is not repeated per item.
#![allow(clippy::missing_safety_doc)]

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use forge_core::abi::Job;
use forge_core::legacy_json::compact_line_bytes;
use jni_sys::{jbyteArray, jclass, jint, jlong, jsize, JNIEnv};

unsafe fn byte_array(env: *mut JNIEnv, bytes: &[u8]) -> jbyteArray {
    let Ok(len) = jsize::try_from(bytes.len()) else { return ptr::null_mut() };
    let functions = &**env;
    let array = (functions.NewByteArray.unwrap())(env, len);
    if !array.is_null() {
        (functions.SetByteArrayRegion.unwrap())(env, array, 0, len, bytes.as_ptr().cast());
    }
    array
}

unsafe fn read_byte_array(env: *mut JNIEnv, array: jbyteArray) -> Option<Vec<u8>> {
    if array.is_null() {
        return None;
    }
    let functions = &**env;
    let len = usize::try_from((functions.GetArrayLength.unwrap())(env, array)).ok()?;
    let mut bytes = vec![0u8; len];
    (functions.GetByteArrayRegion.unwrap())(env, array, 0, len as jsize, bytes.as_mut_ptr().cast());
    Some(bytes)
}

unsafe fn throw(env: *mut JNIEnv, class: &str, message: &str) {
    let functions = &**env;
    let class = std::ffi::CString::new(class).unwrap_or_default();
    let message = std::ffi::CString::new(message.replace('\0', " ")).unwrap_or_default();
    let found = (functions.FindClass.unwrap())(env, class.as_ptr());
    if !found.is_null() {
        (functions.ThrowNew.unwrap())(env, found, message.as_ptr());
    }
}

fn job<'a>(handle: jlong) -> Option<&'a mut Job> {
    // SAFETY: handles come from startJob and stay valid until destroyJob.
    (handle != 0).then(|| unsafe { &mut *(handle as *mut Job) })
}

/// `static native byte[] capabilities()`
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_capabilities(
    env: *mut JNIEnv,
    _class: jclass,
) -> jbyteArray {
    match catch_unwind(|| compact_line_bytes(&forge_core::jobs::capabilities())) {
        Ok(bytes) => byte_array(env, &bytes),
        Err(_) => ptr::null_mut(),
    }
}

/// `static native long startJob(byte[] request)`. Throws
/// IllegalArgumentException with the failed event when the request is refused.
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_startJob(
    env: *mut JNIEnv,
    _class: jclass,
    request: jbyteArray,
) -> jlong {
    let Some(request) = read_byte_array(env, request) else {
        throw(env, "java/lang/IllegalArgumentException", "Forge request is null");
        return 0;
    };
    match catch_unwind(AssertUnwindSafe(|| forge_core::jobs::start(&request))) {
        Ok(Ok(job)) => Box::into_raw(Box::new(job)) as jlong,
        Ok(Err(error)) => {
            let event = compact_line_bytes(&forge_core::abi::error_event(&error));
            throw(env, "java/lang/IllegalArgumentException", &String::from_utf8_lossy(&event));
            0
        }
        Err(_) => {
            throw(env, "java/lang/IllegalStateException", "Forge stopped on an internal error");
            0
        }
    }
}

/// `static native byte[] pollEvent(long job, int timeoutMs)`; null if no event.
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_pollEvent(
    env: *mut JNIEnv,
    _class: jclass,
    handle: jlong,
    timeout_ms: jint,
) -> jbyteArray {
    let Some(job) = job(handle) else { return ptr::null_mut() };
    let timeout = u32::try_from(timeout_ms).unwrap_or(0);
    match catch_unwind(AssertUnwindSafe(|| job.poll(timeout))) {
        Ok(Some(event)) => byte_array(env, &compact_line_bytes(&event)),
        _ => ptr::null_mut(),
    }
}

/// `static native void cancelJob(long job)`
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_cancelJob(
    _env: *mut JNIEnv,
    _class: jclass,
    handle: jlong,
) {
    if let Some(job) = job(handle) {
        job.cancel();
    }
}

/// The bytes `inspect` returns for a request (`forge_core::state`).
pub fn inspect_json(request: &[u8]) -> Vec<u8> {
    forge_core::state::inspect_bytes(request)
}

/// The bytes `explain` returns for an event, error or notice
/// (`forge_core::explain`).
pub fn explain_json(input: &[u8]) -> Vec<u8> {
    forge_core::explain::explain_bytes(input)
}

/// `static native byte[] inspect(byte[] request)`: the installation state.
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_inspect(
    env: *mut JNIEnv,
    _class: jclass,
    request: jbyteArray,
) -> jbyteArray {
    let request = read_byte_array(env, request).unwrap_or_default();
    match catch_unwind(AssertUnwindSafe(|| inspect_json(&request))) {
        Ok(bytes) => byte_array(env, &bytes),
        Err(_) => ptr::null_mut(),
    }
}

/// The bytes `inventory` returns for a request (`forge_core::inventory`).
pub fn inventory_json(request: &[u8]) -> Vec<u8> {
    forge_core::inventory::inventory_bytes(request)
}

/// `static native byte[] inventory(byte[] request)`: where the installation
/// keeps its files, and whether the game or another Forge is running.
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_inventory(
    env: *mut JNIEnv,
    _class: jclass,
    request: jbyteArray,
) -> jbyteArray {
    let request = read_byte_array(env, request).unwrap_or_default();
    match catch_unwind(AssertUnwindSafe(|| inventory_json(&request))) {
        Ok(bytes) => byte_array(env, &bytes),
        Err(_) => ptr::null_mut(),
    }
}

/// The bytes `diagnostics` returns for a request (`forge_core::diagnostics`).
pub fn diagnostics_json(request: &[u8]) -> Vec<u8> {
    forge_core::diagnostics::diagnostics_bytes(request)
}

/// `static native byte[] diagnostics(byte[] request)`: the redacted report,
/// and the support bundle written where the request names it.
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_diagnostics(
    env: *mut JNIEnv,
    _class: jclass,
    request: jbyteArray,
) -> jbyteArray {
    let request = read_byte_array(env, request).unwrap_or_default();
    match catch_unwind(AssertUnwindSafe(|| diagnostics_json(&request))) {
        Ok(bytes) => byte_array(env, &bytes),
        Err(_) => ptr::null_mut(),
    }
}

/// `static native byte[] explain(byte[] input)`: an explanation, or JSON null.
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_explain(
    env: *mut JNIEnv,
    _class: jclass,
    input: jbyteArray,
) -> jbyteArray {
    let input = read_byte_array(env, input).unwrap_or_default();
    match catch_unwind(AssertUnwindSafe(|| explain_json(&input))) {
        Ok(bytes) => byte_array(env, &bytes),
        Err(_) => ptr::null_mut(),
    }
}

/// `static native void destroyJob(long job)`: cancels, joins and frees.
#[no_mangle]
pub unsafe extern "system" fn Java_org_triaevum_android_forge_ForgeNative_destroyJob(
    _env: *mut JNIEnv,
    _class: jclass,
    handle: jlong,
) {
    if handle != 0 {
        // SAFETY: the handle came from startJob and is destroyed once.
        let job = Box::from_raw(handle as *mut Job);
        let _ = catch_unwind(AssertUnwindSafe(move || drop(job)));
    }
}
