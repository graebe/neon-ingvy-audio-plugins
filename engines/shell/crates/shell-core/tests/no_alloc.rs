/*
 * The audio side of the bridge and the handoff allocates nothing, asserted
 * rather than claimed.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The idiom is spectro-core's: a counting global allocator, armed only across
 * the calls the audio thread makes. THIS FILE MUST HOLD EXACTLY ONE TEST -- the
 * counter is global and cargo runs tests in threads.
 */

use shell_core::{Bridge, Handoff, Model, Text};
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

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

/// A model that formats text into its frame, the way a product's does.
struct Echo {
    last: [u8; 64],
    len: usize,
}

impl Model for Echo {
    type Frame = Text;
    fn new_frame(&self) -> Text {
        Text::new(64)
    }
    fn apply(&mut self, cmd: &[u8]) {
        let n = cmd.len().min(self.last.len());
        self.last[..n].copy_from_slice(&cmd[..n]);
        self.len = n;
    }
    fn publish(&self, f: &mut Text) {
        f.fill(|out| {
            let n = self.len.min(out.len());
            out[..n].copy_from_slice(&self.last[..n]);
            n as i32
        });
    }
    fn restore(&mut self, _: &Text) {}
}

#[test]
fn the_audio_side_allocates_nothing() {
    let b = Bridge::new(Echo { last: [0; 64], len: 0 }, None, 1024, 64, 16);
    let h = Handoff::new();
    h.set(Box::into_raw(Box::new(7u32)));

    /* Commands waiting in the queue, posted before the window opens: posting
     * is the main thread's and allocates by design. */
    for i in 0..20u8 {
        b.post(&[b'a' + i; 12]);
    }

    ARMED.store(true, Ordering::SeqCst);
    for _ in 0..64 {
        unsafe {
            let e = b.begin();
            assert!(e.len <= 64);
            b.end(8);
        }
        let p = h.acquire();
        assert!(!p.is_null());
        h.release();
    }
    ARMED.store(false, Ordering::SeqCst);

    assert_eq!(ALLOCS.load(Ordering::SeqCst), 0, "the audio side reached the allocator");
    assert_eq!(b.read(|r| r.frame.as_str().to_owned()), "t".repeat(12));

    let mut h = h;
    h.clear(|p| unsafe { drop(Box::from_raw(p)) });
}
