// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The C ABI as C sees it, and the picture against the design.

use super::*;
use std::ffi::{CStr, CString};

fn text(f: impl Fn(*mut std::os::raw::c_char, i32) -> i32) -> String {
    let mut buf = [0 as std::os::raw::c_char; 64];
    assert!(f(buf.as_mut_ptr(), 64) >= 0);
    unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap().to_owned()
}

fn idx(node: usize, field: usize) -> i32 {
    (FD_NODE_BASE + FD_NODE_FIELDS * node + field) as i32
}

#[test]
fn the_table_crosses_whole() {
    assert_eq!(fd_param_count(), 175);
    let mut info = NiParamInfo { kind: NiParamKind::Float, min: 0.0, max: 0.0, default_value: 0.0, log_travel: false, bipolar: false, resolution: 0.0, automatable: true };
    assert!(unsafe { fd_param_info(idx(2, 4), &mut info) });
    assert!(info.log_travel);
    assert_eq!((info.min, info.max, info.default_value), (10.0, 22_000.0, 70.0));
    assert!(unsafe { fd_param_info(idx(0, 0), &mut info) });
    assert_eq!((info.kind, info.max), (NiParamKind::Choice, 9.0));
    assert!(unsafe { fd_param_info(3, &mut info) });
    assert!(!info.automatable);
    assert!(unsafe { !fd_param_info(175, &mut info) });
    assert_eq!(text(|b, n| unsafe { fd_param_key(idx(2, 4), b, n) }), "n03_freq");
    assert_eq!(text(|b, n| unsafe { fd_param_name(idx(2, 4), b, n) }), "Filter 3 Freq");
    assert_eq!(text(|b, n| unsafe { fd_param_unit(idx(2, 4), b, n) }), "Hz");
    assert_eq!(text(|b, n| unsafe { fd_param_label(idx(0, 0), 7, b, n) }), "Notch");
    assert_eq!(unsafe { fd_param_label(idx(0, 4), 0, std::ptr::null_mut(), 0) }, -1);
    assert_eq!(text(|b, n| unsafe { fd_param_text(idx(0, 4), 1500.0, b, n) }), "1.50 kHz");
    let typed = CString::new("2.5k").unwrap();
    let mut v = 0.0;
    assert!(unsafe { fd_param_parse(idx(0, 4), typed.as_ptr(), &mut v) });
    assert_eq!(v, 2500.0);
    assert!(unsafe { !fd_param_parse(idx(0, 4), CString::new("high").unwrap().as_ptr(), &mut v) });
    assert_eq!(fd_param_denormalise(0, fd_param_normalise(0, 3.0)), 3.0);
    assert_eq!(fd_param_snap(0, 3.004), 3.0);
    assert_eq!(fd_param_normalise(999, 1.0), 0.0);
}

#[test]
fn what_a_type_uses() {
    const FAMILY: u32 = 1;
    const ORDER: u32 = 2;
    const GAIN: u32 = 4;
    const Q: u32 = 8;
    assert_eq!(fd_type_uses(0, 1), 0);
    assert_eq!(fd_type_uses(1, 1), GAIN | Q);
    assert_eq!(fd_type_uses(5, 0), FAMILY | ORDER);
    assert_eq!(fd_type_uses(5, 1), FAMILY | ORDER | Q);
    assert_eq!(fd_type_uses(7, 0), FAMILY | ORDER | Q);
    assert_eq!(fd_type_uses(9, 3), GAIN);
    assert_eq!(fd_type_uses(2, 0), GAIN | ORDER);
    assert_eq!(fd_order_played(2, 0, 7), 3, "a shelf's highest is order 4");
    assert_eq!(fd_order_played(4, 0, 2), 2);
    assert_eq!(fd_order_played(4, 5, 2), 3, "Linkwitz-Riley at 18 dB/oct plays LR4");
    assert_eq!(fd_order_played(6, 5, 2), 2, "a band-pass plays Linkwitz-Riley as Butterworth");
    assert_eq!(fd_order_played(1, 0, 5), 0);
    assert!(fd_family_uses_ripple(4) && !fd_family_uses_ripple(3));
    assert!(fd_family_uses_stopband(3) && !fd_family_uses_stopband(0));
}

