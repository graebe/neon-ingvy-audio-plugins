// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Capturing a voice: the formants of a singer's own vowels, measured from a
recording, as a [`VowelTable`] the engine sings with instead of the
textbook's.

OFFLINE, NOT THE AUDIO THREAD. A recording is analysed once, when it is
given, on whatever thread the shell chooses; this allocates freely. What it
returns is fifty numbers, which the engine takes on the audio thread like any
other value (Engine::set_voice) and a saved set keeps instead of the audio.

HOW, frame by frame (30 ms, every 10 ms):

1. The recording is band-limited and decimated to about 11 kHz: a voice's
   first four formants lie below 5 kHz, and linear prediction finds them best
   with few coefficients over just that band.
2. A frame is kept when it is loud (within 30 dB of the loudest) and voiced
   (its normalised autocorrelation peaks above 0.6 at a pitch between 70 and
   700 Hz): sung vowels, not breaths, consonants or silence.
3. Pre-emphasis, a Hann window, linear prediction of order 12, and the
   predictor's roots (Aberth's iteration): every root above the real axis is
   a resonance, its angle the frequency and its radius the bandwidth. The
   lowest three narrower than 600 Hz are F1, F2 and F3; a fourth is F4.
4. WHICH VOWEL, BY ORDER FIRST. The manual asks for A, E, I, O, U sung one
   after the other. The kept frames fall into stretches -- runs with no gap
   longer than 30 ms -- and when there are exactly five stretches of at least
   50 ms, they are the five vowels in that order. A singer's own vowels need
   not resemble the textbook's for that to hold, which is what makes it the
   first choice.
5. WHOSE VOICE. A bigger or smaller voice moves every formant by about the
   same factor: the factor, searched from 0.8 to 1.4, at which the frames sit
   closest to the textbook's five vowels -- each frame to its own vowel when
   the order said which, else to the nearest -- is the singer's size.
6. BY LIKENESS, OTHERWISE -- and when the order disagrees with the sound: if
   the stretches sit, on average, more than a Bark further from the vowels
   the order gave them than from their nearest, the singer did not sing A E I
   O U in order, and every frame belongs instead to the nearest scaled vowel
   in (F1, F2) on the Bark scale, if it is within 1.5 Bark of it.
7. A vowel heard in at least five frames (50 ms) is the median of its frames:
   F1 to F3 and their bandwidths always, F4 when most frames had one. F5,
   above what 11 kHz can see, and a vowel never sung, are the textbook's
   scaled to the singer: the table is always whole, and `found` says which
   vowels were measured. How far its frames agree is its `confidence`: 1 for
   frames that agree to a hundredth of a Bark, falling by e for each half
   Bark of spread, and scaled down below twenty frames.

A CAPTURE CAN BE CALLED OFF (`capture_cancellable`): the flag is read at every
chunk of the decimation and every frame, so a shell closing mid-capture waits
for a frame, not for the recording.

Sung high, a voice's harmonics are far apart and linear prediction is drawn
towards them; the medians and the Bark tolerance keep that from moving a
vowel far, but a capture of a low or middle register is the truer one.
*/

use core::sync::atomic::{AtomicBool, Ordering};

use crate::lpc::{levinson, Coefficients, IDENTITY};
use crate::model::{bark, VowelTable, BANDWIDTHS, FORMANTS, VOWELS};
use num_complex::Complex64;

/// The rate the analysis runs at, near enough: the recording's rate divided
/// by a whole number.
pub const ANALYSIS_HZ: f64 = 11_025.0;

/// The longest recording analysed, in seconds: the rest is not read.
pub const LONGEST_S: f64 = 120.0;

const FRAME_S: f64 = 0.03;
const HOP_S: f64 = 0.01;
const ORDER: usize = 12;
const LOUD_DB: f64 = -30.0;
const VOICED: f64 = 0.6;
const LOWEST_PITCH: f64 = 70.0;
const HIGHEST_PITCH: f64 = 700.0;
const WIDEST_FORMANT: f64 = 600.0;
const NEAREST_BARK: f64 = 1.5;
const MIN_FRAMES: usize = 5;
/// The longest gap, in frames (10 ms each), inside one sung stretch.
const GAP_FRAMES: usize = 3;
/// How much further from its ordered vowel than from its nearest a stretch
/// may sit, on average, in Bark, before the order is not believed.
const ORDER_SLACK_BARK: f64 = 1.0;
/// Frames enough for a full confidence.
const CONFIDENT_FRAMES: f64 = 20.0;

