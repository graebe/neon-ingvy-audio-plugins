/*!
The `tg_core_*` C ABI, called the way a shell calls it.

`engines/trance-gate/tests/test_core.c` is the contract's real test: it
compiles against the hand-written header and links the library. It links a
release staticlib, though, so cargo's coverage cannot see this file run. These
make the boundary's own claims again from Rust, through the same raw pointers:
null is survived everywhere, garbage arguments are refused without touching the
caller's buffers, the three buffer formats are one gain law, and the binary
render calls size and refuse exactly as documented.

The gate's behaviour is `tg-core`'s to test; `gate.rs` and `envelope.rs` test
their own curves.
*/

use super::*;
use std::ffi::CString;
use std::ptr::{null, null_mut};
use tg_core::params::Param;

const PARAM_COUNT: c_int = 15; /* TG_P_COUNT */
const NUM_RATES: c_int = 13; /* TG_NUM_RATES */
const STATE_MAX: usize = 8192; /* TG_STATE_MAX */
const GATE_MAX: usize = 4096; /* TG_GATE_MAX */
const ENVELOPE_MAX: usize = 1024; /* TG_ENVELOPE_MAX */

fn transport(running: bool, beats: f64) -> TgTransport {
    TgTransport { running: running as c_int, beats, bpm: 120.0 }
}

unsafe fn set(c: *mut TgCore, key: &str, val: &str) {
    let (k, v) = (CString::new(key).unwrap(), CString::new(val).unwrap());
    tg_core_set_param(c, k.as_ptr(), v.as_ptr());
}

/// `tg_core_get_param` as a string, or None for -1.
unsafe fn get(c: *mut TgCore, key: &str) -> Option<String> {
    let k = CString::new(key).unwrap();
    let mut buf = vec![0u8; STATE_MAX];
    let n = tg_core_get_param(c, k.as_ptr(), buf.as_mut_ptr() as *mut c_char, buf.len() as c_int);
    (n >= 0).then(|| String::from_utf8(buf[..n as usize].to_vec()).unwrap())
}

/// A gate that is open on step 0 and shut on step 1 with no envelope, so the
/// gain IS the pattern and a difference between two paths cannot hide.
unsafe fn gate() -> *mut TgCore {
    let c = tg_core_create(48_000.0);
    for (k, v) in [
        ("rate", "1/16"), ("length", "1"), ("pattern", "1"), ("ties", "0"), ("attack", "0"),
        ("decay", "0"), ("sustain", "1"), ("release", "0"), ("hold", "1"), ("amount", "1"),
    ] {
        set(c, k, v);
    }
    c
}

/* ------------------------------------------------------------ the boundary */

#[test]
fn every_entry_point_survives_a_null_instance() {
    let mut lr = [1.0f32; 4];
    let mut s16 = [1000i16; 4];
    let mut l = [1.0f32; 2];
    let mut r = [1.0f32; 2];
    let mut sweep = [-1.0f32; 2];
    let t = transport(true, 0.0);
    let key = CString::new("rate").unwrap();
    let mut buf = [0x7f as c_char; 16];
    unsafe {
        let c = null_mut::<TgCore>();
        tg_core_destroy(c);
        tg_core_set_sample_rate(c, 48_000.0);
        tg_core_set_param(c, key.as_ptr(), key.as_ptr());
        tg_core_set_num(c, Param::Amount as c_int, 0.5);
        tg_core_on_midi(c, [0x90u8, 60, 100].as_ptr(), 3);
        tg_core_process_i16(c, s16.as_mut_ptr(), 2, &t);
        tg_core_process_f32(c, lr.as_mut_ptr(), 2, &t);
        tg_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 2, &t);
        tg_core_process_f32_split_tap(c, l.as_mut_ptr(), r.as_mut_ptr(), sweep.as_mut_ptr(), 2, &t);
        assert_eq!(tg_core_get_param(c, key.as_ptr(), buf.as_mut_ptr(), 16), -1);
        assert_eq!(tg_core_get_sample_rate(c), 0.0);
        assert_eq!(tg_core_phase01(c), 0.0);
    }
    assert_eq!((lr, s16, l, r, sweep), ([1.0; 4], [1000; 4], [1.0; 2], [1.0; 2], [-1.0; 2]));
    assert_eq!(buf, [0x7f as c_char; 16]);
}

#[test]
fn the_sample_rate_is_readable_and_a_nonsense_one_refused() {
    unsafe {
        let c = tg_core_create(48_000.0);
        assert_eq!(tg_core_get_sample_rate(c), 48_000.0);
        tg_core_set_sample_rate(c, 96_000.0);
        assert_eq!(tg_core_get_sample_rate(c), 96_000.0);
        tg_core_set_sample_rate(c, 0.0);
        tg_core_set_sample_rate(c, f64::NAN);
        assert_eq!(tg_core_get_sample_rate(c), 96_000.0, "a nonsense rate was stored");
        tg_core_destroy(c);
    }
}

