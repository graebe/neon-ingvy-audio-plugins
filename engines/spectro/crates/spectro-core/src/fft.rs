/*
 * A real-input FFT: an N/2-point complex transform and a split pass.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * N real samples are packed as N/2 complex ones (even samples real, odd
 * imaginary), transformed, and untangled into bins 0..=N/2 -- half the work of
 * a complex FFT on the same input. The complex transform is radix-2, decimation
 * in time, on separate re/im arrays: the first two stages run as one radix-4
 * pass with trivial twiddles, and every later stage reads its own contiguous
 * twiddle table through iterators, which is what lets the compiler vectorise
 * the butterflies. No unsafe, no dependencies; a naive DFT is the oracle.
 *
 * Everything is precomputed in `new`; `forward` allocates nothing.
 */

pub struct Fft {
    /// Real input length, a power of two >= 2.
    n: usize,
    /// rev[i] is i with its log2(n/2) bits reversed.
    rev: Box<[u32]>,
    /// Twiddles e^(-2*pi*i*k/len) for k < len/2, stage after stage (len = 2,
    /// 4, .. n/2): the stage of length `len` starts at `len/2 - 1`.
    tw_re: Box<[f32]>,
    tw_im: Box<[f32]>,
    /// The split pass's twiddles e^(-2*pi*i*k/n) for k in 1..n/4.
    post_re: Box<[f32]>,
    post_im: Box<[f32]>,
}

fn twiddle(k: usize, len: usize) -> (f32, f32) {
    let theta = -2.0 * core::f64::consts::PI * (k as f64) / (len as f64);
    (theta.cos() as f32, theta.sin() as f32)
}

impl Fft {
    /// `n` must be a power of two, at least 2.
    pub fn new(n: usize) -> Self {
        assert!(n >= 2 && n.is_power_of_two(), "FFT size must be a power of two");
        let m = n / 2;
        let bits = m.trailing_zeros();
        let rev = (0..m as u32)
            .map(|i| if bits == 0 { 0 } else { i.reverse_bits() >> (32 - bits) })
            .collect();

        let (mut tw_re, mut tw_im) = (Vec::with_capacity(m), Vec::with_capacity(m));
        let mut len = 2;
        while len <= m {
            for k in 0..len / 2 {
                let (c, s) = twiddle(k, len);
                tw_re.push(c);
                tw_im.push(s);
            }
            len <<= 1;
        }
        let (post_re, post_im) = (1..(m / 2).max(1)).map(|k| twiddle(k, n)).unzip::<_, _, Vec<_>, Vec<_>>();

        Self {
            n,
            rev,
            tw_re: tw_re.into(),
            tw_im: tw_im.into(),
            post_re: post_re.into(),
            post_im: post_im.into(),
        }
    }

    /// Bins 0..=n/2 of the DFT of `input` (`size()` samples) into `re` and
    /// `im` (`size()/2 + 1` each). Allocates nothing.
    pub fn forward(&self, input: &[f32], re: &mut [f32], im: &mut [f32]) {
        let m = self.n / 2;
        assert!(input.len() == self.n && re.len() == m + 1 && im.len() == m + 1);
        let (zr, zi) = (&mut re[..m], &mut im[..m]);

        /* Pack and permute in one pass: z[j] = x[2j] + i*x[2j+1], written to
         * its bit-reversed slot by gathering from it. */
        for ((r, i), &j) in zr.iter_mut().zip(zi.iter_mut()).zip(self.rev.iter()) {
            let j = j as usize * 2;
            *r = input[j];
            *i = input[j + 1];
        }

        let mut len = 2;
        if m >= 4 {
            radix4_first(zr, zi);
            len = 8;
        }
        /* Two stages per pass while two remain, then a last single one. */
        while len * 2 <= m {
            let q = len / 2;
            radix4(zr, zi, q, self.stage(len), self.stage(len * 2));
            len <<= 2;
        }
        if len <= m {
            let (wr, wi) = self.stage(len);
            stage(zr, zi, len, wr, wi);
        }

        self.split(re, im);
    }

