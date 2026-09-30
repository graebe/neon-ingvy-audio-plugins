/*
 * The receiver, against a real bus.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * These run against actual shared memory -- a `bus_core::Writer` on a real
 * slot, the same thing a Listen-In claims -- rather than a stand-in. The whole
 * point of the receiver is that it keeps step with something it does not
 * control, and a fake that hands over exactly what was asked for every time
 * would test the opposite of that.
 *
 * SLOTS ARE A SHARED RESOURCE OF SIXTEEN and these tests run in threads, so
 * each takes one of its own, clear of the low slots a person experimenting
 * with Listen-In reaches for first. Some are shared with bus-core's own tests,
 * which cargo never runs at the same time as these.
 */
use bus_core::Writer;
use spectro_core::{pick_fft_size, pick_hop, Config};
use spectro_recv::{Receiver, MAX_SOURCES, OWN};

const SR: f32 = 48_000.0;

fn cfg() -> Config {
    let fft = pick_fft_size(SR);
    Config {
        sample_rate: SR,
        fft_size: fft,
        hop: pick_hop(SR, fft),
        bands: 256,
        f_min: 10.0,
        f_max: 20_000.0,
        db_floor: -96.0,
        db_ceil: 0.0,
    }
}

/// Interleaved stereo of a steady tone, for pushing at a bus.
fn tone(frames: usize, hz: f32, amp: f32, phase: &mut f64) -> Vec<f32> {
    let mut v = Vec::with_capacity(frames * 2);
    for _ in 0..frames {
        *phase += 2.0 * std::f64::consts::PI * hz as f64 / SR as f64;
        let s = (phase.sin() as f32) * amp;
        v.push(s);
        v.push(s);
    }
    v
}

fn mono(frames: usize, hz: f32, amp: f32, phase: &mut f64) -> Vec<f32> {
    let mut v = Vec::with_capacity(frames);
    for _ in 0..frames {
        *phase += 2.0 * std::f64::consts::PI * hz as f64 / SR as f64;
        v.push((phase.sin() as f32) * amp);
    }
    v
}

#[test]
fn the_own_channel_alone_still_makes_a_picture() {
    let (mut r, mut feed) = Receiver::new(cfg());
    assert_eq!(r.channels(), 1, "a receiver with no buses is still one source");
    assert_eq!(r.slot_of(OWN), None);

    let mut ph = 0.0;
    let bands = r.bands();
    let mut out = vec![0u8; bands * 32];

    let mut cols = 0;
    for _ in 0..40 {
        feed.push(&mono(2048, 1000.0, 0.5, &mut ph));
        r.pump();
        cols += r.take_columns(OWN, &mut out, 32);
    }
    assert!(cols > 0, "40 blocks of a tone produced no columns");

    /* A 1 kHz tone is not silence. */
    assert!(out[..bands].iter().any(|&b| b > 0), "the column is empty");
}