/// What a capture found.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Capture {
    /// The voice, whole: measured vowels and, for the rest, the textbook's
    /// scaled to the singer.
    pub table: VowelTable,
    /// Which vowels (A E I O U) were measured.
    pub found: [bool; 5],
    /// How many frames each was measured from.
    pub frames: [u32; 5],
    /// The singer's size against the textbook voice: 1.2 is formants a fifth
    /// higher.
    pub scale: f64,
    /// How far each vowel's frames agree, 0 (unmeasured, or all over the
    /// place) to 1: what an editor shows of a capture's quality.
    pub confidence: [f64; 5],
    /// Whether the vowels were told apart by the order they were sung in
    /// (step 4) rather than by likeness (step 6).
    pub by_order: bool,
}

/// Why a capture found no voice.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Failure {
    /// Called off before it finished.
    Cancelled,
    /// Nothing in it was sung: silence, noise, too short, a rate below 8 kHz.
    Unsung,
    /// Something was sung, but none of it near enough any vowel.
    NoVowels,
}

/// One kept frame's resonances, in Hz.
#[derive(Clone, Copy, Debug)]
struct Frame {
    /// Which hop of the recording it is: what tells stretches apart.
    at: usize,
    freq: [f64; 4],
    bandwidth: [f64; 3],
    /// Whether a fourth was found.
    f4: bool,
}

/// The voice in `samples` (mono, at `fs`), or why there is none. Never
/// called off: [`capture_cancellable`] with a flag that stays down.
pub fn capture(samples: &[f32], fs: f64) -> Result<Capture, Failure> {
    capture_cancellable(samples, fs, &AtomicBool::new(false))
}

/// As [`capture`], given up as [`Failure::Cancelled`] as soon as `cancel` is
/// seen raised.
pub fn capture_cancellable(samples: &[f32], fs: f64, cancel: &AtomicBool) -> Result<Capture, Failure> {
    if !fs.is_finite() || fs < 8000.0 {
        return Err(Failure::Unsung);
    }
    let longest = (LONGEST_S * fs) as usize;
    let (x, fa) = decimate(&samples[..samples.len().min(longest)], fs, cancel).ok_or(Failure::Cancelled)?;
    let frames = analyse(&x, fa, cancel).ok_or(Failure::Cancelled)?;
    if frames.is_empty() {
        return Err(Failure::Unsung);
    }

    /* The vowels by the order they were sung in, if the order holds. */
    let ordered = by_order(&frames);
    let was_ordered = ordered.is_some();
    let (scale, members) = match ordered {
        Some(labelled) => (fit_scale(&labelled), group(&labelled)),
        None => {
            let scale = fit_scale(&frames.iter().map(|f| (*f, None)).collect::<Vec<_>>());
            let near = frames.iter().filter_map(|f| {
                let (v, d) = nearest(f, scale);
                (d < NEAREST_BARK).then_some((*f, Some(v)))
            });
            (scale, group(&near.collect::<Vec<_>>()))
        }
    };

    let mut table = VowelTable::TEXTBOOK;
    let mut found = [false; 5];
    let mut counts = [0u32; 5];
    let mut confidence = [0.0; 5];
    for v in 0..5 {
        let m = &members[v];
        counts[v] = m.len() as u32;
        for k in 0..FORMANTS {
            table.freq[v][k] = VOWELS[v][k] * scale;
            table.bandwidth[v][k] = BANDWIDTHS[k];
        }
        if m.len() < MIN_FRAMES {
            continue;
        }
        found[v] = true;
        for k in 0..3 {
            table.freq[v][k] = median(m.iter().map(|f| f.freq[k]));
            table.bandwidth[v][k] = median(m.iter().map(|f| f.bandwidth[k])).clamp(40.0, 300.0);
        }
        let with_f4: Vec<f64> = m.iter().filter(|f| f.f4).map(|f| f.freq[3]).collect();
        if 2 * with_f4.len() >= m.len() {
            table.freq[v][3] = median(with_f4.into_iter());
        }
        let (b1, b2) = (bark(table.freq[v][0]), bark(table.freq[v][1]));
        let spread = median(m.iter().map(|f| (bark(f.freq[0]) - b1).hypot(bark(f.freq[1]) - b2)));
        confidence[v] = ((-spread / 0.5).exp() * (m.len() as f64 / CONFIDENT_FRAMES).min(1.0)).clamp(0.0, 1.0);
    }
    if !found.iter().any(|&f| f) {
        return Err(Failure::NoVowels);
    }
    let table = table.sanitised().ok_or(Failure::NoVowels)?;
    Ok(Capture { table, found, frames: counts, scale, confidence, by_order: was_ordered })
}

