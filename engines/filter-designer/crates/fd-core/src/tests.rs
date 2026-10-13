// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The engine as a whole: exactness, the sound against the design, and no
//! clicks.

use super::*;
use crate::design::{design, Settings, Shape};
use ni_filter::Family;
use proptest::prelude::*;

const FS: f64 = 48_000.0;

fn node(e: &mut Engine, slot: u8, field: Field, v: f64) {
    e.set_num(Param::Node(slot, field), v);
}

fn noise(n: usize, seed: u64) -> Vec<f32> {
    let mut s = seed | 1;
    (0..n)
        .map(|_| {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            ((s >> 11) as f64 / (1u64 << 53) as f64 * 2.0 - 1.0) as f32 * 0.5
        })
        .collect()
}

fn sine(n: usize, hz: f64, amp: f64) -> Vec<f32> {
    (0..n).map(|i| (amp * (2.0 * core::f64::consts::PI * hz * i as f64 / FS).sin()) as f32).collect()
}

fn rms(x: &[f32]) -> f64 {
    (x.iter().map(|v| f64::from(*v).powi(2)).sum::<f64>() / x.len() as f64).sqrt()
}

/// The engine's steady gain at `hz`, in dB, on the left channel.
fn measured_db(e: &mut Engine, hz: f64) -> f64 {
    let n = (FS * 0.6) as usize;
    let mut l = sine(n, hz, 0.25);
    let mut r = l.clone();
    e.process_f32(&mut l, Some(&mut r));
    let tail = (FS * 0.2) as usize;
    20.0 * (rms(&l[n - tail..]) / rms(&sine(n, hz, 0.25)[n - tail..])).log10()
}

#[test]
fn an_empty_instance_is_bit_exact() {
    let mut e = Engine::new(FS);
    let x = noise(4096, 7);
    let (mut l, mut r) = (x.clone(), noise(4096, 9));
    let r0 = r.clone();
    e.process_f32(&mut l, Some(&mut r));
    assert_eq!(l, x);
    assert_eq!(r, r0);
    /* Watching changes nothing in the sound. */
    e.set_watching(true);
    e.process_f32(&mut l, Some(&mut r));
    assert_eq!(l, x);
    /* A node switched off is empty too, whatever its type. */
    node(&mut e, 3, Field::Type, 1.0);
    node(&mut e, 3, Field::Gain, 6.0);
    node(&mut e, 3, Field::On, 0.0);
    e.process_f32(&mut l, Some(&mut r));
    assert_eq!(l, x);
}

fn busy(e: &mut Engine) {
    node(e, 0, Field::Type, 4.0); // high-pass
    node(e, 0, Field::Freq, 40.0);
    node(e, 0, Field::Order, 3.0);
    node(e, 1, Field::Type, 1.0); // bell
    node(e, 1, Field::Freq, 300.0);
    node(e, 1, Field::Gain, -4.0);
    node(e, 2, Field::Type, 7.0); // notch, elliptic
    node(e, 2, Field::Family, 4.0);
    node(e, 2, Field::Order, 1.0);
    node(e, 2, Field::Freq, 3000.0);
    node(e, 3, Field::Type, 3.0); // high shelf, mid only
    node(e, 3, Field::Gain, 3.0);
    node(e, 3, Field::Freq, 9000.0);
    node(e, 3, Field::Channel, 3.0);
    node(e, 4, Field::Type, 5.0); // low-pass, chebyshev I
    node(e, 4, Field::Family, 2.0);
    node(e, 4, Field::Order, 5.0);
    node(e, 4, Field::Freq, 16_000.0);
    e.set_num(Param::IsoOn, 1.0);
    e.set_num(Param::IsoKillLow, 1.0);
    e.set_num(Param::Output, -2.0);
}

