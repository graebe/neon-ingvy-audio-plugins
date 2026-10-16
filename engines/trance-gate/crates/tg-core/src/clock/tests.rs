// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The playhead: the phase-locked loop, and the edge cases a Rust caller can
//! reach that the C ABI's guards used to hide.

use crate::params::Param;
use crate::{Instance, Transport};

fn t(beats: f64) -> Transport {
    Transport { running: true, beats, bpm: 120.0 }
}

#[test]
fn a_zero_frame_block_changes_nothing() {
    /*
     * THE C ABI REFUSES frames <= 0, THE RUST API DID NOT. The loop's
     * correction divided by the frame count, so an empty block made the
     * increment 0/0, and the bypass path -- which advances the phase by
     * `inc * frames` without running the loop -- wrote that NaN into the
     * playhead, where it stayed for the life of the instance.
     */
    let mut p = Instance::new(44100.0);
    p.set_num(Param::Amount, 0.0);
    let mut buf = vec![0.0f32; 256];
    p.process_f32(&mut buf, 128, Some(&t(0.0)));
    let before = p.phase01();
    p.process_f32(&mut buf, 0, Some(&t(128.0 / 44100.0 * 2.0)));
    assert!(p.phase01().is_finite(), "the playhead is NaN");
    assert_eq!(p.phase01(), before, "an empty block moved the playhead");

    /* And with the gate active: the next real block still gates. */
    let mut p = Instance::new(44100.0);
    let mut buf = vec![1.0f32; 256];
    p.process_f32(&mut buf, 128, Some(&t(0.0)));
    p.process_f32(&mut [], 0, Some(&t(0.0)));
    let mut buf = vec![1.0f32; 256];
    p.process_f32(&mut buf, 128, Some(&t(128.0 / 44100.0 * 2.0)));
    assert!(buf.iter().all(|v| v.is_finite()));
}

/// Drive `p` for `seconds` in blocks of `frames` at 120 bpm, 44.1 kHz, with
/// the host's beat position jumping `jump_steps` ahead of the playhead after
/// 0.1 s. Returns the phase error (host minus playhead, in steps) right after
/// the jump and at the end, and the time between the two.
fn track(frames: usize, jump_steps: f64, seconds: f64, rate: &str) -> (f64, f64, f64) {
    let sr = 44100.0;
    let mut p = Instance::new(sr);
    p.set_param("rate", rate);
    let beats_per_step = crate::rates::RATES[crate::rates::index_from(rate)].beats;
    let mut buf = vec![0.5f32; frames * 2];
    let (mut done, mut offset) = (0usize, 0.0);
    let (mut e0, mut t0) = (None, 0.0);
    let mut last = 0.0;
    while (done as f64) < seconds * sr {
        let t = done as f64 / sr;
        if offset == 0.0 && t >= 0.1 {
            offset = jump_steps * beats_per_step;
        }
        let beats = t * 2.0 + offset;
        p.process_f32(&mut buf, frames, Some(&Transport { running: true, beats, bpm: 120.0 }));
        done += frames;
        let target = (done as f64 / sr * 2.0 + offset) / beats_per_step;
        last = target - p.phase.pos;
        if offset != 0.0 && e0.is_none() {
            e0 = Some(jump_steps);
            t0 = (done - frames) as f64 / sr;
        }
    }
    (e0.unwrap(), last, done as f64 / sr - t0)
}

#[test]
fn the_phase_converges_at_the_same_rate_whatever_the_block_size() {
    /*
     * The loop absorbed a fixed FRACTION of the error per BLOCK, so how fast
     * it converged depended on how often it was asked: at one sample a block
     * the whole error was gone in a few ms, at 4096 most of it was still
     * there a quarter of a second later. It is a time constant now, so the
     * error after a given time is the same at every block size.
     */
    for frames in [1usize, 32, 128, 4096] {
        let (e0, e, dt) = track(frames, 0.1, 0.1 + 0.3, "1/16");
        let want = (-dt / ni_dsp::phase::TRACK_TAU_S).exp();
        let got = e / e0;
        assert!(
            got > 0.0 && (got / want - 1.0).abs() < 0.05,
            "{frames}-frame blocks: {got:.5} of the error left after {dt:.3}s, want {want:.5}"
        );
    }
}

#[test]
fn the_playhead_never_runs_backwards() {
    /*
     * A correction larger than the step's own increment made the per-sample
     * increment NEGATIVE: at long steps and small blocks the playhead walked
     * backwards, re-crossed the boundary it had just passed and fired a step
     * that had already played. The increment is floored at half its nominal
     * value, so a lagging host slows the playhead and never reverses it.
     */
    for frames in [1usize, 32, 128, 4096] {
        let sr = 44100.0;
        let mut p = Instance::new(sr);
        p.set_param("rate", "1/1T");
        let mut buf = vec![0.5f32; frames * 2];
        let mut done = 0usize;
        let mut prev = f64::NEG_INFINITY;
        while done < 44100 {
            let t = done as f64 / sr;
            /* The host falls 0.2 of a step behind at 0.5 s. */
            let lag = if t >= 0.5 { 0.2 * 8.0 / 3.0 } else { 0.0 };
            let tr = Transport { running: true, beats: t * 2.0 - lag, bpm: 120.0 };
            p.process_f32(&mut buf, frames, Some(&tr));
            assert!(p.phase.pos >= prev, "{frames}-frame blocks: the playhead went back at {t:.4}s: {prev} -> {}", p.phase.pos);
            prev = p.phase.pos;
            done += frames;
        }
    }
}

