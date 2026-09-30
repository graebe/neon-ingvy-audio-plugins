/*!
What the detector must do, stated as signals rather than as internal state.

EVERY TEST HERE FEEDS AUDIO AND COUNTS ONSETS. That is deliberate: the
constants in `detect.rs` belong to the design system, so a test that asserted
one of them would only restate the file above it. What is worth pinning is the
BEHAVIOUR those constants were chosen for -- a kick fires, a hi-hat does not,
one hit is one ring -- because that is what breaks if somebody retunes a
coefficient and everything still compiles.
*/

use super::*;
use core::sync::atomic::Ordering;
use crate::detect::{Band, BAND_HI_HZ, BAND_LO_HZ, FLOOR};

const SR: f64 = 48_000.0;

/// Samples for a duration in seconds.
fn samples(secs: f64) -> usize {
    (secs * SR).round() as usize
}

/// A kick: a decaying sine at `hz`. Amplitude 1 at the transient, e-folding in
/// `decay` seconds -- close enough to a drum machine's that the detector sees
/// the same shape it will see in a host.
fn kick(hz: f64, decay: f64, n: usize) -> Vec<f64> {
    (0..n)
        .map(|i| {
            let t = i as f64 / SR;
            let a = (-t / decay).exp();
            a * (2.0 * core::f64::consts::PI * hz * t).sin()
        })
        .collect()
}

/// A steady tone at `hz`, faded in over 50 ms.
///
/// THE FADE IS THE POINT, not politeness. A tone that starts abruptly at full
/// scale is a step, and a step has energy at every frequency including 20-80 Hz
/// -- so a hard-started 1 kHz sine legitimately fires this detector, and so
/// would a hard-started anything. Fading in asks the question the test means:
/// does a SUSTAINED high-frequency signal fire it.
fn tone(hz: f64, n: usize) -> Vec<f64> {
    let fade = samples(0.05) as f64;
    (0..n)
        .map(|i| {
            let t = i as f64 / SR;
            let a = (i as f64 / fade).min(1.0);
            a * (2.0 * core::f64::consts::PI * hz * t).sin()
        })
        .collect()
}

/// Silence, `n` samples of it.
fn silence(n: usize) -> Vec<f64> {
    vec![0.0f64; n]
}

/// Feed a mono signal to both channels and collect every onset.
fn run(d: &mut Detector, signal: &[f64]) -> Vec<Onset> {
    signal.iter().filter_map(|&s| d.next(s, s)).collect()
}

/* ---------- the band ---------- */

#[test]
fn the_band_passes_a_kick_and_rejects_a_hi_hat() {
    /* Both signals are full scale. What separates them is only frequency, which
     * is the one thing this detector is supposed to care about. */
    let mut bass = Detector::new(SR);
    run(&mut bass, &tone(60.0, samples(0.5)));

    let mut treble = Detector::new(SR);
    run(&mut treble, &tone(1_000.0, samples(0.5)));

    assert!(
        bass.level() > 0.1,
        "a 60 Hz tone sits inside the band; the envelope reached only {}",
        bass.level()
    );
    assert!(
        treble.level() < FLOOR,
        "a 1 kHz tone is two 12 dB/oct sections outside the band, so it must not \
         even reach the floor; the envelope reached {}",
        treble.level()
    );
}

#[test]
fn the_band_rejects_infrasound_below_it() {
    /* The high-pass end matters as much as the low-pass one: DC offset and a
     * rumble a listener cannot hear would otherwise hold the mean up and
     * desensitise the detector to the kicks it exists for.
     *
     * The assertion is a RATIO rather than an absolute floor, because 12 dB/oct
     * is a slope and not a wall: 5 Hz is two octaves down and arrives at about
     * a tenth of its level, which is small but not under `FLOOR`. What matters
     * is that it cannot compete with a kick, and that a sustained rumble does
     * not keep firing. */
    let mut sub = Detector::new(SR);
    run(&mut sub, &tone(5.0, samples(1.0)));
    let mut mid = Detector::new(SR);
    run(&mut mid, &tone(40.0, samples(1.0)));
    assert!(
        sub.level() * 8.0 < mid.level(),
        "5 Hz ({}) must be far below the band's centre at 40 Hz ({})",
        sub.level(),
        mid.level()
    );

    /* And no ONGOING onsets: the first crossing as the rumble appears is fair
     * (something did get louder), but a steady sub must then settle.
     *
     * EVERY SAMPLE IS FED AND ONLY THE COUNT IS WINDOWED. Skipping the early
     * samples instead would hand the detector a cold start on a signal already
     * at full scale -- a step, which fires legitimately -- and the test would
     * then be measuring its own setup. */
    let mut d = Detector::new(SR);
    let mut late = 0;
    for (i, &s) in tone(5.0, samples(5.0)).iter().enumerate() {
        if d.next(s, s).is_some() && i > samples(0.5) {
            late += 1;
        }
    }
    assert_eq!(late, 0, "a sustained 5 Hz rumble kept firing");
}