#[test]
fn a_render_is_the_same_bits_at_any_block_size() {
    let input_l = noise(9000, 3);
    let input_r = noise(9000, 5);
    let render = |sizes: &dyn Fn(usize) -> usize| {
        let mut e = Engine::new(FS);
        busy(&mut e);
        let (mut l, mut r) = (input_l.clone(), input_r.clone());
        let mut at = 0;
        let mut k = 0;
        while at < l.len() {
            let n = sizes(k).min(l.len() - at);
            e.process_f32(&mut l[at..at + n], Some(&mut r[at..at + n]));
            at += n;
            k += 1;
        }
        (l, r)
    };
    let whole = render(&|_| 9000);
    let one = |_: usize| 1usize;
    let seven = |_: usize| 7usize;
    let many = |_: usize| 64usize;
    let mixed = |k: usize| [3usize, 128, 1, 33, 500][k % 5];
    let all: [&dyn Fn(usize) -> usize; 4] = [&one, &seven, &many, &mixed];
    for sizes in all {
        assert_eq!(render(sizes), whole);
    }
}

#[test]
fn the_sound_is_the_design() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 1.0);
    node(&mut e, 0, Field::Freq, 1000.0);
    node(&mut e, 0, Field::Gain, 9.0);
    node(&mut e, 0, Field::Q, 2.0);
    node(&mut e, 1, Field::Type, 5.0);
    node(&mut e, 1, Field::Family, 3.0); // Chebyshev II
    node(&mut e, 1, Field::Order, 5.0);
    node(&mut e, 1, Field::Freq, 6000.0);
    let s0 = Settings {
        shape: Shape::Bell,
        family: Family::Butterworth,
        order: 2,
        freq: 1000.0,
        gain_db: 9.0,
        q: 2.0,
        ripple_db: 1.0,
        stopband_db: 60.0,
        matched: false,
    };
    let s1 = Settings { shape: Shape::LowPass, family: Family::ChebyshevII, order: 6, freq: 6000.0, gain_db: 0.0, q: core::f64::consts::FRAC_1_SQRT_2, ..s0 };
    let (c0, _) = design(&s0, None, Globals::NEUTRAL, FS);
    let (c1, _) = design(&s1, None, Globals::NEUTRAL, FS);
    for hz in [100.0, 700.0, 1000.0, 2500.0, 5000.0] {
        let want = c0.db(hz, FS) + c1.db(hz, FS);
        let got = measured_db(&mut e, hz);
        assert!((got - want).abs() < 0.02, "{hz} Hz: {got} vs {want}");
    }
}

#[test]
fn a_notch_takes_its_frequency_out() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 7.0);
    node(&mut e, 0, Field::Order, 0.0);
    node(&mut e, 0, Field::Freq, 2000.0);
    node(&mut e, 0, Field::Q, 4.0);
    /* The centre on the warped axis: the edges' geometric mean there. */
    let h = 0.125f64;
    let r = (1.0 + h * h).sqrt();
    let w = |f: f64| (core::f64::consts::PI * f / FS).tan();
    let centre = (w(2000.0 * (r - h)) * w(2000.0 * (r + h))).sqrt().atan() * FS / core::f64::consts::PI;
    assert!(measured_db(&mut e, centre) < -60.0);
}

/// The largest step from one sample to the next.
fn largest_step(x: &[f32]) -> f64 {
    x.windows(2).map(|w| f64::from(w[1] - w[0]).abs()).fold(0.0, f64::max)
}

#[test]
fn what_a_node_is_changes_without_a_click() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 1.0);
    node(&mut e, 0, Field::Freq, 3000.0);
    node(&mut e, 0, Field::Gain, 4.0);
    let mut out = Vec::new();
    let x = sine((FS * 1.2) as usize, 1000.0, 0.5);
    let changes: [(Field, f64); 10] = [
        (Field::Type, 5.0),
        (Field::Family, 4.0),
        (Field::Order, 7.0),
        (Field::Family, 1.0),
        (Field::Type, 4.0),
        (Field::Freq, 200.0),
        (Field::Type, 7.0),
        (Field::Channel, 3.0),
        (Field::On, 0.0),
        (Field::On, 1.0),
    ];
    let block = (FS * 0.05) as usize;
    for (k, chunk) in x.chunks(block).enumerate() {
        if let Some((f, v)) = changes.get(k % 12) {
            node(&mut e, 0, *f, *v);
        }
        let mut l = chunk.to_vec();
        let mut r = chunk.to_vec();
        e.process_f32(&mut l, Some(&mut r));
        out.extend(l);
    }
    /* A sine at 1 kHz, 0.5 peak, through at most +4 dB: its own largest step
     * is 0.5 · ω · 1.6. A switch without a fade jumps by far more. */
    let bound = 0.5 * 2.0 * core::f64::consts::PI * 1000.0 / FS * 1.6 * 1.25;
    let step = largest_step(&out);
    assert!(step < bound, "largest step {step}, bound {bound}");
}

