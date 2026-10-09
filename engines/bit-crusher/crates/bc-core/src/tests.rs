// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

use super::*;
use core::f64::consts::PI;

const SR: f64 = 48_000.0;

fn sine(hz: f64, amp: f64, n: usize) -> Vec<f32> {
    (0..n).map(|i| (amp * (2.0 * PI * hz * i as f64 / SR).sin()) as f32).collect()
}

/// An engine with every glide landed on the settings given.
fn engine(settings: &[(Param, f64)]) -> Engine {
    let mut e = Engine::new(SR);
    for &(p, v) in settings {
        e.set_num(p, v);
    }
    /* A glide's time constant is 5 ms, but landing exactly takes about
     * twenty of them: a second is plenty. */
    let mut warm = vec![0.0f32; SR as usize];
    e.process_f32(&mut warm, None);
    e.reset();
    e
}

fn mono(e: &mut Engine, input: &[f32]) -> Vec<f32> {
    let mut buf = input.to_vec();
    e.process_f32(&mut buf, None);
    buf
}

/// Plain crushing: no hold (Rate above the host's), the DC blocker the only
/// filter left.
const NO_HOLD: (Param, f64) = (Param::Rate, 96_000.0);

#[test]
fn mix_at_zero_is_the_dry_signal_bit_for_bit() {
    let input = sine(440.0, 0.8, 2048);
    let mut e = engine(&[(Param::Mix, 0.0), (Param::Bits, 2.0), (Param::Drive, 12.0)]);
    assert_eq!(mono(&mut e, &input), input);
}

#[test]
fn sixteen_bits_and_no_hold_are_close_to_transparent() {
    let input = sine(1_000.0, 0.5, 9600);
    let mut e = engine(&[(Param::Bits, 16.0), NO_HOLD]);
    let out = mono(&mut e, &input);
    /* What is left is the 16-bit steps. */
    let worst = out.iter().zip(&input).map(|(a, b)| (a - b).abs()).fold(0.0f32, f32::max);
    assert!(f64::from(worst) <= 2.0f64.powi(-16) + 1e-7, "{worst}");
}

#[test]
fn few_bits_put_the_signal_on_few_levels() {
    let input = sine(100.0, 0.9, 4800);
    /* No hold, and a mid-tread grid with no DC blocker: the output is on
     * the 2-bit grid's five levels, exactly. */
    let mut e = engine(&[(Param::Bits, 2.0), NO_HOLD]);
    let out = mono(&mut e, &input);
    let mut seen: Vec<f32> = out.clone();
    seen.sort_by(f32::total_cmp);
    seen.dedup();
    assert_eq!(seen, vec![-1.0, -0.5, 0.0, 0.5, 1.0]);
}

#[test]
fn rate_makes_steps_in_time() {
    let input = sine(200.0, 0.5, 4800);
    let mut e = engine(&[(Param::Bits, 16.0), (Param::Rate, SR / 8.0)]);
    let out = mono(&mut e, &input);
    /* The value changes every eighth sample and only then. */
    let jumps: Vec<usize> = (1..out.len()).filter(|&i| out[i] != out[i - 1]).collect();
    assert!(jumps.len() > 100);
    assert!(jumps.iter().all(|i| i % 8 == 0), "{:?}", &jumps[..8]);
}

#[test]
fn a_mid_rise_grid_on_silence_leaves_no_dc_at_the_output() {
    let mut e = engine(&[(Param::MidRise, 1.0), (Param::Bits, 3.0), NO_HOLD]);
    let out = mono(&mut e, &vec![0.0; SR as usize * 2]);
    assert!(out[0].abs() > 0.05, "a half step at first: {}", out[0]);
    assert!(out.last().unwrap().abs() < 1e-3);
}

#[test]
fn the_panic_brings_silence_on_the_next_sample() {
    let mut e = engine(&[(Param::Bits, 4.0), (Param::Rate, 2_000.0), (Param::PreFilter, 1.0), (Param::PostFilter, 1.0)]);
    mono(&mut e, &sine(300.0, 0.9, 1000));
    e.on_midi(&[0xB3, 123, 0]);
    assert!(mono(&mut e, &[0.0; 256]).iter().all(|y| *y == 0.0));

    mono(&mut e, &sine(300.0, 0.9, 1000));
    e.on_midi(&[0xB0, 120, 0]);
    assert!(mono(&mut e, &[0.0; 256]).iter().all(|y| *y == 0.0));

    /* The text door's panic does the same. */
    mono(&mut e, &sine(300.0, 0.9, 1000));
    assert!(e.set_param("panic", "1"));
    assert!(mono(&mut e, &[0.0; 256]).iter().all(|y| *y == 0.0));
}

