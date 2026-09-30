/*
 * The shell_handoff_* C ABI, called the way a plugin calls it.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * shell-core proves the hazard pointer, two threads and all; tests/
 * shell_handoff.c proves the hand-written header against a real bus pusher.
 * These prove the THIN LAYER between them -- the null guards, the missing
 * release function, and that every object handed in is released exactly
 * once, through the caller's function, at the moment the header says.
 *
 * EACH OBJECT CARRIES ITS OWN COUNTER, rather than one static: cargo runs
 * these tests on parallel threads, and a shared tally would let one test's
 * releases land in another's assertion.
 */

use super::*;
use std::ptr::{null, null_mut};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

/// What a plugin would hand over -- here, a pointer to its own release tally.
type Tally = Arc<AtomicUsize>;

unsafe extern "C" fn release(p: *mut c_void) {
    let tally = Box::from_raw(p as *mut Tally);
    tally.fetch_add(1, Ordering::SeqCst);
}

fn object(tally: &Tally) -> *mut c_void {
    Box::into_raw(Box::new(Arc::clone(tally))) as *mut c_void
}

fn released(tally: &Tally) -> usize {
    tally.load(Ordering::SeqCst)
}

#[test]
fn no_release_function_means_no_handoff() {
    assert!(shell_handoff_new(None).is_null());
}

#[test]
fn a_null_handoff_is_nothing_everywhere() {
    /* The audio thread calls acquire/release unconditionally; a plugin that
     * has no handoff yet must get "nothing to use", not a crash. */
    unsafe {
        assert!(shell_handoff_acquire(null()).is_null());
        shell_handoff_release(null());
        assert!(shell_handoff_current(null()).is_null());
        shell_handoff_set(null(), null_mut());
        assert_eq!(shell_handoff_collect(null()), 0);
        shell_handoff_free(null_mut());
    }
}

#[test]
fn an_empty_handoff_lends_nothing() {
    unsafe {
        let h = shell_handoff_new(Some(release));
        assert!(!h.is_null());
        assert!(shell_handoff_current(h).is_null());
        assert!(shell_handoff_acquire(h).is_null(), "a block with nothing installed");
        shell_handoff_release(h);
        assert_eq!(shell_handoff_collect(h), 0);
        shell_handoff_free(h);
    }
}

#[test]
fn a_retired_object_waits_for_the_block_that_holds_it() {
    let tally = Tally::default();
    unsafe {
        let h = shell_handoff_new(Some(release));
        let a = object(&tally);
        shell_handoff_set(h, a);
        assert_eq!(shell_handoff_current(h), a, "the main thread sees what it installed");

        let held = shell_handoff_acquire(h);
        assert_eq!(held, a, "the audio thread is lent it");

        let b = object(&tally);
        shell_handoff_set(h, b);
        assert_eq!(shell_handoff_current(h), b);
        assert_eq!(shell_handoff_collect(h), 1, "held, so still waiting");
        assert_eq!(released(&tally), 0, "and not released under the block");

        shell_handoff_release(h);
        assert_eq!(shell_handoff_collect(h), 0, "the block let go");
        assert_eq!(released(&tally), 1, "so the retired one went, through our function");

        assert_eq!(shell_handoff_acquire(h), b, "the next block gets the new one");
        shell_handoff_release(h);
        shell_handoff_free(h);
        assert_eq!(released(&tally), 2, "freeing released the live one too");
    }
}

#[test]
fn installing_nothing_retires_the_current_object() {
    let tally = Tally::default();
    unsafe {
        let h = shell_handoff_new(Some(release));
        shell_handoff_set(h, object(&tally));
        shell_handoff_set(h, null_mut());
        assert!(shell_handoff_current(h).is_null());
        assert_eq!(shell_handoff_collect(h), 0, "nobody held it");
        assert_eq!(released(&tally), 1);
        assert!(shell_handoff_acquire(h).is_null(), "the next block gets nothing");
        shell_handoff_release(h);
        shell_handoff_free(h);
        assert_eq!(released(&tally), 1, "nothing left to release");
    }
}

#[test]
fn freeing_releases_retired_objects_nobody_collected() {
    /* The destructor's path: whatever is still waiting goes with the handoff,
     * or it leaks -- and a leaked bus pusher is a slot held until the host
     * quits. */
    let tally = Tally::default();
    unsafe {
        let h = shell_handoff_new(Some(release));
        shell_handoff_set(h, object(&tally));
        shell_handoff_acquire(h);
        shell_handoff_set(h, object(&tally));
        shell_handoff_release(h);
        assert_eq!(released(&tally), 0, "not collected yet");
        shell_handoff_free(h);
        assert_eq!(released(&tally), 2, "one retired, one live, each exactly once");
    }
}
