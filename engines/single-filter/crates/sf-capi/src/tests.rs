// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The C ABI as C sees it, and the picture against what the engine plays.

use super::*;
use std::ffi::{CStr, CString};

fn text(f: impl Fn(*mut std::os::raw::c_char, i32) -> i32) -> String {
    let mut buf = [0 as std::os::raw::c_char; 64];
    assert!(f(buf.as_mut_ptr(), 64) >= 0);
    unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap().to_owned()
}

const SHAPES: [u32; 9] = [
    SF_SHAPE_HIGH_PASS,
    SF_SHAPE_LOW_PASS,
    SF_SHAPE_BAND_PASS,
    SF_SHAPE_NOTCH,
    SF_SHAPE_BELL,
    SF_SHAPE_LOW_SHELF,
    SF_SHAPE_HIGH_SHELF,
    SF_SHAPE_TILT,
    SF_SHAPE_ALL_PASS,
];

#[test]
fn each_products_table_crosses_whole() {
    assert_eq!(SHAPES.map(|s| sf_param_count(s)), [10, 10, 9, 9, 7, 7, 7, 5, 6]);
    assert_eq!(sf_param_count(0), 0, "Empty is no product");
    assert_eq!(sf_param_count(10), 0);
    assert!(SHAPES.iter().all(|&s| sf_param_count(s) as usize <= SF_PARAM_MAX));
    let mut info = NiParamInfo { kind: NiParamKind::Float, min: 0.0, max: 0.0, default_value: 0.0, log_travel: false, bipolar: false, resolution: 0.0, automatable: true };
    assert!(unsafe { sf_param_info(SF_SHAPE_HIGH_PASS, 0, &mut info) });
    assert!(info.log_travel);
    assert_eq!((info.min, info.max, info.default_value), (10.0, 22_000.0, 100.0));
    assert!(unsafe { sf_param_info(SF_SHAPE_HIGH_PASS, 9, &mut info) });
    assert!(!info.automatable, "the analyzer is a view");
    assert!(unsafe { !sf_param_info(SF_SHAPE_HIGH_PASS, 10, &mut info) });
    assert!(unsafe { !sf_param_info(0, 0, &mut info) });
    assert_eq!(text(|b, n| unsafe { sf_param_key(SF_SHAPE_BELL, 1, b, n) }), "gain");
    assert_eq!(text(|b, n| unsafe { sf_param_name(SF_SHAPE_LOW_PASS, 2, b, n) }), "Slope");
    assert_eq!(text(|b, n| unsafe { sf_param_unit(SF_SHAPE_LOW_PASS, 0, b, n) }), "Hz");
    assert_eq!(text(|b, n| unsafe { sf_param_label(SF_SHAPE_LOW_PASS, 3, 4, b, n) }), "Elliptic");
    assert_eq!(unsafe { sf_param_label(SF_SHAPE_LOW_PASS, 0, 0, std::ptr::null_mut(), 0) }, -1);
    assert_eq!(text(|b, n| unsafe { sf_param_text(SF_SHAPE_TILT, 1, 4.25, b, n) }), "+4.3 dB");
    let typed = CString::new("2.5k").unwrap();
    let mut v = 0.0;
    assert!(unsafe { sf_param_parse(SF_SHAPE_NOTCH, 0, typed.as_ptr(), &mut v) });
    assert_eq!(v, 2500.0);
    assert!(unsafe { !sf_param_parse(SF_SHAPE_NOTCH, 0, CString::new("high").unwrap().as_ptr(), &mut v) });
    assert!(unsafe { !sf_param_parse(SF_SHAPE_NOTCH, 99, typed.as_ptr(), &mut v) });
    assert_eq!(sf_param_denormalise(SF_SHAPE_BELL, 1, sf_param_normalise(SF_SHAPE_BELL, 1, 3.0)), 3.0);
    assert_eq!(sf_param_snap(SF_SHAPE_BELL, 1, 3.004), 3.0);
    assert_eq!(sf_param_normalise(SF_SHAPE_BELL, 99, 1.0), 0.0);
    assert_eq!(sf_param_snap(0, 0, 1.0), 0.0);
    assert_eq!(sf_param_denormalise(0, 0, 1.0), 0.0);
}

