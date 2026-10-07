// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio side of the bridge and the handoff allocates nothing, asserted
 * rather than claimed.
 *
 * assert_no_alloc's guard, closed only around the calls the audio thread
 * makes; allocations and frees are both violations, since a free takes the
 * allocator's lock as surely as a malloc does. The guard watches the thread
 * that runs the closure, and no other, so the tests cargo runs beside this
 * one cannot trip it. It counts rather than aborts (warn_debug,
 * warn_release), so the assertion below can say how many.
 */

use shell_core::{Bridge, Handoff, Model, Shared, Text};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

/// A model that formats text into its frame, the way a product's does, from
/// commands whose payload rides in a `Shared`, the way a product's heavy ones
/// do.
struct Echo {
    last: [u8; 64],
    len: usize,
}

impl Model for Echo {
    type Command = Shared<Vec<u8>>;
    type Frame = Text;
    fn new_frame(&self) -> Text {
        Text::new(64)
    }
    fn apply(&mut self, cmd: &Shared<Vec<u8>>) {
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
    let b = Bridge::new(Echo { last: [0; 64], len: 0 }, None, 32, 16);
    let h = Handoff::new();
    h.set(Some(7u32));

    /* Commands waiting in the queue, posted before the window opens: posting
     * is the main thread's and allocates by design. */
    for i in 0..20u8 {
        b.post(Shared::new(b.handle(), vec![b'a' + i; 12]));
    }

    assert_no_alloc(|| {
        for _ in 0..64 {
            unsafe {
                let e = b.begin();
                assert!(e.len <= 64);
                b.end(8);
                assert_eq!(h.acquire(), Some(&7));
                h.release();
            }
        }
    });

    /* THE LAST REFERENCE, LET GO OF ON THE AUDIO THREAD. The main thread
     * retires the object mid-block, so the block's release is the one that
     * drops it -- and that must queue it for the collector, not free it. */
    let held = assert_no_alloc(|| unsafe { h.acquire() }.copied());
    h.set(Some(8));
    assert_no_alloc(|| unsafe { h.release() });

    let n = violation_count();
    assert_eq!(n, 0, "the audio side allocated or freed {n} times");
    assert_eq!(held, Some(7));
    assert_eq!(h.collect(), 0, "freed by the collector instead");
    assert_eq!(b.read(|r| r.frame.as_str().to_owned()), "t".repeat(12));
}
