// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
What the gate SOUNDS like, asserted: the measurements of the Trance Gate's DSP
review (and of the paper "The Trance Gate in Practice", section 4) turned into
limits, so a change that brings a click, an alias floor or a dependence on the
host's block size back fails here rather than in somebody's mix.

Every render is a gain: DC in, the gate's gain out, both channels equal.
*/

use realfft::RealFftPlanner;
use tg_core::params::Param;
use tg_core::{Instance, Transport, EDGE_MIN_MS};

/* ------------------------------------------------------------- harness */

/// A host that knows its timeline to the sample.
#[derive(Clone, Copy)]
struct Host {
    fs: f64,
    bpm: f64,
    /// The timeline loops back to 0 here, in beats.
    loop_beats: Option<f64>,
}

impl Host {
    fn new(fs: f64, bpm: f64) -> Self {
        Self { fs, bpm, loop_beats: None }
    }

    fn at(&self, n: usize) -> Transport {
        let b = n as f64 * self.bpm / (60.0 * self.fs);
        let beats = match self.loop_beats {
            Some(l) => b.rem_euclid(l),
            None => b,
        };
        Transport { running: true, beats, bpm: self.bpm as f32 }
    }
}

/// `n` samples of gain, the host reporting at the start of every block;
/// `edit` runs before each block with the sample it starts at.
fn render(inst: &mut Instance, host: Host, mut block: impl FnMut() -> usize, n: usize, mut edit: impl FnMut(&mut Instance, usize)) -> Vec<f32> {
    let mut out = Vec::with_capacity(n);
    let (mut l, mut r) = (vec![0f32; 8192], vec![0f32; 8192]);
    while out.len() < n {
        let at = out.len();
        let frames = block().clamp(1, 8192).min(n - at);
        edit(inst, at);
        l[..frames].fill(1.0);
        r[..frames].fill(1.0);
        inst.process_f32_split(&mut l[..frames], &mut r[..frames], frames, Some(&host.at(at)));
        out.extend_from_slice(&l[..frames]);
    }
    out
}

fn steps(inst: &mut Instance, host: Host, n: usize) -> Vec<f32> {
    render(inst, host, || 128, n, |_, _| {})
}

/// The largest change in gain from one sample to the next, after `skip`.
fn max_jump(g: &[f32], skip: usize) -> f32 {
    g.windows(2).skip(skip).map(|w| (w[1] - w[0]).abs()).fold(0.0, f32::max)
}

/// The steepest a stage of length `ms` may move the gain per sample, with
/// the steepest curve: the exponential's and the S-curve's slope peaks at
/// 3 / (1 - e^-3) = 3.16 times a straight line's.
fn slope_bound(ms: f64, fs: f64) -> f32 {
    (3.2 / (ms * 1e-3 * fs)) as f32
}

/* ---------------------------------------------------------------- clicks */

/*
 * NO REACHABLE SETTING CLICKS. Every stage at 0 %, a step at level 0, the
 * fastest rate, Width at its least, every curve: the gain never moves faster
 * than the steepest 1 ms edge allows. Before the edge floor, 0 % stages and a
 * level-0 step each moved it by 1.0 in one sample.
 */
#[test]
fn no_setting_moves_the_gain_faster_than_the_shortest_edge() {
    let fs = 48_000.0;
    let host = Host::new(fs, 174.0);
    let bound = slope_bound(EDGE_MIN_MS, fs);
    for curve in [0.0, 1.0, 2.0] {
        for (rate, width) in [(7.0, 1.0), (11.0, 1.0), (12.0, 0.05), (9.0, 0.3)] {
            for stages in [0.0, 1.6, 50.0] {
                let mut p = Instance::new(fs);
                p.set_num(Param::Curve, curve);
                p.set_num(Param::Rate, rate);
                p.set_num(Param::Hold, width);
                p.set_num(Param::Attack, stages);
                p.set_num(Param::Decay, stages);
                p.set_num(Param::Release, stages);
                p.set_num(Param::Sustain, 0.4);
                p.set_param("pattern", "B5B5"); // on, off, on-on runs and gaps
                p.set_param("cursor", "2");
                p.set_param("step_amount", "0"); // a step at level 0
                p.set_param("cursor", "5");
                p.set_param("step_amount", "0.3");
                let g = steps(&mut p, host, (2.0 * fs) as usize);
                let jump = max_jump(&g, 256);
                assert!(
                    jump <= bound,
                    "curve {curve}, rate {rate}, width {width}, stages {stages} %: {jump} > {bound}"
                );
            }
        }
    }
}