#[test]
fn a_rectified_kick_would_read_as_nothing() {
    /* THIS TEST EXISTS BECAUSE THE BUG IT CATCHES WAS WRITTEN. `next` filters
     * the signed signal and rectifies afterwards. Rectifying first -- the move
     * the side-chain's broadband follower opens with -- looks harmless and is
     * not: |sin(2*pi*60*t)| has a DC term and harmonics from 120 Hz up, and no
     * energy at 60 Hz whatsoever, so a 20-80 Hz band returns almost nothing.
     *
     * The symptom was a detector that merely seemed insensitive, and whose level
     * FELL as the kick's pitch rose. Feeding the rectified signal in on purpose
     * and asserting it reads far lower than the real one pins the ordering. */
    let signal = tone(60.0, samples(1.0));
    let rectified: Vec<f64> = signal.iter().map(|s| s.abs()).collect();

    let mut signed = Detector::new(SR);
    run(&mut signed, &signal);
    let mut pre = Detector::new(SR);
    run(&mut pre, &rectified);

    assert!(
        pre.level() * 4.0 < signed.level(),
        "a pre-rectified 60 Hz sine ({}) must read far lower than the signed one \
         ({}) -- if these are close, the band is being applied after a rectifier",
        pre.level(),
        signed.level()
    );
}

#[test]
fn an_absurd_sample_rate_is_a_pass_through_not_a_nan() {
    /* A host that reports a zero sample rate before its first block is a real
     * thing. The detector must survive it and still work once told the truth,
     * rather than latch on a NaN nothing ever clears. */
    let mut d = Detector::new(0.0);
    let onsets = run(&mut d, &kick(60.0, 0.05, 4_800));
    assert!(d.level().is_finite(), "level went non-finite: {}", d.level());
    assert!(
        onsets.iter().all(|o| o.strength.is_finite()),
        "a strength went non-finite"
    );

    d.set_sample_rate(SR);
    assert_eq!(
        run(&mut d, &kick(60.0, 0.05, samples(0.4))).len(),
        1,
        "the detector must work normally once given a real sample rate"
    );
}

/* ---------- onsets ---------- */

#[test]
fn silence_never_fires() {
    let mut d = Detector::new(SR);
    assert!(run(&mut d, &silence(samples(5.0))).is_empty());
}

#[test]
fn a_sustained_hi_hat_never_fires() {
    let mut d = Detector::new(SR);
    assert!(
        run(&mut d, &tone(1_000.0, samples(5.0))).is_empty(),
        "a full-scale 1 kHz tone must not move the ground -- this is the property \
         the side-chain's broadband follower does NOT have, and the reason this \
         detector exists separately from it"
    );
}

#[test]
fn one_kick_is_one_onset() {
    let mut d = Detector::new(SR);
    /* 400 ms is long enough that the release (150 ms) has brought the envelope
     * back down, so a second crossing would show up here if there were one. */
    assert_eq!(run(&mut d, &kick(60.0, 0.05, samples(0.4))).len(), 1);
}

#[test]
fn a_kick_per_beat_at_120_bpm() {
    let beat = samples(0.5);
    let mut signal = Vec::new();
    for _ in 0..8 {
        signal.extend_from_slice(&kick(60.0, 0.05, beat));
    }
    let mut d = Detector::new(SR);
    assert_eq!(
        run(&mut d, &signal).len(),
        8,
        "eight kicks, eight rings -- no merging and no double-triggering"
    );
}

#[test]
fn the_refractory_merges_two_hits_inside_120_ms() {
    /* 60 ms apart: two transients, one ring. The design asks for 120 ms between
     * onsets, and a 32nd-note kick roll must read as one event rather than
     * filling the window with rings. */
    let gap = samples(0.06);
    let mut signal = kick(60.0, 0.05, gap);
    signal.extend_from_slice(&kick(60.0, 0.05, samples(0.4)));

    let mut d = Detector::new(SR);
    assert_eq!(run(&mut d, &signal).len(), 1);
}

