/*
 * The audio side of the bridge and the handoff allocates nothing, asserted
 * rather than claimed.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * ni_testkit's counting allocator, armed only across the calls the audio
 * thread makes; allocations are asserted. THIS FILE MUST HOLD EXACTLY ONE TEST
 * -- the counter is global and cargo runs tests in threads.
 */

use shell_core::{Bridge, Handoff, Model, Text};

#[global_allocator]
static ALLOCATOR: ni_testkit::Counting = ni_testkit::Counting;

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

    ni_testkit::arm();
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
    ni_testkit::disarm();

    assert_eq!(ni_testkit::allocs(), 0, "the audio side reached the allocator");
    assert_eq!(b.read(|r| r.frame.as_str().to_owned()), "t".repeat(12));

    let mut h = h;
    h.clear(|p| unsafe { drop(Box::from_raw(p)) });
}