#[test]
fn garbage_arguments_are_refused_without_side_effects() {
    unsafe {
        let c = tg_core_create(48_000.0);
        let state = get(c, "state");

        /* A NULL key or value is not an empty one: nothing is set. */
        let rate = CString::new("rate").unwrap();
        tg_core_set_param(c, null(), rate.as_ptr());
        tg_core_set_param(c, rate.as_ptr(), null());
        /* A parameter outside the enum is dropped, not clamped onto a
         * neighbour. */
        tg_core_set_num(c, PARAM_COUNT, 1.0);
        tg_core_set_num(c, -1, 1.0);
        assert_eq!(get(c, "state"), state, "a refused edit changed the patch");

        let mut buf = [0x7f as c_char; 8];
        assert_eq!(tg_core_get_param(c, null(), buf.as_mut_ptr(), 8), -1);
        assert_eq!(tg_core_get_param(c, rate.as_ptr(), null_mut(), 8), -1);
        assert_eq!(tg_core_get_param(c, rate.as_ptr(), buf.as_mut_ptr(), 0), -1);
        assert_eq!(buf, [0x7f as c_char; 8]);
        assert_eq!(get(c, "wobble"), None, "an unknown key is -1, so a shell answers its own");

        let mut x = [1.0f32; 4];
        let mut s16 = [1000i16; 4];
        let t = transport(true, 0.0);
        tg_core_process_f32(c, null_mut(), 2, &t);
        tg_core_process_f32(c, x.as_mut_ptr(), 0, &t);
        tg_core_process_i16(c, null_mut(), 2, &t);
        tg_core_process_i16(c, s16.as_mut_ptr(), -1, &t);
        tg_core_process_f32_split(c, null_mut(), x.as_mut_ptr(), 2, &t);
        tg_core_process_f32_split(c, x.as_mut_ptr(), null_mut(), 2, &t);
        tg_core_process_f32_split(c, x.as_mut_ptr(), x.as_mut_ptr(), 0, &t);
        tg_core_process_f32_split_tap(c, null_mut(), x.as_mut_ptr(), null_mut(), 2, &t);
        tg_core_process_f32_split_tap(c, x.as_mut_ptr(), x.as_mut_ptr(), null_mut(), -3, &t);
        assert_eq!((x, s16), ([1.0; 4], [1000; 4]));
        assert_eq!(tg_core_phase01(c), 0.0, "a refused block moved the playhead");
        tg_core_destroy(c);
    }
}

#[test]
fn the_number_door_clamps_and_the_string_door_reads_it_back() {
    unsafe {
        let c = tg_core_create(48_000.0);
        /* TG_STAGE_MAX_PCT is the clamp. */
        tg_core_set_num(c, Param::Attack as c_int, 250.0);
        let attack: f64 = get(c, "attack").unwrap().parse().unwrap();
        assert!((attack - 200.0).abs() < 0.05, "attack came back as {attack}");
        /* Indices on the numeric door are the same indices the string door
         * reads: the rate at TG_RATE_DEFAULT is 1/16. */
        tg_core_set_num(c, Param::Rate as c_int, tg_core_rate_default() as f64);
        assert_eq!(get(c, "rate").as_deref(), Some("1/16"));
        tg_core_destroy(c);
    }
}

#[test]
fn midi_is_accepted_and_changes_nothing() {
    /* Part of the published surface, never used by the engine. */
    unsafe {
        let c = tg_core_create(48_000.0);
        let state = get(c, "state");
        tg_core_on_midi(c, [0xB0u8, 123, 0].as_ptr(), 3);
        tg_core_on_midi(c, null(), 0);
        assert_eq!(get(c, "state"), state);
        tg_core_destroy(c);
    }
}

/* ---------------------------------------------------- one gain law, three formats */