#[test]
fn a_position_the_host_cannot_give_is_no_position() {
    /*
     * JUCE'S SHELL SAYS "NO MUSICAL POSITION" WITH NaN (ni::readClock), where
     * iPlug2's said -1; and a host may report an infinity. Each must play
     * exactly as a stopped transport does -- the cycle open, the playhead
     * parked -- never as a position the phase loop chases.
     */
    let render = |transport: Transport| {
        let mut p = Instance::new(44100.0);
        let mut out = Vec::new();
        for _ in 0..40 {
            let mut buf = vec![0.5f32; 256];
            p.process_f32(&mut buf, 128, Some(&transport));
            out.extend_from_slice(&buf);
        }
        (out, p.playhead().advancing, p.phase01())
    };
    let (stopped, _, parked) = render(Transport { running: false, beats: -1.0, bpm: 120.0 });
    for beats in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        let (out, advancing, phase) = render(t(beats));
        assert!(!advancing, "{beats}: the playhead advanced");
        assert_eq!(phase.to_bits(), parked.to_bits(), "{beats}: the playhead moved");
        assert!(out == stopped, "{beats}: did not play as a stopped transport");
    }
}

/* ---------------------------------------------------------------- panic -- */

const FRAMES: usize = 512;
const PER_BLOCK: f64 = FRAMES as f64 / 44100.0 * 2.0;

/// One block of ones through `p` at `beats`: the gate's gain, sample by
/// sample (the left channel).
fn gains(p: &mut Instance, beats: f64, transport: bool) -> Vec<f32> {
    let mut buf = vec![1.0f32; FRAMES * 2];
    let t = Transport { running: transport, beats: if transport { beats } else { -1.0 }, bpm: 120.0 };
    p.process_f32(&mut buf, FRAMES, Some(&t));
    buf.iter().step_by(2).copied().collect()
}

fn saved(p: &Instance) -> Vec<u8> {
    let mut b = vec![0u8; 1 << 16];
    let n = p.get_param("state", &mut b);
    b.truncate(n as usize);
    b
}

/// A gate in the middle of a step, at `sustain` -- slot 1, every step on,
/// with an attack long enough to watch -- and the beat its next block starts
/// at.
fn open_gate(sustain: &str) -> (Instance, f64) {
    let mut p = Instance::new(44100.0);
    p.set_param("slot", "1");
    p.set_param("attack", "20");
    p.set_param("sustain", sustain);
    let mut beats = 0.0;
    for _ in 0..9 {
        gains(&mut p, beats, true);
        beats += PER_BLOCK;
    }
    (p, beats)
}

#[test]
fn a_panic_shuts_the_gate_and_the_step_strikes_again_from_silence() {
    let (mut p, beats) = open_gate("1");
    let (mut twin, _) = open_gate("1");
    assert!(gains(&mut twin, beats, true).iter().all(|&g| g > 0.99), "the gate is open mid-step");
    let state = saved(&p);
    p.panic();
    let after = gains(&mut p, beats, true);
    assert!(after[0] < 1e-3, "shut at once: {}", after[0]);
    assert!(after.windows(2).all(|w| w[1] >= w[0]), "and opening with the step's attack");
    assert!(after[FRAMES - 1] > 0.1, "{}", after[FRAMES - 1]);
    assert_eq!(saved(&p), state, "nothing saved moved");
}

#[test]
fn a_stopped_gate_holds_nothing_and_a_panic_leaves_it_alone() {
    let run = |panic: bool| {
        /* Held at half: the stop freezes the envelope there and glides
         * Amount out, so the gate opens from half to whole. */
        let (mut p, mut beats) = open_gate("0.5");
        let mut out = Vec::new();
        for b in 0..20 {
            if panic && b == 2 {
                p.panic();
            }
            out.extend(gains(&mut p, beats, false));
            beats += PER_BLOCK;
        }
        out
    };
    let stopped = run(false);
    assert!(stopped[0] < stopped[stopped.len() - 1], "the stop glides the gate open");
    assert_eq!(run(true), stopped);
}

#[test]
fn cc_120_and_123_on_any_channel_are_the_panic_and_nothing_else_is() {
    let (mut quiet, beats) = open_gate("1");
    let unpanicked = gains(&mut quiet, beats, true);
    for msg in [&[0xB0u8, 120, 0][..], &[0xBF, 123, 0]] {
        let (mut p, beats) = open_gate("1");
        p.on_midi(msg);
        assert!(gains(&mut p, beats, true)[0] < 1e-3, "{msg:?} is a panic");
    }
    for msg in [&[0x90u8, 60, 100][..], &[0xB0, 7, 0], &[0xB0, 120], &[]] {
        let (mut p, beats) = open_gate("1");
        p.on_midi(msg);
        assert_eq!(gains(&mut p, beats, true), unpanicked, "{msg:?} is no panic");
    }
    let (mut p, beats) = open_gate("1");
    p.set_param("panic", "1");
    assert!(gains(&mut p, beats, true)[0] < 1e-3, "and the text door takes it too");
}
