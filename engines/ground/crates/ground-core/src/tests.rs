/*!
What the ground must do, stated as a host's transport in and rings out.

EVERY TEST HERE PLAYS A HOST. A `Host` hands the clock one block at a time with
the position, tempo and meter a DAW would report, and the tests count what
rang and where -- because what the owner asked for is a behaviour ("one ring a
beat, stronger on the one, nothing while stopped"), and the arithmetic inside
`beat.rs` is only one way of getting it.
*/

use super::*;
use crate::beat::{MAX_PER_BLOCK, SLACK};

const SR: f64 = 48_000.0;
const BLOCK: usize = 512;

/// A DAW's transport, block by block.
struct Host {
    clock: BeatClock,
    ppq: f64,
    bpm: f64,
    num: i32,
    den: i32,
    playing: bool,
    block: usize,
    /// Every ring so far: the block it rang in and its strength. A block
    /// holding two contributes two entries with its final strength -- what the
    /// atomics publish.
    rang: Vec<(usize, f32)>,
}

impl Host {
    fn new(bpm: f64, num: i32, den: i32) -> Self {
        Host {
            clock: BeatClock::new(SR),
            ppq: 0.0,
            bpm,
            num,
            den,
            playing: true,
            block: 0,
            rang: Vec::new(),
        }
    }

    fn transport(&self) -> Transport {
        Transport { playing: self.playing, ppq: self.ppq, bpm: self.bpm, num: self.num, den: self.den }
    }

    /// One block of `frames`; the position advances only while playing.
    fn block_of(&mut self, frames: usize) -> Rings {
        let r = self.clock.tick(&self.transport(), frames);
        for _ in 0..r.count {
            self.rang.push((self.block, r.strength));
        }
        if self.playing {
            self.ppq += frames as f64 * self.bpm / (60.0 * SR);
        }
        self.block += 1;
        r
    }

    fn run(&mut self, blocks: usize) {
        for _ in 0..blocks {
            self.block_of(BLOCK);
        }
    }

    /// Play every whole block that ends by `ppq`: the beats in `[start, ppq)`
    /// for any `ppq` not within a block of a beat.
    fn play_to(&mut self, ppq: f64) {
        while self.ppq + BLOCK as f64 * self.bpm / (60.0 * SR) <= ppq {
            self.block_of(BLOCK);
        }
    }

    fn strengths(&self) -> Vec<f32> {
        self.rang.iter().map(|r| r.1).collect()
    }
}

const S: f32 = DOWNBEAT_STRENGTH;
const W: f32 = BEAT_STRENGTH;

/* ---------- the rule ---------- */

#[test]
fn four_four_rings_every_quarter_and_the_one_strongest() {
    let mut h = Host::new(120.0, 4, 4);
    h.play_to(16.0);
    assert_eq!(
        h.strengths(),
        [S, W, W, W, S, W, W, W, S, W, W, W, S, W, W, W],
        "four bars of 4/4 from bar 1"
    );
}

#[test]
fn each_ring_lands_in_the_block_that_holds_its_beat() {
    /* At 120 BPM and 48 kHz a beat is 24 000 samples: beat k is in block
     * floor(24 000 k / 512). A ring a block late is a ring out of time. */
    let mut h = Host::new(120.0, 4, 4);
    h.play_to(8.0);
    let blocks: Vec<usize> = h.rang.iter().map(|r| r.0).collect();
    let want: Vec<usize> = (0..8).map(|k| k * 24_000 / BLOCK).collect();
    assert_eq!(blocks, want);
}

#[test]
fn a_ring_is_published_as_a_count_and_the_last_strength() {
    let g = live();
    tick(&g, 0.0, 120.0, 4, 4, true, BLOCK);
    assert_eq!((g.fires(), g.strength()), (1, S), "the downbeat");
    tick(&g, 1.0, 120.0, 4, 4, true, BLOCK);
    assert_eq!((g.fires(), g.strength()), (2, W), "a beat");
}

#[test]
fn the_strengths_are_the_documented_pair() {
    /* docs/tech/ground.md and ground.h state these numbers; the field's
     * calibration (ui-kit/test/field.test.mjs) assumes them. */
    assert_eq!(DOWNBEAT_STRENGTH, 1.0);
    assert_eq!(BEAT_STRENGTH, 0.4);
}

