/*
 * The gnd_* C ABI, called the way a plugin calls it.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * tests/gnd_roundtrip.c makes these same claims from C, against the header,
 * and that is what keeps the header honest. It links a release staticlib,
 * though, so cargo's coverage never sees this file run. These are the claims
 * the four plugins depend on, made again from Rust through the same raw
 * pointers -- not ground-core's detector behaviour, which ground-core tests.
 */

use super::*;
use core::ptr::null;

const SR: f64 = 48_000.0;
const BLOCK: usize = 512;

/// `secs` of a decaying sine at `hz` (no decay when `decay` is 0), pushed in
/// host-sized blocks with the SAME pointer for both channels, as a mono
/// plugin does.
unsafe fn push_tone(g: *const GndDetector, secs: f64, hz: f64, decay: f64) {
    let total = (secs * SR) as usize;
    let mut buf = [0.0f64; BLOCK];
    let mut done = 0;
    while done < total {
        let n = BLOCK.min(total - done);
        for (i, s) in buf[..n].iter_mut().enumerate() {
            let t = (done + i) as f64 / SR;
            let a = if decay > 0.0 { (-t / decay).exp() } else { 1.0 };
            *s = a * (2.0 * core::f64::consts::PI * hz * t).sin();
        }
        gnd_push(g, buf.as_ptr(), buf.as_ptr(), n as i32);
        done += n;
    }
}

unsafe fn kick(g: *const GndDetector) {
    push_tone(g, 0.4, 60.0, 0.05);
}

#[test]
fn a_null_handle_is_silence_not_a_crash() {
    /* ProcessBlock calls these unconditionally; "no detector" must read as
     * "nothing happened". */
    unsafe {
        gnd_free(core::ptr::null_mut());
        gnd_reset(null());
        gnd_set_sample_rate(null(), SR);
        gnd_set_active(null(), 1);
        let x = [1.0f64; 4];
        gnd_push(null(), x.as_ptr(), x.as_ptr(), 4);
        assert_eq!(gnd_fires(null()), 0);
        assert_eq!(gnd_strength(null()), 0.0);
    }
}

#[test]
fn a_new_detector_is_quiet_and_inactive() {
    unsafe {
        let g = gnd_new(SR);
        assert!(!g.is_null());
        assert_eq!(gnd_fires(g), 0, "a fresh detector has seen no kicks");
        assert_eq!(gnd_strength(g), 0.0, "and reports no strength");

        kick(g);
        assert_eq!(gnd_fires(g), 0, "inactive until an editor opens");
        gnd_free(g);
    }
}

#[test]
fn an_empty_or_pointerless_block_is_a_no_op() {
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 1);
        let x = [0.9f64; BLOCK];
        gnd_push(g, x.as_ptr(), x.as_ptr(), 0);
        gnd_push(g, x.as_ptr(), x.as_ptr(), -1);
        gnd_push(g, null(), x.as_ptr(), BLOCK as i32);
        gnd_push(g, x.as_ptr(), null(), BLOCK as i32);
        assert_eq!(gnd_fires(g), 0);

        /* And the guards left it working. */
        kick(g);
        assert_eq!(gnd_fires(g), 1);
        gnd_free(g);
    }
}

#[test]
fn a_kick_fires_once_and_a_hi_hat_does_not() {
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 1);
        kick(g);
        assert_eq!(gnd_fires(g), 1, "a 60 Hz kick is one onset");
        let s = gnd_strength(g);
        assert!((0.3..=1.0).contains(&s), "strength {s} outside the field's 0.3..1");

        push_tone(g, 2.0, 1000.0, 0.0);
        assert_eq!(gnd_fires(g), 1, "a sustained 1 kHz tone adds no onset");
        gnd_free(g);
    }
}

#[test]
fn reset_and_a_new_rate_never_rewind_the_count() {
    /* Every plugin compares the count against the last one it saw, so a
     * rewind would draw a ring on every transport stop. */
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 1);
        kick(g);
        let seen = gnd_fires(g);
        assert_eq!(seen, 1);

        gnd_reset(g);
        assert_eq!(gnd_fires(g), seen);
        gnd_set_sample_rate(g, 44_100.0);
        assert_eq!(gnd_fires(g), seen);

        kick(g);
        assert_eq!(gnd_fires(g), seen + 1, "and it still fires afterwards");
        gnd_free(g);
    }
}

#[test]
fn switching_off_stops_it_and_switching_on_resumes() {
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 7); /* any nonzero is on */
        kick(g);
        assert_eq!(gnd_fires(g), 1);

        gnd_set_active(g, 0);
        kick(g);
        assert_eq!(gnd_fires(g), 1, "a closed editor's detector adds nothing");

        gnd_set_active(g, 1);
        kick(g);
        assert_eq!(gnd_fires(g), 2, "a reopened editor's detector fires again");
        gnd_free(g);
    }
}
