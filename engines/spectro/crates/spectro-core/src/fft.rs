// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The real-input FFT: realfft's, planned once.
 *
 * realfft packs N real samples as N/2 complex ones, runs rustfft's complex
 * transform on them and untangles the result into bins 0..=N/2 -- half the
 * work of a complex FFT on the same input, and the same shape as the
 * hand-written transform it replaced (docs/adr/0003-established-rust-crates.md).
 * A naive DFT is still the oracle it answers to, in the tests below.
 *
 * PLANNED IN `new`, NEVER ON THE AUDIO THREAD. Planning picks the algorithm
 * for this length -- the SIMD one when the CPU has it -- and builds its
 * twiddle tables, which allocates. `forward` only runs the plan, through
 * buffers the plan itself sized here, and allocates nothing
 * (tests/no_alloc.rs).
 */

use std::sync::Arc;

use realfft::num_complex::Complex32;
use realfft::{RealFftPlanner, RealToComplex};

pub struct Fft {
    plan: Arc<dyn RealToComplex<f32>>,
    /// The frame being transformed. realfft uses its input as scratch, and the
    /// caller's frame is not ours to overwrite, so it is copied in here.
    input: Box<[f32]>,
    /// Bins 0..=n/2.
    spectrum: Box<[Complex32]>,
    scratch: Box<[Complex32]>,
}

impl Fft {
    /// `n` must be a power of two, at least 2: the sizes `Config` produces,
    /// and the ones whose cost per transform is the bounded one lib.rs states.
    pub fn new(n: usize) -> Self {
        assert!(n >= 2 && n.is_power_of_two(), "FFT size must be a power of two");
        let plan = RealFftPlanner::<f32>::new().plan_fft_forward(n);
        Self {
            input: plan.make_input_vec().into(),
            spectrum: plan.make_output_vec().into(),
            scratch: plan.make_scratch_vec().into(),
            plan,
        }
    }

