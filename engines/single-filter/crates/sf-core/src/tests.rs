// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! What each product's table is, frozen; that each plays its design, at any
//! block size, without a click; its lanes, its panic, its text door, its
//! tail.

use super::*;
use ni_filter_node::design::{design, prototype};

const FS: f64 = 48_000.0;

fn keys(shape: Shape) -> Vec<&'static str> {
    params::layout(shape).iter().map(|&f| params::info(shape, f).key).collect()
}

#[test]
fn each_products_table_is_frozen() {
    /* The host index is what a session stores: these never change, and a
     * new parameter is appended. */
    let cut = ["freq", "q", "order", "family", "ripple", "stopband", "matched", "channel", "output", "analyzer"];
    let band = ["freq", "q", "order", "family", "ripple", "stopband", "channel", "output", "analyzer"];
    assert_eq!(keys(Shape::HighPass), cut);
    assert_eq!(keys(Shape::LowPass), cut);
    assert_eq!(keys(Shape::BandPass), band);
    assert_eq!(keys(Shape::Notch), band);
    assert_eq!(keys(Shape::Bell), ["freq", "gain", "q", "adaptive_q", "channel", "output", "analyzer"]);
    assert_eq!(keys(Shape::LowShelf), ["freq", "gain", "q", "order", "channel", "output", "analyzer"]);
    assert_eq!(keys(Shape::HighShelf), ["freq", "gain", "q", "order", "channel", "output", "analyzer"]);
    assert_eq!(keys(Shape::Tilt), ["freq", "gain", "channel", "output", "analyzer"]);
    assert_eq!(keys(Shape::AllPass), ["freq", "q", "order", "channel", "output", "analyzer"]);
    assert!(keys(Shape::Empty).is_empty());
    assert_eq!(SHAPES.len(), 9);
}

#[test]
fn each_field_reads_as_filter_designers_and_starts_where_the_product_is_useful() {
    let label = |shape: Shape, f: Field, i: i32| params::info(shape, f).kind.label(i);
    assert_eq!(label(Shape::HighPass, Field::Order, 7), Some("48 dB/oct"));
    assert_eq!(label(Shape::LowShelf, Field::Order, 3), Some("24 dB/oct"));
    assert_eq!(label(Shape::LowShelf, Field::Order, 4), None, "a shelf's slopes stop at 24 dB/oct");
    assert_eq!(label(Shape::AllPass, Field::Order, 0), Some("1st order"));
    assert_eq!(params::info(Shape::AllPass, Field::Order).name, "Order");
    assert_eq!(params::info(Shape::HighPass, Field::Order).name, "Slope");
    assert_eq!(label(Shape::HighPass, Field::Family, 5), Some("Linkwitz-Riley"));
    assert_eq!(label(Shape::BandPass, Field::Family, 5), None, "a Linkwitz-Riley band-pass plays as a Butterworth");
    assert_eq!(label(Shape::Bell, Field::Channel, 4), Some("Side"));
    let freq = |shape| params::info(shape, Field::Freq).kind.default_value();
    assert_eq!(
        SHAPES.map(freq),
        [100.0, 10_000.0, 1000.0, 1000.0, 1000.0, 200.0, 5000.0, 1000.0, 1000.0],
        "High Pass, Low Pass, Band Pass, Notch, Bell, Low Shelf, High Shelf, Tilt, All Pass"
    );
    for shape in SHAPES {
        let d = params::defaults(shape);
        assert_eq!(d[Field::Order.slot()], 1.0, "12 dB/oct");
        assert_eq!(d[Field::Analyzer.slot()], 3.0, "the analyzer shows both");
        assert!(!params::info(shape, Field::Analyzer).kind.default_value().is_nan());
        assert!(matches!(params::info(shape, Field::Analyzer).kind, params::Kind::Choice { automatable: false, .. }));
        for (i, &f) in params::layout(shape).iter().enumerate() {
            assert_eq!(params::field_at(shape, i), Some(f));
            assert_eq!(params::index_of(shape, f), Some(i));
            assert_eq!(params::from_key(shape, params::info(shape, f).key), Some(f));
        }
        assert_eq!(params::field_at(shape, params::layout(shape).len()), None);
    }
    assert_eq!(params::index_of(Shape::HighPass, Field::Gain), None);
    assert_eq!(params::from_key(Shape::Tilt, "q"), None);
}

#[test]
fn the_words_are_filter_designers() {
    let text = |shape, f, v| {
        let mut s = String::new();
        params::write_value(shape, f, &mut s, v).unwrap();
        s
    };
    assert_eq!(text(Shape::HighPass, Field::Freq, 1200.0), "1.20 kHz");
    assert_eq!(text(Shape::Bell, Field::Gain, 3.0), "+3.0 dB");
    assert_eq!(text(Shape::Bell, Field::Q, core::f64::consts::FRAC_1_SQRT_2), "0.71");
    assert_eq!(text(Shape::HighPass, Field::Family, 4.0), "Elliptic");
    assert_eq!(text(Shape::HighPass, Field::Output, f64::NAN), "0.0 dB");
    assert_eq!(params::parse_value(Shape::LowPass, Field::Freq, "1.2k"), Some(1200.0));
    assert_eq!(params::parse_value(Shape::LowPass, Field::Channel, "side"), Some(4.0));
    assert_eq!(params::parse_value(Shape::LowPass, Field::Q, "wide"), None);
}