/// The frames' sung stretches -- runs with no gap longer than
/// [`GAP_FRAMES`] -- of at least [`MIN_FRAMES`].
fn stretches(frames: &[Frame]) -> Vec<&[Frame]> {
    let mut out = Vec::new();
    let mut start = 0;
    for i in 1..=frames.len() {
        if i == frames.len() || frames[i].at - frames[i - 1].at > GAP_FRAMES {
            if i - start >= MIN_FRAMES {
                out.push(&frames[start..i]);
            }
            start = i;
        }
    }
    out
}

/// Every frame labelled with its vowel by the order it was sung in, when there
/// are exactly five stretches and they sit about as near the vowels the order
/// gives them as to their nearest (step 6); `None` otherwise.
fn by_order(frames: &[Frame]) -> Option<Vec<(Frame, Option<usize>)>> {
    let runs = stretches(frames);
    if runs.len() != 5 {
        return None;
    }
    let labelled: Vec<(Frame, Option<usize>)> =
        runs.iter().enumerate().flat_map(|(v, run)| run.iter().map(move |f| (*f, Some(v)))).collect();
    let scale = fit_scale(&labelled);
    let (mut given, mut nearest_d) = (0.0, 0.0);
    for (f, v) in &labelled {
        given += distance(f, v.unwrap_or(0), scale);
        nearest_d += nearest(f, scale).1;
    }
    let n = labelled.len() as f64;
    (given / n <= nearest_d / n + ORDER_SLACK_BARK).then_some(labelled)
}

/// The labelled frames, vowel by vowel.
fn group(labelled: &[(Frame, Option<usize>)]) -> [Vec<Frame>; 5] {
    let mut out: [Vec<Frame>; 5] = Default::default();
    for (f, v) in labelled {
        if let Some(v) = v {
            out[*v].push(*f);
        }
    }
    out
}

/// `x` band-limited and decimated by a whole number to near
/// [`ANALYSIS_HZ`], and the rate it is then at.
/// `None` when called off.
fn decimate(x: &[f32], fs: f64, cancel: &AtomicBool) -> Option<(Vec<f64>, f64)> {
    let d = ((fs / ANALYSIS_HZ).floor() as usize).max(1);
    let fa = fs / d as f64;
    if d == 1 {
        return (!cancel.load(Ordering::Relaxed)).then(|| (x.iter().map(|&s| f64::from(s)).collect(), fa));
    }
    /* A windowed sinc (Blackman), cut at 90 % of the new Nyquist. */
    let half = 8 * d;
    let cut = 0.9 * 0.5 / d as f64;
    let taps: Vec<f64> = (0..=2 * half)
        .map(|j| {
            let t = j as f64 - half as f64;
            let sinc = if t == 0.0 { 2.0 * cut } else { (core::f64::consts::TAU * cut * t).sin() / (core::f64::consts::PI * t) };
            let w = j as f64 / (2 * half) as f64;
            let blackman = 0.42 - 0.5 * (core::f64::consts::TAU * w).cos() + 0.08 * (2.0 * core::f64::consts::TAU * w).cos();
            sinc * blackman
        })
        .collect();
    /* Called off between chunks of a second's output. */
    let chunk = ANALYSIS_HZ as usize;
    let mut out = Vec::with_capacity(x.len() / d);
    for m in 0..x.len() / d {
        if m % chunk == 0 && cancel.load(Ordering::Relaxed) {
            return None;
        }
        let centre = m * d;
        out.push(
            taps.iter()
                .enumerate()
                .filter_map(|(j, h)| (centre + j).checked_sub(half).and_then(|i| x.get(i)).map(|&s| h * f64::from(s)))
                .sum(),
        );
    }
    Some((out, fa))
}