#[test]
fn an_instance_runs_and_panics() {
    let core = fd_core_new(48_000.0);
    unsafe {
        fd_core_set_num(core, idx(0, 0), 1.0);
        fd_core_set_num(core, idx(0, 5), 6.0);
        assert_eq!(fd_core_num(core, idx(0, 5)), 6.0);
        assert!(fd_core_num(core, 999).is_nan());
        let mut l: Vec<f32> = (0..512).map(|i| (i as f32 * 0.3).sin()).collect();
        let mut r = l.clone();
        fd_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), 512);
        fd_core_process_f32(core, l.as_mut_ptr(), std::ptr::null_mut(), 512);
        let mut i16s = vec![1000i16; 64];
        fd_core_process_i16(core, i16s.as_mut_ptr(), 32);
        assert!(!fd_core_is_silent(core));
        fd_core_on_midi(core, [0xB0u8, 123, 0].as_ptr(), 3);
        assert!(fd_core_is_silent(core));
        fd_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), 512);
        fd_core_reset(core);
        assert!(fd_core_is_silent(core));
        fd_core_set_sample_rate(core, 96_000.0);
        fd_core_free(core);
        fd_core_free(std::ptr::null_mut());
        fd_core_process_f32(std::ptr::null_mut(), l.as_mut_ptr(), r.as_mut_ptr(), 1);
    }
}

proptest::proptest! {
    #![proptest_config(proptest::prelude::ProptestConfig::with_cases(2000))]

    /// Any bytes a C caller hands the text door -- not UTF-8, not a number --
    /// are refused or read as a value inside the range.
    #[test]
    fn any_bytes_parse_as_nothing_or_a_value_in_range(
        index in -2i32..(FD_PARAM_COUNT as i32 + 2),
        bytes in proptest::collection::vec(1u8..=255, 0..40),
    ) {
        let text = CString::new(bytes).unwrap();
        let mut v = f64::NAN;
        let read = unsafe { fd_param_parse(index, text.as_ptr(), &mut v) };
        match fd_core::Param::from_i32(index) {
            Some(p) if read => proptest::prop_assert!(v.is_finite() && p.clamp(v) == v, "{} read as {}", index, v),
            Some(_) => proptest::prop_assert!(v.is_nan(), "a refusal wrote {}", v),
            None => proptest::prop_assert!(!read),
        }
    }
}

#[cfg(feature = "shell")]
mod picture {
    use super::*;
    use crate::shell::*;

    fn values() -> Vec<f64> {
        fd_core::defaults().to_vec()
    }

    fn read(shell: *const FdShell, v: &[f64], lane: u32) -> Box<FdPicture> {
        let mut p: Box<FdPicture> = Box::new(unsafe { std::mem::zeroed() });
        unsafe { fd_picture_init(&mut *p) };
        assert!(unsafe { fd_shell_read(shell, v.as_ptr(), v.len(), lane, &mut *p) });
        p
    }

    fn at(p: &FdPicture, hz: f32) -> usize {
        p.hz.iter().enumerate().min_by(|a, b| (a.1 - hz).abs().total_cmp(&(b.1 - hz).abs())).unwrap().0
    }

