// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

use super::*;
use bc_core::{transfer, Param, LATENCY, PARAM_COUNT};
use std::os::raw::{c_char, c_int};
use std::ffi::{CStr, CString};

fn text(f: impl Fn(*mut c_char, c_int) -> c_int) -> Option<String> {
    let mut buf = [0 as c_char; 64];
    let n = f(buf.as_mut_ptr(), buf.len() as c_int);
    (n >= 0).then(|| unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap().to_string())
}

fn info(index: i32) -> Option<BcParamInfo> {
    let mut out = BcParamInfo {
        kind: BcParamKind::Toggle,
        min: 0.0,
        max: 0.0,
        default_value: 0.0,
        log_travel: false,
        bipolar: false,
        resolution: 0.0,
        choice_count: 0,
        automatable: false,
    };
    unsafe { bc_param_info(index, &mut out) }.then_some(out)
}

#[test]
fn refresh_is_a_choice_of_the_window_saved_but_not_automated() {
    let r = Param::Refresh as i32;
    let d = info(r).unwrap();
    assert_eq!(d.kind, BcParamKind::Choice);
    assert_eq!((d.choice_count, d.min, d.max, d.default_value), (4, 0.0, 3.0, 3.0));
    assert!(!d.automatable);
    let words: Vec<String> = (0..4).map(|c| text(|b, n| unsafe { bc_param_choice(r, c, b, n) }).unwrap()).collect();
    assert_eq!(words, ["Live", "10/s", "4/s", "1/s"]);
    assert_eq!(text(|b, n| unsafe { bc_param_text(r, 3.0, b, n) }).unwrap(), "1/s");
    assert_eq!(text(|b, n| unsafe { bc_param_unit(r, b, n) }).unwrap(), "");
    /* No word past the end, and none for a parameter that is no choice. */
    assert!(text(|b, n| unsafe { bc_param_choice(r, 4, b, n) }).is_none());
    assert!(text(|b, n| unsafe { bc_param_choice(r, -1, b, n) }).is_none());
    assert!(text(|b, n| unsafe { bc_param_choice(Param::Bits as i32, 0, b, n) }).is_none());
    /* Every control of the sound is automatable. */
    for i in (0..bc_param_count()).filter(|&i| i != r) {
        let d = info(i).unwrap();
        assert!(d.automatable, "{i}");
        assert_eq!(d.choice_count, if d.kind == BcParamKind::Choice { 3 } else { 0 }, "{i}");
    }
    /* Slope, the other choice: the filter's three, by their words. */
    let s = Param::Slope as i32;
    let words: Vec<String> = (0..3).map(|c| text(|b, n| unsafe { bc_param_choice(s, c, b, n) }).unwrap()).collect();
    assert_eq!(words, ["12 dB", "24 dB", "48 dB"]);
    assert_eq!([0, 1, 2, 3].map(|c| bc_refresh_period_ms(c)), [0.0, 100.0, 250.0, 1000.0]);
    /* The filter's response: flat, -3 dB at the cutoff, the slope, the peak. */
    let mut db = [0.0f32; 3];
    unsafe { bc_filter_response(1_000.0, 0.0, 1, 48_000.0, 100.0, 10_000.0, db.as_mut_ptr(), 3) };
    assert!(db[0].abs() < 0.01 && (db[1] + 3.01).abs() < 0.05 && db[2] < -75.0, "{db:?}");
    unsafe { bc_filter_response(1_000.0, 12.0, 1, 48_000.0, 100.0, 10_000.0, db.as_mut_ptr(), 3) };
    assert!((db[1] - 8.99).abs() < 0.05, "{db:?}");
    unsafe { bc_filter_response(1_000.0, 0.0, 1, 0.0, 100.0, 10_000.0, std::ptr::null_mut(), 3) };
    /* The levels Bits makes, both grids: one more than 2^b mid-tread. */
    assert_eq!((bc_level_count(8.0, false), bc_level_count(8.0, true)), (257, 256));
    assert_eq!((bc_level_count(1.0, false), bc_level_count(1.0, true)), (3, 2));
    assert_eq!(bc_refresh_period_ms(9), 1000.0, "out of range: the default's");
}