#[test]
fn output_and_a_kill_glide() {
    let mut e = Engine::new(FS);
    let x = sine(4800, 200.0, 0.1);
    let (mut l, mut r) = (x.clone(), x.clone());
    e.process_f32(&mut l, Some(&mut r));
    e.set_num(Param::Output, 24.0);
    let (mut l, mut r) = (x.clone(), x.clone());
    e.process_f32(&mut l, Some(&mut r));
    let bound = 0.1 * 2.0 * core::f64::consts::PI * 200.0 / FS * 15.9 * 1.25 + 1e-3;
    assert!(largest_step(&l) < bound, "{} vs {bound}", largest_step(&l));

    /* A kill well inside its band: 60 Hz under a 250 Hz crossover. */
    let x = sine(4800, 60.0, 0.1);
    let mut e = Engine::new(FS);
    e.set_num(Param::IsoOn, 1.0);
    let (mut l, mut r) = (x.clone(), x.clone());
    e.process_f32(&mut l, Some(&mut r));
    e.set_num(Param::IsoKillLow, 1.0);
    let (mut l, mut r) = (x.clone(), x.clone());
    e.process_f32(&mut l, Some(&mut r));
    assert!(largest_step(&l) < 0.1 * 2.0 * core::f64::consts::PI * 60.0 / FS * 1.5 + 1e-3);
    assert!(rms(&l[4000..]) < 0.002, "killed low: {}", rms(&l[4000..]));
}

#[test]
fn the_isolator_at_zero_db_is_flat_and_its_kills_are_deep() {
    for slope in [0.0, 1.0] {
        let mut e = Engine::new(FS);
        e.set_num(Param::IsoOn, 1.0);
        e.set_num(Param::IsoSlope, slope);
        for hz in [60.0, 250.0, 900.0, 2500.0, 9000.0] {
            assert!(measured_db(&mut e, hz).abs() < 0.01, "slope {slope} at {hz}");
        }
        e.set_num(Param::IsoKillMid, 1.0);
        assert!(measured_db(&mut e, 800.0) < -30.0);
        e.set_num(Param::IsoKillMid, 0.0);
        e.set_num(Param::IsoHigh, -60.0);
        assert!(measured_db(&mut e, 12_000.0) < -40.0);
    }
}

#[test]
fn mid_and_side_nodes_touch_only_their_half() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 1.0);
    node(&mut e, 0, Field::Gain, 12.0);
    node(&mut e, 0, Field::Freq, 1000.0);
    node(&mut e, 0, Field::Channel, 4.0); // side
    /* L = R: no side to touch. */
    assert!(measured_db(&mut e, 1000.0).abs() < 1e-6);
    node(&mut e, 0, Field::Channel, 3.0); // mid
    assert!((measured_db(&mut e, 1000.0) - 12.0).abs() < 0.02);
    /* L = -R: no mid. */
    let n = (FS * 0.5) as usize;
    let mut l = sine(n, 1000.0, 0.25);
    let mut r: Vec<f32> = l.iter().map(|v| -v).collect();
    e.process_f32(&mut l, Some(&mut r));
    assert!((rms(&l[n - 4800..]) / rms(&sine(n, 1000.0, 0.25)[n - 4800..]) - 1.0).abs() < 1e-6);
}

#[test]
fn the_panic_empties_every_filter() {
    let mut e = Engine::new(FS);
    busy(&mut e);
    let mut l = noise(2048, 11);
    let mut r = noise(2048, 13);
    e.process_f32(&mut l, Some(&mut r));
    assert!(!e.is_silent());
    e.on_midi(&[0xB3, 120, 0]);
    assert!(e.is_silent());
    let (mut l, mut r) = (vec![0.0f32; 512], vec![0.0f32; 512]);
    e.process_f32(&mut l, Some(&mut r));
    assert!(l.iter().chain(r.iter()).all(|v| *v == 0.0));
    /* A cut-short message is no panic, and a note is nothing. */
    e.process_f32(&mut noise(256, 1), None);
    e.on_midi(&[0xB0, 123]);
    e.on_midi(&[0x90, 60, 100]);
    assert!(!e.is_silent());
    e.on_midi(&[0xB0, 123, 0]);
    assert!(e.is_silent());
}

