// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The engine's claims, measured on what it renders: the identity at Vocal 0,
 * the vowel on white noise in both modes, the morph moving the envelope
 * evenly, the switch between modes, the panic, and the doors.
 */

use super::*;
use crate::model::{VowelTable, VOWELS};
use realfft::RealFftPlanner;

const FS: f64 = 48_000.0;

/// The Mode values this build runs: the Move's has no Spectral engine.
const MODES: &[f64] = if SPECTRAL { &[0.0, 1.0] } else { &[0.0] };

fn noise(n: usize, seed: u64) -> Vec<f32> {
    let mut rng = fastrand::Rng::with_seed(seed);
    (0..n).map(|_| rng.f32() - 0.5).collect()
}

/// Render `left` (and `right`) through `e` in blocks of 512.
fn render(e: &mut Engine, left: &[f32], right: Option<&[f32]>) -> (Vec<f32>, Vec<f32>) {
    let mut l = left.to_vec();
    let mut r = right.map(<[f32]>::to_vec);
    for (i, block) in l.chunks_mut(512).enumerate() {
        let rb = r.as_mut().map(|r| &mut r[i * 512..i * 512 + block.len()]);
        e.process_f32(block, rb);
    }
    (l, r.unwrap_or_default())
}

/// An engine with these settings, every glide landed.
fn engine(settings: &[(Param, f64)]) -> Engine {
    let mut e = Engine::new(FS);
    for &(p, v) in settings {
        e.set_num(p, v);
    }
    render(&mut e, &vec![0.0; FS as usize / 2], None);
    e
}

/// The averaged power spectrum of `x` (Hann, 4096 points, half overlap), as
/// dB per bin.
fn spectrum(x: &[f32]) -> Vec<f64> {
    let n = 4096;
    let plan = RealFftPlanner::<f64>::new().plan_fft_forward(n);
    let mut sum = vec![0.0f64; n / 2 + 1];
    let mut out = plan.make_output_vec();
    let mut start = 0;
    while start + n <= x.len() {
        let mut frame: Vec<f64> = (0..n)
            .map(|i| f64::from(x[start + i]) * (0.5 - 0.5 * (core::f64::consts::TAU * i as f64 / n as f64).cos()))
            .collect();
        plan.process(&mut frame, &mut out).unwrap();
        for (s, o) in sum.iter_mut().zip(&out) {
            *s += o.norm_sqr();
        }
        start += n / 2;
    }
    sum.iter().map(|p| 10.0 * (p + 1e-30).log10()).collect()
}

fn bin_of(hz: f64) -> usize {
    (hz / FS * 4096.0).round() as usize
}

/// The frequency of the highest bin within ±25 % of `hz`, smoothed over
/// five bins so the noise's grain does not pick the peak.
fn peak_near(db: &[f64], hz: f64) -> f64 {
    let smooth = |k: usize| (k - 2..=k + 2).map(|j| db[j]).sum::<f64>() / 5.0;
    let (lo, hi) = (bin_of(hz * 0.75), bin_of(hz * 1.25));
    let k = (lo..=hi).max_by(|&a, &b| smooth(a).total_cmp(&smooth(b))).unwrap();
    k as f64 * FS / 4096.0
}

#[test]
fn at_vocal_zero_the_time_engine_is_the_input_bit_for_bit() {
    /* Turned down from 100 % while a sound plays: the identity as soon as
     * the glide lands, whatever the level follower held. */
    let mut e = engine(&[(Param::Vocal, 100.0)]);
    render(&mut e, &noise(4800, 19), None);
    e.set_num(Param::Vocal, 0.0);
    render(&mut e, &noise(12_000, 20), None);
    let (l, r) = (noise(20_000, 1), noise(20_000, 2));
    let (ol, or) = render(&mut e, &l, Some(&r));
    assert_eq!(ol, l);
    assert_eq!(or, r);
    /* And mono. */
    let (om, _) = render(&mut e, &l, None);
    assert_eq!(om, l);
}