#[test]
fn interleaved_split_and_int16_are_the_same_maths() {
    unsafe {
        let (inter, split, int16) = (gate(), gate(), gate());
        const BL: usize = 100;
        let mut worst_split = 0.0f32;
        let mut worst_i16 = 0.0f32;
        let mut gated = false;
        let mut t = transport(true, 0.0);
        for blk in 0..200 {
            let v: Vec<f32> = (0..BL).map(|i| (0.01 * (blk * BL + i) as f32).sin() * 0.8).collect();
            let mut lr: Vec<f32> = v.iter().flat_map(|&s| [s, s]).collect();
            let mut s16: Vec<i16> = v.iter().flat_map(|&s| {
                let q = (s * 32_767.0).round() as i16;
                [q, q]
            }).collect();
            let (mut l, mut r) = (v.clone(), v.clone());
            tg_core_process_f32(inter, lr.as_mut_ptr(), BL as c_int, &t);
            tg_core_process_f32_split(split, l.as_mut_ptr(), r.as_mut_ptr(), BL as c_int, &t);
            tg_core_process_i16(int16, s16.as_mut_ptr(), BL as c_int, &t);
            for i in 0..BL {
                worst_split = worst_split.max((lr[i * 2] - l[i]).abs()).max((lr[i * 2 + 1] - r[i]).abs());
                worst_i16 = worst_i16.max((s16[i * 2] as f32 / 32_767.0 - lr[i * 2]).abs());
                gated |= v[i].abs() > 0.1 && l[i] == 0.0;
            }
            t.beats += BL as f64 / 48_000.0 * 2.0;
        }
        assert!(gated, "the gate never shut, so nothing was compared");
        assert_eq!(worst_split, 0.0, "the two float layouts must be bit-identical");
        assert!(worst_i16 * 32_767.0 <= 2.0, "int16 strayed {} steps", worst_i16 * 32_767.0);
        for c in [inter, split, int16] {
            tg_core_destroy(c);
        }
    }
}

#[test]
fn a_stopped_transport_holds_the_gate_open() {
    /* Stopped is not beat 0: a gate that re-triggers on every stop is the bug. */
    unsafe {
        let c = gate();
        let mut buf = [1.0f32; 128];
        tg_core_process_f32(c, buf.as_mut_ptr(), 64, &transport(false, 0.0));
        assert!(buf.iter().all(|&s| s == 1.0), "a stopped transport gated the audio");
        tg_core_destroy(c);
    }
}

#[test]
fn the_tap_is_the_split_path_plus_a_rising_sweep() {
    unsafe {
        let (tapped, plain, untapped) = (gate(), gate(), gate());
        let t = transport(true, 0.0);
        const N: usize = 256;
        let (mut l, mut r, mut sweep) = (vec![0.5f32; N], vec![0.5f32; N], vec![-1.0f32; N]);
        tg_core_process_f32_split_tap(tapped, l.as_mut_ptr(), r.as_mut_ptr(), sweep.as_mut_ptr(), N as c_int, &t);
        assert!(sweep.iter().all(|&s| (0.0..1.0).contains(&s)), "the sweep is a cycle phase");
        assert!(sweep.windows(2).all(|w| w[1] >= w[0]), "and rises across a running block");
        assert!(sweep[N - 1] > sweep[0]);

        let (mut pl, mut pr) = (vec![0.5f32; N], vec![0.5f32; N]);
        tg_core_process_f32_split(plain, pl.as_mut_ptr(), pr.as_mut_ptr(), N as c_int, &t);
        assert_eq!((&l, &r), (&pl, &pr), "tapping changed the audio");

        /* A null sweep is the split path. */
        let (mut ul, mut ur) = (vec![0.5f32; N], vec![0.5f32; N]);
        tg_core_process_f32_split_tap(untapped, ul.as_mut_ptr(), ur.as_mut_ptr(), null_mut(), N as c_int, &t);
        assert_eq!(ul, pl);
        assert_eq!(tg_core_phase01(untapped), tg_core_phase01(plain));
        assert!(tg_core_phase01(plain) > 0.0, "a running block moved the playhead");
        for c in [tapped, plain, untapped] {
            tg_core_destroy(c);
        }
    }
}

#[test]
fn a_stopped_sweep_free_runs_across_a_cycle() {
    unsafe {
        let c = gate();
        /* 2 steps of 1/16 at 120 BPM is 250 ms: 12 000 samples at 48 kHz. */
        assert!((scope_cycle_ms(&(*c).0) - 250.0).abs() < 1e-3);
        const N: usize = 3000;
        let (mut l, mut r, mut sweep) = (vec![0.5f32; N], vec![0.5f32; N], vec![-1.0f32; N]);
        tg_core_process_f32_split_tap(c, l.as_mut_ptr(), r.as_mut_ptr(), sweep.as_mut_ptr(), N as c_int, &transport(false, 0.0));
        let moved = sweep[N - 1] - sweep[0];
        assert!((moved - 0.25).abs() < 0.01, "a quarter cycle of samples moved the sweep {moved}");
        tg_core_destroy(c);
    }
}

/* -------------------------------------------------------------- the tables */