#[test]
fn a_fade_lands() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 1.0);
    node(&mut e, 0, Field::Gain, 3.0);
    e.process_f32(&mut vec![0.0; 64], None);
    assert!(e.node(0).is_fading());
    e.process_f32(&mut vec![0.0; (FS * 0.021) as usize], None);
    assert!(!e.node(0).is_fading());
}

#[test]
fn the_text_door_and_the_rings() {
    let mut e = Engine::new(FS);
    assert!(e.set_param("n02_type", "Bell"));
    assert!(e.set_param("n02_freq", "2k"));
    assert!(!e.set_param("nope", "1"));
    assert_eq!(e.num(Param::Node(1, Field::Freq)), 2000.0);
    let mut out = [0u8; 32];
    assert!(e.get_param("n02_freq", &mut out) > 0);
    assert_eq!(e.get_param("nope", &mut out), -1);
    assert!(e.set_param("panic", "1"));
    e.set_watching(true);
    let mut l = sine(RING, 500.0, 0.5);
    e.process_f32(&mut l, None);
    let (mut pre, mut post) = (vec![0.0; RING], vec![0.0; RING]);
    e.rings(&mut pre, &mut post);
    assert!(rms(&pre) > 0.3 && rms(&post) > 0.3);
    let mut i16s = vec![1000i16; 512];
    e.process_i16(&mut i16s, 256);
    e.set_sample_rate(96_000.0);
    assert_eq!(e.sample_rate(), 96_000.0);
    e.set_sample_rate(-1.0);
    assert_eq!(e.sample_rate(), DEFAULT_SAMPLE_RATE);
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(120))]

    #[test]
    fn any_settings_stay_finite_and_bounded(values in proptest::collection::vec(0.0f64..1.0, PARAM_COUNT), fs_i in 0usize..3) {
        let fs = [44_100.0, 48_000.0, 192_000.0][fs_i];
        let mut e = Engine::new(fs);
        for (i, n) in values.iter().enumerate() {
            let p = Param::from_index(i).unwrap();
            e.set_num(p, p.denormalise(*n));
        }
        /* The largest magnitude these settings can have: each heard node's
         * gain times Scale (a gain type), its resonance (a pass, Q up to 40),
         * 0 dB otherwise; the isolator's +6; Output; and 20 dB for a
         * transient's overshoot of the steady-state peak. */
        let v = e.values();
        let scale = (v[Param::Scale.index()] / 100.0).abs();
        let mut db = v[Param::Output.index()] + 6.0 + 20.0;
        for slot in 0..NODES as u8 {
            if !crate::design::is_heard(v, slot) {
                continue;
            }
            let s = crate::design::settings_of(v, slot);
            db += if s.shape.uses_gain() {
                s.gain_db.abs() * scale
            } else {
                20.0 * (s.q / core::f64::consts::FRAC_1_SQRT_2).max(1.0).log10() + 3.0
            };
        }
        let bound = 0.5 * 10f64.powf(db / 20.0);
        let mut l = noise(4096, 17);
        let mut r = noise(4096, 19);
        e.process_f32(&mut l, Some(&mut r));
        for x in l.iter().chain(r.iter()) {
            prop_assert!(x.is_finite(), "not finite");
            prop_assert!(f64::from(x.abs()) <= bound, "{} over the bound {bound}", x.abs());
        }
    }
}

/// Every node an order-8 elliptic low-pass at once, as a preset load does:
/// no control tick designs more than its budget of prototypes, and the
/// budget goes round, so every node is fading in a few ticks later.
#[test]
fn a_preset_load_designs_no_more_than_the_budget_a_tick() {
    let mut e = Engine::new(FS);
    for slot in 0..NODES as u8 {
        node(&mut e, slot, Field::Type, 5.0);
        node(&mut e, slot, Field::Family, 4.0);
        node(&mut e, slot, Field::Order, 7.0);
        node(&mut e, slot, Field::Freq, 500.0 + 500.0 * slot as f64);
    }
    let designed: Vec<usize> = (0..NODES / node::PROTOS_PER_TICK).map(|_| e.control()).collect();
    assert_eq!(designed, vec![node::PROTOS_PER_TICK; NODES / node::PROTOS_PER_TICK]);
    assert!((0..NODES).all(|n| e.node(n).is_fading()), "every node fading in");
    for _ in 0..200 {
        assert!(e.control() <= node::PROTOS_PER_TICK);
    }
}