#[test]
fn well_separated_hits_both_fire() {
    /* The companion to the test above: the refractory is a gate, not a ceiling. */
    let gap = samples(0.3);
    let mut signal = kick(60.0, 0.05, gap);
    signal.extend_from_slice(&kick(60.0, 0.05, samples(0.4)));

    let mut d = Detector::new(SR);
    assert_eq!(run(&mut d, &signal).len(), 2);
}

#[test]
fn the_rearm_and_not_the_refractory_sets_the_practical_floor() {
    /* WORTH KNOWING AND EASY TO GET WRONG. The design's detector row names a
     * 120 ms refractory, so it is natural to read that as "kicks more than
     * 120 ms apart each ring". They do not. The binding constraint is the
     * RE-ARM: after an onset the detector stays un-armed until the envelope's
     * excess over the bed falls back to half of what it took to fire, and with a
     * 150 ms envelope release that takes longer than the refractory does.
     *
     * Measured, the threshold sits between 200 and 250 ms. So a straight
     * sixteenth-note kick roll merges, and eighth notes at 120 BPM (250 ms) do
     * not -- which is the musically useful place for it to land, and is why the
     * 150 ms release is worth keeping even though it is what causes this.
     *
     * The number survived re-keying the detector onto the attack (see
     * `detect.rs`), which is worth recording: the mechanism behind it changed
     * completely and the behaviour did not. This test pins it so a future reader
     * finds the answer here rather than in a host. */
    let both = |ms: f64| {
        let mut signal = kick(60.0, 0.05, samples(ms / 1000.0));
        signal.extend_from_slice(&kick(60.0, 0.05, samples(0.5)));
        let mut d = Detector::new(SR);
        run(&mut d, &signal).len()
    };
    assert_eq!(both(160.0), 1, "160 ms apart merges, despite the 120 ms refractory");
    assert_eq!(both(250.0), 2, "250 ms apart is far enough for the re-arm");
}

#[test]
fn strength_stays_inside_the_range_the_field_expects() {
    /* `Field`'s `gain` is calibrated for s in 0..1, and the design clamps to
     * 0.3..1 so that a marginal kick still shows. Feed everything from a whisper
     * to a clipped kick and the contract must hold for every onset. */
    for amp in [0.02f64, 0.1, 0.5, 1.0, 4.0] {
        let mut d = Detector::new(SR);
        let signal: Vec<f64> = kick(60.0, 0.05, samples(1.0))
            .iter()
            .map(|s| s * amp)
            .collect();
        for onset in run(&mut d, &signal) {
            assert!(
                (0.3..=1.0).contains(&onset.strength),
                "amp {amp} produced strength {}",
                onset.strength
            );
        }
    }
}

#[test]
fn a_louder_kick_is_a_stronger_onset() {
    /* Monotonicity is the only thing about the strength curve worth asserting:
     * the exact value is the design's formula, but a louder kick reading as a
     * SMALLER ring would be visibly wrong however the formula changed. */
    let strength = |amp: f64| {
        let mut d = Detector::new(SR);
        /* A little music first, so the running mean is a real one rather than
         * the near-zero of a cold start -- which saturates the ratio and would
         * give every amplitude the same clamped answer. */
        let bed: Vec<f64> = tone(60.0, samples(1.0)).iter().map(|s| s * 0.05).collect();
        run(&mut d, &bed);
        run(&mut d, &silence(samples(0.5)));
        let signal: Vec<f64> = kick(60.0, 0.05, samples(0.5))
            .iter()
            .map(|s| s * amp)
            .collect();
        run(&mut d, &signal).first().map(|o| o.strength)
    };
    let quiet = strength(0.15).expect("a quiet kick over a quiet bed still fires");
    let loud = strength(1.0).expect("a loud kick fires");
    assert!(
        loud > quiet,
        "a full-scale kick ({loud}) must read at least as strong as a quiet one ({quiet})"
    );
}

#[test]
fn a_nan_in_the_buffer_does_not_kill_the_detector() {
    /* A NaN envelope compares false against every threshold, so without the
     * guard in `next` the ground would go dead for the rest of the session and
     * nothing would say why. */
    let mut d = Detector::new(SR);
    run(&mut d, &[f64::NAN; 64]);
    run(&mut d, &[f64::INFINITY; 64]);
    assert!(d.level().is_finite(), "level is {}", d.level());
    assert_eq!(
        run(&mut d, &kick(60.0, 0.05, samples(0.4))).len(),
        1,
        "the detector must still fire after being fed garbage"
    );
}