/// Every kept frame of `x` (at `fa`): loud, voiced, and with three
/// resonances found; `None` when called off.
fn analyse(x: &[f64], fa: f64, cancel: &AtomicBool) -> Option<Vec<Frame>> {
    let n = (FRAME_S * fa).round() as usize;
    let hop = (HOP_S * fa).round().max(1.0) as usize;
    if x.len() < n || n < 2 * ORDER {
        return Some(Vec::new());
    }
    let starts: Vec<usize> = (0..=x.len() - n).step_by(hop).collect();
    let power = |s: usize| x[s..s + n].iter().map(|v| v * v).sum::<f64>() / n as f64;
    let loudest = starts.iter().map(|&s| power(s)).fold(0.0, f64::max);
    if loudest <= 1e-12 {
        return Some(Vec::new());
    }
    let floor = loudest * 10f64.powf(LOUD_DB / 10.0);
    let window: Vec<f64> = (0..n).map(|i| 0.5 - 0.5 * (core::f64::consts::TAU * i as f64 / (n - 1) as f64).cos()).collect();
    let (lag_lo, lag_hi) = ((fa / HIGHEST_PITCH) as usize, (fa / LOWEST_PITCH) as usize);

    let mut kept = Vec::new();
    for (at, s) in starts.into_iter().enumerate() {
        if cancel.load(Ordering::Relaxed) {
            return None;
        }
        if power(s) < floor || !voiced(&x[s..s + n], lag_lo, lag_hi.min(n / 2)) {
            continue;
        }
        let frame: Vec<f64> = (0..n)
            .map(|i| {
                let prev = if s + i > 0 { x[s + i - 1] } else { 0.0 };
                (x[s + i] - 0.97 * prev) * window[i]
            })
            .collect();
        if let Some(f) = resonances(&frame, fa, at) {
            kept.push(f);
        }
    }
    Some(kept)
}

/// Whether `x` repeats at some lag in `lo..=hi`: its normalised
/// autocorrelation there peaks above [`VOICED`].
fn voiced(x: &[f64], lo: usize, hi: usize) -> bool {
    (lo.max(1)..=hi).any(|k| {
        let (a, b) = (&x[..x.len() - k], &x[k..]);
        let ab: f64 = a.iter().zip(b).map(|(p, q)| p * q).sum();
        let aa: f64 = a.iter().map(|p| p * p).sum();
        let bb: f64 = b.iter().map(|q| q * q).sum();
        aa > 0.0 && bb > 0.0 && ab / (aa * bb).sqrt() > VOICED
    })
}

/// The resonances of one windowed frame at `fa`, or `None` for fewer than
/// three.
fn resonances(frame: &[f64], fa: f64, at: usize) -> Option<Frame> {
    let mut r = [0.0; ORDER + 1];
    for (k, rk) in r.iter_mut().enumerate() {
        *rk = frame[k..].iter().zip(frame).map(|(a, b)| a * b).sum();
    }
    r[0] *= 1.0 + 1e-9;
    let mut a: Coefficients = IDENTITY;
    if levinson(&r, &mut a) <= 0.0 {
        return None;
    }
    let mut found: Vec<(f64, f64)> = roots(&a[..=ORDER])
        .into_iter()
        .filter(|z| z.im > 0.0)
        .map(|z| (z.arg() * fa / core::f64::consts::TAU, -z.norm().ln() * fa / core::f64::consts::PI))
        .filter(|&(f, bw)| f > 90.0 && f < 0.5 * fa - 100.0 && bw > 0.0 && bw < WIDEST_FORMANT)
        .collect();
    found.sort_by(|p, q| p.0.total_cmp(&q.0));
    if found.len() < 3 {
        return None;
    }
    Some(Frame {
        at,
        freq: [found[0].0, found[1].0, found[2].0, found.get(3).map_or(0.0, |p| p.0)],
        bandwidth: [found[0].1, found[1].1, found[2].1],
        f4: found.len() > 3,
    })
}