/*
 * A STEP AT LEVEL 0 IS A GAP: the gate releases into it and attacks out of
 * it, under Join Neighbors too -- where a silent ON step used to count as
 * the "previous ON step" and keep the next one from opening.
 */
#[test]
fn a_step_at_level_0_closes_the_gate_like_a_gap_and_the_next_step_opens() {
    let fs = 48_000.0;
    let host = Host::new(fs, 120.0);
    let step = (0.25 * 60.0 / 120.0 * fs) as usize; // 6000 samples
    for legato in [0.0, 1.0] {
        let mut p = Instance::new(fs);
        p.set_num(Param::Legato, legato);
        p.set_param("pattern", "FFFF");
        p.set_param("cursor", "2");
        p.set_param("step_amount", "0");
        let g = steps(&mut p, host, 8 * step);
        /* Late in step 2 the gate is shut; late in step 3 it is open again. */
        assert!(g[2 * step + step * 3 / 4] < 1e-3, "legato {legato}: step 2 not silent");
        assert!(g[3 * step + step * 3 / 4] > 0.999, "legato {legato}: step 3 did not open");
        assert!(max_jump(&g, 256) <= slope_bound(EDGE_MIN_MS, fs));
    }
}

/* ----------------------------------------------- timing and invariance */

/*
 * THE SAME TRANSPORT GIVES THE SAME GAIN, WHATEVER THE HOST'S BLOCK SIZE.
 * Stage starts used to land on the first whole sample past a boundary, and
 * where a boundary fell exactly on a sample the block size decided which:
 * 1.6e-2 of gain between blocks of 128 and 4096 at 140 bpm.
 */
#[test]
fn the_gain_does_not_depend_on_the_block_size() {
    let fs = 48_000.0;
    let host = Host::new(fs, 140.0);
    let n = (6.0 * fs) as usize;
    let base = render(&mut Instance::new(fs), host, || 128, n, |_, _| {});
    let mut seed = 0x2545_F491_4F6C_DD1Du64;
    let mut random = move || {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        1 + (seed % 8192) as usize
    };
    for (name, g) in [
        ("1", render(&mut Instance::new(fs), host, || 1, n, |_, _| {})),
        ("7", render(&mut Instance::new(fs), host, || 7, n, |_, _| {})),
        ("512", render(&mut Instance::new(fs), host, || 512, n, |_, _| {})),
        ("4096", render(&mut Instance::new(fs), host, || 4096, n, |_, _| {})),
        ("8192", render(&mut Instance::new(fs), host, || 8192, n, |_, _| {})),
        ("random", render(&mut Instance::new(fs), host, &mut random, n, |_, _| {})),
    ] {
        let worst = g.iter().zip(&base).map(|(a, b)| (a - b).abs()).fold(0.0, f32::max);
        assert!(worst < 1e-4, "blocks of {name}: {worst}");
    }
}

/*
 * THE ENVELOPE IS THE SAME IN MILLISECONDS AT EVERY SAMPLE RATE, to well
 * within a sample: 10-90 % of the default attack and 90-10 % of its release.
 */
#[test]
fn stage_times_do_not_depend_on_the_sample_rate() {
    let crossing = |g: &[f32], level: f32, up: bool, after: f64| {
        g.windows(2)
            .enumerate()
            .filter(|(n, _)| *n as f64 > after)
            .find(|(_, w)| if up { w[0] < level && w[1] >= level } else { w[0] > level && w[1] <= level })
            .map(|(n, w)| n as f64 + ((level - w[0]) / (w[1] - w[0])) as f64)
            .unwrap()
    };
    let mut times = Vec::new();
    for fs in [44_100.0, 48_000.0, 88_200.0, 96_000.0, 176_400.0, 192_000.0] {
        let g = steps(&mut Instance::new(fs), Host::new(fs, 140.0), fs as usize);
        let start = 0.2 * fs;
        let r10 = crossing(&g, 0.1, true, start);
        let r90 = crossing(&g, 0.9, true, r10);
        let f90 = crossing(&g, 0.9, false, r90);
        let f10 = crossing(&g, 0.1, false, f90);
        times.push(((r90 - r10) / fs * 1e3, (f10 - f90) / fs * 1e3, fs));
    }
    let (a0, r0, _) = times[0];
    for &(a, r, fs) in &times {
        /* A tenth of a sample at the lowest rate. */
        assert!((a - a0).abs() < 0.0023 && (r - r0).abs() < 0.0023, "{fs}: attack {a} ms, release {r} ms vs {a0}, {r0}");
    }
}