/// Ripple automated on an elliptic node: its prototype follows every
/// PROTO_EVERY-th tick and on the tick Ripple lands, and what plays then is
/// the design of where it landed.
#[test]
fn a_gliding_ripple_redesigns_the_prototype_every_fourth_tick_and_where_it_lands() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 5.0);
    node(&mut e, 0, Field::Family, 4.0);
    node(&mut e, 0, Field::Order, 5.0);
    node(&mut e, 0, Field::Freq, 2000.0);
    node(&mut e, 0, Field::Ripple, 0.1);
    e.process_f32(&mut vec![0.0; (FS * 0.05) as usize], None);
    assert!(!e.node(0).is_fading());
    node(&mut e, 0, Field::Ripple, 2.5);
    let ticks: Vec<usize> = (0..5000).filter(|_| e.control() > 0).collect();
    let (&last, gaps) = ticks.split_last().expect("the prototype followed");
    assert!(gaps.len() > 3, "it followed the glide, {} times", ticks.len());
    for w in gaps.windows(2) {
        assert!(w[1] - w[0] >= node::PROTO_EVERY as usize, "designed at ticks {} and {}", w[0], w[1]);
    }
    assert!(last > gaps[gaps.len() - 1], "and once more where it landed");
    let s = crate::design::settings_of(&e.values, 0);
    let (want, _) = design(&s, crate::design::prototype(&s).as_ref(), e.globals, FS);
    for hz in [500.0, 2000.0, 2400.0, 9000.0] {
        assert!((e.node(0).cascade().db(hz, FS) - want.db(hz, FS)).abs() < 1e-9, "{hz} Hz");
    }
}

/// THE TAIL: a Q 40 notch at 20 Hz under a 20 Hz sine holds the sine's
/// energy; once the sine stops, it rings out at the notch's frequency. Its
/// fall to -60 dB comes within 20 % of the tail the host is told.
#[test]
fn the_tail_is_how_long_a_ringing_notch_goes_on() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 7.0);
    node(&mut e, 0, Field::Order, 0.0);
    node(&mut e, 0, Field::Freq, 20.0);
    node(&mut e, 0, Field::Q, 40.0);
    let told = design::tail_seconds(&e.values, FS);
    assert!(told > 3.0 && told < 6.0, "told {told} s");
    let amp = 0.5;
    let mut x = sine((FS * 8.0) as usize, 20.0, amp);
    e.process_f32(&mut x, None);
    let mut after = vec![0.0f32; (FS * 2.0 * told) as usize];
    e.process_f32(&mut after, None);
    let last = after.iter().rposition(|v| f64::from(v.abs()) > amp * 1e-3).unwrap();
    let measured = last as f64 / FS;
    println!("Q 40 notch at 20 Hz: rang {measured:.3} s to -60 dB, told {told:.3} s");
    assert!((measured - told).abs() < 0.2 * told, "rang {measured} s, told {told} s");
    /* Nothing heard, no tail; the isolator alone, a short one. */
    let mut quiet = defaults();
    assert_eq!(design::tail_seconds(&quiet, FS), 0.0);
    quiet[Param::IsoOn.index()] = 1.0;
    let iso = design::tail_seconds(&quiet, FS);
    assert!(iso > 0.0 && iso < 0.2, "isolator {iso} s");
    assert_eq!(design::tail_seconds(&quiet, 0.0), 0.0);
}

/// `x`'s magnitude spectrum under a Hann window.
fn spectrum(x: &[f32]) -> Vec<f64> {
    let n = x.len();
    let fft = realfft::RealFftPlanner::<f64>::new().plan_fft_forward(n);
    let mut input: Vec<f64> = x
        .iter()
        .enumerate()
        .map(|(i, v)| f64::from(*v) * (0.5 - 0.5 * (2.0 * core::f64::consts::PI * i as f64 / n as f64).cos()))
        .collect();
    let mut out = fft.make_output_vec();
    fft.process(&mut input, &mut out).unwrap();
    out.iter().map(|c| c.norm()).collect()
}