#[test]
fn silence_after_a_signal_decays_to_zero_not_to_subnormals() {
    /*
     * THE SILENCE PATH IS THE HOT PATH. Every plugin feeds this detector on
     * every block, and most of a session is silence between clips. A one-pole
     * or a biquad fed zeros decays geometrically towards zero and, without a
     * flush, lands in the SUBNORMAL range and stays there: `x * (1 - c)` of a
     * subnormal rounds back to the same subnormal once `x * c` underflows, and
     * x86 and many ARM cores take a microcode assist on every operation with
     * one. Nothing here sets FTZ/DAZ, and the Move's aarch64 is not ours to
     * configure, so the detector has to keep its own state clean.
     *
     * The claim is checked all the way through the silence rather than only at
     * the end, so that a value passing through the subnormal range on its way
     * to zero also fails.
     */
    let mut d = Detector::new(SR);
    run(&mut d, &kick(60.0, 0.05, samples(0.5)));
    run(&mut d, &tone(40.0, samples(0.5)));
    let block = samples(0.01);
    let quiet = silence(block);
    for n in 0..(300.0 / 0.01) as usize {
        run(&mut d, &quiet);
        for (i, v) in d.state().into_iter().enumerate() {
            assert!(
                v == 0.0 || v.is_normal(),
                "state[{i}] = {v:e} is subnormal after {:.2} s of silence",
                n as f64 * 0.01
            );
        }
    }
    assert!(
        d.state().iter().all(|&v| v == 0.0),
        "five minutes of silence must leave the detector at exactly zero: {:?}",
        d.state()
    );
    /* And it still works from there. */
    assert_eq!(run(&mut d, &kick(60.0, 0.05, samples(0.4))).len(), 1);
}

#[test]
fn a_subnormal_input_does_not_enter_the_state() {
    /* A host can hand over subnormals itself -- the tail of somebody else's
     * reverb. They must not be let into the delays. */
    let mut d = Detector::new(SR);
    run(&mut d, &[f64::MIN_POSITIVE / 4.0; 256]);
    for (i, v) in d.state().into_iter().enumerate() {
        assert!(v == 0.0 || v.is_normal(), "state[{i}] = {v:e}");
    }
}

#[test]
fn reset_forgets_the_hump_but_not_the_contract() {
    let mut d = Detector::new(SR);
    run(&mut d, &kick(60.0, 0.05, samples(0.05)));
    d.reset();
    assert_eq!(d.level(), 0.0);
    /* And it fires again from a cold start, rather than being stuck un-armed. */
    assert_eq!(run(&mut d, &kick(60.0, 0.05, samples(0.4))).len(), 1);
}

#[test]
fn the_detector_works_at_every_supported_sample_rate() {
    /* The coefficients are all derived from the sample rate, and 20 Hz at
     * 192 kHz is the pole closest to the unit circle the sections will ever be
     * asked to hold. If f64 state were not enough, it would show here. */
    for sr in [44_100.0f64, 48_000.0, 88_200.0, 96_000.0, 192_000.0] {
        let n = (0.4 * sr).round() as usize;
        let signal: Vec<f64> = (0..n)
            .map(|i| {
                let t = i as f64 / sr;
                (-t / 0.05).exp() * (2.0 * core::f64::consts::PI * 60.0 * t).sin()
            })
            .collect();
        let mut d = Detector::new(sr);
        let onsets: Vec<Onset> = signal.iter().filter_map(|&s| d.next(s, s)).collect();
        assert_eq!(onsets.len(), 1, "one kick at {sr} Hz gave {onsets:?}");
    }
}

#[test]
fn a_kick_panned_hard_left_still_fires() {
    /* The detector is the channel maximum, not a sum, so that the threshold
     * reads the drum and not the panner. */
    let signal = kick(60.0, 0.05, samples(0.4));
    let mut d = Detector::new(SR);
    let onsets: Vec<Onset> = signal.iter().filter_map(|&s| d.next(s, 0.0)).collect();
    assert_eq!(onsets.len(), 1);
}

/* ---------- what the editor reads ---------- */

/// A `Ground` switched on, as a plugin's is while its editor is open.
fn live(sr: f64) -> Ground {
    let g = Ground::new(sr);
    g.set_active(true);
    g
}

