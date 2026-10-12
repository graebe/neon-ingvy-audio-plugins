// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Designing a filter and running it allocate nothing: both happen on an
//! audio callback.

use assert_no_alloc::{assert_no_alloc, AllocDisabler};
use ni_filter::{all_pass, bell, family, high_shelf, low_shelf, tilt, Band, Family, Prototype, State};

#[global_allocator]
static A: AllocDisabler = AllocDisabler;

#[test]
fn designing_and_running_allocate_nothing() {
    let mut out = 0.0;
    assert_no_alloc(|| {
        for f in Family::ALL {
            for n in 1..=8 {
                let p = Prototype::new(f, n, 1.0, 60.0);
                for band in [Band::LowCut, Band::HighCut, Band::BandPass, Band::Notch] {
                    let (c, _) = family(&p, band, 1000.0, 2.0, 48_000.0);
                    let mut st = State::default();
                    out += c.run(&mut st, 1.0);
                    out += c.response(0.3).norm();
                }
            }
        }
        for c in [bell(1000.0, 6.0, 1.0, 48_000.0), low_shelf(100.0, 3.0, 1.0, 3, 48_000.0),
                  high_shelf(8000.0, -3.0, 1.0, 4, 48_000.0), tilt(700.0, 4.0, 48_000.0), all_pass(300.0, 1.0, 5, 48_000.0)] {
            let mut st = State::default();
            out += c.run(&mut st, 0.5);
        }
    });
    assert!(out.is_finite());
    assert_eq!(assert_no_alloc::violation_count(), 0);
}
