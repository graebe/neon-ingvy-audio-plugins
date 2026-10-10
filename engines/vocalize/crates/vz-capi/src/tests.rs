// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

use super::*;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use vz_core::{Param, PARAM_COUNT};

fn text(f: impl Fn(*mut c_char, c_int) -> c_int) -> Option<String> {
    let mut buf = [0 as c_char; 64];
    let n = f(buf.as_mut_ptr(), buf.len() as c_int);
    (n >= 0).then(|| unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap().to_string())
}

fn info(index: i32) -> Option<VzParamInfo> {
    let mut out = VzParamInfo {
        kind: VzParamKind::Float,
        min: 0.0,
        max: 0.0,
        default_value: 0.0,
        bipolar: false,
        resolution: 0.0,
    };
    unsafe { vz_param_info(index, &mut out) }.then_some(out)
}

#[test]
fn the_table_describes_every_parameter_and_nothing_else() {
    assert_eq!(vz_param_count(), PARAM_COUNT as i32);
    for i in 0..vz_param_count() {
        let p = Param::from_i32(i).unwrap();
        let key = text(|b, n| unsafe { vz_param_key(i, b, n) }).unwrap();
        assert_eq!(key, p.info().key);
        assert_eq!(text(|b, n| unsafe { vz_param_name(i, b, n) }).unwrap(), p.info().name);
        let d = info(i).unwrap();
        assert!(d.min <= d.default_value && d.default_value <= d.max, "{key}");
        assert!(d.resolution > 0.0);
        assert!(text(|b, n| unsafe { vz_param_unit(i, b, n) }).is_some());
    }
    for bad in [-1, vz_param_count()] {
        assert!(info(bad).is_none());
        assert!(text(|b, n| unsafe { vz_param_key(bad, b, n) }).is_none());
        assert!(text(|b, n| unsafe { vz_param_name(bad, b, n) }).is_none());
        assert!(text(|b, n| unsafe { vz_param_unit(bad, b, n) }).is_none());
        assert!(text(|b, n| unsafe { vz_param_text(bad, 0.0, b, n) }).is_none());
        assert!(text(|b, n| unsafe { vz_param_label(bad, 0, b, n) }).is_none());
        assert_eq!(vz_param_normalise(bad, 1.0), 0.0);
        assert_eq!(vz_param_denormalise(bad, 1.0), 0.0);
        assert_eq!(vz_param_snap(bad, 1.0), 0.0);
    }
    assert!(unsafe { !vz_param_info(0, std::ptr::null_mut()) });
}

#[test]
fn mode_is_a_choice_and_its_labels_are_the_engines() {
    let m = Param::Mode as i32;
    let d = info(m).unwrap();
    assert_eq!(d.kind, VzParamKind::Choice);
    assert_eq!((d.min, d.max, d.default_value, d.resolution), (0.0, 1.0, 0.0, 1.0));
    assert_eq!(text(|b, n| unsafe { vz_param_label(m, 0, b, n) }).as_deref(), Some("Time"));
    assert_eq!(text(|b, n| unsafe { vz_param_label(m, 1, b, n) }).as_deref(), Some("Spectral"));
    assert!(text(|b, n| unsafe { vz_param_label(m, 2, b, n) }).is_none());
    assert!(text(|b, n| unsafe { vz_param_label(m, -1, b, n) }).is_none());
    assert!(text(|b, n| unsafe { vz_param_label(Param::Vocal as i32, 0, b, n) }).is_none());
    assert_eq!(text(|b, n| unsafe { vz_param_unit(m, b, n) }).as_deref(), Some(""));
    assert_eq!(text(|b, n| unsafe { vz_param_text(m, 1.0, b, n) }).as_deref(), Some("Spectral"));
    let size = info(Param::Size as i32).unwrap();
    assert_eq!(size.kind, VzParamKind::Float);
    assert!(size.bipolar);
}