#[test]
fn the_table_describes_every_parameter_and_nothing_else() {
    assert_eq!(bc_param_count(), PARAM_COUNT as i32);
    for i in 0..bc_param_count() {
        let p = Param::from_i32(i).unwrap();
        let key = text(|b, n| unsafe { bc_param_key(i, b, n) }).unwrap();
        assert_eq!(key, p.info().key);
        assert_eq!(text(|b, n| unsafe { bc_param_name(i, b, n) }).unwrap(), p.info().name);
        let d = info(i).unwrap();
        assert!(d.min <= d.default_value && d.default_value <= d.max, "{key}");
        assert!(text(|b, n| unsafe { bc_param_unit(i, b, n) }).is_some());
    }
    for bad in [-1, bc_param_count()] {
        assert!(info(bad).is_none());
        assert!(text(|b, n| unsafe { bc_param_key(bad, b, n) }).is_none());
        assert!(text(|b, n| unsafe { bc_param_name(bad, b, n) }).is_none());
        assert!(text(|b, n| unsafe { bc_param_unit(bad, b, n) }).is_none());
        assert!(text(|b, n| unsafe { bc_param_text(bad, 0.0, b, n) }).is_none());
        assert_eq!(bc_param_normalise(bad, 1.0), 0.0);
        assert_eq!(bc_param_denormalise(bad, 1.0), 0.0);
        assert_eq!(bc_param_snap(bad, 1.0), 0.0);
    }
    assert!(unsafe { !bc_param_info(0, std::ptr::null_mut()) });
}

#[test]
fn kinds_travel_and_units_are_the_engines() {
    let rate = info(Param::Rate as i32).unwrap();
    assert_eq!(rate.kind, BcParamKind::Float);
    assert!(rate.log_travel);
    assert_eq!(rate.resolution, 0.1);
    assert_eq!(bc_param_snap(Param::Rate as i32, 7_000.04), 7_000.0);
    assert_eq!(text(|b, n| unsafe { bc_param_unit(Param::Rate as i32, b, n) }).unwrap(), "Hz");
    let aa = info(Param::AntiAlias as i32).unwrap();
    assert_eq!(aa.kind, BcParamKind::Toggle);
    assert_eq!((aa.min, aa.max, aa.default_value), (0.0, 1.0, 0.0));
    assert_eq!(text(|b, n| unsafe { bc_param_unit(Param::AntiAlias as i32, b, n) }).unwrap(), "");
    assert!(info(Param::Drive as i32).unwrap().bipolar);
    let r = Param::Rate as i32;
    assert!((bc_param_denormalise(r, bc_param_normalise(r, 4_000.0)) - 4_000.0).abs() < 1e-6);
}

#[test]
fn text_goes_out_and_comes_back_in() {
    let r = Param::Rate as i32;
    assert_eq!(text(|b, n| unsafe { bc_param_text(r, 12_000.0, b, n) }).unwrap(), "12.0 kHz");
    let typed = CString::new("8k").unwrap();
    let mut v = 0.0;
    assert!(unsafe { bc_param_parse(r, typed.as_ptr(), &mut v) });
    assert_eq!(v, 8_000.0);
    let junk = CString::new("fast").unwrap();
    v = -1.0;
    assert!(unsafe { !bc_param_parse(r, junk.as_ptr(), &mut v) });
    assert_eq!(v, -1.0);
    assert!(unsafe { !bc_param_parse(r, typed.as_ptr(), std::ptr::null_mut()) });
    assert!(unsafe { !bc_param_parse(99, typed.as_ptr(), &mut v) });
    /* A buffer too small for the whole text gets none of it. */
    let mut tiny = [0 as c_char; 4];
    assert_eq!(unsafe { bc_param_text(r, 12_000.0, tiny.as_mut_ptr(), 4) }, -1);
}