/// A SWEEP OF A NARROW FILTER: a +12 dB bell at Q 20 swept an octave, 700
/// Hz to 1.4 kHz, in 100 ms across a 1 kHz sine. Coefficients that stepped
/// every control tick put lines at the sine ± fs/16 (2 and 4 kHz at 48 kHz),
/// measured at -99 dBc; moved a sample at a time they are below -120 dBc,
/// under the sweep's own modulation, which stays near 1 kHz.
#[test]
fn a_sweep_puts_no_lines_at_the_control_rate() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 1.0);
    node(&mut e, 0, Field::Gain, 12.0);
    node(&mut e, 0, Field::Q, 20.0);
    let (n, len) = (32_768, (FS * 0.1) as usize);
    let start = (n - len) / 2;
    let mut y = sine(n, 1000.0, 0.5);
    for (b, chunk) in y.chunks_mut(SUB).enumerate() {
        let at = (b * SUB).saturating_sub(start).min(len) as f64 / len as f64;
        node(&mut e, 0, Field::Freq, 700.0 * 2f64.powf(at));
        e.process_f32(chunk, None);
    }
    let s = spectrum(&y);
    let bin = |hz: f64| (hz * n as f64 / FS).round() as usize;
    let near = |hz: f64| (bin(hz) - 20..=bin(hz) + 20).map(|k| s[k]).fold(0.0, f64::max);
    let dbc = 20.0 * (near(2000.0).max(near(4000.0)) / near(1000.0)).log10();
    println!("a Q 20 bell swept 700 Hz to 1.4 kHz in 100 ms: lines at 1 kHz ± fs/16 at {dbc:.1} dBc");
    assert!(dbc < -120.0, "{dbc} dBc");
}

/// The order a node plays is the one order_played names: the same design,
/// and a choice it leaves alone.
#[test]
fn the_order_played_is_the_order_designed() {
    use crate::design::{order_played, prototype, settings_of};
    let mut v = defaults();
    for t in 1..=9 {
        for f in 0..6 {
            for o in 0..8 {
                v[Param::Node(0, Field::Type).index()] = f64::from(t);
                v[Param::Node(0, Field::Family).index()] = f64::from(f);
                v[Param::Node(0, Field::Order).index()] = f64::from(o);
                let s = settings_of(&v, 0);
                let played = order_played(s.shape, s.family, o as usize);
                assert_eq!(order_played(s.shape, s.family, played), played, "a fixed point");
                let mut w = v;
                w[Param::Node(0, Field::Order).index()] = played as f64;
                let p = settings_of(&w, 0);
                let a = design(&s, prototype(&s).as_ref(), Globals::NEUTRAL, FS).0;
                let b = design(&p, prototype(&p).as_ref(), Globals::NEUTRAL, FS).0;
                assert_eq!(a, b, "type {t} family {f} order {o} plays {played}");
            }
        }
    }
    v[Param::Node(0, Field::Type).index()] = 4.0;
    v[Param::Node(0, Field::Family).index()] = 5.0;
    assert_eq!(order_played(settings_of(&v, 0).shape, Family::LinkwitzRiley, 2), 3, "LR at 18 dB/oct plays LR4");
}

/// A mono track works out one lane, and sounds bit for bit as a stereo one
/// with both sides the same -- with mid, side and right-only nodes, the
/// isolator, a fade and a glide in it.
#[test]
fn a_mono_track_is_a_stereo_one_with_both_sides_the_same() {
    let (mut a, mut b) = (Engine::new(FS), Engine::new(FS));
    for e in [&mut a, &mut b] {
        busy(e);
        node(e, 5, Field::Type, 1.0);
        node(e, 5, Field::Gain, 6.0);
        node(e, 5, Field::Channel, 4.0); // side
        node(e, 6, Field::Type, 1.0);
        node(e, 6, Field::Gain, -6.0);
        node(e, 6, Field::Channel, 2.0); // right
    }
    let x = noise(8192, 11);
    let (mut mono, mut l, mut r) = (x.clone(), x.clone(), x.clone());
    for (k, ((m, l), r)) in mono.chunks_mut(500).zip(l.chunks_mut(500)).zip(r.chunks_mut(500)).enumerate() {
        if k == 4 {
            for e in [&mut a, &mut b] {
                node(e, 1, Field::Freq, 900.0);
                node(e, 2, Field::Order, 3.0);
            }
        }
        a.process_f32(m, None);
        b.process_f32(l, Some(r));
    }
    assert_eq!(mono, l);
}