#[test]
fn where_each_product_declares_each_field() {
    assert_eq!(sf_field_index(SF_SHAPE_HIGH_PASS, SF_FIELD_MATCHED), 6);
    assert_eq!(sf_field_index(SF_SHAPE_HIGH_PASS, SF_FIELD_GAIN), -1);
    assert_eq!(sf_field_index(SF_SHAPE_BELL, SF_FIELD_ADAPTIVE_Q), 3);
    assert_eq!(sf_field_index(SF_SHAPE_TILT, SF_FIELD_Q), -1);
    assert_eq!(sf_field_index(SF_SHAPE_TILT, SF_FIELD_ANALYZER), 4);
    assert_eq!(sf_field_index(0, SF_FIELD_FREQ), -1);
    assert_eq!(sf_field_index(SF_SHAPE_TILT, 99), -1);
    for s in SHAPES {
        assert_eq!(sf_field_index(s, SF_FIELD_FREQ), 0, "Freq is always first");
        assert!(sf_field_index(s, SF_FIELD_OUTPUT) >= 0 && sf_field_index(s, SF_FIELD_CHANNEL) >= 0);
    }
}

#[test]
fn what_a_product_uses_now() {
    assert_eq!(sf_uses(SF_SHAPE_HIGH_PASS, 0, 0), 0, "a first-order Butterworth cut: no Q, no ripple");
    assert_eq!(sf_uses(SF_SHAPE_HIGH_PASS, 1, 4), SF_USES_Q | SF_USES_RIPPLE | SF_USES_STOPBAND);
    assert_eq!(sf_uses(SF_SHAPE_LOW_PASS, 3, 2), SF_USES_Q | SF_USES_RIPPLE);
    assert_eq!(sf_uses(SF_SHAPE_NOTCH, 0, 3), SF_USES_Q | SF_USES_STOPBAND);
    assert_eq!(sf_uses(SF_SHAPE_BELL, 0, 4), SF_USES_Q, "a bell has no family");
    assert_eq!(sf_uses(SF_SHAPE_TILT, 3, 0), 0);
    assert_eq!(sf_uses(0, 3, 0), 0);
    assert_eq!(sf_order_played(SF_SHAPE_HIGH_PASS, 5, 2), 3, "a Linkwitz-Riley cut is even");
    assert_eq!(sf_order_played(SF_SHAPE_LOW_SHELF, 0, 7), 3);
    assert_eq!(sf_order_played(0, 0, 7), 0);
    assert!((sf_q_for_edge(1000.0, 1000.0 * 2f64.sqrt()) - 1.414).abs() < 0.01);
}

#[test]
fn the_tail_reads_the_hosts_values() {
    let mut v = [100.0, 0.7071, 1.0, 0.0, 1.0, 60.0, 0.0, 0.0, 0.0, 3.0];
    let gentle = unsafe { sf_tail_seconds(SF_SHAPE_HIGH_PASS, v.as_ptr(), v.len(), 48_000.0) };
    (v[0], v[1], v[2], v[3]) = (20.0, 12.0, 7.0, 4.0);
    let ringing = unsafe { sf_tail_seconds(SF_SHAPE_HIGH_PASS, v.as_ptr(), v.len(), 48_000.0) };
    assert!(ringing > gentle && gentle > 0.0);
    assert_eq!(unsafe { sf_tail_seconds(SF_SHAPE_HIGH_PASS, v.as_ptr(), 3, 48_000.0) }, 0.0, "a whole table or none");
    assert_eq!(unsafe { sf_tail_seconds(SF_SHAPE_HIGH_PASS, std::ptr::null(), 10, 48_000.0) }, 0.0);
    assert_eq!(unsafe { sf_tail_seconds(0, v.as_ptr(), 10, 48_000.0) }, 0.0);
}

#[test]
fn an_instance_through_the_c_abi() {
    assert!(sf_core_new(0, 48_000.0).is_null(), "Empty is no product");
    let core = sf_core_new(SF_SHAPE_LOW_PASS, 48_000.0);
    assert!(!core.is_null());
    unsafe {
        sf_core_set_num(core, 0, 500.0);
        sf_core_set_num(core, 99, 1.0);
        assert_eq!(sf_core_num(core, 0), 500.0);
        assert!(sf_core_num(core, 99).is_nan());
        assert!(sf_core_num(std::ptr::null(), 0).is_nan());
        sf_core_set_sample_rate(core, 96_000.0);
        sf_core_reset(core);
        let (mut l, mut r) = (vec![0.5f32; 256], vec![0.5f32; 256]);
        sf_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), 256);
        assert!(!sf_core_is_silent(core));
        sf_core_on_midi(core, [0xB0u8, 123, 0].as_ptr(), 3);
        assert!(sf_core_is_silent(core));
        sf_core_listen_f32(core, l.as_mut_ptr(), std::ptr::null_mut(), 256);
        let mut i16s = vec![1000i16; 512];
        sf_core_process_i16(core, i16s.as_mut_ptr(), 256);
        sf_core_free(core);
        sf_core_free(std::ptr::null_mut());
        assert!(!sf_core_is_silent(std::ptr::null()));
    }
}

