// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The gnd_* C ABI, called the way a plugin calls it.
 *
 * tests/gnd_roundtrip.c makes these same claims from C, against the header,
 * and that is what keeps the header honest. It links a release staticlib,
 * though, so cargo's coverage never sees this file run. These are the claims
 * the four plugins depend on, made again from Rust through the same raw
 * pointers -- not the beat clock's musical rule, which ground-core tests.
 */

use super::*;
use core::ptr::{null, null_mut};

const SR: f64 = 48_000.0;
const BLOCK: i32 = 512;

/// `secs` of a 120 BPM 4/4 transport from `from` quarters, in host-sized
/// blocks; returns the position it stopped at.
unsafe fn play(g: *mut GndGround, from: f64, secs: f64, playing: bool) -> f64 {
    let blocks = (secs * SR / BLOCK as f64) as usize;
    let span = BLOCK as f64 * 2.0 / SR;
    let mut ppq = from;
    for _ in 0..blocks {
        gnd_tick(g, ppq, 120.0, 4, 4, playing as i32, BLOCK);
        if playing {
            ppq += span;
        }
    }
    ppq
}

#[test]
fn a_null_handle_is_silence_not_a_crash() {
    /* ProcessBlock calls these unconditionally; "no ground" must read as
     * "nothing happened". */
    unsafe {
        gnd_free(null_mut());
        gnd_reset(null_mut());
        gnd_set_sample_rate(null_mut(), SR);
        gnd_set_active(null_mut(), 1);
        gnd_tick(null_mut(), 0.0, 120.0, 4, 4, 1, BLOCK);
        assert_eq!(gnd_fires(null()), 0);
        assert_eq!(gnd_strength(null()), 0.0);
    }
}

#[test]
fn a_new_ground_is_quiet_and_inactive() {
    unsafe {
        let g = gnd_new(SR);
        assert!(!g.is_null());
        assert_eq!(gnd_fires(g), 0, "a fresh ground has rung nothing");
        assert_eq!(gnd_strength(g), 0.0, "and reports no strength");

        play(g, 0.0, 2.0, true);
        assert_eq!(gnd_fires(g), 0, "inactive until an editor opens");
        gnd_free(g);
    }
}

#[test]
fn an_empty_block_is_a_no_op() {
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 1);
        gnd_tick(g, 0.0, 120.0, 4, 4, 1, 0);
        gnd_tick(g, 0.0, 120.0, 4, 4, 1, -1);
        assert_eq!(gnd_fires(g), 0);

        /* And the guards left it working. */
        gnd_tick(g, 0.0, 120.0, 4, 4, 1, BLOCK);
        assert_eq!(gnd_fires(g), 1);
        gnd_free(g);
    }
}

#[test]
fn a_playing_transport_rings_each_beat_and_the_downbeat_strongest() {
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 1);
        /* Beat by beat: each half second is one ring. */
        let mut seen = Vec::new();
        let mut ppq = 0.0;
        for _ in 0..8 {
            ppq = play(g, ppq, 0.5, true);
            seen.push((gnd_fires(g), gnd_strength(g)));
        }
        let want: Vec<(u32, f32)> =
            (1..=8).map(|n| (n, if n % 4 == 1 { 1.0 } else { 0.4 })).collect();
        assert_eq!(seen, want);
        gnd_free(g);
    }
}

#[test]
fn a_stopped_transport_rings_nothing() {
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 1);
        play(g, 0.0, 3.0, false);
        assert_eq!(gnd_fires(g), 0);
        gnd_free(g);
    }
}

#[test]
fn a_host_without_a_time_signature_is_four_four() {
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 1);
        gnd_tick(g, 4.0, 120.0, 0, 0, 1, BLOCK);
        assert_eq!((gnd_fires(g), gnd_strength(g)), (1, 1.0), "4 is a downbeat in 4/4");
        gnd_tick(g, 3.0, 120.0, 0, 0, 1, BLOCK);
        assert_eq!((gnd_fires(g), gnd_strength(g)), (2, 0.4), "3 is not");
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
        play(g, 0.0, 0.25, true);
        let seen = gnd_fires(g);
        assert_eq!(seen, 1);

        gnd_reset(g);
        assert_eq!(gnd_fires(g), seen);
        gnd_set_sample_rate(g, 44_100.0);
        assert_eq!(gnd_fires(g), seen);

        play(g, 0.0, 0.25, true);
        assert_eq!(gnd_fires(g), seen + 1, "and it rings again afterwards");
        gnd_free(g);
    }
}

#[test]
fn switching_off_stops_it_and_switching_on_resumes() {
    unsafe {
        let g = gnd_new(SR);
        gnd_set_active(g, 7); /* any nonzero is on */
        let ppq = play(g, 0.0, 0.25, true);
        assert_eq!(gnd_fires(g), 1);

        gnd_set_active(g, 0);
        let ppq = play(g, ppq, 1.0, true);
        assert_eq!(gnd_fires(g), 1, "a closed editor's ground adds nothing");

        gnd_set_active(g, 1);
        play(g, ppq, 1.0, true);
        assert_eq!(gnd_fires(g), 3, "a reopened editor's ground rings the next beats");
        gnd_free(g);
    }
}