    #[test]
    fn the_curves_are_the_designs() {
        let shell = fd_shell_create(48_000.0);
        let mut v = values();
        let p = read(shell, &v, FD_LANE_LEFT);
        assert!(p.total_db.iter().all(|d| *d == 0.0));
        v[idx(0, 0) as usize] = 1.0; // bell
        v[idx(0, 4) as usize] = 1000.0;
        v[idx(0, 5) as usize] = 6.0;
        v[idx(1, 0) as usize] = 1.0; // a bell on the side only
        v[idx(1, 4) as usize] = 5000.0;
        v[idx(1, 5) as usize] = -9.0;
        v[idx(1, 9) as usize] = 4.0;
        v[0] = -2.0; // output
        let p = read(shell, &v, FD_LANE_LEFT);
        let k = at(&p, 1000.0);
        let (c, _) = fd_core::design::design(&fd_core::design::settings_of(&v, 0), None, fd_core::design::Globals::NEUTRAL, 48_000.0);
        assert!((p.node_db[0][k] - c.db(f64::from(p.hz[k]), 48_000.0) as f32).abs() < 1e-4);
        /* A source on the left alone hears the side bell half: (1 + H)/2. */
        let (side, _) = fd_core::design::design(&fd_core::design::settings_of(&v, 1), None, fd_core::design::Globals::NEUTRAL, 48_000.0);
        let half = (side.response(2.0 * std::f64::consts::PI * f64::from(p.hz[k]) / 48_000.0) + 1.0) * 0.5;
        let want = p.node_db[0][k] + (20.0 * half.norm().log10()) as f32 - 2.0;
        assert!((p.total_db[k] - want).abs() < 1e-4, "the side bell in the left lane: {} dB, want {want}", p.total_db[k]);
        let s = read(shell, &v, FD_LANE_SIDE);
        let k5 = at(&s, 5000.0);
        assert!((s.total_db[k5] - (s.node_db[0][k5] + s.node_db[1][k5] - 2.0)).abs() < 1e-4);
        assert!(s.node_db[1][k5] < -8.0);
        /* The handles and edges are the engine's. */
        assert_eq!((p.handle_hz[0], p.handle_db[0]), (1000.0, 6.0));
        assert!(p.handle_hz[2].is_nan());
        let (lo, hi) = (p.edge_hz[0][0], p.edge_hz[0][1]);
        assert!(lo < 1000.0 && hi > 1000.0 && ((lo * hi).sqrt() - 1000.0).abs() < 1e-9);
        assert!((fd_q_for_edge(1000.0, hi) - core::f64::consts::FRAC_1_SQRT_2).abs() < 1e-3);
        assert_eq!(fd_type_for_new(20.0), 4);
        assert_eq!(fd_type_for_new(500.0), 1);
        assert_eq!(fd_type_for_new(18_000.0), 5);
        /* The tail is the core's, and nothing without a whole table. */
        assert_eq!(unsafe { fd_tail_seconds(v.as_ptr(), v.len(), 48_000.0) }, fd_core::design::tail_seconds(&v, 48_000.0));
        assert!(unsafe { fd_tail_seconds(v.as_ptr(), v.len(), 48_000.0) } > 0.0);
        assert_eq!(unsafe { fd_tail_seconds(std::ptr::null(), v.len(), 48_000.0) }, 0.0);
        assert_eq!(unsafe { fd_tail_seconds(v.as_ptr(), v.len() - 1, 48_000.0) }, 0.0);
        /* Bypassed: drawn alone, not in the total. */
        v[idx(0, 1) as usize] = 0.0;
        let p = read(shell, &v, FD_LANE_LEFT);
        assert!(p.node_db[0][k] > 5.0);
        assert!((p.total_db[k] - (want - p.node_db[0][k])).abs() < 1e-4, "only the side bell and Output are left");
        unsafe { fd_shell_destroy(shell) };
    }

    #[test]
    fn the_isolator_is_drawn_and_summed() {
        let shell = fd_shell_create(48_000.0);
        let mut v = values();
        v[4] = 1.0;
        let p = read(shell, &v, FD_LANE_LEFT);
        assert!(p.iso_total_db.iter().all(|d| d.abs() < 1e-3));
        v[9] = 1.0; // kill mid
        let p = read(shell, &v, FD_LANE_LEFT);
        assert!(p.iso_total_db[at(&p, 800.0)] < -30.0);
        assert!(p.total_db[at(&p, 800.0)] < -30.0);
        assert!(p.iso_db[0][at(&p, 50.0)].abs() < 0.1);
        unsafe { fd_shell_destroy(shell) };
    }