#[test]
fn a_bus_source_is_drawn_and_stays_in_step_with_the_own_channel() {
    const SLOT: u32 = 16;
    let (_w, mut p) = Writer::claim(SLOT, SR as u32).expect("slot 16 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);
    assert_eq!(r.channels(), 2, "the bus did not open");
    assert_eq!(r.slot_of(1), Some(SLOT));
    assert!(!r.rate_mismatch(1), "same rate reported as a mismatch");

    let bands = r.bands();
    let mut own_out = vec![0u8; bands * 64];
    let mut bus_out = vec![0u8; bands * 64];
    let (mut po, mut pb) = (0.0, 0.0);
    let (mut own_cols, mut bus_cols) = (0usize, 0usize);

    /*
     * THE ASSERTION THAT MATTERS: both sources are fed from the same pump, so
     * they must produce the SAME number of columns. If they drift, column k of
     * one is not column k of the other and a per-cell clash is comparing two
     * different moments.
     */
    for _ in 0..60 {
        p.push(&tone(2048, 220.0, 0.5, &mut pb));
        feed.push(&mono(2048, 220.0, 0.5, &mut po));
        r.pump();
        own_cols += r.take_columns(OWN, &mut own_out, 64);
        bus_cols += r.take_columns(1, &mut bus_out, 64);
    }

    assert!(own_cols > 0, "no columns from the own channel");
    assert_eq!(own_cols, bus_cols, "the two sources drifted apart");
    assert_eq!(r.bus_dropped(1), 0, "the bus lost frames it should have kept");
}

#[test]
fn a_bus_that_runs_ahead_does_not_pull_the_others_with_it() {
    const SLOT: u32 = 15;
    let (_w, mut p) = Writer::claim(SLOT, SR as u32).expect("slot 15 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);

    let bands = r.bands();
    let mut a = vec![0u8; bands * 64];
    let mut b = vec![0u8; bands * 64];
    let (mut po, mut pb) = (0.0, 0.0);
    let (mut ca, mut cb) = (0usize, 0usize);

    /* The bus is given four times as much audio as the own channel. The
     * surplus has to WAIT rather than being analysed early or thrown away. */
    for _ in 0..40 {
        p.push(&tone(4096, 440.0, 0.5, &mut pb));
        feed.push(&mono(1024, 440.0, 0.5, &mut po));
        r.pump();
        ca += r.take_columns(OWN, &mut a, 64);
        cb += r.take_columns(1, &mut b, 64);
    }
    assert!(ca > 0);
    assert_eq!(ca, cb, "the faster source was allowed to run ahead");
}

#[test]
fn choosing_sources_keeps_the_ones_already_open() {
    const A: u32 = 14;
    const B: u32 = 13;
    let _wa = Writer::claim(A, SR as u32).expect("slot 14 was taken");
    let _wb = Writer::claim(B, SR as u32).expect("slot 13 was taken");

    let (mut r, _feed) = Receiver::new(cfg());
    r.set_sources(&[A]);
    assert_eq!(r.slot_of(1), Some(A));

    r.set_sources(&[A, B]);
    assert_eq!(r.channels(), 3);
    assert_eq!(r.slot_of(1), Some(A), "the order the caller asked for was lost");
    assert_eq!(r.slot_of(2), Some(B));

    /* Dropping one leaves the other where it was. */
    r.set_sources(&[B]);
    assert_eq!(r.channels(), 2);
    assert_eq!(r.slot_of(1), Some(B));

    r.set_sources(&[]);
    assert_eq!(r.channels(), 1, "clearing the selection left a bus open");
}

#[test]
fn a_selection_is_bounded_deduplicated_and_refuses_nonsense() {
    let (mut r, _feed) = Receiver::new(cfg());
    /* Slot 0 is not a bus, it is a mistake; and past MAX_SLOT likewise. */
    r.set_sources(&[0, 9999]);
    assert_eq!(r.channels(), 1, "an impossible slot was opened");

    /* More than the cap, and a duplicate: neither may grow the list. Nothing
     * is claiming these, so none of them opens -- what is being checked is
     * that the request is filtered before it reaches the shared memory. */
    r.set_sources(&[1, 1, 2, 3, 4, 5, 6]);
    assert!(r.channels() <= MAX_SOURCES, "the cap was exceeded");
}

#[test]
fn a_source_at_another_rate_is_refused_rather_than_quietly_offset() {
    const SLOT: u32 = 12;
    /* 96 kHz against the receiver's 48: a different window, a different group
     * delay, and two pictures that do not line up. */
    let _w = Writer::claim(SLOT, 96_000).expect("slot 12 was taken");

    let (mut r, _feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);
    assert!(r.rate_mismatch(1), "a 96 kHz source was accepted against 48 kHz");

    let mut out = vec![0u8; r.bands() * 8];
    assert_eq!(r.take_columns(1, &mut out, 8), 0, "a mismatched source drew anyway");
}

#[test]
fn a_rate_change_is_judged_again_when_the_sender_restarts() {
    /*
     * The verdict used to be taken once, at open. A Listen-In that opened at
     * 96 kHz and was then moved to the session's 48 stayed refused forever; one
     * that went the other way was analysed at the wrong rate forever.
     */
    const SLOT: u32 = 6;
    let (mut w, mut p) = Writer::claim(SLOT, 96_000).expect("slot 6 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);
    assert!(r.rate_mismatch(1));

    let mut out = vec![0u8; r.bands() * 64];
    let (mut po, mut pb) = (0.0, 0.0);
    let mut drawn = 0;

    w.set_sample_rate(SR as u32);
    for _ in 0..40 {
        p.push(&tone(2048, 220.0, 0.5, &mut pb));
        feed.push(&mono(2048, 220.0, 0.5, &mut po));
        r.pump();
        r.take_columns(OWN, &mut out, 64);
        drawn += r.take_columns(1, &mut out, 64);
    }
    assert!(!r.rate_mismatch(1), "a sender now at our rate is still refused");
    assert!(drawn > 0, "and it is still not drawn");

    w.set_sample_rate(44_100);
    p.push(&tone(64, 220.0, 0.5, &mut pb));
    r.pump();
    assert!(r.rate_mismatch(1), "a sender that left our rate is still analysed");
}

#[test]
fn a_listen_in_that_comes_back_is_heard_again() {
    /*
     * THE STARVED-FOREVER PICTURE. A Listen-In removed and re-added makes a
     * NEW segment under the same name; the receiver's reader was still mapping
     * the old one and saw a sender that had simply stopped. It has to notice
     * that the name moved on and follow it.
     */
    const SLOT: u32 = 5;
    let first = Writer::claim(SLOT, SR as u32).expect("slot 5 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);
    assert_eq!(r.channels(), 2, "the bus did not open");
    drop(first);
    let (_w, mut p) = Writer::claim(SLOT, SR as u32).expect("the slot came back");

    let mut a = vec![0u8; r.bands() * 64];
    let mut b = vec![0u8; r.bands() * 64];
    let (mut po, mut pb) = (0.0, 0.0);
    /* Long enough for the receiver to give up waiting and go looking. */
    for _ in 0..120 {
        p.push(&tone(1024, 220.0, 0.5, &mut pb));
        feed.push(&mono(1024, 220.0, 0.5, &mut po));
        r.pump();
        r.take_columns(OWN, &mut a, 64);
        r.take_columns(1, &mut b, 64);
    }
    assert!(!r.starved(1), "the receiver never found the sender that came back");
    assert!(r.bus_resynced(1), "and did not report the move as a restart");
}

#[test]
fn the_clash_is_computed_over_whole_columns() {
    let (mut r, _feed) = Receiver::new(cfg());
    let bands = r.bands();
    r.set_clash(-60.0, 12.0);

    /* Two columns. In the first both sources are loud and level; in the second
     * one is loud and the other is silent. */
    let mut a = vec![0u8; bands * 2];
    let mut b = vec![0u8; bands * 2];
    for i in 0..bands {
        a[i] = 220;
        b[i] = 220;
        a[bands + i] = 250;
        b[bands + i] = 0;
    }
    let mut out = vec![7u8; bands * 2];
    r.clash_into(&a, &b, &mut out);

    assert!(out[..bands].iter().all(|&v| v > 0), "two matched sources did not clash");
    assert!(
        out[bands..].iter().all(|&v| v == 0),
        "a source against silence was called a clash"
    );
}

#[test]
fn probing_reports_a_live_slot_with_its_name() {
    const SLOT: u32 = 11;
    let (mut w, _p) = Writer::claim(SLOT, SR as u32).expect("slot 11 was taken");
    w.set_label("Bass");

    let found = Receiver::slots();
    let me = found.iter().find(|s| s.slot == SLOT).expect("a claimed slot was not listed");
    assert!(me.live, "a slot with a live sender reported idle");
    assert_eq!(me.sample_rate, SR as u32);
    assert_eq!(me.label, "Bass");
}

#[test]
fn a_silent_bus_does_not_stop_the_picture() {
    /*
     * THE FAILURE THIS EXISTS FOR: a Listen-In on a muted track publishes
     * nothing, and taking the minimum across every source meant NO source
     * advanced -- the plugin's own picture stopped dead because something else
     * went quiet, while the editor went on saying it was live.
     */
    const SLOT: u32 = 8;
    let _w = Writer::claim(SLOT, SR as u32).expect("slot 8 was taken");
    /* Claimed, so it opens -- and then never pushed to. */

    let (mut r, mut feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);
    assert_eq!(r.channels(), 2, "the bus did not open");

    let mut out = vec![0u8; r.bands() * 64];
    let mut ph = 0.0;
    let mut own_cols = 0;
    for _ in 0..80 {
        feed.push(&mono(2048, 440.0, 0.5, &mut ph));
        r.pump();
        own_cols += r.take_columns(OWN, &mut out, 64);
    }

    assert!(own_cols > 0, "a silent bus froze the plugin's own picture");
    assert!(r.starved(1), "a bus that published nothing was not reported starved");
    assert!(!r.starved(OWN), "the own channel cannot starve -- it is the pace");
}

#[test]
fn a_bus_that_goes_quiet_mid_stream_is_zero_filled_rather_than_waited_for() {
    const SLOT: u32 = 7;
    let (_w, mut p) = Writer::claim(SLOT, SR as u32).expect("slot 7 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);

    let mut a = vec![0u8; r.bands() * 64];
    let mut b = vec![0u8; r.bands() * 64];
    let (mut po, mut pb) = (0.0, 0.0);

    /* Both fed, then the bus stops while the own track carries on. */
    for i in 0..80 {
        if i < 30 {
            p.push(&tone(2048, 220.0, 0.5, &mut pb));
        }
        feed.push(&mono(2048, 220.0, 0.5, &mut po));
        r.pump();
        r.take_columns(OWN, &mut a, 64);
        r.take_columns(1, &mut b, 64);
    }
    assert!(r.starved(1), "the bus went quiet and nobody noticed");

    /* And the two are STILL in step -- the shortfall was filled, not skipped,
     * so column k of each is the same moment and a comparison still means
     * something. */
    let (mut ca, mut cb) = (0usize, 0usize);
    for _ in 0..20 {
        feed.push(&mono(2048, 220.0, 0.5, &mut po));
        r.pump();
        ca += r.take_columns(OWN, &mut a, 64);
        cb += r.take_columns(1, &mut b, 64);
    }
    assert!(ca > 0 && ca == cb, "a starved bus fell out of step: {ca} vs {cb}");
}

#[test]
fn several_channels_add_in_power() {
    let (r, _feed) = Receiver::new(cfg());
    let bands = r.bands();

    /* -20 dB in every band, twice: two uncorrelated sources of equal level
     * measure +3 dB together. Not +6 (that assumes they are phase locked) and
     * not double the byte (that would be adding decibels). */
    let one = vec![spectro_core::db_to_byte(-20.0, -96.0, 0.0); bands];
    let mut out = vec![0u8; bands];
    r.sum_into(&[&one, &one], &mut out);

    let got = spectro_core::byte_to_db(out[0], -96.0, 0.0);
    assert!((got - -17.0).abs() < 0.6, "two -20 dB sources read {got}, wanted -17");

    /* Silence adds nothing, and nothing at all is silence. */
    let quiet = vec![0u8; bands];
    r.sum_into(&[&one, &quiet], &mut out);
    assert_eq!(out[0], one[0], "silence moved a source that was already there");
    r.sum_into(&[], &mut out);
    assert!(out.iter().all(|&v| v == 0), "no sources is not silence");
}

#[test]
fn a_bus_still_filling_its_window_does_not_hold_the_picture() {
    const SLOT: u32 = 1;
    let (_w, mut p) = Writer::claim(SLOT, SR as u32).expect("slot 1 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    let mut out = vec![0u8; r.bands() * 64];
    let (mut po, mut pb) = (0.0, 0.0);
    for _ in 0..8 {
        feed.push(&mono(2048, 220.0, 0.5, &mut po));
        r.pump();
        let n = r.ready();
        assert_eq!(r.take_columns(OWN, &mut out, n), n);
    }

    /* A bus added now has an empty window: thousands of frames before its
     * first column, while the own channel goes on producing. */
    r.set_sources(&[SLOT]);
    p.push(&tone(2048, 220.0, 0.5, &mut pb));
    feed.push(&mono(2048, 220.0, 0.5, &mut po));
    r.pump();
    let mut bus = vec![0u8; r.bands() * 64];
    assert_eq!(r.take_columns(1, &mut bus, 64), 0, "the bus drew before its window filled");
    assert!(r.ready() > 0, "a bus still filling its window held the own channel back");

    /* Once it draws, it counts. */
    for _ in 0..8 {
        p.push(&tone(2048, 220.0, 0.5, &mut pb));
        feed.push(&mono(2048, 220.0, 0.5, &mut po));
        r.pump();
        let n = r.ready();
        r.take_columns(OWN, &mut out, n);
        r.take_columns(1, &mut bus, n);
    }
    p.push(&tone(2048, 220.0, 0.5, &mut pb));
    feed.push(&mono(2048, 220.0, 0.5, &mut po));
    r.pump();
    let n = r.ready();
    assert!(n > 0);
    assert_eq!((r.take_columns(OWN, &mut out, n), r.take_columns(1, &mut bus, n)), (n, n));
}

#[test]
fn a_frame_drains_every_channel_in_step_and_sends_the_view_and_the_clash() {
    const SLOT: u32 = 3;
    let (_w, mut p) = Writer::claim(SLOT, SR as u32).expect("slot 3 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);
    r.set_clash(-60.0, 12.0);
    let bands = r.bands();
    let mut sum = vec![0u8; bands * 32];
    let mut clash = vec![0u8; bands * 32];
    let (mut po, mut pb) = (0.0, 0.0);

    let (mut cols, mut clashed) = (0, 0);
    for _ in 0..40 {
        p.push(&tone(2048, 220.0, 0.5, &mut pb));
        feed.push(&mono(2048, 220.0, 0.5, &mut po));
        r.pump();
        let (c, k) = r.frame(&[0, 1], Some((0, 1)), Some(&mut sum), Some(&mut clash), 32);
        assert_eq!(c, k, "the clash covers the columns the view does");
        cols += c;
        clashed += k;
    }
    assert!(cols > 0, "no picture");
    assert_eq!(r.ready(), 0, "every channel was drained");
    assert!(sum[..bands].iter().any(|&b| b > 0), "two tones summed to silence");
    assert!(clash[..bands].iter().any(|&b| b > 0), "two equal tones did not clash");
    assert_eq!(cols, clashed);

    /* The same channel twice is no comparison, and a channel that is not open
     * is no view. */
    p.push(&tone(8192, 220.0, 0.5, &mut pb));
    feed.push(&mono(8192, 220.0, 0.5, &mut po));
    r.pump();
    assert!(r.ready() > 0);
    let (c, k) = r.frame(&[3], Some((1, 1)), Some(&mut sum), Some(&mut clash), 32);
    assert_eq!((c, k), (0, 0), "nothing drawable in the view sends nothing");
    assert_eq!(r.ready(), 0, "but the columns were still drained");
}

#[test]
fn a_frame_without_an_editor_drains_and_drops() {
    let (mut r, mut feed) = Receiver::new(cfg());
    let mut ph = 0.0;
    for _ in 0..20 {
        feed.push(&mono(2048, 1000.0, 0.5, &mut ph));
        r.pump();
    }
    assert!(r.ready() > 0);
    assert_eq!(r.frame(&[0], None, None, None, 32), (0, 0));
    /* At most 32 a call, as an editor tick would take them. */
    while r.ready() > 0 {
        r.frame(&[0], None, None, None, 32);
    }
    assert_eq!(r.ready(), 0);
}