#[test]
fn messages_that_are_not_a_panic_change_nothing() {
    let input = sine(300.0, 0.9, 1000);
    let mut a = engine(&[(Param::Bits, 4.0), (Param::Rate, 2_000.0)]);
    let mut b = engine(&[(Param::Bits, 4.0), (Param::Rate, 2_000.0)]);
    mono(&mut a, &input);
    mono(&mut b, &input);
    for msg in [&[0xB0, 64, 127][..], &[0xB0, 123][..], &[0x90, 60, 100][..], &[123, 0][..], &[][..]] {
        b.on_midi(msg);
    }
    assert_eq!(mono(&mut a, &input), mono(&mut b, &input));
}

#[test]
fn a_reset_renders_as_a_fresh_instance_would() {
    let settings = [(Param::Jitter, 70.0), (Param::Dither, 50.0), (Param::Rate, 3_000.0)];
    let input = sine(500.0, 0.7, 3000);
    let mut fresh = engine(&settings);
    let mut used = engine(&settings);
    mono(&mut used, &sine(77.0, 0.3, 5000));
    used.reset();
    assert_eq!(mono(&mut fresh, &input), mono(&mut used, &input));
}

#[test]
fn jitter_widens_and_without_it_both_sides_agree() {
    let input = sine(700.0, 0.6, 4800);
    let stereo = |jitter: f64| {
        let mut e = engine(&[(Param::Jitter, jitter), (Param::Rate, 4_000.0)]);
        let (mut l, mut r) = (input.clone(), input.clone());
        e.process_f32(&mut l, Some(&mut r));
        (l, r)
    };
    let (l, r) = stereo(0.0);
    assert_eq!(l, r);
    let (l, r) = stereo(100.0);
    assert_ne!(l, r);
}

#[test]
fn a_short_right_channel_is_processed_as_far_as_it_goes() {
    let mut e = engine(&[]);
    let mut l = vec![0.3f32; 64];
    let mut r = vec![0.3f32; 16];
    e.process_f32(&mut l, Some(&mut r));
    assert!(r.iter().all(|y| *y != 0.3));
}

#[test]
fn a_moved_control_glides_rather_than_jumps() {
    /* Output, from -24 dB to 0, on a steady level the crusher keeps as it
     * is (0.5 is a level of every whole-bit grid). */
    let input = vec![0.5f32; 64];
    let mut e = engine(&[(Param::Output, -24.0), NO_HOLD]);
    mono(&mut e, &input);
    e.set_num(Param::Output, 0.0);
    let out = mono(&mut e, &input);
    /* The first sample after has barely moved; it gets there. */
    assert!(out[0] < 0.05, "{}", out[0]);
    assert!(out.windows(2).all(|w| w[1] > w[0]));
    let mut settled = mono(&mut e, &vec![0.5f32; 4800]);
    assert_eq!(settled.pop(), Some(0.5));
}

#[test]
fn switching_a_filter_in_is_a_crossfade() {
    let input = sine(5_000.0, 0.8, 4800);
    let mut e = engine(&[(Param::Bits, 16.0), (Param::Rate, 2_000.0)]);
    let before = mono(&mut e, &input);
    e.set_num(Param::PreFilter, 1.0);
    let after = mono(&mut e, &input);
    /* The largest step between neighbouring samples does not grow at the
     * switch beyond what the signal already does. */
    let worst = |v: &[f32]| v.windows(2).map(|w| (w[1] - w[0]).abs()).fold(0.0f32, f32::max);
    assert!(worst(&after[..256]) <= worst(&before) + 1e-3);
}