/* ---------- starting ---------- */

#[test]
fn a_start_exactly_on_a_beat_rings_once_not_twice() {
    let mut h = Host::new(120.0, 4, 4);
    h.ppq = 4.0;
    h.run(4);
    assert_eq!(h.rang, [(0, S)]);
}

#[test]
fn a_start_reported_a_hair_before_the_beat_is_on_it() {
    /* A host's position is a double that went through a tempo map. */
    let mut h = Host::new(120.0, 4, 4);
    h.ppq = 4.0 - 1e-9;
    h.run(4);
    assert_eq!(h.rang, [(0, S)]);
}

#[test]
fn a_host_that_holds_the_start_position_for_a_few_blocks_rings_once() {
    /* Some hosts report the same position for the first block or two after
     * pressing play. That is not a loop back to the start. */
    let mut c = BeatClock::new(SR);
    let at0 = Transport { playing: true, ppq: 0.0, bpm: 120.0, num: 4, den: 4 };
    let n: u32 = (0..4).map(|_| c.tick(&at0, BLOCK).count).sum();
    assert_eq!(n, 1);
}

#[test]
fn a_start_between_beats_waits_for_the_next() {
    let mut h = Host::new(120.0, 4, 4);
    h.ppq = 4.3;
    h.play_to(6.0);
    assert_eq!(h.strengths(), [W], "beat 5, and nothing for the 4.3 we started on");
}

/* ---------- jumps ---------- */

#[test]
fn a_loop_back_rings_its_start_once_and_never_the_beats_it_skipped() {
    /* A two-bar loop, 0..8, three times round. A loop end that falls inside a
     * block rings the next bar's downbeat there; the wrapped block that
     * follows does not ring it again. */
    let mut h = Host::new(120.0, 4, 4);
    for _ in 0..3 {
        h.play_to(8.0);
        h.ppq -= 8.0;
    }
    let s = h.strengths();
    assert_eq!(s.len(), 24, "eight rings a time round: {s:?}");
    for (i, x) in s.iter().enumerate() {
        assert_eq!(*x, if i % 4 == 0 { S } else { W }, "ring {i} of {s:?}");
    }
}

#[test]
fn a_jump_back_onto_a_beat_rings_it_and_off_one_rings_nothing() {
    let mut h = Host::new(120.0, 4, 4);
    h.play_to(7.9);
    let before = h.rang.len();
    h.ppq = 4.0;
    h.block_of(BLOCK);
    assert_eq!(h.rang.len() - before, 1, "back to bar 2's downbeat");
    assert_eq!(h.rang.last().unwrap().1, S);

    h.play_to(7.9);
    let before = h.rang.len();
    h.ppq = 4.5;
    h.block_of(BLOCK);
    assert_eq!(h.rang.len(), before, "back to between beats");
}

#[test]
fn a_seek_forward_rings_at_most_one_and_never_a_burst() {
    let mut h = Host::new(120.0, 4, 4);
    h.play_to(2.2);
    let before = h.rang.len();
    h.ppq = 50.0;
    h.block_of(BLOCK);
    assert_eq!(h.rang.len() - before, 1, "47 skipped beats, one ring: the one it landed on");
    assert_eq!(h.rang.last().unwrap().1, W, "50 is beat 3 of bar 13");

    let before = h.rang.len();
    h.ppq = 120.3;
    h.block_of(BLOCK);
    assert_eq!(h.rang.len(), before, "landing between beats rings nothing");
    h.play_to(121.5);
    assert_eq!(h.rang.len() - before, 1, "and the next beat rings in time");
}

#[test]
fn slack_is_the_line_between_rounding_and_a_seek() {
    /* A block ending at 0.99, and the next starting a little past beat 1.
     * Within SLACK that is the same playback, and beat 1 rings (once); past
     * it, it is a seek, and the beat it skipped stays skipped. */
    let at = |ppq| Transport { playing: true, ppq, bpm: 120.0, num: 4, den: 4 };
    let span = BLOCK as f64 * 2.0 / SR;
    for (past, rings) in [(0.5, 1), (1.5, 0)] {
        let mut c = BeatClock::new(SR);
        assert_eq!(c.tick(&at(0.99 - span), BLOCK).count, 0);
        assert_eq!(c.tick(&at(0.99 + SLACK * past), BLOCK).count, rings, "{past} x SLACK");
    }
}