#[cfg(feature = "spectral")]
#[test]
fn at_vocal_zero_the_spectral_engine_is_the_input_one_frame_late() {
    let mut e = engine(&[(Param::Vocal, 0.0), (Param::Mode, 1.0)]);
    assert_eq!(e.mode(), Mode::Spectral);
    let n = e.latency();
    assert_eq!(n, 1024);
    let x = noise(30_000, 3);
    let (y, _) = render(&mut e, &x, None);
    let worst = (n..x.len()).map(|i| f64::from((y[i] - x[i - n]).abs())).fold(0.0, f64::max);
    /* f32 out: the transform's error is far below the float's own step. */
    assert!(worst < 1e-6, "{worst}");
}

#[test]
fn both_modes_put_the_vowel_on_white_noise() {
    let x = noise(FS as usize, 4);
    for &mode in MODES {
        for (vowel, row) in VOWELS.iter().enumerate() {
            for size in [-100.0, 0.0, 100.0] {
                let mut e = engine(&[
                    (Param::Mode, mode),
                    (Param::Vocal, 100.0),
                    (Param::Vowel, vowel as f64),
                    (Param::Size, size),
                ]);
                let (y, _) = render(&mut e, &x, None);
                let db = spectrum(&y[4096..]);
                let scale = 1.25f64.powf(-size / 100.0);
                for (k, f) in row.iter().take(2).enumerate() {
                    let want = f * scale;
                    let got = peak_near(&db, want);
                    assert!(
                        (got / want - 1.0).abs() < 0.05,
                        "mode {mode} vowel {} size {size}: F{} at {got:.0} Hz, not {want:.0}",
                        VOWEL_LABELS[vowel],
                        k + 1
                    );
                }
            }
        }
    }
}

#[test]
fn vocal_moves_the_sound_towards_the_vowel_step_by_step() {
    /* A coloured input: noise through one strong resonance at 3 kHz, where
     * the vowel A has none. The distance between the output's envelope and
     * the vowel's, in dB, shrinks with every step of Vocal. */
    let mut y = [0.0f64; 2];
    let pole = Pole::new(3_000.0, 80.0, FS, 1.0);
    let x: Vec<f32> = noise(FS as usize, 5).iter().map(|&s| pole.run(f64::from(s), &mut y) as f32).collect();
    let vowel = Formants::at(0.0, 0.0, 50.0).poles(FS, 1.0);
    let bands: Vec<f64> = (0..40).map(|i| 200.0 * 20f64.powf(i as f64 / 40.0)).collect();
    for &mode in MODES {
        let mut last = f64::MAX;
        for vocal in [0.0, 25.0, 50.0, 75.0, 100.0] {
            let mut e = engine(&[(Param::Mode, mode), (Param::Vocal, vocal)]);
            let (out, _) = render(&mut e, &x, None);
            let db = spectrum(&out[4096..]);
            let diff: Vec<f64> = bands
                .iter()
                .map(|&f| {
                    let k = bin_of(f);
                    let measured = (k - 3..=k + 3).map(|j| db[j]).sum::<f64>() / 7.0;
                    measured - model::ln_gain(&vowel, f, FS) * 20.0 / core::f64::consts::LN_10
                })
                .collect();
            let mean = diff.iter().sum::<f64>() / diff.len() as f64;
            let spread = (diff.iter().map(|d| (d - mean).powi(2)).sum::<f64>() / diff.len() as f64).sqrt();
            assert!(spread < last, "mode {mode}: {vocal} % is {spread:.2} dB from the vowel, after {last:.2}");
            last = spread;
        }
        assert!(last < 3.0, "mode {mode}: {last:.2} dB from the vowel at 100 %");
    }
}