#[test]
fn text_goes_out_and_comes_back_in() {
    let v = Param::Vowel as i32;
    assert_eq!(text(|b, n| unsafe { vz_param_text(v, 1.3, b, n) }).unwrap(), "E>I 30 %");
    let typed = CString::new("o").unwrap();
    let mut x = 0.0;
    assert!(unsafe { vz_param_parse(v, typed.as_ptr(), &mut x) });
    assert_eq!(x, 3.0);
    let junk = CString::new("why").unwrap();
    x = -1.0;
    assert!(unsafe { !vz_param_parse(Param::Vocal as i32, junk.as_ptr(), &mut x) });
    assert_eq!(x, -1.0);
    assert!(unsafe { !vz_param_parse(v, typed.as_ptr(), std::ptr::null_mut()) });
    assert!(unsafe { !vz_param_parse(99, typed.as_ptr(), &mut x) });
    assert_eq!(vz_param_snap(v, 1.30004), 1.3);
    assert_eq!(vz_param_normalise(v, 2.0), 0.5);
    assert_eq!(vz_param_denormalise(v, 0.25), 1.0);
    /* A buffer too small for the whole text gets none of it. */
    let mut tiny = [0 as c_char; 4];
    assert_eq!(unsafe { vz_param_text(v, 1.3, tiny.as_mut_ptr(), 4) }, -1);
}

#[test]
fn the_formants_and_the_latency_are_the_engines() {
    let mut f = [0.0f64; VZ_FORMANTS];
    unsafe { vz_formants(0.0, 0.0, 50.0, f.as_mut_ptr()) };
    assert_eq!(f, vz_core::model::VOWELS[0]);
    unsafe { vz_formants(4.0, 100.0, 50.0, f.as_mut_ptr()) };
    assert!((f[0] - vz_core::model::VOWELS[4][0] * 0.8).abs() < 1e-9);
    unsafe { vz_formants(0.0, 0.0, 50.0, std::ptr::null_mut()) };
    assert_eq!(vz_latency(0.0, 48_000.0), 0);
    assert_eq!(vz_latency(1.0, 48_000.0), 1024);
    assert_eq!(vz_latency(1.0, 96_000.0), 2048);
}

#[test]
fn an_instance_processes_both_layouts_and_the_panic() {
    unsafe {
        let core = vz_core_new(48_000.0);
        vz_core_set_num(core, Param::Vocal as i32, 0.0);
        vz_core_set_num(core, 99, 1.0);
        assert_eq!(vz_core_num(core, Param::Vocal as i32), 0.0);
        assert!(vz_core_num(core, 99).is_nan());
        assert!(vz_core_num(std::ptr::null(), 0).is_nan());
        vz_core_set_sample_rate(core, 44_100.0);
        vz_core_set_sample_rate(core, -1.0);
        assert_eq!((*core).engine().sample_rate(), 44_100.0);
        assert_eq!(vz_core_latency(core), 0);
        assert_eq!(vz_core_latency(std::ptr::null()), 0);

        /* A second, so the glide to Vocal 0 has landed: then the input. */
        let mut l = vec![0.3f32; 48_000];
        let mut r = vec![0.3f32; 48_000];
        vz_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), l.len() as u32);
        let mut x: Vec<f32> = (0..512).map(|i| (i as f32 * 0.01).sin() * 0.5).collect();
        let want = x.clone();
        vz_core_process_f32(core, x.as_mut_ptr(), std::ptr::null_mut(), 512);
        assert_eq!(x, want);
        vz_core_process_f32(core, std::ptr::null_mut(), r.as_mut_ptr(), 16);

        let mut i16s = [9_000i16; 512];
        vz_core_process_i16(core, i16s.as_mut_ptr(), 256);
        assert!(i16s.iter().all(|s| *s == 9_000));
        vz_core_process_i16(core, std::ptr::null_mut(), 256);

        vz_core_set_num(core, Param::Mode as i32, 1.0);
        let mut more = vec![0.1f32; 4800];
        vz_core_process_f32(core, more.as_mut_ptr(), std::ptr::null_mut(), 4800);
        assert_eq!(vz_core_latency(core), 1024);

        vz_core_on_midi(core, [0xB0, 123, 0].as_ptr(), 3);
        vz_core_on_midi(core, std::ptr::null(), 3);
        vz_core_reset(core);
        let mut silence = [0.0f32; 64];
        vz_core_process_f32(core, silence.as_mut_ptr(), std::ptr::null_mut(), 64);
        assert!(silence.iter().all(|y| *y == 0.0));

        /* Every entry point takes null for its instance. */
        vz_core_set_num(std::ptr::null_mut(), 0, 1.0);
        vz_core_set_sample_rate(std::ptr::null_mut(), 1.0);
        vz_core_process_f32(std::ptr::null_mut(), l.as_mut_ptr(), std::ptr::null_mut(), 4);
        vz_core_process_i16(std::ptr::null_mut(), i16s.as_mut_ptr(), 4);
        vz_core_on_midi(std::ptr::null_mut(), [0xB0, 123, 0].as_ptr(), 3);
        vz_core_reset(std::ptr::null_mut());
        vz_core_free(core);
        vz_core_free(std::ptr::null_mut());
    }
}