    /// Bins 0..=n/2 of the DFT of `input` (n samples) into `re` and `im`
    /// (n/2 + 1 each). Allocates nothing.
    pub fn forward(&mut self, input: &[f32], re: &mut [f32], im: &mut [f32]) {
        let bins = self.spectrum.len();
        assert!(input.len() == self.input.len() && re.len() == bins && im.len() == bins);
        self.input.copy_from_slice(input);
        /* It fails only on a buffer of the wrong length, and every one of them
         * was sized by the plan itself, in `new`. */
        let ran = self.plan.process_with_scratch(&mut self.input, &mut self.spectrum, &mut self.scratch);
        debug_assert!(ran.is_ok(), "a buffer sized by its own plan was refused");
        for ((r, i), bin) in re.iter_mut().zip(im.iter_mut()).zip(self.spectrum.iter()) {
            *r = bin.re;
            *i = bin.im;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The oracle: O(n^2), straight from the definition, in f64 so its own
    /// error is far below the tolerance we hold the f32 transform to.
    fn dft(x: &[f32]) -> (Vec<f64>, Vec<f64>) {
        let n = x.len();
        let mut or = vec![0.0; n / 2 + 1];
        let mut oi = vec![0.0; n / 2 + 1];
        for k in 0..=n / 2 {
            for (t, &v) in x.iter().enumerate() {
                /* k*t mod n keeps the angle small, so f64 stays exact. */
                let theta = -2.0 * core::f64::consts::PI * ((k * t) % n) as f64 / n as f64;
                let (s, c) = theta.sin_cos();
                or[k] += v as f64 * c;
                oi[k] += v as f64 * s;
            }
        }
        (or, oi)
    }

    fn noise(n: usize) -> Vec<f32> {
        /* A fixed seed, so a failure is reproducible. Noise rather than a
         * sine, because a sine exercises two bins and would pass with half the
         * butterflies wrong. */
        let mut rng = fastrand::Rng::with_seed(0x2545_F491_4F6C_DD1D);
        (0..n).map(|_| rng.f32() * 2.0 - 1.0).collect()
    }

    #[test]
    fn agrees_with_a_naive_dft() {
        for n in [2usize, 4, 8, 16, 64, 256, 1024, 4096] {
            let x = noise(n);
            let (er, ei) = dft(&x);
            let (mut gr, mut gi) = (vec![0.0f32; n / 2 + 1], vec![0.0f32; n / 2 + 1]);
            Fft::new(n).forward(&x, &mut gr, &mut gi);

            /* Rounding grows with log2(n) steps over sums of n terms. */
            let tol = 1e-5 * n as f64;
            for k in 0..=n / 2 {
                assert!(
                    (gr[k] as f64 - er[k]).abs() < tol && (gi[k] as f64 - ei[k]).abs() < tol,
                    "n={n} bin {k}: got ({}, {}), want ({}, {})",
                    gr[k], gi[k], er[k], ei[k]
                );
            }
        }
    }

    #[test]
    fn a_sine_lands_on_its_own_bin() {
        let n = 1024;
        let bin = 64;
        let x: Vec<f32> = (0..n)
            .map(|t| (2.0 * core::f64::consts::PI * bin as f64 * t as f64 / n as f64).sin() as f32)
            .collect();
        let (mut re, mut im) = (vec![0.0f32; n / 2 + 1], vec![0.0f32; n / 2 + 1]);
        Fft::new(n).forward(&x, &mut re, &mut im);

        let mag = |k: usize| (re[k] * re[k] + im[k] * im[k]).sqrt();
        /* Amplitude 1.0 over two conjugate bins is n/2 in each. */
        assert!((mag(bin) - n as f32 / 2.0).abs() < 1.0, "peak was {}", mag(bin));
        assert!(mag(bin - 1) < 1.0 && mag(bin + 1) < 1.0, "leaked into a neighbour");
        assert!(mag(0) < 1.0, "energy at DC");
    }

    #[test]
    fn dc_and_nyquist_are_real() {
        let n = 16;
        let x: Vec<f32> = (0..n).map(|t| 1.0 + if t % 2 == 0 { 0.5 } else { -0.5 }).collect();
        let (mut re, mut im) = (vec![9.0f32; n / 2 + 1], vec![9.0f32; n / 2 + 1]);
        Fft::new(n).forward(&x, &mut re, &mut im);
        assert!((re[0] - 16.0).abs() < 1e-5 && im[0] == 0.0);
        assert!((re[n / 2] - 8.0).abs() < 1e-5 && im[n / 2] == 0.0);
        for k in 1..n / 2 {
            assert!(re[k].abs() < 1e-5 && im[k].abs() < 1e-5, "bin {k}");
        }
    }

    /// Timings, not assertions: `cargo test --release -p spectro-core --lib
    /// -- --ignored --nocapture fft_timings`. The best of several rounds, so
    /// a busy machine inflates the number less.
    #[test]
    #[ignore]
    fn fft_timings() {
        for n in [1024usize, 8192, 16384] {
            let mut fft = Fft::new(n);
            let src: Vec<f32> = (0..n).map(|i| ((i * 7919) % 1000) as f32 / 500.0 - 1.0).collect();
            let (mut re, mut im) = (vec![0.0f32; n / 2 + 1], vec![0.0f32; n / 2 + 1]);
            let iters = (5_000_000 / (n * n.trailing_zeros() as usize)).max(10);
            let mut best = f64::MAX;
            for _ in 0..15 {
                let t = std::time::Instant::now();
                for _ in 0..iters {
                    fft.forward(core::hint::black_box(&src), &mut re, &mut im);
                    core::hint::black_box(&re);
                }
                best = best.min(t.elapsed().as_secs_f64() * 1e6 / iters as f64);
            }
            println!("fft n={n:5}: {best:8.2} us (best of 15 x {iters})");
        }
    }
}