/*
 * A LOOP OR SEEK INSIDE A BLOCK IS PICKED UP AT THE NEXT BLOCK -- the engine
 * is told the position once a block -- and the gate carries on from where its
 * gain is: no sample-to-sample jump, whatever the block size.
 */
#[test]
fn a_loop_never_clicks() {
    let fs = 48_000.0;
    let mut host = Host::new(fs, 140.0);
    host.loop_beats = Some(3.75); // 15 sixteenths: the pattern's phase flips at each loop
    for block in [64, 512, 4096] {
        let g = render(&mut Instance::new(fs), host, || block, (10.0 * fs) as usize, |_, _| {});
        assert!(max_jump(&g, 256) <= slope_bound(EDGE_MIN_MS, fs), "blocks of {block}");
    }
}

/* ------------------------------------------------------------ robustness */

/*
 * A BAD SAMPLE IN, A BAD SAMPLE OUT -- AND NOTHING ELSE. NaN, infinity and
 * denormals in the audio pass through the gain for the samples that carry
 * them and leave no trace: no audio reaches the engine's state.
 */
#[test]
fn bad_input_samples_do_not_reach_the_state() {
    let fs = 48_000.0;
    let host = Host::new(fs, 140.0);
    let clean = render(&mut Instance::new(fs), host, || 128, 96_000, |_, _| {});
    for bad in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 1e-40] {
        let mut p = Instance::new(fs);
        let mut out = Vec::new();
        let (mut l, mut r) = (vec![0f32; 128], vec![0f32; 128]);
        for b in 0..96_000 / 128 {
            let poison = b == 300;
            l.fill(if poison { bad } else { 1.0 });
            r.fill(if poison { bad } else { 1.0 });
            p.process_f32_split(&mut l, &mut r, 128, Some(&host.at(b * 128)));
            out.extend_from_slice(&l);
        }
        let after = &out[301 * 128..];
        assert!(after.iter().zip(&clean[301 * 128..]).all(|(a, b)| a.to_bits() == b.to_bits()), "{bad}");
    }
}

/* -------------------------------------------------------------- aliasing */

/*
 * THE ALIAS FLOOR, COUNTED. The paper's harness: 2^17 samples hold exactly 12
 * cycles of an alternating 1/16 gate (131.8359375 bpm puts a step at 5461 1/3
 * samples, so edges fall between samples) and the carrier sits on a bin
 * congruent to 1 mod 12. Every true component of the gated sine lands on a
 * bin congruent to +-1 mod 12, every folded one elsewhere, so no window is
 * needed and nothing is estimated. Sample-quantised stage starts held the
 * default gate at 67.8 dB; the same edges placed exactly reach 96.8 dB.
 */
#[test]
fn the_default_gate_aliases_less_than_its_edges_allow() {
    const N: usize = 1 << 17;
    const Q: usize = 12;
    let fs = 48_000.0;
    let bpm = Q as f64 * 30.0 * fs / N as f64;
    let g = steps(&mut Instance::new(fs), Host::new(fs, bpm), N + fs as usize);
    let g = &g[fs as usize..];
    for hz in [1_000.0, 10_000.0] {
        let k = (hz * N as f64 / fs).round() as usize;
        let c = k - k % Q + 1;
        let mut x: Vec<f64> = g
            .iter()
            .enumerate()
            .map(|(n, &v)| v as f64 * (2.0 * core::f64::consts::PI * ((c * n) % N) as f64 / N as f64).cos())
            .collect();
        let fft = RealFftPlanner::<f64>::new().plan_fft_forward(N);
        let mut spec = fft.make_output_vec();
        fft.process(&mut x, &mut spec).unwrap();
        let (mut signal, mut alias) = (0.0, 0.0);
        for (bin, z) in spec.iter().enumerate() {
            if bin % Q == c % Q || (bin + c).is_multiple_of(Q) {
                signal += z.norm_sqr();
            } else {
                alias += z.norm_sqr();
            }
        }
        let asr = 10.0 * (signal / alias).log10();
        assert!(asr > 90.0, "{hz} Hz carrier: signal-to-alias {asr:.1} dB");
    }
}
