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

fn columns_after(blocks: usize) -> usize {
    let (fft, hop) = (cfg().fft_size, cfg().hop);
    if blocks * BLOCK < fft {
        0
    } else {
        (blocks * BLOCK - fft) / hop + 1
    }
}

/*
 * NOTHING HERE DEPENDS ON HOW FAST THE WORKER IS. It runs below this thread's
 * priority on a machine that may be busy, so every wait is for a CONDITION --
 * a column count -- and the clock only bounds how long a broken build may take
 * to fail. Audio is pushed no further ahead of what has been drawn than a few
 * blocks, so no ring can overflow however long the worker is descheduled.
 */
const PATIENCE: Duration = Duration::from_secs(20);
const LAG_BLOCKS: usize = 3;

/// Drain every channel `ready()` columns at a time, asserting they hand over
/// the same number -- the property the plugin's sum and clash rest on.
fn drain_in_step(r: &mut Receiver, into: &mut [Vec<u8>], buf: &mut [u8]) {
    let bands = r.bands();
    let ready = r.ready().min(buf.len() / bands);
    for (ch, out) in into.iter_mut().enumerate() {
        let n = r.take_columns(ch, buf, ready);
        assert_eq!(n, ready, "channel {ch} handed over {n} of {ready} ready columns");
        out.extend_from_slice(&buf[..n * bands]);
    }
}

/// Drain, spinning, until channel 0 holds `want` columns.
fn drain_until(r: &mut Receiver, into: &mut [Vec<u8>], buf: &mut [u8], want: usize) {
    let bands = r.bands();
    let deadline = Instant::now() + PATIENCE;
    while into[0].len() < want * bands {
        drain_in_step(r, into, buf);
        assert!(Instant::now() < deadline, "the worker never reached {want} columns");
        thread::yield_now();
    }
}

/// Deterministic noise, so every column differs from its neighbours and an
/// offset of one column cannot pass for alignment.
fn noise(frames: usize, seed: &mut u32) -> Vec<f32> {
    (0..frames)
        .map(|_| {
            *seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            ((*seed >> 8) as f32 / (1u32 << 24) as f32 - 0.5) * 0.5
        })
        .collect()
}

#[test]
fn the_worker_draws_what_pumping_by_hand_draws() {
    let blocks = 60;
    let mut seed = 1;
    let audio: Vec<Vec<f32>> = (0..blocks).map(|_| noise(BLOCK, &mut seed)).collect();
    let expect = columns_after(blocks);

    /* By hand, the reference. */
    let (mut inline, mut feed) = Receiver::new(cfg());
    let bands = inline.bands();
    let mut buf = vec![0u8; bands * 64];
    let mut want = vec![Vec::new()];
    for b in &audio {
        feed.push(b);
        inline.pump();
        drain_in_step(&mut inline, &mut want, &mut buf);
    }
    assert_eq!(want[0].len() / bands, expect, "the reference is not what it should be");

    /* With the worker, this thread only pushing and draining. */
    let (mut r, mut feed) = Receiver::new(cfg());
    assert!(r.start(), "the worker did not start");
    assert_eq!(r.pump(), 0, "pumping by hand while the worker runs");
    let mut got = vec![Vec::new()];
    for (i, b) in audio.iter().enumerate() {
        feed.push(b);
        drain_until(&mut r, &mut got, &mut buf, columns_after((i + 1).saturating_sub(LAG_BLOCKS)));
    }
    drain_until(&mut r, &mut got, &mut buf, expect);

    assert_eq!(got[0].len() / bands, expect);
    assert!(got[0] == want[0], "the worker drew different columns");
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

    let bands = r.bands();
    let mut buf = vec![0u8; bands * 64];
    let mut cols = vec![Vec::new(), Vec::new()];
    let mut seed = 7;
    let blocks: usize = 200;
    for i in 0..blocks {
        /*
         * THE SAME AUDIO ON BOTH, so aligned columns are byte-identical and a
         * one-column slip is visible. Drained continuously, from this thread,
         * while the worker feeds one analyzer after the other -- which is
         * where a drain used to land between them and take a column from the
         * own channel that the bus did not have yet.
         */
        let block = noise(BLOCK, &mut seed);
        p.push(&stereo(&block));
        feed.push(&block);
        drain_until(&mut r, &mut cols, &mut buf, columns_after((i + 1).saturating_sub(LAG_BLOCKS)));
    }
    drain_until(&mut r, &mut cols, &mut buf, columns_after(blocks));

    assert_eq!(cols[0].len() / bands, columns_after(blocks), "the own channel lost or gained columns");
    assert_eq!(cols[1].len(), cols[0].len(), "the two sources drifted apart");
    let first_off = cols[0].chunks(bands).zip(cols[1].chunks(bands)).position(|(a, b)| a != b);
    assert_eq!(first_off, None, "column pairs stopped being the same moment");
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

    /*
     * A bus added mid-stream starts its window when it is added, so it is not
     * in step with the own channel and is drained on its own. Each block waits
     * until the own channel has drawn to within LAG_BLOCKS of it, and the run
     * ends once every channel has drawn -- conditions, not delays.
     */
    let mut own_total = 0usize;
    let mut blocks_total = 0usize;
    let mut run = |r: &mut Receiver, blocks: usize| -> Vec<usize> {
        let mut n = vec![0usize; r.channels()];
        let deadline = Instant::now() + PATIENCE;
        let mut take = |r: &mut Receiver, n: &mut [usize], own_total: &mut usize| {
            for (ch, c) in n.iter_mut().enumerate() {
                let got = r.take_columns(ch, &mut buf, 64);
                *c += got;
                if ch == OWN {
                    *own_total += got;
                }
            }
            assert!(Instant::now() < deadline, "the worker stopped drawing: {n:?}");
            thread::yield_now();
        };
        for _ in 0..blocks {
            pa.push(&stereo(&mono(BLOCK, 220.0, &mut ph_a)));
            pb.push(&stereo(&mono(BLOCK, 440.0, &mut ph_b)));
            feed.push(&mono(BLOCK, 330.0, &mut po));
            blocks_total += 1;
            while own_total < columns_after(blocks_total.saturating_sub(LAG_BLOCKS)) {
                take(r, &mut n, &mut own_total);
            }
        }
        while n.contains(&0) {
            take(r, &mut n, &mut own_total);
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
    /* An upper bound for a broken join, not a timing claim: a stop is one
     * unpark and at most one pump. */
    assert!(t.elapsed() < PATIENCE, "the worker took {:?} to stop", t.elapsed());
    /* The audio thread's end outlives the receiver harmlessly. */
    feed.push(&mono(BLOCK, 440.0, &mut ph));
}
