// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The beat clock behind the animated ground: the host's transport in, rings out.

THE RULE, which is the owner's and is the whole of what this file decides:

  - one ring on every quarter note, while the transport plays;
  - a STRONG ring on each bar's downbeat, the bar being `num * 4 / den`
    quarters long -- 4/4 when the host reports no time signature;
  - nothing at all while the transport is stopped.

It reads nothing but the host's clock, so every plugin rings identically --
on a drum bus or on a silent track. It replaced a 20-80 Hz onset detector that
listened to each plugin's own input (docs/tech/ground.md says why).

POSITIONS ARE QUARTERS, which is what a host's "PPQ position" is, and bars are
counted from position 0 -- what every host's ruler does for a song with one
time signature. A bar that is not a whole number of quarters (7/8 is three and
a half) puts its downbeat between two quarters, and it rings there: 7/8 is
quarters at 0 1 2 3, a downbeat at 3.5, quarters at 4 5 6, a downbeat at 7.
A downbeat that falls ON a quarter is one ring, the strong one.

WHAT A BLOCK RINGS. A block covers the quarter positions `[p0, p1)`, where p0
is the host's position at the block's first sample and p1 is p0 advanced by
the block at the host's tempo. A ring belongs to the block whose range holds
it: start-inclusive, end-exclusive, so a beat on a block boundary is the next
block's and nobody's twice. Both ends are moved EPS earlier, together, so a
boundary that rounding put a hair past a beat -- p1 = 8.0000000001 -- still
leaves that beat to the next block, which is where a host that loops at 8
puts it (at 0).

CONTINUITY IS DECIDED, NOT ASSUMED. Hosts report p0 per block; consecutive
blocks of one playback agree with each other to rounding, and every exception
-- a start, a loop, a seek, a scrub -- is a JUMP. So each block is either:

  continuous   the transport was rolling last block, and p0 is no further
               back than last block's start and no further on than where last
               block ended, both give or take SLACK. The range starts where the
               last one ended, NOT at p0: a beat that rounding put a hair either
               side of the boundary is then rung once, by exactly one block.
               A host whose position stalls for a block or two (some do at
               transport start) is continuous too, and rings nothing new.
  a jump       anything else. The range starts at p0, so a start reported
               as 3.9999999999 is still ON beat 4. Nothing between the old
               position and the new one is rung -- a seek over twenty bars is
               not twenty bars of rings -- so a jump rings at most what its
               own block holds.

SLACK is a sixty-fourth note. It is far wider than any host's rounding and
than the drift a tempo ramp causes inside one block (the end is projected at
the block's starting tempo), and narrower than the gap between two rings in
any meter with a denominator up to 32 -- so a jump small enough to pass for
continuity can still never ring more than one beat it skipped.

STRENGTHS. 1.0 for a downbeat, BEAT_STRENGTH for every other beat. The field
(ui-kit/src/lib/field.js) is linear in strength, so 0.4 is a ring two fifths as
tall: clearly there, clearly the lesser. 0.55, the first candidate, was measured
in the field at 120 BPM and read too close to the downbeat, because at that
tempo each ring is still swelling when the next one starts; docs/tech/ground.md
has the numbers.

AUDIO-THREAD SAFE. `tick` is arithmetic on a few f64s: no allocation, no lock,
no system call, and a bounded loop (MAX_PER_BLOCK) whatever the host reports.
*/

/// A downbeat's ring: the field's full source strength.
pub const DOWNBEAT_STRENGTH: f32 = 1.0;
/// Every other beat's ring.
pub const BEAT_STRENGTH: f32 = 0.4;

/// The tempo a host that reports none (or nonsense) is taken to be at --
/// the same default `ni::wire::host_transport` gives the engines.
pub const DEFAULT_BPM: f64 = 120.0;

/// How far a block's start may sit from where the last block ended, in
/// quarters, and still be the same playback. A sixty-fourth note.
pub(crate) const SLACK: f64 = 1.0 / 16.0;
/// A position this close before a boundary is on it, in quarters.
pub(crate) const EPS: f64 = 1e-6;
/// The most rings one block can produce. At 999 BPM a 4096-frame block at
/// 44.1 kHz holds 1.5 quarters; this only bounds a nonsensical host.
pub(crate) const MAX_PER_BLOCK: u32 = 16;

/// The host's clock for one block, as iPlug2 reads it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Transport {
    /// Whether the transport is playing.
    pub playing: bool,
    /// The position at the block's first sample, in quarter notes.
    pub ppq: f64,
    /// Quarter notes per minute.
    pub bpm: f64,
    /// Time signature. Either not positive is "the host gave none": 4/4.
    pub num: i32,
    pub den: i32,
}