#[test]
fn the_rate_table_is_the_engines_own() {
    unsafe {
        let c = tg_core_create(48_000.0);
        let mut label = [0 as c_char; 32];
        for i in 0..NUM_RATES {
            let n = tg_core_rate_label(i, label.as_mut_ptr(), 32);
            assert!(n > 0, "rate {i} has no label");
            set(c, "rate", &i.to_string());
            assert_eq!(get(c, "rate").as_deref(), Some(ni_dsp::ffi::cstr(label.as_ptr())));
        }
        assert_eq!(tg_core_rate_label(NUM_RATES, label.as_mut_ptr(), 32), -1, "nothing past the end");
        assert_eq!(tg_core_rate_label(-1, label.as_mut_ptr(), 32), -1);
        let before = label;
        assert_eq!(tg_core_rate_label(0, label.as_mut_ptr(), 2), -1, "a buffer it does not fit");
        assert_eq!(label, before, "and a refusal writes nothing");
        assert_eq!(tg_core_rate_default(), 7, "TG_RATE_DEFAULT");
        tg_core_destroy(c);
    }
}

#[test]
fn the_plots_render_whole_or_not_at_all() {
    unsafe {
        let c = tg_core_create(48_000.0);
        let state = CString::new(get(c, "state").unwrap()).unwrap();
        let mut buf = vec![0u8; GATE_MAX];

        let n = tg_core_render_gate(state.as_ptr(), buf.as_mut_ptr() as *mut c_char, GATE_MAX as c_int);
        assert_eq!(n, 6 + 16 * 64, "one raw byte per sample of a 16-step cycle");
        assert_eq!(&buf[..6], b"16:64:");
        /* Refused, not truncated: a truncated curve is a different curve. */
        let mut small = [0xAAu8; 64];
        assert_eq!(tg_core_render_gate(state.as_ptr(), small.as_mut_ptr() as *mut c_char, 64), -1);
        assert_eq!(small, [0xAA; 64]);
        assert_eq!(tg_core_render_gate(state.as_ptr(), null_mut(), GATE_MAX as c_int), -1);
        assert_eq!(tg_core_render_gate(state.as_ptr(), buf.as_mut_ptr() as *mut c_char, -1), -1);
        assert_eq!(tg_core_render_gate(null(), buf.as_mut_ptr() as *mut c_char, GATE_MAX as c_int), -1, "nothing to draw");

        let mut env = vec![0u8; ENVELOPE_MAX];
        let n = tg_core_render_envelope(state.as_ptr(), env.as_mut_ptr() as *mut c_char, ENVELOPE_MAX as c_int);
        assert_eq!(n, 5 + 2 * 4 * 64, "two curves of four steps");
        assert_eq!(&env[..5], b"4:64:");
        assert_eq!(tg_core_render_envelope(state.as_ptr(), small.as_mut_ptr() as *mut c_char, 64), -1);
        assert_eq!(small, [0xAA; 64]);
        assert_eq!(tg_core_render_envelope(state.as_ptr(), null_mut(), ENVELOPE_MAX as c_int), -1);
        assert_eq!(tg_core_render_envelope(state.as_ptr(), env.as_mut_ptr() as *mut c_char, -1), -1);
        assert_eq!(tg_core_render_envelope(null(), env.as_mut_ptr() as *mut c_char, ENVELOPE_MAX as c_int), -1);
        tg_core_destroy(c);
    }
}

#[test]
fn the_shape_hooks_are_the_engines_curves() {
    for curve in 0..3 {
        assert_eq!(tg_test_shape(curve, 0.0), 0.0);
        assert_eq!(tg_test_shape(curve, 1.0), 1.0);
        let t = 0.37;
        assert!((tg_test_shape_inv(curve, tg_test_shape(curve, t)) - t).abs() < 1e-9);
    }
    assert!(tg_test_shape(1, 0.5) > 0.6, "exponential leads linear");
    assert!(tg_test_shape(2, 0.25) < 0.25 && tg_test_shape(2, 0.75) > 0.75, "the S-curve crosses it");
}

#[test]
fn inner_is_the_same_instance_the_abi_drives() {
    unsafe {
        let c = tg_core_create(48_000.0);
        (*c).inner().set_param("rate", "1/8");
        assert_eq!(get(c, "rate").as_deref(), Some("1/8"));
        tg_core_destroy(c);
    }
}

#[test]
fn the_meter_reaches_the_detents_and_a_null_is_ignored() {
    unsafe {
        let c = tg_core_create(48000.0);
        set(c, "rate", "1/32");
        let last = |c| get(c, "params").unwrap().rsplit(':').next().unwrap().to_owned();
        assert_eq!(last(c), "16,32,64,128");
        tg_core_set_meter(c, 7, 8);
        assert_eq!(last(c), "14,28,56,112");
        tg_core_set_meter(c, 0, 0);
        assert_eq!(last(c), "16,32,64,128", "no meter is common time");
        tg_core_set_meter(null_mut(), 3, 4);
        tg_core_destroy(c);
    }
}