#[test]
fn rounding_either_side_of_a_block_boundary_rings_each_beat_once() {
    /* A host whose positions wobble by 1e-9 around the exact values -- and
     * one whose blocks overlap or leave a hair of gap -- still rings exactly
     * once a beat. */
    for wobble in [1e-9, -1e-9, 1e-7, -1e-7] {
        let mut c = BeatClock::new(SR);
        let span = BLOCK as f64 * 2.0 / SR;
        let mut n = 0;
        for k in 0..2_000usize {
            let jitter = if k % 2 == 0 { wobble } else { -wobble };
            let ppq = k as f64 * span + jitter;
            n += c.tick(&Transport { playing: true, ppq, bpm: 120.0, num: 4, den: 4 }, BLOCK).count;
        }
        let beats = (2_000.0 * span).ceil() as u32;
        assert_eq!(n, beats, "wobble {wobble}");
    }
}

/* ---------- tempo ---------- */

#[test]
fn a_tempo_change_keeps_one_ring_a_beat() {
    let mut h = Host::new(120.0, 4, 4);
    h.play_to(6.0);
    h.bpm = 87.0;
    h.play_to(12.0);
    h.bpm = 174.0;
    h.play_to(20.0);
    assert_eq!(h.rang.len(), 20);
    assert_eq!(h.strengths().iter().filter(|s| **s == S).count(), 5);
}

#[test]
fn a_tempo_ramp_inside_a_block_is_not_a_jump() {
    /* The clock projects a block's end at the block's starting tempo; a host
     * ramping tempo reports the next start a little off that projection. It
     * is still the same playback, so no beat rings twice or goes missing. */
    let mut c = BeatClock::new(SR);
    let (mut ppq, mut bpm, mut n) = (0.0, 60.0, 0u32);
    while ppq < 63.5 {
        n += c.tick(&Transport { playing: true, ppq, bpm, num: 4, den: 4 }, BLOCK).count;
        let next = bpm * 1.02;
        /* The true advance at the mean tempo of the block. */
        ppq += BLOCK as f64 * 0.5 * (bpm + next) / (60.0 * SR);
        bpm = next.min(240.0);
    }
    assert_eq!(n, 64, "beats 0..=63");
}

#[test]
fn a_very_slow_tempo_rings_every_beat() {
    let mut h = Host::new(20.0, 4, 4);
    h.play_to(8.0);
    assert_eq!(h.strengths(), [S, W, W, W, S, W, W, W]);
    let gaps: Vec<usize> = h.rang.windows(2).map(|w| w[1].0 - w[0].0).collect();
    /* Three seconds a beat: 281 blocks of 512 at 48 kHz, give or take one. */
    assert!(gaps.iter().all(|g| (280..=282).contains(g)), "{gaps:?}");
}

#[test]
fn a_very_fast_tempo_rings_every_beat() {
    let mut h = Host::new(999.0, 4, 4);
    h.play_to(64.0);
    assert_eq!(h.rang.len(), 64);
    assert_eq!(h.strengths().iter().filter(|s| **s == S).count(), 16);
}

#[test]
fn a_block_holding_two_beats_rings_both() {
    let mut c = BeatClock::new(SR);
    /* Two quarters' worth of frames at 120 BPM: a second. Positions 3.5..5.5
     * hold the downbeat 4 and beat 5. */
    let r = c.tick(&Transport { playing: true, ppq: 3.5, bpm: 120.0, num: 4, den: 4 }, 48_000);
    assert_eq!(r.count, 2);
    assert_eq!(r.strength, W, "the strength published is the later ring's");

    let g = live();
    tick(&g, 3.5, 120.0, 4, 4, true, 48_000);
    assert_eq!(g.fires(), 2, "the count carries both");
}

#[test]
fn a_nonsensical_host_cannot_make_a_block_ring_without_bound() {
    let mut c = BeatClock::new(SR);
    /* A 1/64 bar is a downbeat every sixteenth of a quarter. */
    let r = c.tick(&Transport { playing: true, ppq: 0.0, bpm: 120.0, num: 1, den: 64 }, 96_000);
    assert_eq!(r.count, MAX_PER_BLOCK);
    let r = c.tick(&Transport { playing: true, ppq: 0.0, bpm: 1e12, num: 4, den: 4 }, 4096);
    assert_eq!(r.count, MAX_PER_BLOCK);
}