#[test]
fn the_level_stays_the_inputs() {
    let x = noise(FS as usize, 6);
    let rms = |s: &[f32]| (s.iter().map(|v| f64::from(*v).powi(2)).sum::<f64>() / s.len() as f64).sqrt();
    for &mode in MODES {
        for vowel in [0.0, 2.0, 4.0] {
            let mut e = engine(&[(Param::Mode, mode), (Param::Vocal, 100.0), (Param::Vowel, vowel)]);
            let (y, _) = render(&mut e, &x, None);
            let db = 20.0 * (rms(&y[8192..]) / rms(&x[8192..])).log10();
            assert!(db.abs() < 1.5, "mode {mode} vowel {vowel}: {db:.2} dB");
        }
    }
}

#[cfg(feature = "spectral")]
#[test]
fn a_mode_switch_dips_lands_and_reports_its_latency() {
    let mut e = engine(&[(Param::Vocal, 0.0)]);
    assert_eq!((e.mode(), e.latency()), (Mode::Time, 0));
    assert_eq!(latency(Mode::Spectral, FS), 1024);
    assert_eq!(latency(Mode::Time, FS), 0);
    assert_eq!(latency(Mode::Spectral, f64::NAN), 1024);

    /* A steady tone through the switch: the output falls to nothing and
     * comes back, and never jumps. */
    e.set_num(Param::Mode, 1.0);
    let x: Vec<f32> = (0..4800).map(|i| (0.5 * (i as f64 * 0.03).sin()) as f32).collect();
    let (y, _) = render(&mut e, &x, None);
    assert_eq!((e.mode(), e.latency()), (Mode::Spectral, 1024));
    let quiet = y[470..490].iter().map(|v| v.abs()).fold(0.0f32, f32::max);
    assert!(quiet < 0.02, "the dip's floor: {quiet}");
    let jump = y.windows(2).map(|w| (w[1] - w[0]).abs()).fold(0.0f32, f32::max);
    assert!(jump < 0.05, "a jump of {jump}");
    /* After the dip: the input, one frame late. */
    for i in 2000..4800 {
        assert!((y[i] - x[i - 1024]).abs() < 1e-5, "{i}");
    }
    /* And back. */
    e.set_num(Param::Mode, 0.0);
    let (z, _) = render(&mut e, &x, None);
    assert_eq!((e.mode(), e.latency()), (Mode::Time, 0));
    assert_eq!(&z[2000..], &x[2000..]);
}

#[test]
fn a_panic_lets_everything_go() {
    for &mode in MODES {
        let mut e = engine(&[(Param::Mode, mode), (Param::Vocal, 100.0), (Param::Resonance, 100.0)]);
        render(&mut e, &noise(10_000, 7), None);
        e.on_midi(&[0xB3, 120, 0]);
        let (y, _) = render(&mut e, &vec![0.0; 4096], None);
        assert!(y.iter().all(|&s| s == 0.0), "mode {mode}: CC 120 left a tail");
        render(&mut e, &noise(10_000, 8), None);
        e.on_midi(&[0xB0, 123, 0]);
        let (y, _) = render(&mut e, &vec![0.0; 4096], None);
        assert!(y.iter().all(|&s| s == 0.0), "mode {mode}: CC 123 left a tail");
        render(&mut e, &noise(10_000, 9), None);
        assert!(e.set_param("panic", "1"));
        let (y, _) = render(&mut e, &vec![0.0; 4096], None);
        assert!(y.iter().all(|&s| s == 0.0), "mode {mode}: the panic key left a tail");
    }
    /* Anything else is not a panic. */
    let mut e = engine(&[]);
    render(&mut e, &noise(10_000, 10), None);
    e.on_midi(&[0xB0, 64, 127]);
    e.on_midi(&[0xB0, 120]);
    e.on_midi(&[]);
    let (y, _) = render(&mut e, &vec![0.0; 64], None);
    assert!(y.iter().any(|&s| s != 0.0));
}