/// `push` from the one thread these tests run on, which trivially satisfies its
/// single-producer contract.
fn feed(g: &Ground, l: &[f64], r: &[f64]) {
    unsafe { g.push(l, r) }
}

/// The detector's level, read from the test thread while nothing is pushing.
fn level_of(g: &Ground) -> f64 {
    unsafe { (*g.detector.get()).level() }
}

#[test]
fn the_published_pair_reports_each_kick_once() {
    let g = live(SR);
    assert_eq!(g.fires(), 0);
    assert_eq!(g.strength(), 0.0);

    let beat = kick(60.0, 0.05, samples(0.5));
    feed(&g, &beat, &beat);
    assert_eq!(g.fires(), 1);
    let first = g.strength();
    assert!((0.3..=1.0).contains(&first), "strength {first}");

    feed(&g, &beat, &beat);
    assert_eq!(g.fires(), 2, "the count is monotonic across blocks");
}

#[test]
fn push_takes_the_shorter_of_two_channels() {
    /* The only reading of a mismatch that cannot index past an end. A host
     * should never hand us one, which is exactly why it must not be UB when it
     * does. */
    let g = live(SR);
    let long = kick(60.0, 0.05, samples(0.5));
    feed(&g, &long, &long[..10]);
    assert_eq!(g.fires(), 0, "ten samples cannot contain an onset");
}

#[test]
fn reset_does_not_rewind_the_count() {
    /* The editor compares the count against what it saw last. If a reset moved
     * it backwards, a reader would see "changed" and draw a ring nothing
     * caused -- on every transport stop. */
    let g = live(SR);
    let beat = kick(60.0, 0.05, samples(0.5));
    feed(&g, &beat, &beat);
    let before = g.fires();
    g.reset();
    feed(&g, &[0.0], &[0.0]);
    assert_eq!(g.fires(), before);
    g.set_sample_rate(44_100.0);
    feed(&g, &[0.0], &[0.0]);
    assert_eq!(g.fires(), before);
}

/* ---------- who may touch what, and when ---------- */

#[test]
fn a_new_ground_is_inactive_and_ignores_audio() {
    /* The detector only drives an editor. With none open it must cost nothing,
     * which means a fresh one has to wait to be switched on. */
    let g = Ground::new(SR);
    assert!(!g.is_active());
    let beat = kick(60.0, 0.05, samples(0.5));
    feed(&g, &beat, &beat);
    assert_eq!(g.fires(), 0);
    assert_eq!(level_of(&g), 0.0, "an inactive push must not even run the band");
}

#[test]
fn deactivating_stops_the_detector_mid_signal() {
    let g = live(SR);
    let beat = kick(60.0, 0.05, samples(0.5));
    feed(&g, &beat, &beat);
    assert_eq!(g.fires(), 1);
    g.set_active(false);
    feed(&g, &beat, &beat);
    assert_eq!(g.fires(), 1, "a closed editor's detector must not fire");
}

#[test]
fn reactivating_resumes_from_silence_not_from_the_stale_hump() {
    /* Switched off on a kick's peak, the detector is holding a high envelope.
     * Switched back on, it must start from zero: a resumed hump would compare
     * against a bed that no longer describes the music. */
    let g = live(SR);
    let beat = kick(60.0, 0.05, samples(0.02));
    feed(&g, &beat, &beat);
    assert!(level_of(&g) > 0.0);
    g.set_active(false);
    g.set_active(true);
    /* The reset is a REQUEST, applied by the next push on the audio thread. */
    feed(&g, &[0.0], &[0.0]);
    assert_eq!(level_of(&g), 0.0, "reopening must start the detector cold");
    /* ... and it still finds the next kick. */
    let beat = kick(60.0, 0.05, samples(0.5));
    feed(&g, &beat, &beat);
    assert!(g.fires() >= 1);
}

#[test]
fn a_reset_is_applied_by_the_next_push_and_not_before() {
    /* The reset used to rewrite the detector from whatever thread called it --
     * racing a `push` halfway through a block. Now it is a request, and only
     * the audio thread ever writes the detector. */
    let g = live(SR);
    let beat = kick(60.0, 0.05, samples(0.02));
    feed(&g, &beat, &beat);
    let held = level_of(&g);
    assert!(held > 0.0);
    g.reset();
    assert_eq!(level_of(&g), held, "reset must not touch the detector itself");
    feed(&g, &[0.0], &[0.0]);
    assert_eq!(level_of(&g), 0.0);
}

