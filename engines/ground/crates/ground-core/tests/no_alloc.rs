/*
 * The audio thread allocates nothing, asserted rather than claimed.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * `Ground::tick` runs in every plugin's ProcessBlock, once a block, so it is
 * measured through every branch a host can send it into: playing, stopped, a
 * jump, a stall, a meter and a tempo change, the requests (reset, rate,
 * active) the other threads post and the tick applies.
 *
 * THIS FILE MUST HOLD EXACTLY ONE TEST. The counter is global and cargo runs
 * tests in threads, so a second test in this binary could allocate inside the
 * measured window and the failure would look like a real regression.
 */

use ground_core::{Ground, Transport};

#[global_allocator]
static ALLOCATOR: ni_testkit::Counting = ni_testkit::Counting;

#[test]
fn tick_and_the_requests_it_applies_allocate_nothing() {
    let g = Ground::new(48_000.0); /* allocates nothing either, but is allowed to */
    g.set_active(true);

    ni_testkit::arm();
    let mut ppq = 0.0;
    for block in 0..4_096usize {
        let (num, den) = [(4, 4), (7, 8), (0, 0), (6, 8)][block / 1_024];
        let t = Transport {
            playing: block % 300 < 290,
            ppq,
            bpm: if block % 700 < 350 { 120.0 } else { 999.0 },
            num,
            den,
        };
        unsafe { g.tick(&t, 512) };
        ppq += 512.0 * t.bpm / (60.0 * 48_000.0);
        match block % 97 {
            0 => ppq = 4.0,      /* a loop */
            50 => ppq += 37.25,  /* a seek */
            _ => {}
        }
        if block % 211 == 0 {
            g.reset();
        }
        if block % 509 == 0 {
            g.set_sample_rate(if block % 2 == 0 { 44_100.0 } else { 48_000.0 });
        }
        if block % 401 == 0 {
            g.set_active(false);
            g.set_active(true);
        }
        let _ = (g.fires(), g.strength());
    }
    ni_testkit::disarm();

    let (a, f) = (ni_testkit::allocs(), ni_testkit::frees());
    assert!(g.fires() > 100, "the measured window rang ({})", g.fires());
    assert_eq!((a, f), (0, 0), "the audio path allocated {a} times and freed {f} times");
}