#[test]
fn nan_and_infinity_in_are_silence_and_nan_settings_are_ignored() {
    let mut e = engine(&[]);
    e.set_num(Param::Bits, f64::NAN);
    assert_eq!(e.num(Param::Bits), 8.0);
    let out = mono(&mut e, &[f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 0.0]);
    assert!(out.iter().all(|y| y.is_finite()));
    assert!(mono(&mut e, &[0.0; 64]).iter().all(|y| y.is_finite()));
}

#[test]
fn values_are_clamped_at_the_door() {
    let mut e = Engine::new(SR);
    e.set_num(Param::Bits, 40.0);
    assert_eq!(e.num(Param::Bits), 16.0);
    e.set_num(Param::Rate, 1.0);
    assert_eq!(e.num(Param::Rate), 50.0);
    e.set_num(Param::PreFilter, 0.7);
    assert_eq!(e.num(Param::PreFilter), 1.0);
}

#[test]
fn the_text_door_reads_words_and_numbers() {
    let mut e = Engine::new(SR);
    assert!(e.set_param("bits", "5.5"));
    assert_eq!(e.num(Param::Bits), 5.5);
    assert!(e.set_param("rate", "8k"));
    assert_eq!(e.num(Param::Rate), 8_000.0);
    assert!(e.set_param("pre_filter", "On"));
    assert_eq!(e.num(Param::PreFilter), 1.0);
    /* Junk is no value; the key is still the engine's. */
    assert!(e.set_param("bits", "lots"));
    assert_eq!(e.num(Param::Bits), 5.5);
    assert!(!e.set_param("volume", "1"));

    let mut out = [0u8; 32];
    let n = e.get_param("rate", &mut out);
    assert_eq!(&out[..n as usize], b"8000");
    assert_eq!(out[n as usize], 0);
    assert_eq!(e.get_param("volume", &mut out), -1);
    /* Too small to hold it: cut, as snprintf cuts. */
    let mut tiny = [0u8; 3];
    assert_eq!(e.get_param("rate", &mut tiny), 2);
}

#[test]
fn the_sixteen_bit_path_agrees_with_the_float_path() {
    let n = 4800;
    let input = sine(300.0, 0.7, n);
    let settings = [(Param::Bits, 6.0), (Param::Rate, 8_000.0), (Param::PostFilter, 1.0)];
    let mut f = engine(&settings);
    let mut i = engine(&settings);
    let (mut l, mut r) = (input.clone(), input.clone());
    f.process_f32(&mut l, Some(&mut r));
    let mut inter: Vec<i16> = input
        .iter()
        .flat_map(|x| {
            let s = (f64::from(*x) * 32_768.0).round() as i16;
            [s, s]
        })
        .collect();
    i.process_i16(&mut inter, n);
    /* The 16-bit input differs by less than a sixteen-bit step, which can
     * put a sample on the other side of a step's edge: rarely, and never by
     * more than a step. Everything else agrees to the 16-bit output's step. */
    let step = 2.0f64.powi(1 - 6);
    let mut off = 0;
    for (k, y) in l.iter().enumerate() {
        let d = (f64::from(inter[2 * k]) / 32_768.0 - f64::from(*y)).abs();
        assert!(d <= step + 1e-3, "{k}: {d}");
        if d > 2.0 / 32_768.0 {
            off += 1;
        }
    }
    assert!(off < n / 50, "{off} of {n} samples disagree");
}

#[test]
fn the_sixteen_bit_path_clips_and_stops_at_frames() {
    let mut e = engine(&[(Param::Bits, 1.0), NO_HOLD, (Param::Output, 12.0)]);
    let mut buf = [32_000i16, -32_000, 32_000, -32_000, 7, 7];
    e.process_i16(&mut buf, 2);
    assert_eq!(&buf[..2], &[32_767, -32_768]);
    assert_eq!(&buf[4..], &[7, 7]);
}

#[test]
fn a_new_sample_rate_retunes_and_absurd_ones_are_refused() {
    let mut e = Engine::new(0.0);
    assert_eq!(e.sample_rate(), DEFAULT_SAMPLE_RATE);
    e.set_sample_rate(f64::NAN);
    assert_eq!(e.sample_rate(), DEFAULT_SAMPLE_RATE);
    e.set_sample_rate(96_000.0);
    assert_eq!(e.sample_rate(), 96_000.0);
    e.set_sample_rate(96_000.0);
    let out = mono(&mut e, &sine(440.0, 0.5, 512));
    assert!(out.iter().all(|y| y.is_finite()));
}