#[test]
fn a_sample_rate_change_is_applied_by_the_next_push() {
    /* 60 Hz sampled at 96 kHz and analysed as if it were 48 kHz reads as a
     * 30 Hz tone; the point here is only that the new rate is the one in use
     * after the next push. One kick at the new rate is one onset. */
    let g = live(SR);
    g.set_sample_rate(96_000.0);
    let n = (0.5 * 96_000.0) as usize;
    let beat: Vec<f64> = (0..n)
        .map(|i| {
            let t = i as f64 / 96_000.0;
            (-t / 0.05).exp() * (2.0 * core::f64::consts::PI * 60.0 * t).sin()
        })
        .collect();
    feed(&g, &beat, &beat);
    assert_eq!(g.fires(), 1);
}

#[test]
fn two_threads_hammering_one_ground_stay_consistent() {
    /*
     * THE THREAD CONTRACT, UNDER LOAD. One thread is the audio callback,
     * pushing a kick every half second of audio; the other is everything else
     * at once -- the message thread polling the pair, the host resetting and
     * changing the rate, the editor opening and closing. Nothing may tear:
     * the count only moves forward, the strength is always a value `push`
     * could have written, and when the storm is over the detector still works.
     */
    use std::sync::atomic::AtomicBool;
    use std::sync::Arc;

    let g = Arc::new(live(SR));
    let done = Arc::new(AtomicBool::new(false));

    let audio = {
        let g = Arc::clone(&g);
        let done = Arc::clone(&done);
        std::thread::spawn(move || {
            let beat = kick(60.0, 0.05, samples(0.5));
            let mut blocks = 0usize;
            while !done.load(Ordering::Relaxed) {
                for chunk in beat.chunks(256) {
                    /* SAFETY: this is the only thread that pushes. */
                    unsafe { g.push(chunk, chunk) };
                    blocks += 1;
                }
            }
            blocks
        })
    };

    let mut last = g.fires();
    for i in 0..20_000u32 {
        let f = g.fires();
        assert!(f.wrapping_sub(last) < u32::MAX / 2, "the count went backwards: {last} -> {f}");
        last = f;
        let s = g.strength();
        assert!(s == 0.0 || (0.3..=1.0).contains(&s), "torn strength {s}");
        match i % 7 {
            0 => g.reset(),
            1 => g.set_sample_rate(if i % 2 == 0 { 44_100.0 } else { 48_000.0 }),
            2 => g.set_active(false),
            3 => g.set_active(true),
            _ => {}
        }
        if i % 64 == 0 {
            std::thread::yield_now();
        }
    }
    g.set_sample_rate(SR);
    g.set_active(true);
    /* Let the audio thread run clean for a while: it must still be detecting. */
    let before = g.fires();
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(10);
    while g.fires() == before && std::time::Instant::now() < deadline {
        std::thread::yield_now();
    }
    done.store(true, Ordering::Relaxed);
    let blocks = audio.join().expect("the audio thread must not panic");
    assert!(blocks > 0);
    assert_ne!(g.fires(), before, "the detector must still fire after the storm");
}

#[test]
fn the_band_is_the_butterworth_response_it_claims_to_be() {
    /* MEASURED, NOT ASSUMED. The coefficients are the Audio EQ Cookbook's
     * precisely so that this band matches the pair of Web Audio nodes the design
     * system's reference builds, and a transcription slip in one of the five
     * coefficients gives a filter that is stable, plausible and wrong. So the
     * response is swept and checked against the closed form.
     *
     * The corners must read 1/sqrt(2) -- that IS the definition of the -3 dB
     * point, and it is the one number a wrong Q or a swapped sign would move.
     * The 12 dB/oct slopes are checked two octaves out on each side, where a
     * second-order section has fallen by a factor of sixteen. */
    let magnitude = |hz: f64| {
        let mut band = Band::new(SR);
        let n = samples(2.0);
        let settled = n - samples(0.5);
        let mut peak = 0.0f64;
        for i in 0..n {
            let t = i as f64 / SR;
            let y = band.next((2.0 * core::f64::consts::PI * hz * t).sin());
            if i > settled && y.abs() > peak {
                peak = y.abs();
            }
        }
        peak
    };

    let corner = core::f64::consts::FRAC_1_SQRT_2;
    for hz in [BAND_LO_HZ, BAND_HI_HZ] {
        let m = magnitude(hz);
        assert!(
            (m - corner).abs() < 0.01,
            "{hz} Hz is a corner and must read {corner:.4}, not {m:.4}"
        );
    }

    /* Inside the band, near its centre, essentially everything gets through. */
    let centre = magnitude(40.0);
    assert!(centre > 0.9, "40 Hz sits inside the band and read {centre:.4}");

    /* Two octaves below 20 Hz and two above 80 Hz: down by ~16, i.e. 24 dB. */
    let below = magnitude(BAND_LO_HZ / 4.0);
    let above = magnitude(BAND_HI_HZ * 4.0);
    for (name, m) in [("5 Hz", below), ("320 Hz", above)] {
        assert!(
            m < 0.08,
            "{name} is two octaves outside a 12 dB/oct edge and must fall below \
             0.08; it read {m:.4}"
        );
    }
    /* And the two edges are symmetric to within a few percent, which is what
     * says both sections are the same order and the same Q. */
    assert!(
        (below - above).abs() < 0.02,
        "the two edges are not the same slope: {below:.4} below, {above:.4} above"
    );
}

