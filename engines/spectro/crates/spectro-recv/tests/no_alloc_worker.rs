/*
 * The receiver's worker allocates nothing once it is running.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * no_alloc.rs measures the pump when it is called by hand. This measures it
 * where the plugin runs it -- on the worker thread, woken by its own timer --
 * and counts every allocation in the process while it works, so anything the
 * thread's own loop does (the clock, the parking, the hand-over) is counted
 * too. The test thread in the window only pushes, drains and sleeps.
 *
 * THIS FILE MUST HOLD EXACTLY ONE TEST, for the reason no_alloc.rs gives: the
 * counter is global.
 */

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::time::Duration;

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

#[test]
fn the_running_worker_allocates_nothing() {
    let (mut r, mut feed) = Receiver::new(Config::default());
    assert!(r.start());

    let block: Vec<f32> = (0..1024).map(|i| (i as f32 * 0.01).sin() * 0.5).collect();
    let mut out = vec![0u8; r.bands() * 32];
    let mut cols = 0usize;

    /* Let the thread start, take its engine and run a few ticks unmeasured. */
    for _ in 0..16 {
        feed.push(&block);
        std::thread::sleep(Duration::from_millis(2));
        cols += r.take_columns(OWN, &mut out, 32);
    }

    ARMED.store(true, Ordering::Relaxed);
    for _ in 0..100 {
        feed.push(&block);
        std::thread::sleep(Duration::from_millis(2));
        cols += r.take_columns(OWN, &mut out, 32);
    }
    let dropped = r.own_dropped();
    ARMED.store(false, Ordering::Relaxed);

    let n = ALLOCS.load(Ordering::Relaxed);
    assert_eq!(n, 0, "allocated or freed {n} times while the worker ran");
    assert!(cols > 0, "no columns were produced, so nothing was measured");
    assert_eq!(dropped, 0, "the ring overflowed during the measurement");
}