#[cfg(feature = "spectral")]
#[test]
fn a_switch_under_way_lands_on_a_panic() {
    let mut e = engine(&[]);
    e.set_num(Param::Mode, 1.0);
    render(&mut e, &vec![0.1; 100], None);
    assert_eq!(e.mode(), Mode::Time);
    e.reset();
    assert_eq!((e.mode(), e.latency()), (Mode::Spectral, 1024));
}

#[test]
fn nonsense_in_is_silence_not_nonsense_out() {
    for &mode in MODES {
        let mut e = engine(&[(Param::Mode, mode), (Param::Vocal, 100.0), (Param::Excite, 100.0)]);
        let mut x = noise(8192, 11);
        x[100] = f32::NAN;
        x[2000] = f32::INFINITY;
        x[3000] = f32::NEG_INFINITY;
        let (y, _) = render(&mut e, &x, None);
        assert!(y.iter().all(|s| s.is_finite()), "mode {mode}");
        let (z, _) = render(&mut e, &vec![1e-30; 8192], None);
        assert!(z.iter().all(|s| s.is_finite()));
    }
}

#[test]
fn the_doors_read_and_write_the_same_values() {
    let mut e = Engine::new(FS);
    let mut out = [0u8; 64];
    let get = |e: &Engine, k: &str, out: &mut [u8; 64]| {
        let n = e.get_param(k, out);
        (n >= 0).then(|| String::from_utf8(out[..n as usize].to_vec()).unwrap())
    };
    assert!(e.set_param("vowel", "E>I 30 %"));
    assert_eq!(get(&e, "vowel", &mut out).as_deref(), Some("1.3"));
    assert!(e.set_param("mode", "Spectral"));
    assert_eq!(e.num(Param::Mode), 1.0);
    assert!(e.set_param("output", "-6 dB"));
    assert_eq!(get(&e, "output", &mut out).as_deref(), Some("-6"));
    /* Text with no value in it changes nothing, but the key is the engine's. */
    assert!(e.set_param("vocal", "loud"));
    assert_eq!(e.num(Param::Vocal), 80.0);
    assert!(!e.set_param("nope", "1"));
    assert_eq!(get(&e, "nope", &mut out), None);
    e.set_num(Param::Size, f64::NAN);
    assert_eq!(e.num(Param::Size), 0.0);
    e.set_num(Param::Size, 1e9);
    assert_eq!(e.num(Param::Size), 100.0);
}

#[test]
fn the_move_s_path_renders_what_the_plugin_s_does() {
    let settings = [(Param::Vocal, 90.0), (Param::Vowel, 2.5), (Param::Excite, 30.0)];
    let mut a = engine(&settings);
    let mut b = engine(&settings);
    /* Quiet enough that the resonators' peaks stay inside sixteen bits. */
    let quiet = |s: Vec<f32>| s.iter().map(|v| v * 0.2).collect::<Vec<f32>>();
    let (l, r) = (quiet(noise(4096, 12)), quiet(noise(4096, 13)));
    let (fl, fr) = render(&mut a, &l, Some(&r));
    let mut i16s: Vec<i16> = l.iter().zip(&r).flat_map(|(&x, &y)| [(x * 32768.0) as i16, (y * 32768.0) as i16]).collect();
    for (i, chunk) in i16s.chunks_mut(256).enumerate() {
        let frames = chunk.len() / 2;
        b.process_i16(chunk, frames);
        let _ = i;
    }
    for (i, f) in i16s.chunks(2).enumerate() {
        assert!((f64::from(f[0]) / 32768.0 - f64::from(fl[i])).abs() < 2e-3, "{i}: {} {}", f[0], fl[i]);
        assert!((f64::from(f[1]) / 32768.0 - f64::from(fr[i])).abs() < 2e-3, "{i}");
    }
}

