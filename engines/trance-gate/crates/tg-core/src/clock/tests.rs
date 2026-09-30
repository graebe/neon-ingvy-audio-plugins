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
