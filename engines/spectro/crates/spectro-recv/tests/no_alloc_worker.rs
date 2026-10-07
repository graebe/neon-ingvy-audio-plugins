// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The receiver's worker allocates nothing once it is running.
 *
 * no_alloc.rs measures the pump when it is called by hand. This measures it
 * where the plugin runs it -- on the worker thread, woken by its own timer --
 * and counts every allocation in the process while it works, so anything the
 * thread's own loop does (the clock, the parking, the hand-over) is counted
 * too. The test thread in the window only pushes, drains and sleeps.
 *
 * SO THE COUNTER IS THE PROCESS'S, not assert_no_alloc's. That guard, the one
 * every other no_alloc test installs, watches the thread that runs its
 * closure, and the thread measured here is one the receiver starts itself;
 * no closure of the test's reaches it. A global counter is what sees it, and
 * a global counter is why THIS FILE MUST HOLD EXACTLY ONE TEST: cargo runs
 * tests in threads, and a second one could allocate inside the window.
 */

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::time::{Duration, Instant};

use spectro_core::Config;
use spectro_recv::{Receiver, OWN};

struct Counting;

static ARMED: AtomicBool = AtomicBool::new(false);
static ALLOCS: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
        }
        System.alloc(layout)
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
        }
        System.dealloc(ptr, layout)
    }
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
        }
        System.realloc(ptr, layout, new_size)
    }
}

#[global_allocator]
static ALLOCATOR: Counting = Counting;

/// Columns the default configuration draws from `blocks` blocks of 1024.
fn columns_after(blocks: usize) -> usize {
    let c = Config::default();
    if blocks * 1024 < c.fft_size {
        0
    } else {
        (blocks * 1024 - c.fft_size) / c.hop + 1
    }
}

#[test]
fn the_running_worker_allocates_nothing() {
    let (mut r, mut feed) = Receiver::new(Config::default());
    assert!(r.start());

    let block: Vec<f32> = (0..1024).map(|i| (i as f32 * 0.01).sin() * 0.5).collect();
    let mut out = vec![0u8; r.bands() * 32];
    let mut cols = 0usize;
    let deadline = Instant::now() + Duration::from_secs(20);

    /*
     * Each block waits until the worker has drawn to within three blocks of
     * it: a condition, so a slow or descheduled worker makes the test slower,
     * never wrong -- the ring cannot overflow. The first 16 blocks, unmeasured,
     * let the thread start, take its engine and run.
     */
    let mut pushed = 0usize;
    let mut step = |r: &mut Receiver, cols: &mut usize| {
        feed.push(&block);
        pushed += 1;
        while *cols < columns_after(pushed.saturating_sub(3)) {
            *cols += r.take_columns(OWN, &mut out, 32);
            if Instant::now() > deadline {
                return false;
            }
            std::thread::sleep(Duration::from_millis(1));
        }
        *cols += r.take_columns(OWN, &mut out, 32);
        true
    };
    for _ in 0..16 {
        assert!(step(&mut r, &mut cols), "the worker never started drawing");
    }

    ARMED.store(true, Ordering::Relaxed);
    let mut kept_up = true;
    for _ in 0..100 {
        kept_up &= step(&mut r, &mut cols);
    }
    let dropped = r.own_dropped();
    ARMED.store(false, Ordering::Relaxed);

    let n = ALLOCS.load(Ordering::Relaxed);
    assert_eq!(n, 0, "allocated or freed {n} times while the worker ran");
    assert!(kept_up, "the worker stopped drawing");
    assert!(cols >= columns_after(113), "no columns were produced, so nothing was measured");
    assert_eq!(dropped, 0, "the ring overflowed during the measurement");
}
