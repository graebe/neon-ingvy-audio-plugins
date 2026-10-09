// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The mip-mapped wavetable: every frame of the table is stored once per level,
each copy band-limited by zeroing its spectrum above a harmonic count that
halves every `per_octave` levels. Playback picks the level whose highest
harmonic stays under a ceiling, interpolates linearly within a frame, and
crossfades between neighbouring frames (the morph, "wavetable position").

Two decisions, and section 4 of the paper measures both. The ceiling: `fs / 2`
is strict, and nothing folds; `fs - 20 kHz` lets harmonics fold, but only to
where nobody hears them. The spacing: with one level per octave, a note just
above a level's boundary loses up to an octave of its top end, and no ceiling
short of twice the strict one wins it back, since the next level has twice the
harmonics. Three levels per octave lose at most a third, for three times the
memory.

All the work is in `new`, which allocates and runs the FFTs. Reading
allocates nothing.
*/

use realfft::num_complex::Complex;
use realfft::RealFftPlanner;

use super::{Osc, Phasor};

/// Frames as stored: `levels` copies, each `frames x (size + 1)` samples
/// (the extra sample repeats the first, so interpolation never wraps).
pub struct Wavetable {
    size: usize,
    frames: usize,
    per_octave: usize,
    levels: usize,
    data: Vec<f32>,
}

// ANCHOR: mip
impl Wavetable {
    /// The highest harmonic level `l` keeps: half the frame, halved every
    /// `per_octave` levels.
    pub fn harmonics(&self, level: usize) -> usize {
        let octaves = level as f64 / self.per_octave as f64;
        (((self.size / 2) as f64 * (-octaves).exp2()).floor() as usize).max(1)
    }

    /// The first (brightest) level whose top harmonic stays under `ceiling` Hz.
    pub fn level_for(&self, f0: f32, ceiling: f32) -> usize {
        let fit = (ceiling / f0.max(1e-3)).floor() as usize; // harmonics that fit
        (0..self.levels)
            .find(|&l| self.harmonics(l) <= fit)
            .unwrap_or(self.levels - 1)
    }
}
// ANCHOR_END: mip

impl Wavetable {
    /// Builds every level of every frame. Each frame is one cycle, all the
    /// same power-of-two length.
    pub fn new(frames: &[Vec<f32>], per_octave: usize) -> Self {
        let size = frames[0].len();
        assert!(size.is_power_of_two() && size >= 8, "a frame is a power of two");
        assert!(frames.iter().all(|f| f.len() == size), "frames differ in length");
        let per_octave = per_octave.max(1);
        // From half the frame down to a single sine.
        let levels = per_octave * (size / 2).trailing_zeros() as usize + 1;
        let stride = size + 1;
        let mut table =
            Self { size, frames: frames.len(), per_octave, levels, data: vec![0.0; levels * frames.len() * stride] };

        let mut planner = RealFftPlanner::<f64>::new();
        let fwd = planner.plan_fft_forward(size);
        let inv = planner.plan_fft_inverse(size);
        let mut spectrum = fwd.make_output_vec();
        let mut band = spectrum.clone();
        let mut buf = vec![0.0f64; size];

        for (fi, frame) in frames.iter().enumerate() {
            buf.iter_mut().zip(frame).for_each(|(b, &x)| *b = x as f64);
            fwd.process(&mut buf, &mut spectrum).expect("sizes are the plan's");
            for l in 0..levels {
                let keep = table.harmonics(l);
                for (k, (b, s)) in band.iter_mut().zip(&spectrum).enumerate() {
                    /* No DC, no Nyquist bin (its phase is ambiguous), nothing
                     * above the level's harmonic count. */
                    let pass = k >= 1 && k <= keep && k < size / 2;
                    *b = if pass { *s } else { Complex::new(0.0, 0.0) };
                }
                inv.process(&mut band, &mut buf).expect("sizes are the plan's");
                let at = (l * table.frames + fi) * stride;
                let out = &mut table.data[at..at + stride];
                for (o, &x) in out.iter_mut().zip(&buf) {
                    *o = (x / size as f64) as f32;
                }
                out[size] = out[0];
            }
        }
        table
    }

    pub fn size(&self) -> usize {
        self.size
    }

    pub fn levels(&self) -> usize {
        self.levels
    }

    pub fn frames(&self) -> usize {
        self.frames
    }

    fn frame(&self, level: usize, frame: usize) -> &[f32] {
        let stride = self.size + 1;
        let at = (level * self.frames + frame) * stride;
        &self.data[at..at + stride]
    }

    // ANCHOR: read
    /// One sample at `phase` in [0, 1) and morph `position` in [0, 1].
    #[inline]
    pub fn read(&self, level: usize, position: f32, phase: f32) -> f32 {
        let lerp = |a: f32, b: f32, t: f32| a + (b - a) * t;
        let sample = |frame: &[f32]| {
            let x = phase * self.size as f32;
            let i = x as usize; // phase < 1, so i < size, and i + 1 is the guard at worst
            lerp(frame[i], frame[i + 1], x - i as f32)
        };
        if self.frames == 1 {
            return sample(self.frame(level, 0));
        }
        let m = position.clamp(0.0, 1.0) * (self.frames - 1) as f32;
        let f = (m as usize).min(self.frames - 2); // position 1 is the last pair's end
        lerp(sample(self.frame(level, f)), sample(self.frame(level, f + 1)), m - f as f32)
    }
    // ANCHOR_END: read
}