#[test]
fn the_curve_is_the_staircase_over_the_span_it_is_given() {
    let mut out = [0.0f32; 5];
    unsafe { bc_transfer_render(0.0, 1.0, 0.0, false, 1.0, out.as_mut_ptr(), 5) };
    assert_eq!(out, [-1.0, -1.0, 0.0, 1.0, 1.0]);
    let mut one = [9.0f32; 1];
    unsafe { bc_transfer_render(0.0, 1.0, 0.0, true, 1.0, one.as_mut_ptr(), 1) };
    assert_eq!(one, [0.5]);
    unsafe { bc_transfer_render(0.0, 1.0, 0.0, true, 1.0, std::ptr::null_mut(), 5) };
    /* 8 bits over its span: the steps of 1/128 from -1/16 to 1/16. */
    let span = bc_transfer_span(0.0, 8.0, 0.0, false);
    assert_eq!(span, 1.0 / 16.0);
    let mut zoomed = [0.0f32; 3];
    unsafe { bc_transfer_render(0.0, 8.0, 0.0, false, span, zoomed.as_mut_ptr(), 3) };
    assert_eq!(zoomed, [-0.0625, 0.0, 0.0625]);
    /* A span that is no number, or none, is the whole range. */
    for bad in [0.0, -1.0, f64::NAN, f64::INFINITY] {
        unsafe { bc_transfer_render(0.0, 1.0, 0.0, false, bad, out.as_mut_ptr(), 5) };
        assert_eq!(out, [-1.0, -1.0, 0.0, 1.0, 1.0], "span {bad}");
    }
}

#[test]
fn an_instance_processes_both_layouts_and_the_panic() {
    unsafe {
        let core = bc_core_new(48_000.0);
        bc_core_set_num(core, Param::Bits as i32, 2.0);
        bc_core_set_num(core, Param::Rate as i32, 96_000.0);
        bc_core_set_num(core, 99, 1.0);
        assert_eq!(bc_core_num(core, Param::Bits as i32), 2.0);
        assert!(bc_core_num(core, 99).is_nan());
        assert!(bc_core_num(std::ptr::null(), 0).is_nan());
        bc_core_set_sample_rate(core, 44_100.0);
        bc_core_set_sample_rate(core, -1.0);
        assert_eq!((*core).engine().sample_rate(), 44_100.0);

        let mut l = vec![0.3f32; 48_000];
        let mut r = vec![0.3f32; 48_000];
        bc_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), l.len() as u32);
        assert_eq!(*l.last().unwrap(), 0.5);
        assert_eq!(l, r);
        let mut mono = [0.3f32; 16];
        bc_core_process_f32(core, mono.as_mut_ptr(), std::ptr::null_mut(), 16);
        bc_core_process_f32(core, std::ptr::null_mut(), r.as_mut_ptr(), 16);

        let mut i16s = [9_000i16; 512];
        bc_core_process_i16(core, i16s.as_mut_ptr(), 256);
        assert!(i16s.iter().all(|s| *s == 16_384), "{:?}", &i16s[..80]);
        bc_core_process_i16(core, std::ptr::null_mut(), 256);

        assert_eq!(bc_latency() as usize, LATENCY);
        bc_core_set_active(core, false);
        bc_core_set_active(std::ptr::null_mut(), true);
        let mut passing = vec![0.3f32; 48_000];
        bc_core_process_f32(core, passing.as_mut_ptr(), std::ptr::null_mut(), 48_000);
        assert_eq!(*passing.last().unwrap(), 0.3);
        bc_core_set_active(core, true);

        bc_core_on_midi(core, [0xB0, 123, 0].as_ptr(), 3);
        bc_core_on_midi(core, std::ptr::null(), 3);
        bc_core_reset(core);
        let mut silence = [0.0f32; 64];
        bc_core_process_f32(core, silence.as_mut_ptr(), std::ptr::null_mut(), 64);
        assert!(silence.iter().all(|y| *y == 0.0));

        /* Every entry point takes null for its instance. */
        bc_core_set_num(std::ptr::null_mut(), 0, 1.0);
        bc_core_set_sample_rate(std::ptr::null_mut(), 1.0);
        bc_core_process_f32(std::ptr::null_mut(), l.as_mut_ptr(), std::ptr::null_mut(), 4);
        bc_core_process_i16(std::ptr::null_mut(), i16s.as_mut_ptr(), 4);
        bc_core_on_midi(std::ptr::null_mut(), [0xB0, 123, 0].as_ptr(), 3);
        bc_core_reset(std::ptr::null_mut());
        bc_core_free(core);
        bc_core_free(std::ptr::null_mut());
    }
}