/* ---------- meters ---------- */

#[test]
fn six_eight_is_a_downbeat_every_three_quarters() {
    let mut h = Host::new(120.0, 6, 8);
    h.play_to(12.0);
    assert_eq!(h.strengths(), [S, W, W, S, W, W, S, W, W, S, W, W]);
}

#[test]
fn seven_eight_puts_every_other_downbeat_between_two_quarters() {
    /* 7/8 is three and a half quarters: rings at 0 1 2 3, the downbeat at
     * 3.5, 4 5 6, then the downbeat at 7 -- which is on a quarter, so one. */
    let mut h = Host::new(120.0, 7, 8);
    h.play_to(7.5);
    assert_eq!(h.strengths(), [S, W, W, W, S, W, W, W, S]);
    /* The 3.5 downbeat rings half a beat after beat 3, in its own block. */
    assert_eq!(h.rang[3].0, 72_000 / BLOCK);
    assert_eq!(h.rang[4].0, 84_000 / BLOCK);
}

#[test]
fn three_four_and_five_four() {
    let mut h = Host::new(140.0, 3, 4);
    h.play_to(9.0);
    assert_eq!(h.strengths(), [S, W, W, S, W, W, S, W, W]);
    let mut h = Host::new(140.0, 5, 4);
    h.play_to(10.0);
    assert_eq!(h.strengths(), [S, W, W, W, W, S, W, W, W, W]);
}

#[test]
fn a_host_that_reports_no_time_signature_is_four_four() {
    /* iPlug2's AU wrapper reports 0/0 when the host has no musical-time
     * callback; others leave its 4/4 default. Either way: 4/4. */
    for (num, den) in [(0, 0), (3, 0), (0, 8), (-3, 4), (4, -4)] {
        let mut h = Host::new(120.0, num, den);
        h.play_to(8.0);
        assert_eq!(h.strengths(), [S, W, W, W, S, W, W, W], "{num}/{den}");
    }
    assert_eq!(bar_quarters(0, 0), 4.0);
    assert_eq!(bar_quarters(6, 8), 3.0);
    assert_eq!(bar_quarters(7, 8), 3.5);
}

/* ---------- stopping ---------- */

#[test]
fn a_stopped_transport_never_rings() {
    let mut h = Host::new(120.0, 4, 4);
    h.playing = false;
    h.run(1_000);
    assert!(h.rang.is_empty());
}

#[test]
fn stopping_stops_the_rings_and_restarting_starts_fresh() {
    let mut h = Host::new(120.0, 4, 4);
    h.play_to(2.1);
    assert_eq!(h.rang.len(), 3);
    h.playing = false;
    h.run(500);
    assert_eq!(h.rang.len(), 3, "rings stop with the transport");

    /* Restart where it stopped, mid-beat: nothing until beat 3. */
    h.playing = true;
    h.play_to(2.9);
    assert_eq!(h.rang.len(), 3);
    h.play_to(3.1);
    assert_eq!(h.rang.len(), 4);

    /* Stop, return to zero, play: the downbeat rings at once. */
    h.playing = false;
    h.block_of(BLOCK);
    h.ppq = 0.0;
    h.playing = true;
    h.block_of(BLOCK);
    assert_eq!(h.rang.last(), Some(&(h.block - 1, S)));
}

#[test]
fn a_host_with_no_tempo_is_taken_at_120() {
    for bpm in [0.0, -10.0, f64::NAN, f64::INFINITY] {
        let mut h = Host::new(120.0, 4, 4);
        let mut c = BeatClock::new(SR);
        let mut n = 0;
        /* 120 BPM's worth of position, reported with a useless tempo. */
        for _ in 0..200 {
            n += c.tick(&Transport { playing: true, ppq: h.ppq, bpm, num: 4, den: 4 }, BLOCK).count;
            h.ppq += BLOCK as f64 * 2.0 / SR;
        }
        assert_eq!(n, h.ppq.ceil() as u32, "bpm {bpm}");
    }
}

