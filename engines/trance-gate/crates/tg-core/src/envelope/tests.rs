// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The curve algebra: endpoints, bounds, and what a NaN turns into.

use crate::envelope::{shape, shape_inv, Curve};

const CURVES: [Curve; 3] = [Curve::Linear, Curve::Exp, Curve::SCurve];

#[test]
fn a_nan_position_is_the_start_of_the_stage_not_a_nan_gain() {
    /* `t <= 0.0` is false for a NaN, and so is `t >= 1.0`, so a NaN used to
     * fall through both guards into the curve and come out as a NaN level --
     * which the gain law then multiplied into every later sample. */
    for c in CURVES {
        assert_eq!(shape(c, f64::NAN), 0.0, "{c:?}");
        assert_eq!(shape_inv(c, f64::NAN), 0.0, "{c:?} inverse");
    }
}

#[test]
fn shape_endpoints_and_bounds() {
    for c in CURVES {
        assert_eq!(shape(c, 0.0), 0.0);
        assert_eq!(shape(c, 1.0), 1.0);
        assert_eq!(shape(c, -3.0), 0.0);
        assert_eq!(shape(c, 7.0), 1.0);
        for i in 0..=100 {
            let t = i as f64 / 100.0;
            let w = shape(c, t);
            assert!((0.0..=1.0).contains(&w), "{c:?} {t}");
            assert!((shape_inv(c, w) - t).abs() < 1e-9, "{c:?} inverse at {t}");
        }
    }
}