/// What one block rang: how many, and the strength of the last.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rings {
    pub count: u32,
    pub strength: f32,
}

/// The bar's length in quarters: `num * 4 / den`, or 4 when the host reported
/// no time signature.
pub fn bar_quarters(num: i32, den: i32) -> f64 {
    if num <= 0 || den <= 0 {
        4.0
    } else {
        f64::from(num) * 4.0 / f64::from(den)
    }
}

/// The clock. Plain single-threaded state: `Ground` gives it to the audio
/// thread alone.
#[derive(Clone, Debug)]
pub struct BeatClock {
    sample_rate: f64,
    /// The last block was playing; `start` and `end` describe it.
    rolling: bool,
    start: f64,
    end: f64,
}

impl BeatClock {
    pub fn new(sample_rate: f64) -> Self {
        BeatClock { sample_rate, rolling: false, start: 0.0, end: 0.0 }
    }

    /// A new rate, and the next block is a fresh start.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        self.sample_rate = sample_rate;
        self.reset();
    }

    /// Forget the last block: the next playing block is a start, which rings
    /// only what it holds.
    pub fn reset(&mut self) {
        self.rolling = false;
    }

    /// One block of `frames` samples at the host's clock `t`.
    pub fn tick(&mut self, t: &Transport, frames: usize) -> Rings {
        let rate_ok = self.sample_rate.is_finite() && self.sample_rate > 0.0;
        if !t.playing || !t.ppq.is_finite() || !rate_ok {
            self.rolling = false;
            return Rings::default();
        }
        if frames == 0 {
            return Rings::default();
        }
        let bpm = if t.bpm.is_finite() && t.bpm > 0.0 { t.bpm } else { DEFAULT_BPM };
        let p0 = t.ppq;
        let p1 = p0 + frames as f64 * bpm / (60.0 * self.sample_rate);

        let continuous =
            self.rolling && p0 >= self.start - SLACK && p0 <= self.end + SLACK;
        let lo = if continuous { self.end } else { p0 };
        let rings = ring(lo - EPS, p1 - EPS, bar_quarters(t.num, t.den));

        self.rolling = true;
        self.start = p0;
        self.end = if continuous { self.end.max(p1) } else { p1 };
        rings
    }
}

/// The rings in `[lo, hi)`: every quarter, and every bar start, a bar start on
/// a quarter being one ring.
fn ring(lo: f64, hi: f64, bar: f64) -> Rings {
    let mut out = Rings::default();
    if !(hi > lo) {
        return out;
    }
    let mut quarter = lo.ceil();
    let mut bar_index = (lo / bar).ceil();
    /* (lo / bar).ceil() * bar can round to just under lo; that bar was the
     * previous range's. */
    if bar_index * bar < lo {
        bar_index += 1.0;
    }
    while out.count < MAX_PER_BLOCK {
        let downbeat = bar_index * bar;
        if quarter.min(downbeat) >= hi {
            break;
        }
        if (downbeat - quarter).abs() <= EPS {
            quarter += 1.0;
            bar_index += 1.0;
            out.strength = DOWNBEAT_STRENGTH;
        } else if downbeat < quarter {
            bar_index += 1.0;
            out.strength = DOWNBEAT_STRENGTH;
        } else {
            quarter += 1.0;
            out.strength = BEAT_STRENGTH;
        }
        out.count += 1;
    }
    out
}
