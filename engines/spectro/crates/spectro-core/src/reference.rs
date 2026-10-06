// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The analysis as it was before it was made fast -- kept as the oracle.
 *
 * A complex radix-2 FFT on real input, a sqrt per bin, `powf` between bins,
 * and a `powf` per cell to sum sources. Slow and plainly right, which is what
 * an oracle is for. `equivalence` below holds the fast path to it: every column
 * byte within one step of this, on a fixed set of signals and configurations.
 */

use crate::bands::{amplitude_to_byte, byte_to_db, db_to_byte, Bands};
use crate::window::Window;
use crate::Config;

fn fft(re: &mut [f32], im: &mut [f32]) {
    let n = re.len();
    let bits = n.trailing_zeros();
    for i in 0..n {
        let j = ((i as u32).reverse_bits() >> (32 - bits)) as usize;
        if i < j {
            re.swap(i, j);
            im.swap(i, j);
        }
    }
    let (cos, sin): (Vec<f32>, Vec<f32>) = (0..n / 2)
        .map(|k| {
            let theta = -2.0 * core::f64::consts::PI * (k as f64) / (n as f64);
            (theta.cos() as f32, theta.sin() as f32)
        })
        .unzip();
    let mut len = 2;
    while len <= n {
        let half = len / 2;
        let stride = n / len;
        let mut base = 0;
        while base < n {
            for k in 0..half {
                let t = k * stride;
                let (wr, wi) = (cos[t], sin[t]);
                let (a, b) = (base + k, base + k + half);
                let xr = re[b] * wr - im[b] * wi;
                let xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
            base += len;
        }
        len <<= 1;
    }
}

/// Every column the analyzer would produce from `signal`, concatenated.
pub fn columns(cfg: Config, signal: &[f32]) -> Vec<u8> {
    let cfg = cfg.sanitised();
    let n = cfg.fft_size;
    let window = Window::hann(n);
    let bands = Bands::new(cfg.bands, n / 2 + 1, cfg.sample_rate, cfg.f_min, cfg.f_max);
    let mut out = Vec::new();
    let mut t = cfg.hop;
    while t <= signal.len() {
        if t >= n {
            let mut re: Vec<f32> = signal[t - n..t]
                .iter()
                .zip(&window.gain)
                .map(|(&s, &g)| if s.is_finite() { s } else { 0.0 } * g)
                .collect();
            let mut im = vec![0.0f32; n];
            fft(&mut re, &mut im);
            let scale = window.amplitude_scale;
            let mag = |k: usize| -> f32 {
                let m = (re[k] * re[k] + im[k] * im[k]).sqrt() * scale;
                if k == n / 2 {
                    m * 0.5
                } else {
                    m
                }
            };
            for range in &bands.ranges {
                let v = match range.frac {
                    None => (range.lo..range.hi).map(mag).fold(0.0f32, f32::max),
                    Some(x) => {
                        let t = (x - range.lo as f32).clamp(0.0, 1.0);
                        let lo = mag(range.lo).max(1e-20);
                        let hi = mag(range.lo + 1).max(1e-20);
                        lo * (hi / lo).powf(t)
                    }
                };
                out.push(amplitude_to_byte(v, cfg.db_floor, cfg.db_ceil));
            }
        }
        t += cfg.hop;
    }
    out
}

/// One cell of `sum_column`, the way it was: a `powf` per source, a `log10`.
pub fn sum_cell(cells: &[u8], db_floor: f32, db_ceil: f32) -> u8 {
    let mut power = 0.0f32;
    for &c in cells {
        let db = byte_to_db(c, db_floor, db_ceil);
        if db.is_finite() {
            power += 10.0f32.powf(db * 0.1);
        }
    }
    if power > 0.0 {
        db_to_byte(10.0 * power.log10(), db_floor, db_ceil)
    } else {
        0
    }
}

#[cfg(test)]
mod equivalence {
    use super::*;
    use crate::{Analyzer, PowerTable};

    /*
     * THE TOLERANCE, PINNED: one byte step (0.38 dB on the default -96..0
     * scale), and on no more than 1% of the cells. A byte is a rounded dB, so a
     * value sitting on a rounding edge may land either side of it once the
     * arithmetic before it is reordered -- that is the only difference allowed.
     */
    const MAX_STEP: i16 = 1;
    const MAX_OFF_FRACTION: f64 = 0.01;

    fn lcg(seed: &mut u64) -> f32 {
        *seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        ((*seed >> 40) as f32 / (1u64 << 24) as f32) * 2.0 - 1.0
    }

    /// The fixed signal set: each one aimed at a different part of the path.
    fn signals(sr: f32, len: usize) -> Vec<(&'static str, Vec<f32>)> {
        let tau = 2.0 * core::f64::consts::PI;
        let mut seed = 0x2545_F491_4F6C_DD1Du64;
        let noise: Vec<f32> = (0..len).map(|_| 0.5 * lcg(&mut seed)).collect();
        let faint: Vec<f32> = (0..len).map(|_| 2e-5 * lcg(&mut seed)).collect();
        let tones: Vec<f32> = (0..len)
            .map(|i| {
                let t = i as f64 / sr as f64;
                (0.5 * (tau * 1000.0 * t).sin()
                    + 0.25 * (tau * 37.3 * t).sin()
                    + 0.1 * (tau * 12_345.6 * t).sin()
                    + 0.01 * (tau * 88.0 * t).sin()) as f32
            })
            .collect();
        let chirp: Vec<f32> = {
            let (f0, f1) = (20.0f64, 20_000.0f64);
            let dur = len as f64 / sr as f64;
            let k = (f1 / f0).ln() / dur;
            (0..len)
                .map(|i| {
                    let t = i as f64 / sr as f64;
                    (0.7 * (tau * f0 * ((k * t).exp() - 1.0) / k).sin()) as f32
                })
                .collect()
        };
        let clicks: Vec<f32> = (0..len).map(|i| if i % 997 == 0 { 1.0 } else { 0.0 }).collect();
        let square: Vec<f32> = (0..len).map(|i| if (i / 50) % 2 == 0 { 1.0 } else { -1.0 }).collect();
        vec![
            ("noise", noise),
            ("faint noise", faint),
            ("tones", tones),
            ("chirp", chirp),
            ("clicks", clicks),
            ("square", square),
            ("silence", vec![0.0; len]),
        ]
    }

    fn configs() -> Vec<Config> {
        let d = Config::default();
        vec![
            d,
            Config { sample_rate: 96_000.0, fft_size: 16384, hop: 2048, ..d },
            Config { sample_rate: 44_100.0, fft_size: 1024, hop: 256, bands: 128, f_min: 20.0, ..d },
            Config { fft_size: 2048, hop: 512, bands: 512, f_min: 30.0, f_max: 16_000.0, db_floor: -120.0, db_ceil: 6.0, ..d },
            Config { fft_size: 64, hop: 32, bands: 16, ..d },
        ]
    }

    #[test]
    fn the_columns_match_the_reference_analysis() {
        for cfg in configs() {
            let c = cfg.sanitised();
            let len = c.fft_size + 12 * c.hop;
            for (name, sig) in signals(c.sample_rate, len) {
                let want = columns(cfg, &sig);

                let mut a = Analyzer::new(cfg);
                /* Pushed in awkward block sizes, as a host would. */
                for chunk in sig.chunks(333) {
                    a.push(chunk);
                }
                let mut got = vec![0u8; want.len() + c.bands];
                let cols = a.take_columns(&mut got, usize::MAX / c.bands.max(1));
                got.truncate(cols * c.bands);
                assert_eq!(got.len(), want.len(), "{name} @ {c:?}: column count");

                let mut off = 0usize;
                for (i, (&g, &w)) in got.iter().zip(&want).enumerate() {
                    let d = (g as i16 - w as i16).abs();
                    assert!(
                        d <= MAX_STEP,
                        "{name} @ {c:?}: col {} band {} is {g}, reference {w}",
                        i / c.bands,
                        i % c.bands
                    );
                    off += usize::from(d != 0);
                }
                let frac = off as f64 / want.len().max(1) as f64;
                println!("{name:>12} fft {:5} bands {:3}: {off} of {} cells off by one", c.fft_size, c.bands, want.len());
                assert!(frac <= MAX_OFF_FRACTION, "{name} @ {c:?}: {off} of {} cells off by one", want.len());
            }
        }
    }

    #[test]
    fn the_sum_matches_the_reference_for_every_pair_of_bytes() {
        for (floor, ceil) in [(-96.0f32, 0.0f32), (-120.0, 6.0), (-60.0, -10.0)] {
            let mut off = 0usize;
            let mut out = [0u8; 256];
            let table = PowerTable::new(floor, ceil);
            for a in 0..=255u8 {
                let col_a = [a; 256];
                let col_b: [u8; 256] = core::array::from_fn(|i| i as u8);
                table.sum_column(&[&col_a, &col_b], &mut out);
                for b in 0..=255u8 {
                    let want = sum_cell(&[a, b], floor, ceil);
                    let d = (out[b as usize] as i16 - want as i16).abs();
                    assert!(d <= MAX_STEP, "{a}+{b} on {floor}..{ceil}: {} vs {want}", out[b as usize]);
                    off += usize::from(d != 0);
                }
            }
            println!("sum of two, {floor}..{ceil} dB: {off} of 65536 pairs off by one");
            assert!(off as f64 / 65536.0 <= MAX_OFF_FRACTION, "{off} pairs off by one");
        }
    }

    #[test]
    fn the_sum_matches_the_reference_for_three_and_four_sources() {
        let mut seed = 7u64;
        let mut byte = || ((lcg(&mut seed) * 0.5 + 0.5) * 255.99) as u8;
        let (floor, ceil) = (-96.0f32, 0.0f32);
        let mut off = 0usize;
        let table = PowerTable::new(floor, ceil);
        let cells = 256;
        let rounds = 64;
        for _ in 0..rounds {
            let cols: Vec<Vec<u8>> = (0..4).map(|_| (0..cells).map(|_| byte()).collect()).collect();
            for k in [3usize, 4] {
                let view: Vec<&[u8]> = cols[..k].iter().map(|c| c.as_slice()).collect();
                let mut out = vec![0u8; cells];
                table.sum_column(&view, &mut out);
                for i in 0..cells {
                    let cell: Vec<u8> = cols[..k].iter().map(|c| c[i]).collect();
                    let want = sum_cell(&cell, floor, ceil);
                    let d = (out[i] as i16 - want as i16).abs();
                    assert!(d <= MAX_STEP, "{cell:?}: {} vs {want}", out[i]);
                    off += usize::from(d != 0);
                }
            }
        }
        println!("sum of three and four: {off} of {} cells off by one", 2 * rounds * cells);
        assert!(off as f64 / (2 * rounds * cells) as f64 <= MAX_OFF_FRACTION);
    }
}
