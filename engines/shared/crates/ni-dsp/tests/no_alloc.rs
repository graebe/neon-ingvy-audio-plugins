// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The text on the wire costs the audio thread no allocation, asserted rather
 * than claimed.
 *
 * Both halves run on an audio callback: a Move module's `get_param` writes
 * through `Buf`, `f` and `g`, and its `set_param` reads through `atof` and
 * `atoi`. The reading is lexical-core's, which promises a stack-only parse at
 * any length -- a mantissa past its fast path included, the one with a big
 * integer in it -- and this holds it to that, along with every branch of the
 * writing: both %g styles, a truncated Buf, the non-finite values.
 *
 * The guard is assert_no_alloc's, and it watches one thread: inside the
 * closure, an allocation or a free on the thread that runs it is a violation,
 * while the threads cargo runs other tests on are not watched at all. It
 * counts rather than aborts (warn_debug, warn_release), so the assertion
 * below can say how many.
 */

use core::fmt::Write;
use ni_dsp::fmt::{atof, atoi, f, g, Buf};
use std::hint::black_box;

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

#[test]
fn reading_and_writing_numbers_allocates_nothing() {
    /* Built before the window opens: eight hundred digits of mantissa. */
    let long = format!("0.{}e-3", "1234567890".repeat(80));
    let texts = [
        "0.5abc", "  -3.25e-2x", "1e", "5.e-", "inf", "nan", "0x10", "", "-",
        "9223372036854775808", "123456789012", "1e400", long.as_str(),
    ];
    let values = [0.0, -0.0, 1.0 / 3.0, 199.999985, 9.9999999996, 1e-5, 1e300, 5e-324, f64::NAN, f64::NEG_INFINITY];
    let mut out = [0u8; 48];

    let sum = assert_no_alloc(|| {
        let mut sum = 0.0;
        for _ in 0..64 {
            for t in texts {
                sum += black_box(atof(black_box(t)));
                sum += black_box(atoi(black_box(t))) as f64;
            }
            for v in values {
                for sig in [1, 9, 17, 40] {
                    let mut b = Buf::new(&mut out);
                    let _ = g(&mut b, black_box(v), sig);
                    let _ = b.write_char(':');
                    /* %f of 1e300 is three hundred digits into 48 bytes: the
                     * truncating path, every time. */
                    let _ = f(&mut b, black_box(v), 3);
                    sum += b.finish() as f64;
                }
            }
        }
        sum
    });

    black_box(sum);
    let n = violation_count();
    assert_eq!(n, 0, "reading and writing allocated or freed {n} times");
}