#[test]
fn the_picture_is_the_vowel_put_on_the_input() {
    let hz: Vec<f64> = (0..128).map(|i| 50.0 * 200f64.powf(i as f64 / 127.0)).collect();
    for &mode in MODES {
        let mut e = engine(&[(Param::Mode, mode), (Param::Vocal, 100.0), (Param::Vowel, 0.0)]);
        render(&mut e, &noise(FS as usize / 2, 14), None);
        let (mut input, mut output) = (vec![0.0; 128], vec![0.0; 128]);
        e.envelopes(&hz, &mut input, &mut output);
        for curve in [&input, &output] {
            assert_eq!(curve.iter().cloned().fold(f64::MIN, f64::max), 0.0);
        }
        /* White noise is flat, give or take; the result peaks at A's F1. */
        let flat = input.iter().cloned().fold(f64::MAX, f64::min);
        assert!(flat > -12.0, "mode {mode}: the input spans {flat:.1} dB");
        let top = hz[output.iter().enumerate().max_by(|a, b| a.1.total_cmp(b.1)).unwrap().0];
        assert!((top / VOWELS[0][0] - 1.0).abs() < 0.1, "mode {mode}: the peak at {top:.0} Hz");
    }
}

#[cfg(feature = "spectral")]
#[test]
fn a_new_rate_resizes_everything_and_allocates_nothing_new() {
    let mut e = engine(&[(Param::Mode, 1.0)]);
    e.set_sample_rate(96_000.0);
    assert_eq!((e.sample_rate(), e.latency()), (96_000.0, 2048));
    e.set_sample_rate(96_000.0);
    e.set_sample_rate(-1.0);
    assert_eq!((e.sample_rate(), e.latency()), (DEFAULT_SAMPLE_RATE, 1024));
    e.set_sample_rate(f64::INFINITY);
    assert_eq!(e.sample_rate(), DEFAULT_SAMPLE_RATE);
    let (y, _) = render(&mut e, &noise(8192, 15), None);
    assert!(y.iter().all(|s| s.is_finite()));
    assert_eq!(Engine::new(0.0).sample_rate(), DEFAULT_SAMPLE_RATE);
}

#[test]
fn excite_gives_a_sine_something_to_sing_with() {
    /* A pure 110 Hz sine has nothing at E's F2 (2089 Hz); excited, it has. */
    let x: Vec<f32> = (0..FS as usize).map(|i| (0.5 * (core::f64::consts::TAU * 110.0 * i as f64 / FS).sin()) as f32).collect();
    let at_f2 = |excite: f64| {
        let mut e = engine(&[(Param::Vocal, 100.0), (Param::Vowel, 1.0), (Param::Excite, excite)]);
        let (y, _) = render(&mut e, &x, None);
        let db = spectrum(&y[8192..]);
        let k = bin_of(2089.0);
        (k - 20..=k + 20).map(|j| db[j]).fold(f64::MIN, f64::max) - db[bin_of(110.0)]
    };
    let (pure, excited) = (at_f2(0.0), at_f2(100.0));
    assert!(excited > pure + 20.0, "{pure:.1} -> {excited:.1} dB");
}

#[test]
fn a_right_channel_shorter_than_the_left_goes_as_far_as_it_goes() {
    let mut e = engine(&[(Param::Vocal, 0.0)]);
    let mut l = vec![0.25f32; 64];
    let mut r = vec![0.5f32; 32];
    e.process_f32(&mut l, Some(&mut r));
    assert!(l.iter().all(|&s| s == 0.25));
    assert!(r.iter().all(|&s| s == 0.5));
}

#[cfg(not(feature = "spectral"))]
#[test]
fn without_the_spectral_engine_every_mode_is_time() {
    let mut e = engine(&[(Param::Mode, 1.0), (Param::Vocal, 0.0)]);
    assert_eq!((e.mode(), e.latency()), (Mode::Time, 0));
    assert_eq!(latency(Mode::Spectral, FS), 0);
    let x = noise(4096, 16);
    let (y, _) = render(&mut e, &x, None);
    assert_eq!(y, x);
    e.set_sample_rate(96_000.0);
    assert_eq!(e.sample_rate(), 96_000.0);
}

