/*
 * A radix-2 complex FFT, in place.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Ninety lines, no dependencies, and a naive DFT in the tests as the oracle.
 *
 * IT IS A COMPLEX FFT ON REAL INPUT, WHICH IS TWICE THE WORK A REAL FFT NEEDS,
 * and that is a deliberate trade. The real-input packing trick (pack N reals
 * into N/2 complex bins, then untangle with a half-length post-pass) is four
 * more expressions to get wrong, and the whole transform is ~15 us at N=1024
 * against an audio callback with milliseconds of slack. When that stops being
 * true the packing goes in behind the same `Fft::forward` and the tests do not
 * move.
 *
 * Everything the transform needs is precomputed in `new`: the twiddles and the
 * bit-reversal permutation. `forward` allocates nothing, which is what lets it
 * run on the audio thread.
 */

pub struct Fft {
    n: usize,
    /* cos/sin for k in 0..n/2 -- the twiddle e^(-2*pi*i*k/n). Stored as two
     * flat arrays rather than tuples so the inner loop reads sequentially. */
    cos: Vec<f32>,
    sin: Vec<f32>,
    /* rev[i] is i with its log2(n) bits reversed. A table because the loop
     * that computes it per element is the one place this file was wrong
     * first. */
    rev: Vec<u32>,
}

impl Fft {
    /// `n` must be a power of two, at least 2.
    pub fn new(n: usize) -> Self {
        assert!(n >= 2 && n.is_power_of_two(), "FFT size must be a power of two");

        let half = n / 2;
        let mut cos = Vec::with_capacity(half);
        let mut sin = Vec::with_capacity(half);
        for k in 0..half {
            let theta = -2.0 * core::f64::consts::PI * (k as f64) / (n as f64);
            cos.push(theta.cos() as f32);
            sin.push(theta.sin() as f32);
        }

        let bits = n.trailing_zeros();
        let mut rev = Vec::with_capacity(n);
        for i in 0..n {
            rev.push((i as u32).reverse_bits() >> (32 - bits));
        }

        Self { n, cos, sin, rev }
    }

    /// In-place forward transform. `re` and `im` must both be `size()` long.
    /// Allocates nothing: safe on the audio thread.
    pub fn forward(&self, re: &mut [f32], im: &mut [f32]) {
        debug_assert_eq!(re.len(), self.n);
        debug_assert_eq!(im.len(), self.n);

        /* Decimation in time: permute into bit-reversed order, then the
         * butterflies run over contiguous pairs. Only swap i < j, or every
         * pair is swapped twice and the permutation is the identity again --
         * which is a bug that looks like "the FFT returns the input". */
        for i in 0..self.n {
            let j = self.rev[i] as usize;
            if i < j {
                re.swap(i, j);
                im.swap(i, j);
            }
        }

        let mut len = 2;
        while len <= self.n {
            let half = len / 2;
            /* The twiddle table is indexed for the FULL length, so a stage of
             * length `len` strides through it by n/len. */
            let stride = self.n / len;
            let mut base = 0;
            while base < self.n {
                for k in 0..half {
                    let t = k * stride;
                    let (wr, wi) = (self.cos[t], self.sin[t]);
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
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The oracle: O(n^2), straight from the definition, in f64 so its own
    /// error is far below the tolerance we hold the f32 transform to.
    fn dft(re: &[f32], im: &[f32]) -> (Vec<f64>, Vec<f64>) {
        let n = re.len();
        let mut or = vec![0.0; n];
        let mut oi = vec![0.0; n];
        for k in 0..n {
            for t in 0..n {
                let theta = -2.0 * core::f64::consts::PI * (k as f64) * (t as f64) / (n as f64);
                let (s, c) = theta.sin_cos();
                or[k] += re[t] as f64 * c - im[t] as f64 * s;
                oi[k] += re[t] as f64 * s + im[t] as f64 * c;
            }
        }
        (or, oi)
    }

    #[test]
    fn agrees_with_a_naive_dft() {
        for n in [2usize, 8, 64, 256, 1024] {
            /* A deterministic pseudo-random signal: a fixed LCG, so a failure
             * is reproducible. Noise rather than a sine, because a sine
             * exercises exactly two bins and would pass with half the
             * butterflies wrong. */
            let mut seed = 0x2545_F491_4F6C_DD1Du64;
            let re: Vec<f32> = (0..n)
                .map(|_| {
                    seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
                    ((seed >> 33) as f32 / (1u64 << 31) as f32) - 1.0
                })
                .collect();
            let im = vec![0.0f32; n];

            let (er, ei) = dft(&re, &im);
            let (mut gr, mut gi) = (re.clone(), im.clone());
            Fft::new(n).forward(&mut gr, &mut gi);

            for k in 0..n {
                /* Tolerance scales with n: a radix-2 FFT accumulates log2(n)
                 * rounding steps over sums of n terms. */
                let tol = 1e-4 * n as f64;
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
        let fft = Fft::new(n);
        let mut re: Vec<f32> = (0..n)
            .map(|t| {
                (2.0 * core::f64::consts::PI * bin as f64 * t as f64 / n as f64).sin() as f32
            })
            .collect();
        let mut im = vec![0.0f32; n];
        fft.forward(&mut re, &mut im);

        let mag = |k: usize| (re[k] * re[k] + im[k] * im[k]).sqrt();
        /* An exact-bin sine has all its energy in k and n-k: amplitude 1.0
         * over two conjugate bins is n/2 each. */
        assert!((mag(bin) - n as f32 / 2.0).abs() < 1.0, "peak was {}", mag(bin));
        assert!(mag(bin - 1) < 1.0 && mag(bin + 1) < 1.0, "leaked into a neighbour");
        assert!(mag(0) < 1.0, "energy at DC");
    }
    /// Timings, not assertions: `cargo test --release -p spectro-core --lib
    /// -- --ignored --nocapture fft_timings`.
    #[test]
    #[ignore]
    fn fft_timings() {
        for n in [1024usize, 8192, 16384] {
            let fft = Fft::new(n);
            let src: Vec<f32> = (0..n).map(|i| ((i * 7919) % 1000) as f32 / 500.0 - 1.0).collect();
            let (mut re, mut im) = (vec![0.0f32; n], vec![0.0f32; n]);
            let iters = (50_000_000 / (n * n.trailing_zeros() as usize)).max(20);
            let mut sink = 0.0f32;
            let t = std::time::Instant::now();
            for _ in 0..iters {
                re.copy_from_slice(&src);
                im.fill(0.0);
                fft.forward(&mut re, &mut im);
                sink += re[1];
            }
            let us = t.elapsed().as_secs_f64() * 1e6 / iters as f64;
            println!("fft n={n:5}: {us:8.2} us ({iters} runs, {sink:.1})");
        }
    }
}