#[test]
fn an_unusable_position_or_rate_rings_nothing() {
    let play = |ppq| Transport { playing: true, ppq, bpm: 120.0, num: 4, den: 4 };
    let mut c = BeatClock::new(SR);
    assert_eq!(c.tick(&play(f64::NAN), BLOCK).count, 0);
    assert_eq!(c.tick(&play(f64::INFINITY), BLOCK).count, 0);
    for sr in [0.0, -1.0, f64::NAN] {
        let mut c = BeatClock::new(sr);
        assert_eq!(c.tick(&play(0.0), BLOCK).count, 0, "rate {sr}");
    }
    let mut c = BeatClock::new(SR);
    assert_eq!(c.tick(&play(0.0), 0).count, 0, "an empty block");
    assert_eq!(c.tick(&play(0.0), BLOCK).count, 1, "and it did not count as the start");
}

#[test]
fn a_negative_position_counts_in() {
    /* A count-in or pre-roll reports positions before zero. Bars count back
     * from zero the same way: -4 is a downbeat. */
    let mut h = Host::new(120.0, 4, 4);
    h.ppq = -4.0;
    h.play_to(1.0);
    assert_eq!(h.strengths(), [S, W, W, W, S]);
}

/* ---------- the published pair, and who may touch it ---------- */

/// A `Ground` switched on, as a plugin's is while its editor is open.
fn live() -> Ground {
    let g = Ground::new(SR);
    g.set_active(true);
    g
}

/// `tick` from the one thread these tests run on, which trivially satisfies
/// its single-producer contract.
fn tick(g: &Ground, ppq: f64, bpm: f64, num: i32, den: i32, playing: bool, frames: usize) {
    unsafe { g.tick(&Transport { playing, ppq, bpm, num, den }, frames) }
}

#[test]
fn a_new_ground_is_inactive_and_ignores_the_transport() {
    let g = Ground::new(SR);
    assert!(!g.is_active());
    tick(&g, 0.0, 120.0, 4, 4, true, BLOCK);
    assert_eq!((g.fires(), g.strength()), (0, 0.0));
}

#[test]
fn deactivating_stops_the_rings() {
    let g = live();
    tick(&g, 0.0, 120.0, 4, 4, true, BLOCK);
    assert_eq!(g.fires(), 1);
    g.set_active(false);
    assert!(!g.is_active());
    tick(&g, 1.0, 120.0, 4, 4, true, BLOCK);
    assert_eq!(g.fires(), 1, "a closed editor's ground must not ring");
}

#[test]
fn reopening_mid_song_rings_no_backlog() {
    /* Closed at beat 1, reopened at beat 40.5: nothing for the 39 beats in
     * between, and the next beat rings. */
    let g = live();
    tick(&g, 0.0, 120.0, 4, 4, true, BLOCK);
    g.set_active(false);
    g.set_active(true);
    tick(&g, 40.5, 120.0, 4, 4, true, BLOCK);
    assert_eq!(g.fires(), 1);
    tick(&g, 40.99, 120.0, 4, 4, true, BLOCK);
    assert_eq!(g.fires(), 2);
}

#[test]
fn reset_does_not_rewind_the_count_and_makes_the_next_block_a_start() {
    let g = live();
    tick(&g, 0.0, 120.0, 4, 4, true, BLOCK);
    let span = BLOCK as f64 * 2.0 / SR;
    tick(&g, span, 120.0, 4, 4, true, BLOCK);
    assert_eq!(g.fires(), 1);
    g.reset();
    assert_eq!(g.fires(), 1, "a reset never moves the count");
    /* The same position again, after a reset, is a start: had the clock kept
     * its memory this would be a stall and ring nothing. */
    tick(&g, 0.0, 120.0, 4, 4, true, BLOCK);
    assert_eq!(g.fires(), 2);
}

#[test]
fn a_sample_rate_change_is_applied_by_the_next_tick() {
    let g = live();
    g.set_sample_rate(96_000.0);
    assert_eq!(g.fires(), 0);
    /* At 96 kHz a 48 000-frame block is a quarter at 120 BPM; at 48 kHz it
     * would have been two. */
    tick(&g, 0.5, 120.0, 4, 4, true, 48_000);
    assert_eq!(g.fires(), 1);
}

#[test]
fn the_count_wraps_rather_than_saturating() {
    let g = live();
    g.fires.store(u32::MAX, core::sync::atomic::Ordering::Relaxed);
    tick(&g, 0.0, 120.0, 4, 4, true, BLOCK);
    assert_eq!(g.fires(), 0, "a reader compares with != and sees it move");
}