#[test]
fn the_wrapper_counts_what_it_processed() {
    let mut core = VzCore::new(48_000.0);
    let mut block = [0.0f32; 300];
    core.process(&mut block, None);
    assert_eq!(core.serial, 300);
    core.engine_mut().set_num(Param::Mix, 0.0);
    assert_eq!(core.engine().num(Param::Mix), 0.0);
}

#[cfg(feature = "shell")]
mod shell_tests {
    use super::*;
    use crate::shell::*;

    fn read(shell: *const VzShell) -> VzPicture {
        let mut out = std::mem::MaybeUninit::<VzPicture>::zeroed();
        assert!(unsafe { vz_shell_read(shell, out.as_mut_ptr()) });
        unsafe { out.assume_init() }
    }

    fn run(shell: *const VzShell, blocks: usize, x: impl Fn(usize) -> f32) {
        unsafe {
            for b in 0..blocks {
                let core = vz_shell_begin(shell);
                let mut l: Vec<f32> = (0..480).map(|i| x(b * 480 + i)).collect();
                vz_core_process_f32(core, l.as_mut_ptr(), std::ptr::null_mut(), 480);
                vz_shell_end(shell, 480);
            }
        }
    }

    #[test]
    fn the_picture_shows_the_vowel_put_on_noise() {
        unsafe {
            let shell = vz_shell_create(48_000.0);
            let core = vz_shell_begin(shell);
            vz_core_set_num(core, Param::Vocal as i32, 100.0);
            vz_shell_end(shell, 0);
            let mut rng = 1u32;
            let noise = move |_| {
                rng = rng.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
                (rng >> 8) as f32 / (1u32 << 24) as f32 - 0.5
            };
            let noise = std::cell::RefCell::new(noise);
            run(shell, 100, |i| (noise.borrow_mut())(i));
            let p = read(shell);
            assert_ne!(p.serial, 0);
            assert_eq!((p.mode, p.latency, p.sample_rate), (0, 0, 48_000.0));
            assert_eq!(p.hz[0], VZ_LOWEST_HZ);
            assert!((p.hz[VZ_POINTS - 1] - VZ_HIGHEST_HZ).abs() < 1e-6);
            assert_eq!(p.output_db.iter().cloned().fold(f64::MIN, f64::max), 0.0);
            /* The loudest point out is near A's F1. */
            let top = p.hz[p.output_db.iter().enumerate().max_by(|a, b| a.1.total_cmp(b.1)).unwrap().0];
            assert!((top / 768.0 - 1.0).abs() < 0.15, "{top}");
            vz_shell_destroy(shell);
        }
    }

    #[test]
    fn commands_apply_at_the_next_block_and_null_is_nothing() {
        unsafe {
            let shell = vz_shell_create(-5.0);
            vz_shell_post_sample_rate(shell, 96_000.0);
            vz_shell_post_sample_rate(shell, f64::NAN);
            vz_shell_post_reset(shell);
            let core = vz_shell_begin(shell);
            vz_core_set_num(core, Param::Mode as i32, 1.0);
            vz_shell_end(shell, 0);
            run(shell, 20, |_| 0.0);
            let p = read(shell);
            assert_eq!((p.mode, p.latency, p.sample_rate), (1, 2048, 96_000.0));
            assert!(p.input_db.iter().chain(&p.output_db).all(|x| x.is_finite()));

            assert!(vz_shell_begin(std::ptr::null()).is_null());
            vz_shell_end(std::ptr::null(), 1);
            vz_shell_post_reset(std::ptr::null());
            vz_shell_post_sample_rate(std::ptr::null(), 1.0);
            assert!(!vz_shell_read(std::ptr::null(), std::ptr::null_mut()));
            assert!(!vz_shell_read(shell, std::ptr::null_mut()));
            vz_shell_destroy(shell);
            vz_shell_destroy(std::ptr::null_mut());
        }
    }
}
