/*
 * The receiver with its worker running: two threads, as the plugin runs it.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * receiver.rs pins the pump's behaviour by calling it by hand. These run it
 * the way the Spectrogram does -- `start`, then this thread only pushes and
 * drains while the worker pumps -- and check what that must not change: the
 * same columns, every source in step, source changes adopted mid-stream, and a
 * receiver that can be dropped while its thread is busy.
 */
use std::thread;
use std::time::{Duration, Instant};

use bus_core::Writer;
use spectro_core::{pick_fft_size, pick_hop, Config};
use spectro_recv::{Receiver, OWN};

const SR: f32 = 48_000.0;
const BLOCK: usize = 1024;

fn cfg() -> Config {
    let fft = pick_fft_size(SR);
    Config { sample_rate: SR, fft_size: fft, hop: pick_hop(SR, fft), ..Config::default() }
}

fn mono(frames: usize, hz: f32, phase: &mut f64) -> Vec<f32> {
    (0..frames)
        .map(|_| {
            *phase += 2.0 * std::f64::consts::PI * hz as f64 / SR as f64;
            (phase.sin() as f32) * 0.5
        })
        .collect()
}

fn stereo(m: &[f32]) -> Vec<f32> {
    m.iter().flat_map(|&s| [s, s]).collect()
}

/// Drain until every channel holds `expect` columns, or five seconds pass --
/// the worker runs below this thread's priority on a machine that may be busy,
/// so "quiet for a while" is no proof it has finished. Then a little longer,
/// so a channel that produced too many is caught too.
fn settle(r: &mut Receiver, into: &mut [Vec<u8>], expect: usize) {
    let bands = r.bands();
    let mut buf = vec![0u8; bands * 64];
    let deadline = Instant::now() + Duration::from_secs(5);
    let mut drain = |r: &mut Receiver, into: &mut [Vec<u8>]| {
        for (ch, out) in into.iter_mut().enumerate() {
            let n = r.take_columns(ch, &mut buf, 64);
            out.extend_from_slice(&buf[..n * bands]);
        }
    };
    while Instant::now() < deadline && into.iter().any(|c| c.len() < expect * bands) {
        drain(r, into);
        thread::sleep(Duration::from_millis(2));
    }
    thread::sleep(Duration::from_millis(30));
    drain(r, into);
}

#[test]
fn the_worker_draws_what_pumping_by_hand_draws() {
    let blocks = 60;
    let mut ph = 0.0;
    let audio: Vec<Vec<f32>> = (0..blocks).map(|_| mono(BLOCK, 997.0, &mut ph)).collect();

    /* By hand, the reference. */
    let (mut inline, mut feed) = Receiver::new(cfg());
    let mut want = vec![Vec::new()];
    for b in &audio {
        feed.push(b);
        inline.pump();
    }
    while inline.pump() > 0 {}
    let expect = (blocks * BLOCK - cfg().fft_size) / cfg().hop + 1;
    settle(&mut inline, &mut want, expect);

    /* With the worker, this thread only pushing and draining. */
    let (mut r, mut feed) = Receiver::new(cfg());
    assert!(r.start(), "the worker did not start");
    assert_eq!(r.pump(), 0, "pumping by hand while the worker runs");
    let mut got = vec![Vec::new()];
    let bands = r.bands();
    let mut buf = vec![0u8; bands * 64];
    for b in &audio {
        feed.push(b);
        let n = r.take_columns(OWN, &mut buf, 64);
        got[0].extend_from_slice(&buf[..n * bands]);
        thread::sleep(Duration::from_millis(1));
    }
    settle(&mut r, &mut got, expect);

    assert_eq!(want[0].len() / bands, expect, "the reference is not what it should be");
    assert_eq!(got[0], want[0], "the worker drew different columns");
    assert_eq!(r.own_dropped(), 0);
}