#[test]
fn the_ring_keeps_the_last_samples_dry_and_wet() {
    /* Each wet sample beside the dry one it was made from: LATENCY apart in
     * time, side by side in the ring. */
    let mut core = BcCore::new(48_000.0);
    let mut block: Vec<f32> = (0..3000).map(|i| i as f32 / 3000.0).collect();
    core.process(&mut block, None);
    let recent: Vec<(f32, f32)> = core.recent(4).collect();
    let dry: Vec<f32> = (2996 - LATENCY..3000 - LATENCY).map(|i| i as f32 / 3000.0).collect();
    assert_eq!(recent.iter().map(|r| r.0).collect::<Vec<_>>(), dry);
    assert_eq!(recent.iter().map(|r| r.1).collect::<Vec<_>>(), block[2996..].to_vec());
    let mut more = [5.0f32; LATENCY + 3];
    core.process(&mut more, None);
    let recent: Vec<(f32, f32)> = core.recent(4).collect();
    assert_eq!(recent[0].0, 2999.0 / 3000.0);
    assert_eq!(recent[3].0, 5.0);
    assert_eq!(core.recent(99_999).count(), BC_SCOPE_RING - LATENCY);
}

#[cfg(feature = "shell")]
mod shell_tests {
    use super::*;
    use crate::shell::*;

    fn read(shell: *const BcShell) -> BcScope {
        let mut out = std::mem::MaybeUninit::<BcScope>::zeroed();
        assert!(unsafe { bc_shell_read(shell, out.as_mut_ptr()) });
        unsafe { out.assume_init() }
    }

    #[test]
    fn a_steady_tone_is_triggered_on_its_upward_crossing() {
        unsafe {
            let shell = bc_shell_create(48_000.0);
            for block in 0..20 {
                let core = bc_shell_begin(shell);
                let mut l: Vec<f32> = (0..480)
                    .map(|i| (2.0 * std::f32::consts::PI * 200.0 * (block * 480 + i) as f32 / 48_000.0 + 0.3).sin())
                    .collect();
                bc_core_process_f32(core, l.as_mut_ptr(), std::ptr::null_mut(), 480);
                bc_shell_end(shell, 480);
            }
            let s = read(shell);
            assert!(s.triggered);
            assert_eq!(s.sample_rate, 48_000.0);
            assert!(s.dry[0] >= 0.0 && s.dry[0] < 0.05, "{}", s.dry[0]);
            assert!(s.dry[1] > s.dry[0]);
            assert_ne!(s.serial, 0);
            bc_shell_destroy(shell);
        }
    }

    /// Twenty blocks of a 200 Hz tone through a shell at 48 kHz, after `set`.
    unsafe fn tone_through(set: impl Fn(*mut BcCore)) -> BcScope {
        let shell = bc_shell_create(48_000.0);
        for block in 0..20 {
            let core = bc_shell_begin(shell);
            set(core);
            let mut l: Vec<f32> = (0..480)
                .map(|i| 0.8 * (2.0 * std::f32::consts::PI * 200.0 * (block * 480 + i) as f32 / 48_000.0).sin())
                .collect();
            bc_core_process_f32(core, l.as_mut_ptr(), std::ptr::null_mut(), 480);
            bc_shell_end(shell, 480);
        }
        let s = read(shell);
        bc_shell_destroy(shell);
        s
    }

    #[test]
    fn the_window_says_what_the_quantiser_saw_and_made_of_it() {
        let s = unsafe { tone_through(|_| {}) };
        /* The defaults: 8 bits, no Drive, Shape, Dither or pre-filter -- the
         * level is the staircase at the input, beside the very same sample. */
        let mut exact = 0;
        for i in 0..BC_SCOPE {
            let want = transfer(0.0, 8.0, 0.0, false, f64::from(s.amp_in[i])) as f32;
            assert!((s.amp_out[i] - want).abs() <= 1.0 / 128.0 + 1e-6, "{i}");
            exact += usize::from(s.amp_out[i] == want);
            assert_eq!(s.amp_in[i], s.dry[i], "the input, Drive at 0 dB, is the dry sample");
        }
        assert!(exact > BC_SCOPE * 99 / 100, "{exact}");
    }

