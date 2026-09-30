/*
 * What the whole picture costs, timed rather than asserted.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 *     cargo test --release -p spectro-recv --test bench -- --ignored --nocapture
 *
 * Four sources at 96 kHz -- the own channel and three real buses -- for ten
 * seconds of audio, doing per tick what the Spectrogram does with the result:
 * pump, drain every channel, sum the view, compute the clash. Only the
 * receiver's calls are on the clock; generating and publishing the test audio
 * is not.
 *
 * Twice: pumped by hand, which times the whole pipeline on one thread; and
 * with the worker started, which times what is left on the message thread.
 *
 * Ignored by default because a number from a debug build means nothing.
 */
use std::time::{Duration, Instant};

use bus_core::Writer;
use spectro_core::{pick_fft_size, pick_hop, Config};
use spectro_recv::{Receiver, MAX_SOURCES, OWN};

const SR: f32 = 96_000.0;
const SECONDS: usize = 10;
const BLOCK: usize = 1024;

/// A deterministic noise-plus-tone source, so every channel has a full spectrum.
struct Source {
    seed: u32,
    phase: f64,
    hz: f64,
}

impl Source {
    fn next(&mut self) -> f32 {
        self.seed = self.seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        let noise = (self.seed >> 8) as f32 / (1u32 << 24) as f32 - 0.5;
        self.phase += 2.0 * std::f64::consts::PI * self.hz / SR as f64;
        0.1 * noise + 0.4 * self.phase.sin() as f32
    }
}

#[test]
#[ignore]
fn four_sources_at_96k_for_ten_seconds() {
    const SLOTS: [u32; 3] = [7, 8, 9];
    let mut pushers = Vec::new();
    let mut writers = Vec::new();
    for s in SLOTS {
        let (w, p) = Writer::claim(s, SR as u32).expect("a bench slot was taken");
        writers.push(w);
        pushers.push(p);
    }

    for threaded in [false, true] {
        let fft = pick_fft_size(SR);
        let cfg = Config { sample_rate: SR, fft_size: fft, hop: pick_hop(SR, fft), ..Config::default() };
        let (mut r, mut feed) = Receiver::new(cfg);
        r.set_sources(&SLOTS);
        assert_eq!(r.channels(), MAX_SOURCES);
        if threaded {
            assert!(r.start());
        }

        let bands = r.bands();
        let max_cols = 16;
        let mut chans = vec![vec![0u8; bands * max_cols]; MAX_SOURCES];
        let mut sum = vec![0u8; bands * max_cols];
        let mut clash = vec![0u8; bands * max_cols];

        let mut srcs: Vec<Source> = (0..MAX_SOURCES)
            .map(|i| Source { seed: 17 + i as u32, phase: 0.0, hz: 110.0 * (i + 1) as f64 })
            .collect();
        let mut mono = vec![0.0f32; BLOCK];
        let mut stereo = vec![0.0f32; BLOCK * 2];

        let mut busy = Duration::ZERO;
        let mut columns = 0usize;
        let blocks = SECONDS * SR as usize / BLOCK;
        let mut tick = |r: &mut Receiver, busy: &mut Duration, columns: &mut usize| {
            let t = Instant::now();
            r.pump();
            let ready = r.ready().min(max_cols);
            let mut common = usize::MAX;
            for (ch, buf) in chans.iter_mut().enumerate() {
                common = common.min(r.take_columns(ch, buf, ready));
            }
            if common > 0 && common != usize::MAX {
                let n = common * bands;
                let view: [&[u8]; MAX_SOURCES] = core::array::from_fn(|i| &chans[i][..n]);
                r.sum_into(&view, &mut sum[..n]);
                r.clash_into(&chans[OWN][..n], &chans[1][..n], &mut clash[..n]);
                *columns += common;
            }
            *busy += t.elapsed();
        };
        for _ in 0..blocks {
            for s in mono.iter_mut() {
                *s = srcs[0].next();
            }
            feed.push(&mono);
            for (p, src) in pushers.iter_mut().zip(srcs.iter_mut().skip(1)) {
                for f in stereo.chunks_exact_mut(2) {
                    let v = src.next();
                    f[0] = v;
                    f[1] = v;
                }
                p.push(&stereo);
            }
            tick(&mut r, &mut busy, &mut columns);
            if threaded {
                /* About ten times real time: well inside what the worker keeps up with. */
                std::thread::sleep(Duration::from_millis(1));
            }
        }
        if threaded {
            let end = Instant::now() + Duration::from_secs(5);
            while columns < 460 && Instant::now() < end {
                std::thread::sleep(Duration::from_millis(5));
                tick(&mut r, &mut busy, &mut columns);
            }
        }

        let what = if threaded { "message thread, worker running" } else { "whole pipeline, pumped by hand" };
        println!(
            "{what}: 4 sources @ 96 kHz, {SECONDS} s of audio: {:.1} ms busy, {:.2}% of one core, {columns} columns",
            busy.as_secs_f64() * 1e3,
            100.0 * busy.as_secs_f64() / SECONDS as f64
        );
        assert!(columns > 0);
        assert_eq!(r.own_dropped(), 0);
    }
}
