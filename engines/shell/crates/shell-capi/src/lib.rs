// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The shell_handoff_* C ABI, and the source of shell_handoff.h, which build.rs
generates from this file (cbindgen's configuration is
cbindgen/shell_handoff.toml). The mechanism and its proof are
shell_core::Handoff; this only carries it across the boundary with an opaque
pointer, and wraps each object handed in with the caller's release function,
which runs when the handoff lets go of it.

AN OBJECT THE MAIN THREAD OWNS, LENT TO THE AUDIO THREAD. A bus pusher or an
analyzer receiver maps memory and allocates, so it is built and freed on the
main thread; the audio thread uses it in between. Swapping a plain pointer
frees the old object while a block may still be using it. This defers the free
until the audio thread has let go:

  audio thread, every block        main thread (idle)
  ------------------------         ------------------
  p = shell_handoff_acquire(h);    shell_handoff_set(h, fresh);  // retires old
  ... use p (may be NULL) ...      shell_handoff_collect(h);     // frees old
  shell_handoff_release(h);                                      // once unheld

THE RULES. One audio thread. acquire/release are wait-free and allocate
nothing; everything else is the main thread's. An object, once replaced, is
never installed again. shell_handoff_free releases everything and must only
run once no block can start -- in the plugin's destructor.

The mechanism is basedrop's reference counting: a block holds a counted copy,
and the last copy let go of -- on whichever thread -- is queued, never released
there; shell_handoff_collect and shell_handoff_free are where the release
function runs. engines/shell/crates/shell-core/src/handoff.rs has the argument
for why it is sound.
*/

use shell_core::Handoff;
use std::ffi::{c_int, c_void};
use std::ptr::null_mut;

/// How a handoff lets go of an object it was given: the caller's function,
/// called with the object, on the thread that collects or frees the handoff.
/// May be null in C, which `shell_handoff_new` refuses.
pub type ShellReleaseFn = Option<unsafe extern "C" fn(object: *mut c_void)>;

/* The same, once `shell_handoff_new` has checked it is there. */
type ReleaseFn = unsafe extern "C" fn(*mut c_void);

/* One object handed over by C, released through C's function when the last
 * reference to it goes -- which the handoff arranges to be on the thread that
 * collects. */
struct Lent {
    object: *mut c_void,
    release: ReleaseFn,
}

// SAFETY: the object is the caller's, lent across threads on the terms
// shell_handoff.h states: used by one audio thread at a time, released by the
// thread that collects or frees the handoff.
unsafe impl Send for Lent {}
unsafe impl Sync for Lent {}

impl Drop for Lent {
    fn drop(&mut self) {
        // SAFETY: `shell_handoff_set` took the object over, and this is the
        // one place it is let go of.
        unsafe { (self.release)(self.object) }
    }
}

pub struct ShellHandoff {
    inner: Handoff<Lent>,
    release: ReleaseFn,
}

impl ShellHandoff {
    fn current(&self) -> *mut c_void {
        self.inner.with_current(|l| l.map_or(null_mut(), |l| l.object))
    }
}

/// A handoff whose retired objects are released through `release`; NULL if
/// `release` is NULL. Allocates; the main thread only.
#[no_mangle]
pub extern "C" fn shell_handoff_new(release: ShellReleaseFn) -> *mut ShellHandoff {
    let Some(release) = release else { return null_mut() };
    Box::into_raw(Box::new(ShellHandoff { inner: Handoff::new(), release }))
}

/// Release the live object and every retired one, then the handoff. Only
/// once no audio thread can reach it.
///
/// # Safety
/// `h` is null or from `shell_handoff_new`, and not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_free(h: *mut ShellHandoff) {
    if !h.is_null() {
        drop(Box::from_raw(h));
    }
}

/// The audio thread's hold on the live object, or null. Pair with
/// `shell_handoff_release` before the block ends. Wait-free, allocation-free.
///
/// # Safety
/// `h` is null or live, and this is its one audio thread.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_acquire(h: *const ShellHandoff) -> *mut c_void {
    let Some(h) = h.as_ref() else { return null_mut() };
    h.inner.acquire().map_or(null_mut(), |l| l.object)
}

/// The audio thread's hold, let go of: the end of the block that acquired it.
/// Wait-free, allocation-free.
///
/// # Safety
/// `h` is null or live, and this is its one audio thread.
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
    h.as_ref().map_or(null_mut(), ShellHandoff::current)
}

/// Install `next` (null for none) and retire the previous object. The main
/// thread only. An object once retired must never be installed again; the
/// one installed now is not installed twice, which would release it twice.
///
/// # Safety
/// `h` is null or live.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_set(h: *const ShellHandoff, next: *mut c_void) {
    let Some(h) = h.as_ref() else { return };
    if next == h.current() {
        return;
    }
    /* `then`, not `then_some`: a Lent built for a null object and dropped
     * would hand C's release function a NULL. */
    h.inner.set((!next.is_null()).then(|| Lent { object: next, release: h.release }));
}

/// Release every retired object the audio thread is not holding. Returns how
/// many are still waiting. The main thread only.
///
/// # Safety
/// `h` is null or live.
#[no_mangle]
pub unsafe extern "C" fn shell_handoff_collect(h: *const ShellHandoff) -> c_int {
    h.as_ref().map_or(0, |h| h.inner.collect() as c_int)
}

#[cfg(test)]
mod tests;