/// The roots of the monic polynomial z^n + c[1] z^(n-1) + ... + c[n], by
/// Aberth's iteration.
fn roots(c: &[f64]) -> Vec<Complex64> {
    let n = c.len() - 1;
    let eval = |z: Complex64| {
        let (mut p, mut dp) = (Complex64::new(1.0, 0.0), Complex64::new(0.0, 0.0));
        for &ck in &c[1..] {
            dp = dp * z + p;
            p = p * z + ck;
        }
        (p, dp)
    };
    /* Start on a circle, off the real axis and off any symmetry. */
    let mut z: Vec<Complex64> =
        (0..n).map(|k| Complex64::from_polar(0.9, core::f64::consts::TAU * k as f64 / n as f64 + 0.4)).collect();
    for _ in 0..200 {
        let mut moved: f64 = 0.0;
        for k in 0..n {
            let (p, dp) = eval(z[k]);
            if p.norm() == 0.0 {
                continue;
            }
            let ratio = p / dp;
            let pull: Complex64 = (0..n).filter(|&j| j != k).map(|j| (z[k] - z[j]).inv()).sum();
            let step = ratio / (Complex64::new(1.0, 0.0) - ratio * pull);
            if step.is_finite() {
                z[k] -= step;
                moved = moved.max(step.norm());
            }
        }
        if moved < 1e-12 {
            break;
        }
    }
    z
}

/// How far `f` sits from textbook vowel `v` with every formant scaled by
/// `scale`, in Bark over (F1, F2).
fn distance(f: &Frame, v: usize, scale: f64) -> f64 {
    let d1 = bark(f.freq[0]) - bark(VOWELS[v][0] * scale);
    let d2 = bark(f.freq[1]) - bark(VOWELS[v][1] * scale);
    d1.hypot(d2)
}

/// The textbook vowel nearest `f` with every formant scaled by `scale`, and
/// how far.
fn nearest(f: &Frame, scale: f64) -> (usize, f64) {
    (0..5)
        .map(|v| (v, distance(f, v, scale)))
        .min_by(|p, q| p.1.total_cmp(&q.1))
        .unwrap_or((0, f64::MAX))
}

/// The scale, 0.8 to 1.4, at which the frames sit closest to the textbook's
/// vowels -- each to the vowel it is labelled with, or to its nearest --
/// each distance capped at 3 Bark so a stray frame cannot pull the fit.
fn fit_scale(frames: &[(Frame, Option<usize>)]) -> f64 {
    (0..=120)
        .map(|i| 0.8 + 0.005 * i as f64)
        .map(|s| {
            let cost = frames
                .iter()
                .map(|(f, v)| match v {
                    Some(v) => distance(f, *v, s),
                    None => nearest(f, s).1,
                })
                .map(|d| d.min(3.0))
                .sum::<f64>();
            (s, cost)
        })
        .min_by(|p, q| p.1.total_cmp(&q.1))
        .map_or(1.0, |p| p.0)
}

