// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The editor's half: the rings' spectra, as a picture draws them.

Each frame's input and output go through a periodic Hann window (its sum is
the level a full-scale sine reads) and one real FFT of [`RING`] points, and
come out as dB per band of the picture's `N` frequencies: the loudest bin
inside a band, or the spectrum between two bins where a band holds none. A
band rises at once and falls over [`RELEASE_S`], so the picture reads as a
level rather than a flicker.

FOLLOWING A LANE. A picture asks for its lane ([`Analyzer::ask`]); a new one
starts the bands again from the floor, and the caller tells its engine, whose
rings start again from empty. Only a frame of that lane that is new is read
([`Analyzer::offer`]).
*/

use crate::{Lane, RING};
use realfft::num_complex::Complex32;
use realfft::{RealFftPlanner, RealToComplex};
use std::sync::Arc;

/// How a band falls, in seconds.
pub const RELEASE_S: f64 = 0.3;
/// The analyzer's floor, in dBFS.
pub const FLOOR_DB: f32 = -150.0;

/// The analyzer of a picture with `N` frequencies.
pub struct Analyzer<const N: usize> {
    hz: [f64; N],
    fft: Arc<dyn RealToComplex<f32>>,
    window: Vec<f32>,
    norm: f32,
    input: Vec<f32>,
    spectrum: Vec<Complex32>,
    scratch: Vec<Complex32>,
    bins: Vec<f32>,
    pre_ring: Vec<f32>,
    post_ring: Vec<f32>,
    pre: [f32; N],
    post: [f32; N],
    last_serial: Option<u32>,
    /// The lane last asked for: the one the bands are of.
    asked_lane: Option<Lane>,
}

impl<const N: usize> Analyzer<N> {
    /// An analyzer of the picture's frequencies `hz`, lowest first, its
    /// bands at the floor. Allocates.
    pub fn new(hz: [f64; N]) -> Analyzer<N> {
        let fft = RealFftPlanner::<f32>::new().plan_fft_forward(RING);
        /* Periodic Hann: a window's sum, the level a full-scale sine reads. */
        let window: Vec<f32> = (0..RING)
            .map(|i| (0.5 - 0.5 * (2.0 * core::f64::consts::PI * i as f64 / RING as f64).cos()) as f32)
            .collect();
        let sum: f32 = window.iter().sum();
        Analyzer {
            hz,
            input: fft.make_input_vec(),
            spectrum: fft.make_output_vec(),
            scratch: fft.make_scratch_vec(),
            fft,
            window,
            norm: 2.0 / sum,
            bins: vec![FLOOR_DB; RING / 2 + 1],
            pre_ring: vec![0.0; RING],
            post_ring: vec![0.0; RING],
            pre: [FLOOR_DB; N],
            post: [FLOOR_DB; N],
            last_serial: None,
            asked_lane: None,
        }
    }

    /// The picture is of `lane`. True when that is a new one: the bands
    /// start again from the floor, and the caller asks its engine for it.
    pub fn ask(&mut self, lane: Lane) -> bool {
        if self.asked_lane == Some(lane) {
            return false;
        }
        self.asked_lane = Some(lane);
        self.pre = [FLOOR_DB; N];
        self.post = [FLOOR_DB; N];
        true
    }

    /// A published frame: its serial, whether its engine kept the rings,
    /// their lane, and the rings. Taken -- true -- when it is watched, of the
    /// lane asked for, and new; then [`Analyzer::analyse`] it.
    pub fn offer(&mut self, serial: u32, watching: bool, lane: Lane, pre: &[f32], post: &[f32]) -> bool {
        let fresh = watching && Some(lane) == self.asked_lane && self.last_serial != Some(serial);
        if fresh {
            self.pre_ring.copy_from_slice(pre);
            self.post_ring.copy_from_slice(post);
        }
        fresh
    }

    /// The frame last taken, `serial` its serial (samples, wrapping) at the
    /// rate `fs`: each band up at once, down over RELEASE_S.
    pub fn analyse(&mut self, serial: u32, fs: f64) {
        let elapsed_s = self.last_serial.map_or(1.0, |s| f64::from(serial.wrapping_sub(s)) / fs);
        let fall = (1.0 - (-elapsed_s / RELEASE_S).exp()) as f32;
        let mut fresh = [FLOOR_DB; N];
        for post in [false, true] {
            self.spectrum_of(post);
            self.banded(fs, &mut fresh);
            let kept = if post { &mut self.post } else { &mut self.pre };
            for (k, f) in kept.iter_mut().zip(fresh.iter()) {
                *k = if *f > *k { *f } else { *k + (*f - *k) * fall };
            }
        }
        self.last_serial = Some(serial);
    }

    /// The input's level per band, dBFS.
    pub fn pre(&self) -> &[f32; N] {
        &self.pre
    }

    /// The output's level per band, dBFS.
    pub fn post(&self) -> &[f32; N] {
        &self.post
    }

    /// A ring's spectrum, as dBFS per bin, into `bins`.
    fn spectrum_of(&mut self, post: bool) {
        let ring = if post { &self.post_ring } else { &self.pre_ring };
        for ((x, s), w) in self.input.iter_mut().zip(ring.iter()).zip(self.window.iter()) {
            *x = s * w;
        }
        if self.fft.process_with_scratch(&mut self.input, &mut self.spectrum, &mut self.scratch).is_err() {
            return;
        }
        for (b, c) in self.bins.iter_mut().zip(self.spectrum.iter()) {
            *b = 20.0 * (c.norm() * self.norm).max(1e-9).log10();
        }
    }

    /// The bins, banded at the picture's frequencies, at the rate `fs`.
    fn banded(&self, fs: f64, out: &mut [f32; N]) {
        let df = fs / RING as f64;
        let last = self.bins.len() - 1;
        for (i, o) in out.iter_mut().enumerate() {
            let hz = self.hz[i];
            if hz >= 0.5 * fs {
                *o = FLOOR_DB;
                continue;
            }
            let lo = if i == 0 { hz } else { (self.hz[i - 1] * hz).sqrt() };
            let hi = if i + 1 == N { hz } else { (self.hz[i + 1] * hz).sqrt() };
            let (k0, k1) = ((lo / df).ceil() as usize, ((hi / df).floor() as usize).min(last));
            *o = if k1 >= k0 && k0 <= last {
                self.bins[k0..=k1].iter().copied().fold(FLOOR_DB, f32::max)
            } else {
                let k = hz / df;
                let (a, t) = ((k.floor() as usize).min(last), (k - k.floor()) as f32);
                let b = (a + 1).min(last);
                self.bins[a] + t * (self.bins[b] - self.bins[a])
            };
        }
    }
}