/// Timings, not assertions: `cargo test --release -p vz-core --lib --
/// --ignored --nocapture timings`. Ten seconds of stereo noise at 48 kHz
/// through each mode, every control still: the cost of a block, as a share
/// of the time it plays for.
#[test]
#[ignore]
fn timings() {
    let seconds = 10.0;
    let n = (FS * seconds) as usize;
    let (l, r) = (noise(n, 17), noise(n, 18));
    for &mode in MODES {
        let mut e = engine(&[(Param::Mode, mode), (Param::Vocal, 100.0), (Param::Excite, 50.0)]);
        let t = std::time::Instant::now();
        render(&mut e, &l, Some(&r));
        let s = t.elapsed().as_secs_f64();
        println!("mode {mode}: {:.2} ms for {seconds} s of stereo, {:.3} % of real time", s * 1e3, 100.0 * s / seconds);
    }
}

/// The textbook voice with every formant times `scale`: a bigger or smaller
/// singer, as a capture would find one.
fn scaled_voice(scale: f64) -> VowelTable {
    let mut t = VowelTable::TEXTBOOK;
    for row in t.freq.iter_mut() {
        for f in row.iter_mut() {
            *f *= scale;
        }
    }
    t
}

#[test]
fn a_captured_voice_is_sung_as_far_as_voice_says() {
    let x = noise(FS as usize, 21);
    for &mode in MODES {
        for (amount, want) in [(100.0, 1.2), (0.0, 1.0), (50.0, 0.0)] {
            let mut e = Engine::new(FS);
            assert!(e.set_voice(&scaled_voice(1.2)));
            for (p, v) in [(Param::Mode, mode), (Param::Vocal, 100.0), (Param::Vowel, 1.0), (Param::Voice, amount)] {
                e.set_num(p, v);
            }
            render(&mut e, &vec![0.0; FS as usize / 2], None);
            let (y, _) = render(&mut e, &x, None);
            let db = spectrum(&y[4096..]);
            /* Halfway, each formant is halfway on the Bark scale. */
            let f2 = VOWELS[1][1];
            let target = if amount == 50.0 { model::hz(0.5 * (model::bark(f2) + model::bark(f2 * 1.2))) } else { f2 * want };
            let got = peak_near(&db, target);
            assert!((got / target - 1.0).abs() < 0.05, "mode {mode} voice {amount}: F2 at {got:.0}, not {target:.0}");
        }
    }
}

#[test]
fn a_new_voice_glides_in_and_nonsense_is_refused() {
    let mut e = engine(&[(Param::Vocal, 100.0), (Param::Resonance, 90.0)]);
    assert_eq!(e.voice(), &VowelTable::TEXTBOOK);
    let tone: Vec<f32> = (0..9600).map(|i| (0.3 * (core::f64::consts::TAU * 110.0 * i as f64 / FS).sin()) as f32).collect();
    render(&mut e, &tone, None);
    let (before, _) = render(&mut e, &tone, None);
    let step = |y: &[f32]| y.windows(2).map(|w| (w[1] - w[0]).abs()).fold(0.0f32, f32::max);
    assert!(e.set_voice(&scaled_voice(1.3)));
    let (during, _) = render(&mut e, &tone, None);
    /* No sample moves further than the music itself does. */
    assert!(step(&during) < 1.5 * step(&before), "{} vs {}", step(&during), step(&before));
    assert_eq!(e.voice(), &scaled_voice(1.3));
    /* A voice set while another is still arriving starts from where it is. */
    assert!(e.set_voice(&VowelTable::TEXTBOOK));
    render(&mut e, &tone[..100], None);
    assert!(e.set_voice(&scaled_voice(0.9)));
    let mut bad = scaled_voice(1.1);
    bad.bandwidth[4][4] = f64::NAN;
    assert!(!e.set_voice(&bad));
    assert_eq!(e.voice(), &scaled_voice(0.9));
}