#[test]
fn no_sustained_tone_anywhere_in_the_band_chatters() {
    /* The design's rule is that the ground settles back to the static design
     * when nothing is happening. A held bass note is "nothing happening", and a
     * detector whose RMS window (20 ms) is shorter than the period it is
     * measuring (50 ms at 20 Hz) is exactly the shape that could ripple its way
     * across the threshold once a second. It does not -- because the running
     * mean ripples with it -- and that is worth holding still. */
    for hz in [5.0f64, 10.0, 20.0, 30.0, 40.0, 60.0, 80.0] {
        let mut d = Detector::new(SR);
        let mut late = 0;
        let signal = tone(hz, samples(10.0));
        for (i, &s) in signal.iter().enumerate() {
            if d.next(s, s).is_some() && i > samples(0.5) {
                late += 1;
            }
        }
        assert_eq!(
            late, 0,
            "a sustained {hz} Hz tone fired {late} times after settling"
        );
    }
}

/* ---------- real program material ---------- */

/*
 * THE TEST THAT WOULD HAVE CAUGHT IT, AND DID NOT EXIST.
 *
 * Every other test in this file feeds the detector an isolated kick, sometimes
 * with a tone beside it. So did the design system's own preview. And on that
 * material the design's specified test -- `env > 1.8 * mean(300 ms)` -- works
 * perfectly, which is why it shipped.
 *
 * On music it did not work at all. A kick over a loud sustained low end adds only
 * about a third to the level of the 20-80 Hz band, because the bass is already
 * filling that band; a third is 1.3x and 1.3 is not 1.8. Sixteen kicks in eight
 * seconds produced ONE onset on a limited mix, and one on a sustained bassline.
 * The background stayed perfectly still on exactly the music people make, and
 * every test passed.
 *
 * So this table is the real specification, and it is deliberately the least
 * clever test here: build something that sounds like a record, count the rings.
 */
mod material {
    use super::*;

    const BEAT: f64 = 0.5; // 120 BPM
    const BARS: f64 = 8.0; // seconds
    const KICKS: usize = (BARS / BEAT) as usize;

    /// A kick every `BEAT`: a decaying sine at `hz`.
    fn kicks(buf: &mut [f64], hz: f64, amp: f64, decay: f64) {
        let step = samples(BEAT);
        let mut start = 0;
        while start < buf.len() {
            let span = samples(decay * 6.0).min(buf.len() - start);
            for i in 0..span {
                let t = i as f64 / SR;
                buf[start + i] +=
                    amp * (-t / decay).exp() * (2.0 * core::f64::consts::PI * hz * t).sin();
            }
            start += step;
        }
    }

    /// A sustained tone, faded in over 50 ms.
    ///
    /// THE FADE MATTERS, for the reason `tone` gives above: a tone that starts
    /// abruptly at full scale is a step, and a step has energy in every band
    /// including this one. Without it the "no kick" row below fires once, and
    /// would be measuring the test's own setup.
    fn sustain(buf: &mut [f64], hz: f64, amp: f64) {
        let fade = samples(0.05) as f64;
        for (i, s) in buf.iter_mut().enumerate() {
            let a = (i as f64 / fade).min(1.0);
            *s += amp * a * (2.0 * core::f64::consts::PI * hz * (i as f64 / SR)).sin();
        }
    }

