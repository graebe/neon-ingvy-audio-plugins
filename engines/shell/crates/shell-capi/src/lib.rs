/*
 * The shell_handoff_* C ABI.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * `engines/shell/include/shell_handoff.h` is the contract, written by hand for
 * the reason every header in this repository is. The mechanism and its proof
 * are shell_core::Handoff; this only carries it across the boundary with an
 * opaque pointer and the caller's release function.
 */

use shell_core::Handoff;
use std::ffi::c_void;

pub type ReleaseFn = unsafe extern "C" fn(*mut c_void);

pub struct ShellHandoff {
    inner: Handoff<c_void>,
    release: ReleaseFn,
}

/// A handoff whose retired objects are released through `release`. Allocates;
/// the main thread only.
#[no_mangle]
pub extern "C" fn shell_handoff_new(release: Option<ReleaseFn>) -> *mut ShellHandoff {
    let Some(release) = release else { return std::ptr::null_mut() };
    Box::into_raw(Box::new(ShellHandoff { inner: Handoff::new(), release }))
}

/// Release the live object and every retired one, then the handoff. Only
/// once no audio thread can reach it.
///
/// # Safety
/// `h` is null or from `shell_handoff_new`, and not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_free(h: *mut ShellHandoff) {
    if h.is_null() {
        return;
    }
    let mut h = Box::from_raw(h);
    let release = h.release;
    h.inner.clear(|p| release(p));
}

/// The audio thread's hold on the live object, or null. Pair with
/// `shell_handoff_release` before the block ends. Wait-free, allocation-free.
///
/// # Safety
/// `h` is null or live.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_acquire(h: *const ShellHandoff) -> *mut c_void {
    h.as_ref().map_or(std::ptr::null_mut(), |h| h.inner.acquire())
}

/// # Safety
/// `h` is null or live.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_release(h: *const ShellHandoff) {
    if let Some(h) = h.as_ref() {
        h.inner.release();
    }
}

/// The object last installed. The main thread only.
///
/// # Safety
/// `h` is null or live.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_current(h: *const ShellHandoff) -> *mut c_void {
    h.as_ref().map_or(std::ptr::null_mut(), |h| h.inner.current())
}

/// Install `next` (null for none) and retire the previous object. The main
/// thread only. An object once retired must never be installed again.
///
/// # Safety
/// `h` is null or live.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_set(h: *const ShellHandoff, next: *mut c_void) {
    if let Some(h) = h.as_ref() {
        h.inner.set(next);
    }
}

/// Release every retired object the audio thread is not holding. Returns how
/// many are still waiting. The main thread only.
///
/// # Safety
/// `h` is null or live.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_collect(h: *const ShellHandoff) -> i32 {
    let Some(h) = h.as_ref() else { return 0 };
    let release = h.release;
    h.inner.collect(|p| release(p)) as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    static RELEASED: AtomicUsize = AtomicUsize::new(0);

    unsafe extern "C" fn release(p: *mut c_void) {
        RELEASED.fetch_add(1, Ordering::SeqCst);
        drop(Box::from_raw(p as *mut u64));
    }

    fn boxed(v: u64) -> *mut c_void {
        Box::into_raw(Box::new(v)) as *mut c_void
    }

    #[test]
    fn the_c_surface_defers_and_then_releases() {
        unsafe {
            assert!(shell_handoff_new(None).is_null());
            assert!(shell_handoff_acquire(std::ptr::null()).is_null());

            let h = shell_handoff_new(Some(release));
            let a = boxed(1);
            shell_handoff_set(h, a);
            assert_eq!(shell_handoff_current(h), a);

            let held = shell_handoff_acquire(h);
            assert_eq!(held, a);
            shell_handoff_set(h, boxed(2));
            assert_eq!(shell_handoff_collect(h), 1);
            assert_eq!(RELEASED.load(Ordering::SeqCst), 0);
            shell_handoff_release(h);
            assert_eq!(shell_handoff_collect(h), 0);
            assert_eq!(RELEASED.load(Ordering::SeqCst), 1);

            shell_handoff_free(h);
            assert_eq!(RELEASED.load(Ordering::SeqCst), 2, "the live one too");
        }
    }
}