    /// The twiddles of the stage of length `len`.
    fn stage(&self, len: usize) -> (&[f32], &[f32]) {
        let half = len / 2;
        (&self.tw_re[half - 1..len - 1], &self.tw_im[half - 1..len - 1])
    }

    /*
     * THE SPLIT: Z = FFT(z) holds the even and odd samples' spectra tangled
     * together. With Zk = Z[k] and Zc = conj(Z[m-k]):
     *
     *     E = (Zk + Zc) / 2        the even samples' spectrum
     *     O = (Zk - Zc) / 2i       the odd samples'
     *     X[k]   = E + W^k O       W = e^(-2*pi*i/n)
     *     X[m-k] = conj(E - W^k O)
     *
     * so each pair (k, m-k) is untangled in place. DC and Nyquist come from
     * Z[0] alone, and the middle bin k = m/2 is conj(Z[m/2]).
     */
    fn split(&self, re: &mut [f32], im: &mut [f32]) {
        let m = self.n / 2;
        let (z0r, z0i) = (re[0], im[0]);
        re[0] = z0r + z0i;
        im[0] = 0.0;
        re[m] = z0r - z0i;
        im[m] = 0.0;
        if m < 2 {
            return;
        }
        im[m / 2] = -im[m / 2];

        let (lo_re, hi_re) = re[1..m].split_at_mut(m / 2 - 1);
        let (lo_im, hi_im) = im[1..m].split_at_mut(m / 2 - 1);
        /* hi starts with the middle bin, already done; the rest pairs with lo
         * back to front. */
        let (hi_re, hi_im) = (&mut hi_re[1..], &mut hi_im[1..]);
        for ((((ar, ai), (br, bi)), &wr), &wi) in lo_re
            .iter_mut()
            .zip(lo_im.iter_mut())
            .zip(hi_re.iter_mut().rev().zip(hi_im.iter_mut().rev()))
            .zip(self.post_re.iter())
            .zip(self.post_im.iter())
        {
            let (a, b, c, d) = (*ar, *ai, *br, *bi);
            let (er, ei) = (0.5 * (a + c), 0.5 * (b - d));
            let (or, oi) = (0.5 * (b + d), -0.5 * (a - c));
            let (tr, ti) = (wr * or - wi * oi, wr * oi + wi * or);
            *ar = er + tr;
            *ai = ei + ti;
            *br = er - tr;
            *bi = -(ei - ti);
        }
    }
}

/// Stages len=2 and len=4 at once. The twiddles are 1 and -i, so there are no
/// multiplies at all.
fn radix4_first(re: &mut [f32], im: &mut [f32]) {
    for (r, i) in re.chunks_exact_mut(4).zip(im.chunks_exact_mut(4)) {
        let (b0r, b0i) = (r[0] + r[1], i[0] + i[1]);
        let (b1r, b1i) = (r[0] - r[1], i[0] - i[1]);
        let (b2r, b2i) = (r[2] + r[3], i[2] + i[3]);
        let (b3r, b3i) = (r[2] - r[3], i[2] - i[3]);
        r[0] = b0r + b2r;
        i[0] = b0i + b2i;
        r[2] = b0r - b2r;
        i[2] = b0i - b2i;
        /* -i * b3 = (b3i, -b3r) */
        r[1] = b1r + b3i;
        i[1] = b1i - b3r;
        r[3] = b1r - b3i;
        i[3] = b1i + b3r;
    }
}