// ANCHOR: saw_frame
/// One cycle of the sawtooth `2t - 1`, summed from its harmonics rather than
/// sampled: a sampled ramp is already aliased, since its corner has every
/// harmonic and the frame keeps only `size / 2` of them.
pub fn saw_frame(size: usize) -> Vec<f32> {
    let n = size as f64;
    (0..size)
        .map(|i| {
            let t = 2.0 * core::f64::consts::PI * i as f64 / n;
            let sum: f64 = (1..size / 2).map(|h| (h as f64 * t).sin() / h as f64).sum();
            (-2.0 / core::f64::consts::PI * sum) as f32
        })
        .collect()
}
// ANCHOR_END: saw_frame

/// A wavetable oscillator over a shared table.
pub struct WavetableOsc<'a> {
    table: &'a Wavetable,
    phasor: Phasor,
    level: usize,
    /// The highest harmonic frequency allowed, in Hz. See the module notes.
    pub ceiling: f32,
    pub position: f32,
}

impl<'a> WavetableOsc<'a> {
    pub fn new(table: &'a Wavetable, f0: f32, fs: f32, ceiling: f32) -> Self {
        let mut o = Self { table, phasor: Phasor::new(f0, fs), level: 0, ceiling, position: 0.0 };
        o.set_freq(f0, fs);
        o
    }

    pub fn level(&self) -> usize {
        self.level
    }
}

impl Osc for WavetableOsc<'_> {
    #[inline]
    fn next(&mut self) -> f32 {
        let p = self.phasor.tick();
        self.table.read(self.level, self.position, p)
    }

    fn set_freq(&mut self, f0: f32, fs: f32) {
        self.phasor.set_freq(f0, fs);
        self.level = self.table.level_for(f0, self.ceiling);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn levels_halve_and_pick_the_brightest_that_fits() {
        let t = Wavetable::new(&[saw_frame(2048)], 1);
        assert_eq!(t.levels(), 11);
        assert_eq!(t.harmonics(0), 1024);
        assert_eq!(t.harmonics(10), 1);
        /* 440 Hz under a 24 kHz ceiling: 54 harmonics fit, so the 32 of level 5. */
        let l = t.level_for(440.0, 24_000.0);
        assert_eq!((l, t.harmonics(l)), (5, 32));
        /* A ceiling of 28 kHz fits 63: still level 5. 30 Hz fits everything. */
        assert_eq!(t.level_for(440.0, 28_000.0), 5);
        assert_eq!(t.level_for(20.0, 24_000.0), 0);
        /* Above the top level's reach, the last level (a sine) is all there is. */
        assert_eq!(t.level_for(30_000.0, 24_000.0), 10);
        assert_eq!(t.size(), 2048);
        assert_eq!(t.frames(), 1);

        /* Three per octave: 31 levels, and 440 Hz gets 50 of the 54 that fit. */
        let t = Wavetable::new(&[saw_frame(256)], 3);
        assert_eq!(t.levels(), 3 * 7 + 1);
        assert_eq!((t.harmonics(0), t.harmonics(3), t.harmonics(21)), (128, 64, 1));
        let fine = Wavetable::new(&[saw_frame(2048)], 3);
        let l = fine.level_for(440.0, 24_000.0);
        assert_eq!(fine.harmonics(l), 50);
    }

    #[test]
    fn the_top_level_is_the_fundamental_alone() {
        let t = Wavetable::new(&[saw_frame(64)], 1);
        let top = t.levels() - 1;
        /* The saw's fundamental is -(2/pi) sin(2 pi t). */
        for i in 0..64 {
            let p = i as f32 / 64.0;
            let want = -(2.0 / core::f32::consts::PI) * (2.0 * core::f32::consts::PI * p).sin();
            assert!((t.read(top, 0.0, p) - want).abs() < 1e-5);
        }
    }

    #[test]
    fn morphing_crossfades_neighbouring_frames() {
        let size = 256;
        let up = saw_frame(size);
        let down: Vec<f32> = up.iter().map(|x| -x).collect();
        let t = Wavetable::new(&[up, down], 1);
        let p = 0.3;
        let a = t.read(2, 0.0, p);
        let b = t.read(2, 1.0, p);
        assert!((a + b).abs() < 1e-5);
        assert!(t.read(2, 0.5, p).abs() < 1e-5);
        assert_eq!(t.read(2, 7.0, p), b); // clamped
    }

    #[test]
    fn the_oscillator_follows_its_pitch_to_the_right_level() {
        let t = Wavetable::new(&[saw_frame(2048)], 1);
        let mut o = WavetableOsc::new(&t, 440.0, 48_000.0, 24_000.0);
        assert_eq!(o.level(), 5);
        o.set_freq(55.0, 48_000.0);
        assert_eq!(o.level(), 2);
        let peak = (0..4800).map(|_| o.next().abs()).fold(0.0f32, f32::max);
        assert!((0.9..1.2).contains(&peak), "{peak}");
    }
}
