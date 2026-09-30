//! The detector's edge logic.

use crate::follower::Follower;
use crate::params::db_to_amp;
use crate::tests::hit;

/* --------------------------------------------------------------- follower */


fn count_fires(key: &[f32], threshold: f64, lockout_samples: f64, sr: f64) -> usize {
    let mut f = Follower::new(sr);
    key.iter()
        .filter(|s| f.next(**s, **s, threshold, lockout_samples))
        .count()
}

#[test]
fn one_hit_is_one_trigger() {
    /* THE FAULT THIS PINS: a bare `|x| > threshold` fires on every sample the
     * waveform spends above the line -- dozens per hit. */
    let sr = 48000.0;
    let mut key = Vec::new();
    for _ in 0..4 {
        hit(&mut key, 0.9, 480, 9600);
    }
    assert_eq!(count_fires(&key, db_to_amp(-24.0), 0.0, sr), 4);
}

#[test]
fn the_lockout_absorbs_a_second_hit_inside_its_window() {
    /* THE DIVISION OF LABOUR THIS PINS. Two hits 100 ms apart are two humps as
     * far as the detector is concerned -- that is what the 25 ms fall buys --
     * so whether they are one trigger or two is the Lockout's decision and
     * nothing else's. A longer fall would answer it first and silently. */
    let sr = 48000.0;
    let mut key = Vec::new();
    hit(&mut key, 0.9, 240, 4560); /* 100 ms between the two onsets */
    hit(&mut key, 0.9, 240, 9600);
    let th = db_to_amp(-24.0);
    assert_eq!(count_fires(&key, th, 0.0, sr), 2, "no lockout: both fire");
    /* 150 ms of lockout is 7200 samples, which spans the second onset. */
    assert_eq!(count_fires(&key, th, 7200.0, sr), 1, "lockout should absorb it");
}

#[test]
fn the_detector_recovers_between_hits_on_its_own() {
    /* The guard on FALL_MS: if the fall grows past one waveform cycle it starts
     * merging hits the user asked to keep apart, and the Lockout stops working.
     * At 100 ms of separation the detector must come back under the line by
     * itself. */
    let sr = 48000.0;
    let mut key = Vec::new();
    for _ in 0..6 {
        hit(&mut key, 0.9, 240, 4560);
    }
    assert_eq!(count_fires(&key, db_to_amp(-24.0), 0.0, sr), 6);
}

#[test]
fn the_threshold_is_a_threshold() {
    let sr = 48000.0;
    let mut quiet = Vec::new();
    hit(&mut quiet, 0.02, 480, 4800); /* about -34 dB peak */
    assert_eq!(count_fires(&quiet, db_to_amp(-24.0), 0.0, sr), 0);
    assert_eq!(count_fires(&quiet, db_to_amp(-50.0), 0.0, sr), 1);
}

#[test]
fn the_detector_reads_the_louder_channel_not_their_mean() {
    /* A kick panned hard left halves in a sum and can fall under a threshold
     * set while it was centred -- the threshold would be reading the panner. */
    let mut f = Follower::new(48000.0);
    let th = db_to_amp(-12.0);
    let mut fired = false;
    for _ in 0..480 {
        if f.next(0.9, 0.0, th, 0.0) {
            fired = true;
        }
    }
    assert!(fired, "a hard-panned hit must still trigger");
}

#[test]
fn a_nan_in_the_key_does_not_latch_the_detector() {
    /* A NaN env compares false against everything, so `above` would latch and
     * the detector would never fire again for the life of the session. */
    let mut f = Follower::new(48000.0);
    let th = db_to_amp(-24.0);
    for _ in 0..64 {
        f.next(f32::NAN, f32::INFINITY, th, 0.0);
    }
    let mut fired = false;
    for _ in 0..480 {
        if f.next(0.9, 0.9, th, 0.0) {
            fired = true;
        }
    }
    assert!(fired, "the detector is stuck after a NaN");
}