#[test]
fn a_bus_stays_in_step_with_the_own_channel_across_threads() {
    const SLOT: u32 = 4;
    let (_w, mut p) = Writer::claim(SLOT, SR as u32).expect("slot 4 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    r.set_sources(&[SLOT]);
    assert!(r.start());
    assert_eq!(r.channels(), 2);

    let (mut po, mut pb) = (0.0, 0.0);
    let mut cols = vec![Vec::new(), Vec::new()];
    let bands = r.bands();
    let mut buf = vec![0u8; bands * 64];
    let blocks = 80;
    for _ in 0..blocks {
        p.push(&stereo(&mono(BLOCK, 220.0, &mut pb)));
        feed.push(&mono(BLOCK, 330.0, &mut po));
        /*
         * Drained WHILE the worker pushes, the way OnIdle does: whatever
         * instant this lands on, `ready` is a count both channels have, so
         * both hand over the same number.
         */
        let ready = r.ready().min(64);
        let a = r.take_columns(OWN, &mut buf, ready);
        cols[0].extend_from_slice(&buf[..a * bands]);
        let b = r.take_columns(1, &mut buf, ready);
        cols[1].extend_from_slice(&buf[..b * bands]);
        assert_eq!((a, b), (ready, ready), "a drain split the two channels");
        thread::sleep(Duration::from_millis(1));
    }
    let expect = (blocks * BLOCK - cfg().fft_size) / cfg().hop + 1;
    settle(&mut r, &mut cols, expect);

    assert_eq!(cols[0].len() / bands, expect, "the own channel lost or gained columns");
    assert_eq!(cols[1].len(), cols[0].len(), "the two sources drifted apart");
    assert!(!r.starved(1), "a bus fed in step was called starved");
    assert_eq!(r.bus_dropped(1), 0);
    assert_eq!(r.own_dropped(), 0);
}

#[test]
fn sources_change_while_the_worker_runs() {
    const A: u32 = 3;
    const B: u32 = 2;
    let (_wa, mut pa) = Writer::claim(A, SR as u32).expect("slot 3 was taken");
    let (_wb, mut pb) = Writer::claim(B, SR as u32).expect("slot 2 was taken");

    let (mut r, mut feed) = Receiver::new(cfg());
    assert!(r.start());
    let (mut po, mut ph_a, mut ph_b) = (0.0, 0.0, 0.0);
    let bands = r.bands();
    let mut buf = vec![0u8; bands * 64];

    let mut run = |r: &mut Receiver, blocks: usize| -> Vec<usize> {
        let mut n = vec![0usize; r.channels()];
        for _ in 0..blocks {
            pa.push(&stereo(&mono(BLOCK, 220.0, &mut ph_a)));
            pb.push(&stereo(&mono(BLOCK, 440.0, &mut ph_b)));
            feed.push(&mono(BLOCK, 330.0, &mut po));
            thread::sleep(Duration::from_millis(1));
            for (ch, c) in n.iter_mut().enumerate() {
                *c += r.take_columns(ch, &mut buf, 64);
            }
        }
        /* Until every channel has drawn something, however busy the machine. */
        let deadline = Instant::now() + Duration::from_secs(5);
        while n.contains(&0) && Instant::now() < deadline {
            thread::sleep(Duration::from_millis(2));
            for (ch, c) in n.iter_mut().enumerate() {
                *c += r.take_columns(ch, &mut buf, 64);
            }
        }
        n
    };

    r.set_sources(&[A]);
    assert_eq!((r.channels(), r.slot_of(1)), (2, Some(A)));
    let n = run(&mut r, 30);
    assert!(n[1] > 0, "the bus added under a running worker drew nothing");

    r.set_sources(&[A, B]);
    assert_eq!((r.channels(), r.slot_of(1), r.slot_of(2)), (3, Some(A), Some(B)));
    let n = run(&mut r, 30);
    assert!(n[1] > 0 && n[2] > 0, "a source went quiet after the change: {n:?}");

    r.set_sources(&[B]);
    assert_eq!((r.channels(), r.slot_of(1)), (2, Some(B)));
    let n = run(&mut r, 30);
    assert!(n[1] > 0, "the kept bus stopped drawing");

    r.set_sources(&[]);
    assert_eq!(r.channels(), 1);
    let n = run(&mut r, 30);
    assert!(n[0] > 0, "the own channel stopped after the buses went");
}

#[test]
fn dropping_a_busy_receiver_stops_its_thread_promptly() {
    let (mut r, mut feed) = Receiver::new(cfg());
    assert!(r.start());
    assert!(r.start(), "starting twice is not an error");
    let mut ph = 0.0;
    for _ in 0..20 {
        feed.push(&mono(BLOCK, 440.0, &mut ph));
    }
    let t = Instant::now();
    drop(r); /* joins the worker */
    assert!(t.elapsed() < Duration::from_secs(1), "the worker took {:?} to stop", t.elapsed());
    /* The audio thread's end outlives the receiver harmlessly. */
    feed.push(&mono(BLOCK, 440.0, &mut ph));
}