fn median(values: impl Iterator<Item = f64>) -> f64 {
    let mut v: Vec<f64> = values.collect();
    v.sort_by(f64::total_cmp);
    match v.len() {
        0 => 0.0,
        n if n % 2 == 1 => v[n / 2],
        n => 0.5 * (v[n / 2 - 1] + v[n / 2]),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{Formants, Pole};

    /// A singer whose voice is the textbook's times `scale`, with A's F2
    /// moved by `a_f2`: each of `vowels` sung for `seconds` at `pitch`, a
    /// breath of silence between, as a glottal pulse train through the
    /// formants -- the way a voice is made.
    fn sing(vowels: &[usize], scale: f64, a_f2: f64, pitch: f64, seconds: f64, fs: f64) -> Vec<f32> {
        let mut table = VowelTable::TEXTBOOK;
        for row in table.freq.iter_mut() {
            for f in row.iter_mut() {
                *f *= scale;
            }
        }
        table.freq[0][1] *= a_f2;
        sing_table(&table, vowels, pitch, seconds, fs)
    }

    /// Each of `vowels` of `table`'s voice sung for `seconds` at `pitch`, a
    /// breath of silence after each, the loudest peak at 0.8.
    fn sing_table(table: &VowelTable, vowels: &[usize], pitch: f64, seconds: f64, fs: f64) -> Vec<f32> {
        let mut out = Vec::new();
        for &v in vowels {
            let poles: [Pole; FORMANTS] = Formants::of(table, v as f64, 0.0, 50.0).poles(fs, 1.0);
            let mut y = [[0.0; 2]; FORMANTS];
            let mut tilt = 0.0;
            let period = fs / pitch;
            let n = (seconds * fs) as usize;
            for i in 0..n {
                /* A pulse a period, then a -6 dB/oct glottal tilt. */
                let pulse = if (i as f64 % period) < 1.0 { 1.0 } else { 0.0 };
                tilt = 0.97 * tilt + pulse;
                let mut x = tilt;
                for (p, s) in poles.iter().zip(y.iter_mut()) {
                    x = p.run(x, s);
                }
                out.push((x * 0.05) as f32);
            }
            out.extend(std::iter::repeat_n(0.0f32, (0.1 * fs) as usize));
        }
        let peak = out.iter().fold(0.0f32, |m, s| m.max(s.abs()));
        out.iter().map(|s| s / peak * 0.8).collect()
    }

    #[test]
    fn a_bigger_voice_singing_all_five_is_measured_as_sung() {
        for (fs, scale, pitch) in [(44_100.0, 1.0, 120.0), (48_000.0, 1.18, 200.0), (96_000.0, 0.9, 110.0)] {
            let x = sing(&[0, 1, 2, 3, 4], scale, 1.08, pitch, 0.6, fs);
            let c = capture(&x, fs).expect("a voice");
            assert_eq!(c.found, [true; 5], "fs {fs}: {:?}", c.frames);
            assert!((c.scale / scale - 1.0).abs() < 0.06, "fs {fs}: scale {} for {scale}", c.scale);
            for (v, row) in VOWELS.iter().enumerate() {
                let want_f2 = row[1] * scale * if v == 0 { 1.08 } else { 1.0 };
                let want_f1 = row[0] * scale;
                assert!((c.table.freq[v][0] / want_f1 - 1.0).abs() < 0.1, "fs {fs} vowel {v}: F1 {} for {want_f1}", c.table.freq[v][0]);
                assert!((c.table.freq[v][1] / want_f2 - 1.0).abs() < 0.06, "fs {fs} vowel {v}: F2 {} for {want_f2}", c.table.freq[v][1]);
            }
        }
    }

    #[test]
    fn a_vowel_never_sung_is_the_textbooks_scaled_to_the_singer() {
        let x = sing(&[0, 2, 4], 1.15, 1.0, 140.0, 0.6, 48_000.0);
        let c = capture(&x, 48_000.0).expect("a voice");
        assert_eq!(c.found, [true, false, true, false, true]);
        assert_eq!(c.frames[1], 0);
        for (got, textbook) in c.table.freq[1].iter().zip(VOWELS[1]) {
            assert!((got - textbook * c.scale).abs() < 1e-9);
        }
        assert!(c.table.sanitised() == Some(c.table));
    }

    #[test]
    fn silence_noise_and_nonsense_are_no_voice() {
        assert_eq!(capture(&vec![0.0; 48_000], 48_000.0), Err(Failure::Unsung));
        let mut rng = fastrand::Rng::with_seed(9);
        let noise: Vec<f32> = (0..48_000).map(|_| rng.f32() - 0.5).collect();
        assert_eq!(capture(&noise, 48_000.0), Err(Failure::Unsung));
        assert_eq!(capture(&[0.5; 100], 48_000.0), Err(Failure::Unsung));
        assert_eq!(capture(&noise, 4_000.0), Err(Failure::Unsung));
        assert_eq!(capture(&noise, f64::NAN), Err(Failure::Unsung));
        assert_eq!(capture(&[], 48_000.0), Err(Failure::Unsung));
    }

    #[test]
    fn a_sung_sound_far_from_every_vowel_is_no_vowels() {
        /* Pulses through two formants no vowel has: 1500 and 1700 Hz. */
        let fs = 48_000.0;
        let poles = [Pole::new(1500.0, 60.0, fs, 1.0), Pole::new(1700.0, 60.0, fs, 1.0), Pole::new(3300.0, 120.0, fs, 1.0)];
        let mut y = [[0.0; 2]; 3];
        let mut tilt = 0.0;
        let x: Vec<f32> = (0..fs as usize)
            .map(|i| {
                tilt = 0.97 * tilt + if (i as f64 % (fs / 150.0)) < 1.0 { 1.0 } else { 0.0 };
                let mut s = tilt;
                for (p, st) in poles.iter().zip(y.iter_mut()) {
                    s = p.run(s, st);
                }
                (s * 0.01) as f32
            })
            .collect();
        assert_eq!(capture(&x, fs), Err(Failure::NoVowels));
    }

    #[test]
    fn five_vowels_in_order_are_taken_by_their_order() {
        /* A singer whose E is far from the textbook's -- nearer its I -- is
         * still heard as E, because it came second. */
        let fs = 48_000.0;
        let mut x = sing(&[0], 1.1, 1.0, 130.0, 0.6, fs);
        let mut odd_e = VowelTable::TEXTBOOK;
        for row in odd_e.freq.iter_mut() {
            for f in row.iter_mut() {
                *f *= 1.1;
            }
        }
        odd_e.freq[1][0] = 360.0;
        odd_e.freq[1][1] = 2300.0;
        x.extend(sing_table(&odd_e, &[1], 130.0, 0.6, fs));
        x.extend(sing(&[2, 3, 4], 1.1, 1.0, 130.0, 0.6, fs));
        let c = capture(&x, fs).expect("a voice");
        assert!(c.by_order);
        assert_eq!(c.found, [true; 5]);
        assert!((c.table.freq[1][1] / 2300.0 - 1.0).abs() < 0.06, "E's F2 {}", c.table.freq[1][1]);
    }

    #[test]
    fn five_vowels_out_of_order_are_told_apart_by_likeness() {
        let x = sing(&[4, 3, 2, 1, 0], 1.0, 1.0, 120.0, 0.6, 48_000.0);
        let c = capture(&x, 48_000.0).expect("a voice");
        assert!(!c.by_order);
        assert_eq!(c.found, [true; 5]);
        for (v, row) in VOWELS.iter().enumerate() {
            assert!((c.table.freq[v][1] / row[1] - 1.0).abs() < 0.06, "vowel {v}: F2 {}", c.table.freq[v][1]);
        }
    }

    #[test]
    fn a_steady_vowel_is_trusted_more_than_a_wandering_one() {
        let fs = 48_000.0;
        let steady = capture(&sing(&[0], 1.0, 1.0, 130.0, 0.8, fs), fs).expect("a voice");
        /* The same A, its F2 wandering a fifth either way. */
        let mut wandering = Vec::new();
        for (i, a_f2) in [0.8, 1.2, 0.85, 1.15, 0.9, 1.1, 0.8, 1.2].iter().enumerate() {
            let mut part = sing(&[0], 1.0, *a_f2, 130.0, 0.1, fs);
            part.truncate((0.1 * fs) as usize);
            wandering.extend(part);
            let _ = i;
        }
        wandering.extend(std::iter::repeat_n(0.0f32, (0.1 * fs) as usize));
        let wandering = capture(&wandering, fs).expect("a voice");
        assert!(steady.confidence[0] > 0.6, "{:?}", steady.confidence);
        assert!(wandering.confidence[0] < steady.confidence[0] * 0.8, "{} vs {}", wandering.confidence[0], steady.confidence[0]);
        assert_eq!(steady.confidence[1], 0.0);
    }

    #[test]
    fn a_capture_called_off_stops_with_cancelled() {
        let x = sing(&[0, 1, 2, 3, 4], 1.0, 1.0, 120.0, 0.6, 48_000.0);
        assert_eq!(capture_cancellable(&x, 48_000.0, &AtomicBool::new(true)), Err(Failure::Cancelled));
        /* At 11 kHz there is nothing to decimate, and the frames see it. */
        let y = sing(&[0], 1.0, 1.0, 120.0, 0.6, 11_025.0);
        assert_eq!(capture_cancellable(&y, 11_025.0, &AtomicBool::new(true)), Err(Failure::Cancelled));
    }

    #[test]
    fn the_roots_are_the_polynomials() {
        /* (z - 0.5)(z + 0.25)(z² - 2·0.9 cos 0.7 z + 0.81) */
        let pair = [1.0, -2.0 * 0.9 * 0.7f64.cos(), 0.81];
        let lin = [1.0, -0.25, -0.125];
        let mut c = [0.0; 5];
        for i in 0..3 {
            for j in 0..3 {
                c[i + j] += pair[i] * lin[j];
            }
        }
        let mut found = roots(&c);
        found.sort_by(|a, b| a.re.total_cmp(&b.re).then(a.im.total_cmp(&b.im)));
        let upper = found.iter().find(|z| z.im > 1e-6).unwrap();
        assert!((upper.norm() - 0.9).abs() < 1e-9 && (upper.arg() - 0.7).abs() < 1e-9);
        assert!(found.iter().any(|z| (z.re - 0.5).abs() < 1e-9 && z.im.abs() < 1e-9));
        assert!(found.iter().any(|z| (z.re + 0.25).abs() < 1e-9 && z.im.abs() < 1e-9));
        assert_eq!(median([3.0, 1.0, 2.0, 10.0].into_iter()), 2.5);
        assert_eq!(median(std::iter::empty()), 0.0);
    }
}
