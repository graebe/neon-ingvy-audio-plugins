// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing, asserted rather than claimed.
 *
 * `Ground::tick` runs in every plugin's ProcessBlock, once a block, so it is
 * measured through every branch a host can send it into: playing, stopped, a
 * jump, a stall, a meter and a tempo change, the requests (reset, rate,
 * active) the other threads post and the tick applies.
 *
 * The guard is assert_no_alloc's, and it watches one thread: inside the
 * closure, an allocation or a free on the thread that runs it is a violation,
 * while the threads cargo runs other tests on are not watched at all. It
 * counts rather than aborts (warn_debug, warn_release), so the assertion
 * below can say how many.
 */

use ground_core::{Ground, Transport};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

#[test]
fn tick_and_the_requests_it_applies_allocate_nothing() {
    let g = Ground::new(48_000.0); /* allocates nothing either, but is allowed to */
    g.set_active(true);

    assert_no_alloc(|| {
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
    });

    let n = violation_count();
    assert!(g.fires() > 100, "the measured window rang ({})", g.fires());
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
}