proptest::proptest! {
    /// Any bytes through the text door: a value in range, or none.
    #[test]
    fn any_bytes_parse_to_a_value_in_range_or_none(bytes in proptest::collection::vec(1u8.., 0..24), shape in 1u32..10, index in 0i32..10) {
        let text = CString::new(bytes).unwrap();
        let mut v = f64::NAN;
        let mut info = NiParamInfo { kind: NiParamKind::Float, min: 0.0, max: 0.0, default_value: 0.0, log_travel: false, bipolar: false, resolution: 0.0, automatable: true };
        if unsafe { sf_param_parse(shape, index, text.as_ptr(), &mut v) } {
            let known = unsafe { sf_param_info(shape, index, &mut info) };
            proptest::prop_assert!(known);
            proptest::prop_assert!(v >= info.min && v <= info.max, "{v} outside {}..{}", info.min, info.max);
        }
    }
}

#[cfg(feature = "shell")]
mod shell {
    use super::*;
    use crate::shell::*;

    const FS: f64 = 48_000.0;

    fn read(shell: *mut SfShell, v: &[f64]) -> Box<SfPicture> {
        let mut p: Box<SfPicture> = Box::new(unsafe { std::mem::zeroed() });
        unsafe { sf_picture_init(&mut *p) };
        assert!(unsafe { sf_shell_read(shell, v.as_ptr(), v.len(), &mut *p) });
        p
    }

    fn at(p: &SfPicture, hz: f32) -> usize {
        p.hz.iter().position(|&h| h >= hz).unwrap()
    }

    fn defaults(shape: u32) -> Vec<f64> {
        (0..sf_param_count(shape))
            .map(|i| {
                let mut info = NiParamInfo { kind: NiParamKind::Float, min: 0.0, max: 0.0, default_value: 0.0, log_travel: false, bipolar: false, resolution: 0.0, automatable: true };
                assert!(unsafe { sf_param_info(shape, i, &mut info) });
                info.default_value
            })
            .collect()
    }