    #[test]
    fn the_holds_window_is_sixteen_periods_at_any_rate_the_ring_holds() {
        /* 12 kHz at 48 kHz: a take every fourth sample, seventeen takes. */
        let s = unsafe { tone_through(|_| {}) };
        let (len, takes) = (s.hold_len as usize, s.hold_takes as usize);
        assert_eq!(takes, BC_HOLD_PERIODS + 1);
        assert!(s.hold_took[0], "it starts on a take");
        let at: Vec<usize> = (0..len).filter(|&i| s.hold_took[i]).collect();
        assert_eq!(at.len(), takes);
        assert!(at.windows(2).all(|w| w[1] - w[0] == 4), "{at:?}");
        assert!(len > 64 && len <= 68, "{len}");
        /* 50 Hz: a period of 960 samples, so the ring holds three takes at most. */
        let slow = unsafe { tone_through(|c| bc_core_set_num(c, Param::Rate as i32, 50.0)) };
        assert!(slow.hold_takes as usize <= 3 && slow.hold_takes >= 2, "{}", slow.hold_takes);
        assert!(slow.hold_len as usize <= BC_HOLD);
        assert!(slow.hold_took[0]);
    }

    #[test]
    fn the_look_says_which_settings_made_it_and_since_when() {
        let s = unsafe { tone_through(|c| bc_core_set_num(c, Param::Bits as i32, 5.5)) };
        assert_eq!((s.bits, s.drive_db, s.mid_rise, s.rate), (5.5, 0.0, 0.0, 12_000.0));
        assert_eq!(s.settled, 20 * 480, "set before the first block, the same value every block after");
    }

    #[test]
    fn with_anti_alias_the_holds_window_marks_the_exact_instants() {
        unsafe {
            let shell = bc_shell_create(44_100.0);
            for block in 0..40 {
                let core = bc_shell_begin(shell);
                bc_core_set_num(core, Param::AntiAlias as i32, 1.0);
                let mut l: Vec<f32> =
                    (0..441).map(|i| (0.5 * ((block * 441 + i) as f64 * 0.01).sin()) as f32).collect();
                bc_core_process_f32(core, l.as_mut_ptr(), std::ptr::null_mut(), 441);
                bc_shell_end(shell, 441);
            }
            let s = read(shell);
            bc_shell_destroy(shell);
            assert_eq!(s.anti_alias, 1.0);
            assert_eq!(s.hold_takes as usize, BC_HOLD_PERIODS + 1);
            let at: Vec<f64> = (0..s.hold_len as usize)
                .filter(|&i| s.hold_took[i])
                .map(|i| i as f64 + f64::from(s.hold_at[i]))
                .collect();
            /* 12 kHz at 44.1 kHz: 3.675 samples apart, not 3 and 4. */
            assert!(at.windows(2).all(|w| (w[1] - w[0] - 3.675).abs() < 1e-4), "{at:?}");
        }
    }

    #[test]
    fn silence_is_the_latest_window_untriggered_and_commands_apply() {
        unsafe {
            let shell = bc_shell_create(-5.0);
            bc_shell_post_sample_rate(shell, 96_000.0);
            bc_shell_post_sample_rate(shell, f64::NAN);
            bc_shell_post_reset(shell);
            for _ in 0..4 {
                let core = bc_shell_begin(shell);
                let mut l = [0.0f32; 1024];
                bc_core_process_f32(core, l.as_mut_ptr(), std::ptr::null_mut(), 1024);
                bc_shell_end(shell, 1024);
            }
            let s = read(shell);
            assert!(!s.triggered);
            assert_eq!(s.sample_rate, 96_000.0);
            assert!(s.dry.iter().chain(&s.wet).all(|x| *x == 0.0));

            /* Null everywhere is nothing. */
            assert!(bc_shell_begin(std::ptr::null()).is_null());
            bc_shell_end(std::ptr::null(), 1);
            bc_shell_post_reset(std::ptr::null());
            bc_shell_post_sample_rate(std::ptr::null(), 1.0);
            assert!(!bc_shell_read(std::ptr::null(), std::ptr::null_mut()));
            assert!(!bc_shell_read(shell, std::ptr::null_mut()));
            bc_shell_destroy(shell);
            bc_shell_destroy(std::ptr::null_mut());
        }
    }
}
