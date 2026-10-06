// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio side of the bridge and the handoff allocates nothing, asserted
 * rather than claimed.
 *
 * ni_testkit's counting allocator, armed only across the calls the audio
 * thread makes; allocations and frees are asserted, since a free takes the
 * allocator's lock as surely as a malloc does. THIS FILE MUST HOLD EXACTLY ONE
 * TEST -- the counter is global and cargo runs tests in threads.
 */

use shell_core::{Bridge, Handoff, Model, Shared, Text};

#[global_allocator]
static ALLOCATOR: ni_testkit::Counting = ni_testkit::Counting;

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

    ni_testkit::arm();
    for _ in 0..64 {
        unsafe {
            let e = b.begin();
            assert!(e.len <= 64);
            b.end(8);
            assert_eq!(h.acquire(), Some(&7));
            h.release();
        }
    }
    ni_testkit::disarm();

    /* THE LAST REFERENCE, LET GO OF ON THE AUDIO THREAD. The main thread
     * retires the object mid-block, so the block's release is the one that
     * drops it -- and that must queue it for the collector, not free it. */
    ni_testkit::arm();
    let held = unsafe { h.acquire() }.copied();
    ni_testkit::disarm();
    h.set(Some(8));
    ni_testkit::arm();
    unsafe { h.release() };
    ni_testkit::disarm();

    assert_eq!(ni_testkit::allocs(), 0, "the audio side reached the allocator");
    assert_eq!(ni_testkit::frees(), 0, "the audio side freed");
    assert_eq!(held, Some(7));
    assert_eq!(h.collect(), 0, "freed by the collector instead");
    assert_eq!(b.read(|r| r.frame.as_str().to_owned()), "t".repeat(12));
}