/*
 * Stages `len` and `2*len` in one pass (q = len/2). Per group of four points
 * a quarter-block apart:
 *
 *     b0, b1 = a0 +- w1*a1         stage len,   w1 = W_len^j
 *     b2, b3 = a2 +- w1*a3
 *     x0, x2 = b0 +- w2*b2         stage 2len,  w2 = W_2len^j
 *     x1, x3 = b1 +- (-i)*w2*b3    W_2len^(j+q) = -i * W_2len^j
 *
 * Three complex multiplies where two radix-2 passes take four, and one trip
 * through memory instead of two.
 */
fn radix4(re: &mut [f32], im: &mut [f32], q: usize, s1: (&[f32], &[f32]), s2: (&[f32], &[f32])) {
    let (w1r, w1i) = (&s1.0[..q], &s1.1[..q]);
    let (w2r, w2i) = (&s2.0[..q], &s2.1[..q]);
    for (r, i) in re.chunks_exact_mut(4 * q).zip(im.chunks_exact_mut(4 * q)) {
        let (r01, r23) = r.split_at_mut(2 * q);
        let (r0, r1) = r01.split_at_mut(q);
        let (r2, r3) = r23.split_at_mut(q);
        let (i01, i23) = i.split_at_mut(2 * q);
        let (i0, i1) = i01.split_at_mut(q);
        let (i2, i3) = i23.split_at_mut(q);
        for j in 0..q {
            let (wr, wi) = (w1r[j], w1i[j]);
            let (t1r, t1i) = (r1[j] * wr - i1[j] * wi, r1[j] * wi + i1[j] * wr);
            let (t3r, t3i) = (r3[j] * wr - i3[j] * wi, r3[j] * wi + i3[j] * wr);
            let (b0r, b0i) = (r0[j] + t1r, i0[j] + t1i);
            let (b1r, b1i) = (r0[j] - t1r, i0[j] - t1i);
            let (b2r, b2i) = (r2[j] + t3r, i2[j] + t3i);
            let (b3r, b3i) = (r2[j] - t3r, i2[j] - t3i);

            let (wr, wi) = (w2r[j], w2i[j]);
            let (u2r, u2i) = (b2r * wr - b2i * wi, b2r * wi + b2i * wr);
            /* (-i) * (w2 * b3) = (im, -re) of the product. */
            let (v3r, v3i) = (b3r * wr - b3i * wi, b3r * wi + b3i * wr);
            let (u3r, u3i) = (v3i, -v3r);

            r0[j] = b0r + u2r;
            i0[j] = b0i + u2i;
            r2[j] = b0r - u2r;
            i2[j] = b0i - u2i;
            r1[j] = b1r + u3r;
            i1[j] = b1i + u3i;
            r3[j] = b1r - u3r;
            i3[j] = b1i - u3i;
        }
    }
}

/// One radix-2 stage of length `len`, with that stage's own twiddles.
fn stage(re: &mut [f32], im: &mut [f32], len: usize, wr: &[f32], wi: &[f32]) {
    let half = len / 2;
    for (r, i) in re.chunks_exact_mut(len).zip(im.chunks_exact_mut(len)) {
        let (ar, br) = r.split_at_mut(half);
        let (ai, bi) = i.split_at_mut(half);
        for ((((ar, ai), (br, bi)), &wr), &wi) in ar
            .iter_mut()
            .zip(ai.iter_mut())
            .zip(br.iter_mut().zip(bi.iter_mut()))
            .zip(wr)
            .zip(wi)
        {
            let xr = *br * wr - *bi * wi;
            let xi = *br * wi + *bi * wr;
            *br = *ar - xr;
            *bi = *ai - xi;
            *ar += xr;
            *ai += xi;
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
        /* A fixed LCG, so a failure is reproducible. Noise rather than a sine,
         * because a sine exercises two bins and would pass with half the
         * butterflies wrong. */
        let mut seed = 0x2545_F491_4F6C_DD1Du64;
        (0..n)
            .map(|_| {
                seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
                ((seed >> 33) as f32 / (1u64 << 31) as f32) - 1.0
            })
            .collect()
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
            let fft = Fft::new(n);
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