    #[test]
    fn the_analyzer_finds_a_sine_at_its_level() {
        let shell = fd_shell_create(48_000.0);
        unsafe { fd_shell_post_watching(shell, true) };
        let v = values();
        for block in 0..80 {
            let core = unsafe { fd_shell_begin(shell) };
            let mut l: Vec<f32> =
                (0..512).map(|i| (0.5 * (2.0 * std::f64::consts::PI * 1000.0 * (block * 512 + i) as f64 / 48_000.0).sin()) as f32).collect();
            let mut r = l.clone();
            unsafe {
                fd_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), 512);
                fd_shell_end(shell, 512);
            }
        }
        /* The stereo view's analyzer is the mid, the mono sum: what the engine
         * keeps from the start. */
        let p = read(shell, &v, FD_LANE_STEREO);
        assert!(p.analyzer);
        let k = at(&p, 1000.0);
        /* A sine between two bins reads up to 1.42 dB low through a Hann
         * window (its scalloping loss). */
        assert!(p.post_db[k] <= -6.02 + 0.05 && p.post_db[k] > -6.02 - 1.43, "a 0.5 sine reads -6 dBFS: {}", p.post_db[k]);
        assert!(p.post_db[at(&p, 100.0)] < -60.0);
        unsafe {
            fd_shell_post_reset(shell);
            fd_shell_post_sample_rate(shell, 44_100.0);
            fd_shell_destroy(shell);
        }
    }

    /// A chain that mixes Stereo, Left, Mid and Side nodes, behind the
    /// isolator: each lane's total is what the engine renders in that lane.
    #[test]
    fn the_total_is_what_each_lane_plays() {
        const FS: f64 = 48_000.0;
        let mut v = values();
        let mut node = |slot: usize, fields: &[(usize, f64)]| {
            for &(f, x) in fields {
                v[idx(slot, f) as usize] = x;
            }
        };
        node(0, &[(0, 1.0), (4, 1000.0), (5, 6.0)]); // a Stereo bell
        node(1, &[(0, 1.0), (4, 5000.0), (5, -9.0), (9, 4.0)]); // a Side bell
        node(2, &[(0, 5.0), (4, 8000.0), (9, 1.0)]); // a Left low-pass
        node(3, &[(0, 3.0), (4, 3000.0), (5, 4.0), (9, 3.0)]); // a Mid high shelf
        node(4, &[(0, 1.0), (4, 300.0), (5, -5.0), (6, 2.0), (9, 2.0)]); // a Right bell
        v[0] = -2.0;
        v[4] = 1.0; // the isolator, its mid down
        v[6] = -6.0;

        let mut core = FdCore::new(FS);
        for (i, x) in v.iter().enumerate() {
            core.engine_mut().set_num(fd_core::Param::from_index(i).unwrap(), *x);
        }
        let shell = fd_shell_create(FS);
        let n = 1 << 16;
        /* Each lane's input alone, as (left, right), and how its output is read. */
        let lanes: [(u32, (f32, f32), fn(f64, f64) -> f64); 5] = [
            (FD_LANE_LEFT, (1.0, 0.0), |l, _| l),
            (FD_LANE_RIGHT, (0.0, 1.0), |_, r| r),
            (FD_LANE_MID, (1.0, 1.0), |l, r| 0.5 * (l + r)),
            (FD_LANE_SIDE, (1.0, -1.0), |l, r| 0.5 * (l - r)),
            (FD_LANE_STEREO, (1.0, 1.0), |l, r| 0.5 * (l + r)),
        ];
        for (lane, (a, b), read_out) in lanes {
            core.engine_mut().reset();
            let (mut l, mut r) = (vec![0.0f32; n], vec![0.0f32; n]);
            (l[0], r[0]) = (a, b);
            core.process(&mut l, Some(&mut r));
            let y: Vec<f64> = l.iter().zip(&r).map(|(l, r)| read_out(f64::from(*l), f64::from(*r))).collect();
            let p = read(shell, &v, lane);
            for k in (0..FD_POINTS).step_by(17).filter(|&k| p.hz[k] > 20.0 && p.hz[k] < 20_000.0) {
                let w = 2.0 * std::f64::consts::PI * f64::from(p.hz[k]) / FS;
                let h = y.iter().enumerate().fold(ni_filter::Complex64::new(0.0, 0.0), |s, (t, x)| s + ni_filter::Complex64::from_polar(*x, -w * t as f64));
                let played = 20.0 * h.norm().log10();
                if played > -40.0 {
                    let drawn = f64::from(p.total_db[k]);
                    assert!((played - drawn).abs() < 0.01, "lane {lane} at {} Hz: plays {played:.3} dB, drawn {drawn:.3} dB", p.hz[k]);
                }
            }
        }
        unsafe { fd_shell_destroy(shell) };
    }

    /// The analyzer reads the lane drawn: a centred sine is all mid and no
    /// side.
    #[test]
    fn the_analyzer_follows_the_lane() {
        let shell = fd_shell_create(48_000.0);
        unsafe { fd_shell_post_watching(shell, true) };
        let v = values();
        let run = |blocks: usize| {
            for block in 0..blocks {
                let core = unsafe { fd_shell_begin(shell) };
                let mut l: Vec<f32> =
                    (0..512).map(|i| (0.5 * (2.0 * std::f64::consts::PI * 1000.0 * (block * 512 + i) as f64 / 48_000.0).sin()) as f32).collect();
                let mut r = l.clone();
                unsafe {
                    fd_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), 512);
                    fd_shell_end(shell, 512);
                }
            }
        };
        for (lane, loud) in [(FD_LANE_MID, true), (FD_LANE_SIDE, false), (FD_LANE_LEFT, true), (FD_LANE_STEREO, true)] {
            read(shell, &v, lane); // asks the engine for the lane
            run(40);
            let p = read(shell, &v, lane);
            let k = at(&p, 1000.0);
            if loud {
                assert!(p.post_db[k] > -6.02 - 1.43, "lane {lane}: {} dBFS", p.post_db[k]);
            } else {
                assert!(p.post_db[k] < -100.0, "lane {lane}: the side of a centred sine reads {} dBFS", p.post_db[k]);
            }
        }
        unsafe { fd_shell_destroy(shell) };
    }

    /// Listening leaves the audio as it came and feeds the analyzer with it.
    #[test]
    fn listening_filters_nothing() {
        let shell = fd_shell_create(48_000.0);
        unsafe { fd_shell_post_watching(shell, true) };
        let mut v = values();
        v[idx(0, 0) as usize] = 5.0; // a low-pass at 30 Hz, which would take the sine out
        let core = unsafe { fd_shell_begin(shell) };
        for i in 0..fd_core::PARAM_COUNT {
            unsafe { fd_core_set_num(core, i as i32, v[i]) };
        }
        unsafe { fd_shell_end(shell, 0) };
        read(shell, &v, FD_LANE_STEREO);
        for block in 0..40 {
            let core = unsafe { fd_shell_begin(shell) };
            let x: Vec<f32> = (0..512).map(|i| (0.5 * (2.0 * std::f64::consts::PI * 1000.0 * (block * 512 + i) as f64 / 48_000.0).sin()) as f32).collect();
            let (mut l, mut r) = (x.clone(), x.clone());
            unsafe {
                fd_core_listen_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), 512);
                fd_shell_end(shell, 512);
            }
            assert_eq!(l, x);
            assert_eq!(r, x);
        }
        let p = read(shell, &v, FD_LANE_STEREO);
        let k = at(&p, 1000.0);
        assert!(p.post_db[k] > -6.02 - 1.43 && (p.post_db[k] - p.pre_db[k]).abs() < 1e-3, "post {} pre {}", p.post_db[k], p.pre_db[k]);
        unsafe { fd_shell_destroy(shell) };
    }

    #[test]
    fn a_read_without_its_table_is_refused() {
        let shell = fd_shell_create(48_000.0);
        let v = values();
        let mut p: Box<FdPicture> = Box::new(unsafe { std::mem::zeroed() });
        assert!(unsafe { !fd_shell_read(shell, v.as_ptr(), 3, 0, &mut *p) });
        assert!(unsafe { !fd_shell_read(std::ptr::null(), v.as_ptr(), v.len(), 0, &mut *p) });
        assert!(unsafe { fd_shell_begin(std::ptr::null()) }.is_null());
        unsafe { fd_shell_destroy(shell) };
    }
}