    /// Every product's drawn total is what its engine plays in the filter's
    /// lane: the impulse response's spectrum against the picture, 0.01 dB.
    #[test]
    fn the_total_is_what_the_engine_plays() {
        for shape in SHAPES {
            for channel in [0.0, 1.0, 2.0, 3.0, 4.0] {
                let mut v = defaults(shape);
                let set = |v: &mut Vec<f64>, field: u32, x: f64| {
                    let i = sf_field_index(shape, field);
                    if i >= 0 {
                        v[i as usize] = x;
                    }
                };
                set(&mut v, SF_FIELD_FREQ, 2000.0);
                set(&mut v, SF_FIELD_GAIN, -7.0);
                set(&mut v, SF_FIELD_Q, 3.0);
                set(&mut v, SF_FIELD_ORDER, 2.0);
                set(&mut v, SF_FIELD_FAMILY, 2.0);
                set(&mut v, SF_FIELD_CHANNEL, channel);
                set(&mut v, SF_FIELD_OUTPUT, 2.5);
                let core = sf_core_new(shape, FS);
                for (i, x) in v.iter().enumerate() {
                    unsafe { sf_core_set_num(core, i as i32, *x) };
                }
                unsafe { sf_core_reset(core) };
                /* The lane's own input alone, as (left, right). */
                let (a, b, lane): (f32, f32, fn(f64, f64) -> f64) = match channel as u32 {
                    1 => (1.0, 0.0, |l, _| l),
                    2 => (0.0, 1.0, |_, r| r),
                    4 => (1.0, -1.0, |l, r| 0.5 * (l - r)),
                    _ => (1.0, 1.0, |l, r| 0.5 * (l + r)),
                };
                let n = 1 << 16;
                let (mut l, mut r) = (vec![0.0f32; n], vec![0.0f32; n]);
                (l[0], r[0]) = (a, b);
                unsafe { sf_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), n as u32) };
                unsafe { sf_core_free(core) };
                let y: Vec<f64> = l.iter().zip(&r).map(|(l, r)| lane(f64::from(*l), f64::from(*r))).collect();
                let shell = sf_shell_create(shape, FS);
                let p = read(shell, &v);
                unsafe { sf_shell_destroy(shell) };
                for k in (0..SF_POINTS).step_by(23).filter(|&k| p.hz[k] > 20.0 && p.hz[k] < 20_000.0) {
                    let w = 2.0 * std::f64::consts::PI * f64::from(p.hz[k]) / FS;
                    let h = y.iter().enumerate().fold(ni_filter::Complex64::new(0.0, 0.0), |s, (t, x)| s + ni_filter::Complex64::from_polar(*x, -w * t as f64));
                    let played = 20.0 * h.norm().log10();
                    if played > -40.0 {
                        let drawn = f64::from(p.total_db[k]);
                        assert!((played - drawn).abs() < 0.01, "shape {shape} channel {channel} at {} Hz: plays {played:.3} dB, drawn {drawn:.3} dB", p.hz[k]);
                        assert!((f64::from(p.curve_db[k]) + 2.5 - drawn).abs() < 1e-4, "the total is the curve and Output");
                    }
                }
            }
        }
    }

    #[test]
    fn the_handle_and_the_edges() {
        let shell = sf_shell_create(SF_SHAPE_BELL, FS);
        let mut v = defaults(SF_SHAPE_BELL);
        v[1] = 9.0;
        let p = read(shell, &v);
        assert_eq!((p.handle_hz, p.handle_db), (1000.0, 9.0));
        assert!(p.edge_hz[0] < 1000.0 && p.edge_hz[1] > 1000.0);
        assert!(!p.clamped);
        unsafe { sf_shell_destroy(shell) };
        let shell = sf_shell_create(SF_SHAPE_HIGH_PASS, FS);
        let p = read(shell, &defaults(SF_SHAPE_HIGH_PASS));
        assert!((p.handle_db + 3.01).abs() < 0.01, "a cut's handle on its -3 dB point");
        assert!(p.edge_hz[0].is_nan());
        let mut short = defaults(SF_SHAPE_HIGH_PASS);
        short.pop();
        let mut q: Box<SfPicture> = Box::new(unsafe { std::mem::zeroed() });
        assert!(!unsafe { sf_shell_read(shell, short.as_ptr(), short.len(), &mut *q) }, "a whole table or none");
        assert!(!unsafe { sf_shell_read(std::ptr::null(), short.as_ptr(), short.len(), &mut *q) });
        unsafe { sf_shell_destroy(shell) };
        assert!(sf_shell_create(0, FS).is_null());
    }

    fn feed(shell: *mut SfShell, blocks: usize, l: f32, r: f32) {
        for block in 0..blocks {
            let core = unsafe { sf_shell_begin(shell) };
            let s = |a: f32| -> Vec<f32> { (0..512).map(|i| a * (2.0 * std::f64::consts::PI * 1000.0 * (block * 512 + i) as f64 / FS).sin() as f32).collect() };
            let (mut x, mut y) = (s(l), s(r));
            unsafe {
                sf_core_process_f32(core, x.as_mut_ptr(), y.as_mut_ptr(), 512);
                sf_shell_end(shell, 512);
            }
        }
    }

    /// The analyzer reads the filter's lane: a centred sine is all mid and
    /// no side; a Side filter's picture shows the side.
    #[test]
    fn the_analyzer_follows_the_filters_lane() {
        let shell = sf_shell_create(SF_SHAPE_TILT, FS);
        unsafe { sf_shell_post_watching(shell, true) };
        let mut v = defaults(SF_SHAPE_TILT);
        let _ = read(shell, &v);
        feed(shell, 80, 0.5, 0.5);
        let p = read(shell, &v);
        assert!(p.analyzer);
        let k = at(&p, 1000.0);
        assert!(p.pre_db[k] <= -6.02 + 0.05 && p.pre_db[k] > -6.02 - 1.43, "a 0.5 sine reads -6 dBFS in the mid: {}", p.pre_db[k]);
        v[2] = 4.0; // Side
        let p = read(shell, &v);
        assert!(p.pre_db.iter().all(|&d| d == ni_analyzer::FLOOR_DB), "a new lane starts from the floor");
        feed(shell, 80, 0.5, 0.5);
        let p = read(shell, &v);
        assert!(p.pre_db[k] < -100.0, "a centred sine has no side: {}", p.pre_db[k]);
        unsafe {
            sf_shell_post_reset(shell);
            sf_shell_post_sample_rate(shell, 44_100.0);
            sf_shell_destroy(shell);
        }
    }
}