/// THE WHOLE ENGINE AT EVERY RATE: a bell and a Chebyshev II low-pass sound
/// as their designs at 44.1 to 384 kHz -- the control grid in samples, every
/// time constant from the rate.
#[test]
fn the_sound_is_the_design_at_every_rate() {
    for fs in [44_100.0, 48_000.0, 88_200.0, 96_000.0, 176_400.0, 192_000.0, 384_000.0] {
        let mut e = Engine::new(fs);
        node(&mut e, 0, Field::Type, 1.0);
        node(&mut e, 0, Field::Freq, 1000.0);
        node(&mut e, 0, Field::Gain, 6.0);
        node(&mut e, 0, Field::Q, 2.0);
        node(&mut e, 1, Field::Type, 5.0);
        node(&mut e, 1, Field::Family, 3.0);
        node(&mut e, 1, Field::Order, 5.0);
        node(&mut e, 1, Field::Freq, 6000.0);
        let v = e.values;
        let designs: Vec<_> = (0..2)
            .map(|slot| {
                let s = design::settings_of(&v, slot);
                design(&s, design::prototype(&s).as_ref(), Globals::NEUTRAL, fs).0
            })
            .collect();
        /* Whole cycles: 100 of each frequency in the measured tenth of a second. */
        for hz in [100.0, 1000.0, 2500.0, 5000.0] {
            let n = (fs * 0.25) as usize;
            let tail = (fs * 0.1) as usize;
            let x: Vec<f32> = (0..n).map(|i| (0.25 * (2.0 * core::f64::consts::PI * hz * i as f64 / fs).sin()) as f32).collect();
            let (mut l, mut r) = (x.clone(), x.clone());
            e.process_f32(&mut l, Some(&mut r));
            let got = 20.0 * (rms(&l[n - tail..]) / rms(&x[n - tail..])).log10();
            let want: f64 = designs.iter().map(|c| c.db(hz, fs)).sum();
            assert!((got - want).abs() < 0.01, "{fs} Hz, {hz} Hz: {got} dB, the design {want} dB");
        }
    }
}

/// MATCHED CUTS: flipped, the high-pass and low-pass nodes fade to their
/// matched designs -- and only they -- and what plays is that design: near
/// the top, the analog slope instead of the bilinear transform's cliff.
#[test]
fn matched_cuts_fade_the_cuts_to_their_matched_designs() {
    let mut e = Engine::new(FS);
    node(&mut e, 0, Field::Type, 5.0); // low-pass, 24 dB/oct at 14 kHz
    node(&mut e, 0, Field::Order, 3.0);
    node(&mut e, 0, Field::Freq, 14_000.0);
    node(&mut e, 1, Field::Type, 1.0); // a bell, which does not change
    node(&mut e, 1, Field::Gain, 3.0);
    e.process_f32(&mut vec![0.0; (FS * 0.05) as usize], None);
    let bilinear = measured_db(&mut e, 19_000.0);
    e.set_num(Param::MatchedCuts, 1.0);
    e.process_f32(&mut vec![0.0; 64], None);
    assert!(e.node(0).is_fading() && !e.node(1).is_fading(), "the cut fades, the bell does not");
    let matched = measured_db(&mut e, 19_000.0);
    let v = e.values;
    let want: f64 = (0..2)
        .map(|slot| {
            let s = design::settings_of(&v, slot);
            design(&s, design::prototype(&s).as_ref(), Globals::NEUTRAL, FS).0.db(19_000.0, FS)
        })
        .sum();
    assert!(design::settings_of(&v, 0).matched && !design::settings_of(&v, 1).matched);
    assert!((matched - want).abs() < 0.05, "{matched} dB played, {want} dB designed");
    /* Half an octave above the cutoff the bilinear cut has fallen far past
     * the analog 24 dB/oct; the matched one is near it. */
    println!("a 24 dB/oct low-pass at 14 kHz, at 19 kHz: bilinear {bilinear:.1} dB, matched {matched:.1} dB");
    assert!(matched > bilinear + 6.0);
}