/// The energy of a rendered tone outside its own harmonics: what aliasing
/// put there.
fn alias_energy(anti_alias: bool) -> f64 {
    use realfft::RealFftPlanner;
    let n = 1 << 14;
    let f0 = 4_987.0;
    let mut e = engine(&[(Param::Bits, 3.0), NO_HOLD, (Param::AntiAlias, anti_alias as i32 as f64)]);
    let rendered = mono(&mut e, &sine(f0, 0.9, n));
    let mut x: Vec<f64> = rendered
        .iter()
        .enumerate()
        .map(|(i, y)| f64::from(*y) * (0.5 - 0.5 * (2.0 * PI * i as f64 / n as f64).cos()))
        .collect();
    let fft = RealFftPlanner::<f64>::new().plan_fft_forward(n);
    let mut spectrum = fft.make_output_vec();
    fft.process(&mut x, &mut spectrum).unwrap();
    let bin_hz = SR / n as f64;
    let harmonic = |hz: f64| (1..=4).any(|k| (hz - f64::from(k) * f0).abs() < 4.0 * bin_hz);
    spectrum
        .iter()
        .enumerate()
        .filter(|(i, _)| {
            let hz = *i as f64 * bin_hz;
            hz > 40.0 && !harmonic(hz)
        })
        .map(|(_, c)| c.norm_sqr())
        .sum()
}

#[test]
fn anti_alias_takes_out_most_of_what_folded_back() {
    let plain = alias_energy(false);
    let clean = alias_energy(true);
    let db = 10.0 * (plain / clean).log10();
    assert!(db > 6.0, "only {db:.1} dB less aliasing");
}

#[test]
fn dither_turns_a_quiet_tone_lost_between_steps_into_one_heard_in_noise() {
    /* A tone a third of a step high: without dither it is all on the zero
     * level, with it the output follows it on average. */
    let step = 2.0f64.powi(1 - 6);
    let input = sine(100.0, step / 3.0, 9600);
    let plain = mono(&mut engine(&[(Param::Bits, 6.0), NO_HOLD]), &input);
    assert!(plain[4800..].iter().all(|y| y.abs() < 1e-3));
    let dithered = mono(&mut engine(&[(Param::Bits, 6.0), NO_HOLD, (Param::Dither, 100.0)]), &input);
    let corr: f64 = dithered[4800..].iter().zip(&input[4800..]).map(|(a, b)| f64::from(*a) * f64::from(*b)).sum();
    assert!(corr > 0.0);
}

#[test]
fn the_transfer_curve_is_the_engines_staircase() {
    assert_eq!(transfer(0.0, 1.0, 0.0, false, 0.9), 1.0);
    assert_eq!(transfer(0.0, 1.0, 0.0, false, 0.4), 0.0);
    assert_eq!(transfer(0.0, 1.0, 0.0, true, 0.1), 0.5);
    /* Drive moves the input up the stairs. */
    assert!(transfer(12.0, 4.0, 0.0, false, 0.1) > transfer(0.0, 4.0, 0.0, false, 0.1));
    /* Shape keeps quiet levels. */
    assert!(transfer(0.0, 4.0, 100.0, false, 0.02) > 0.0);
    assert_eq!(transfer(0.0, 4.0, 0.0, false, 0.02), 0.0);
    /* A NaN setting is its default: 8 bits, no shape, no drive. */
    assert_eq!(transfer(f64::NAN, f64::NAN, f64::NAN, false, 0.3), transfer(0.0, 8.0, 0.0, false, 0.3));
}

#[test]
fn every_control_at_its_ends_renders_finite_audio() {
    let input = sine(220.0, 1.0, 1024);
    for p in Param::ALL {
        for n in [0.0, 1.0] {
            let mut e = Engine::new(SR);
            e.set_num(p, p.denormalise(n));
            let (mut l, mut r) = (input.clone(), input.clone());
            e.process_f32(&mut l, Some(&mut r));
            assert!(l.iter().chain(&r).all(|y| y.is_finite() && y.abs() <= 4.0), "{p:?} at {n}");
        }
    }
}