fn engine(shape: Shape, set: &[(Field, f64)]) -> Engine {
    let mut e = Engine::new(shape, FS).unwrap();
    for &(f, v) in set {
        e.set(f, v);
    }
    e.reset();
    e
}

fn sine(hz: f64, n: usize) -> Vec<f32> {
    (0..n).map(|i| (2.0 * core::f64::consts::PI * hz * i as f64 / FS).sin() as f32 * 0.5).collect()
}

/// The amplitude of `hz` in `x`, by projection: `x` holds whole periods.
fn amplitude(x: &[f32], hz: f64) -> f64 {
    let w = 2.0 * core::f64::consts::PI * hz / FS;
    let (s, c) = x
        .iter()
        .enumerate()
        .fold((0.0, 0.0), |(s, c), (i, &v)| (s + f64::from(v) * (w * i as f64).sin(), c + f64::from(v) * (w * i as f64).cos()));
    2.0 * s.hypot(c) / x.len() as f64
}

#[test]
fn each_product_plays_its_design_and_its_output() {
    for shape in SHAPES {
        let set = [(Field::Freq, 900.0), (Field::Gain, 7.5), (Field::Q, 2.0), (Field::Order, 3.0), (Field::Family, 2.0), (Field::Output, -6.0)];
        let mut e = engine(shape, &set);
        let s = target_of(shape, e.values()).settings;
        let (c, _) = design(&s, prototype(&s).as_ref(), Globals::NEUTRAL, FS);
        for hz in [300.0, 900.0, 3000.0] {
            let mut l = sine(hz, 24_000);
            let mut r = l.clone();
            e.process_f32(&mut l, Some(&mut r));
            let got = 20.0 * (amplitude(&l[19_200..], hz) / 0.5).log10();
            let want = c.db(hz, FS) - 6.0;
            assert!((got - want).abs() < 0.01 || want < -80.0, "{shape:?} at {hz} Hz: {got} dB, the design {want} dB");
            assert_eq!(l, r);
        }
    }
}

#[test]
fn a_render_is_the_same_bits_at_any_block_size_and_a_sweep_is_smooth() {
    let render = |shape: Shape, block: usize| {
        let mut e = engine(shape, &[]);
        let (mut out_l, mut out_r) = (Vec::new(), Vec::new());
        let x = sine(440.0, 19_200);
        for (k, chunk) in x.chunks(1920).enumerate() {
            e.set(Field::Freq, 100.0 * 1.5f64.powi(k as i32));
            e.set(Field::Order, (k % 4) as f64);
            e.set(Field::Family, (k % 5) as f64);
            e.set(Field::Channel, (k / 3 % 5) as f64);
            let (mut l, mut r) = (chunk.to_vec(), chunk.iter().map(|v| -v * 0.5).collect::<Vec<_>>());
            for (a, b) in l.chunks_mut(block).zip(r.chunks_mut(block)) {
                e.process_f32(a, Some(b));
            }
            out_l.extend(l);
            out_r.extend(r);
        }
        (out_l, out_r)
    };
    for shape in SHAPES {
        let a = render(shape, 1920);
        for block in [1, 13, 64, 500] {
            assert!(render(shape, block) == a, "{shape:?}, block {block}");
        }
        /* No click: no step from one sample to the next far beyond what the
         * input itself moves. A 440 Hz sine at 0.5 moves at most 0.03 a
         * sample. */
        let worst = [&a.0, &a.1].iter().flat_map(|x| x.windows(2)).map(|w| (w[1] - w[0]).abs()).fold(0.0f32, f32::max);
        assert!(worst < 0.25, "{shape:?}: a step of {worst}");
    }
}

#[test]
fn the_channel_says_which_half_is_filtered() {
    let mut e = engine(Shape::LowPass, &[(Field::Freq, 500.0), (Field::Order, 7.0), (Field::Channel, 1.0)]);
    let (mut l, mut r) = (sine(5000.0, 9600), sine(5000.0, 9600));
    e.process_f32(&mut l, Some(&mut r));
    assert!(amplitude(&l[4800..], 5000.0) < 1e-3 && (amplitude(&r[4800..], 5000.0) - 0.5).abs() < 1e-6, "Left only");
    /* A mono track is its own left; a Side filter does nothing to it. */
    let mut e = engine(Shape::LowPass, &[(Field::Freq, 500.0), (Field::Channel, 4.0)]);
    let mut m = sine(5000.0, 4800);
    let want = m.clone();
    e.process_f32(&mut m, None);
    assert_eq!(m, want);
}