    /// Hard-clip, the way a loud master is.
    fn limit(buf: &mut [f64], ceil: f64) {
        for s in buf.iter_mut() {
            *s = s.clamp(-ceil, ceil);
        }
    }

    fn onsets(buf: &[f64]) -> usize {
        let mut d = Detector::new(SR);
        buf.iter().filter_map(|&s| d.next(s, s)).count()
    }

    fn bed() -> Vec<f64> {
        vec![0.0f64; samples(BARS)]
    }

    #[test]
    fn a_kick_on_its_own() {
        let mut b = bed();
        kicks(&mut b, 60.0, 1.0, 0.05);
        assert_eq!(onsets(&b), KICKS);
    }

    #[test]
    fn a_kick_under_a_quiet_bassline() {
        let mut b = bed();
        kicks(&mut b, 60.0, 1.0, 0.05);
        sustain(&mut b, 50.0, 0.1);
        assert_eq!(onsets(&b), KICKS);
    }

    #[test]
    fn a_kick_under_a_loud_sustained_bassline() {
        /* THE CASE THAT WAS BROKEN: 1 of 16 before the detector was re-keyed onto
         * the attack. One kick may still be lost while the bed is settling from
         * silence at the very start, which is why this is not an equality. */
        let mut b = bed();
        kicks(&mut b, 60.0, 1.0, 0.05);
        sustain(&mut b, 50.0, 0.7);
        let n = onsets(&b);
        assert!(
            n >= KICKS - 1,
            "a loud bassline hid the kick: {n} of {KICKS} rings"
        );
    }

    #[test]
    fn a_kick_under_a_pad_and_hats() {
        let mut b = bed();
        kicks(&mut b, 60.0, 1.0, 0.05);
        sustain(&mut b, 400.0, 0.5);
        sustain(&mut b, 4_000.0, 0.3);
        assert_eq!(onsets(&b), KICKS);
    }

    #[test]
    fn a_full_limited_mix() {
        /* THE OTHER CASE THAT WAS BROKEN: 2 of 16. This is what a master sounds
         * like to the detector, and it is the single most important row here. */
        let mut b = bed();
        kicks(&mut b, 60.0, 1.0, 0.05);
        sustain(&mut b, 50.0, 0.35);
        sustain(&mut b, 400.0, 0.4);
        sustain(&mut b, 4_000.0, 0.2);
        limit(&mut b, 0.9);
        assert_eq!(onsets(&b), KICKS, "a limited mix lost its kicks");
    }

    #[test]
    fn an_808_with_a_long_tail() {
        /* AND THE THIRD: 2 of 16. A sub kick that rings for a quarter of a second
         * is its own sustained bass, so it used to hide its own next hit. */
        let mut b = bed();
        kicks(&mut b, 40.0, 1.0, 0.25);
        assert_eq!(onsets(&b), KICKS);
    }

    #[test]
    fn a_quiet_kick_still_counts() {
        /* -20 dB and nothing else. The threshold is relative, so a quiet track
         * must behave like a loud one -- this is the row that stops the absolute
         * floor being raised to buy robustness somewhere else. */
        let mut b = bed();
        kicks(&mut b, 60.0, 0.1, 0.05);
        assert_eq!(onsets(&b), KICKS);
    }

    #[test]
    fn a_track_with_no_kick_stays_still() {
        /* COUNTED ONCE THE MUSIC IS PLAYING, not from the first sample, and the
         * distinction is real rather than convenient.
         *
         * A pad that appears out of silence has an amplitude ramp, and a ramp has
         * a spectrum: a fast one puts real energy into 20-80 Hz whatever pitch it
         * is playing. So the entry itself is a low-frequency transient and firing
         * on it is not wrong -- the same is true of a track starting, a clip
         * launching, or anyone un-muting a channel.
         *
         * What must not happen is the ground moving THROUGH material that has no
         * kick in it, which is what this measures. It is the same window the
         * sustained-tone test uses, for the same reason. */
        let mut b = bed();
        sustain(&mut b, 400.0, 0.5);
        sustain(&mut b, 4_000.0, 0.3);
        let mut d = Detector::new(SR);
        let late = b
            .iter()
            .enumerate()
            .filter(|(i, _)| {
                // every sample is fed; only the count is windowed
                let _ = i;
                true
            })
            .filter_map(|(i, &s)| d.next(s, s).map(|_| i))
            .filter(|i| *i > samples(0.5))
            .count();
        assert_eq!(late, 0, "the ground kept moving with no kick in the signal");
    }
}