#[test]
fn the_panic_empties_the_filter_and_the_settings_stay() {
    let mut e = engine(Shape::Notch, &[(Field::Family, 4.0), (Field::Order, 7.0)]);
    let (mut l, mut r) = (sine(1000.0, 4800), sine(1000.0, 4800));
    e.process_f32(&mut l, Some(&mut r));
    assert!(!e.is_silent());
    e.on_midi(&[0xB3, 120, 0]);
    assert!(e.is_silent(), "CC 120 on any channel");
    e.process_f32(&mut l, Some(&mut r));
    e.on_midi(&[0xB0, 123, 0]);
    assert!(e.is_silent(), "CC 123");
    e.process_f32(&mut l, Some(&mut r));
    e.on_midi(&[0xB0, 7, 100]);
    assert!(!e.is_silent(), "any other message does nothing");
    assert!(e.set_param("panic", ""));
    assert!(e.is_silent());
    assert_eq!(e.get(Field::Family), 4.0);
}

#[test]
fn the_text_door_speaks_keys() {
    let mut e = Engine::new(Shape::HighShelf, FS).unwrap();
    assert!(e.set_param("freq", "3.5k"));
    assert_eq!(e.get(Field::Freq), 3500.0);
    assert!(e.set_param("gain", "nonsense"), "a key it owns, with no value in the text");
    assert_eq!(e.get(Field::Gain), 0.0);
    assert!(!e.set_param("family", "Elliptic"), "a shelf has no family");
    let mut out = [0u8; 32];
    let n = e.get_param("freq", &mut out);
    assert_eq!(&out[..n as usize], b"3500");
    assert_eq!(e.get_param("matched", &mut out), -1);
    e.set(Field::Matched, 1.0);
    assert_eq!(e.get(Field::Matched), 0.0, "a field the product lacks keeps its default");
    e.set(Field::Freq, f64::NAN);
    assert_eq!(e.get(Field::Freq), 3500.0);
    e.set(Field::Freq, 1e9);
    assert_eq!(e.get(Field::Freq), 22_000.0);
    assert!(Engine::new(Shape::Empty, FS).is_none());
    assert_eq!(e.shape(), Shape::HighShelf);
}

#[test]
fn the_rate_and_the_tail() {
    let mut e = Engine::new(Shape::HighPass, 0.0).unwrap();
    assert_eq!(e.sample_rate(), DEFAULT_SAMPLE_RATE);
    e.set_sample_rate(96_000.0);
    assert_eq!(e.sample_rate(), 96_000.0);
    e.set_sample_rate(96_000.0);
    let low = tail_seconds(Shape::HighPass, &[20.0, 0.0, 10.0, 0.0, 7.0, 4.0, 1.0, 60.0, 0.0, 0.0, 0.0, 3.0], FS);
    let high = tail_seconds(Shape::HighPass, &params::defaults(Shape::HighPass), FS);
    assert!(low > high && low <= TAIL_MAX_SECONDS, "a steep, resonant 20 Hz cut rings longest: {low} > {high}");
    assert_eq!(tail_seconds(Shape::HighPass, &params::defaults(Shape::HighPass), 0.0), 0.0);
    assert_eq!(tail_seconds(Shape::Empty, &params::defaults(Shape::HighPass), FS), 0.0);
}

#[test]
fn the_analyzer_keeps_the_lane_it_is_asked_for_and_listening_filters_nothing() {
    let mut e = engine(Shape::LowPass, &[(Field::Freq, 200.0)]);
    e.set_watching(true);
    e.set_lane(Lane::Side);
    let (mut l, mut r) = (sine(1000.0, 1024), vec![0.0f32; 1024]);
    let before = l.clone();
    e.listen_f32(&l, Some(&r));
    assert_eq!(l, before, "listening filters nothing");
    let (mut pre, mut post) = (vec![0.0; RING], vec![0.0; RING]);
    e.rings(&mut pre, &mut post);
    assert_eq!(pre[RING - 1], 0.5 * before[1023], "the side of a hard-left source");
    e.process_f32(&mut l, Some(&mut r));
    e.rings(&mut pre, &mut post);
    assert!(post[RING - 1].abs() < pre[RING - 1].abs() + 1e-6);
    assert!(!e.node().cascade().is_empty());
}

#[test]
fn adaptive_q_narrows_a_boosting_bell() {
    let wide = engine(Shape::Bell, &[(Field::Gain, 18.0)]);
    let narrow = engine(Shape::Bell, &[(Field::Gain, 18.0), (Field::AdaptiveQ, 1.0)]);
    let at = |e: &Engine| e.node().cascade().db(2000.0, FS);
    assert!(at(&narrow) < at(&wide) - 3.0, "an octave up, the adaptive bell is well down: {} vs {}", at(&narrow), at(&wide));
}
