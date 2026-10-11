// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
THE PLUGIN AND THE MOVE, RENDERED SIDE BY SIDE IN ONE PROCESS.

AGENTS.md asks for one DSP behind both shells, and "the plugin sounds like the
Move" used to rest on two constants pinned separately: tests/tg_render_golden.h
hashes the plugin's float split path (tests/render_plugin.c), and
engines/trance-gate/tests/run.sh hashes the Move's int16 vtable path
(render_ref.c). Equal hashes said the two agreed; but every intended change to
the sound re-records both, and two constants re-recorded together hold nothing
to each other.

So this renders the same patch, input and transport through BOTH paths and
compares them sample for sample -- no golden, nothing to re-record:

  plugin  tg_core_create, tg_core_set_param, tg_core_set_meter and
          tg_core_process_f32_split_tap: the calls plugins/trance-gate's
          processor makes, in float on split channels. Its output rounded to
          int16 half away from zero, as render_plugin.c does (f32::round).
  Move    move_audio_fx_init_v2 and its vtable, with the host's clock behind
          get_bpm and get_beat_position, in int16 interleaved -- what the
          chain host calls on the device.

The two must be BYTE-IDENTICAL. The Move's buffer is int16 to begin with, so
the plugin's input is the same int16 values as floats; the gain is the same
code; and process_i16 rounds the way the plugin's output is rounded here. A
difference is a difference between the shells' paths -- a transport read
another way, a parameter routed differently, a rounding -- and the first
differing frame says where.
*/

use crate::tests::{api, hold_the_clock, BEATS, BPM};
use std::ffi::CString;
use std::sync::atomic::Ordering;
use tg_capi::{
    tg_core_create, tg_core_destroy, tg_core_process_f32_split_tap, tg_core_set_meter, tg_core_set_param, TgCore,
    TgTransport,
};

const SR: f64 = 44100.0;
const BLOCK: usize = 128;

/// What happens at the start of one block, to both paths alike.
struct Block {
    /// The transport: `None` stopped, else the beat at the block's first sample.
    beats: Option<f64>,
    bpm: f32,
    /// Parameter writes before the block, as a host or the editor makes them.
    set: Vec<(&'static str, String)>,
}

/// The two shells around their own engine each.
struct Paths {
    plugin: *mut TgCore,
    mv: *mut std::ffi::c_void,
}

impl Paths {
    fn new() -> Self {
        let api = api();
        Paths { plugin: tg_core_create(SR), mv: (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null()) }
    }

    fn set(&self, k: &str, v: &str) {
        let (k, v) = (CString::new(k).unwrap(), CString::new(v).unwrap());
        unsafe { tg_core_set_param(self.plugin, k.as_ptr(), v.as_ptr()) };
        (api().set_param.unwrap())(self.mv, k.as_ptr(), v.as_ptr());
    }

    /// One block of `input` (interleaved int16) through both: (plugin, Move).
    fn process(&self, b: &Block, input: &[i16], with_sweep: bool) -> (Vec<i16>, Vec<i16>) {
        let frames = input.len() / 2;

        /* The plugin: split float channels and a transport struct, shaped as
         * the processor shapes it (ni::wire::host_transport: a stopped
         * transport is running 0 at beat -1), 4/4 as it passes when the host
         * names no meter. */
        let mut l: Vec<f32> = input.iter().step_by(2).map(|&v| f32::from(v)).collect();
        let mut r: Vec<f32> = input.iter().skip(1).step_by(2).map(|&v| f32::from(v)).collect();
        let mut sweep = vec![0f32; frames];
        let t = TgTransport { running: b.beats.is_some() as i32, beats: b.beats.unwrap_or(-1.0), bpm: b.bpm };
        let sweep_ptr = if with_sweep { sweep.as_mut_ptr() } else { std::ptr::null_mut() };
        unsafe {
            tg_core_set_meter(self.plugin, 4, 4);
            tg_core_process_f32_split_tap(self.plugin, l.as_mut_ptr(), r.as_mut_ptr(), sweep_ptr, frames as i32, &t);
        }
        let round = |x: f32| x.round().clamp(-32768.0, 32767.0) as i16;
        let plugin = l.iter().zip(&r).flat_map(|(&a, &b)| [round(a), round(b)]).collect();

        /* The Move: the same block, in place, with the clock the host's two
         * callbacks report -- a negative beat is its stopped transport. */
        BPM.store(f64::from(b.bpm), Ordering::SeqCst);
        BEATS.store(b.beats.unwrap_or(-1.0), Ordering::SeqCst);
        let mut mv = input.to_vec();
        (api().process_block.unwrap())(self.mv, mv.as_mut_ptr(), frames as i32);

        (plugin, mv)
    }
}

impl Drop for Paths {
    fn drop(&mut self) {
        unsafe { tg_core_destroy(self.plugin) };
        (api().destroy_instance.unwrap())(self.mv);
    }
}

/// A deterministic stereo input: a 220 Hz sine on the left, and on the right
/// the same sine under a little noise, so a swapped or summed channel shows.
fn input(blocks: usize) -> Vec<i16> {
    let w = 2.0 * std::f64::consts::PI * 220.0 / SR;
    let mut seed = 0x2545_F491u32;
    (0..blocks * BLOCK)
        .flat_map(|i| {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            let s = 22000.0 * (w * i as f64).sin();
            let noise = f64::from(seed % 2001) - 1000.0;
            [s.round() as i16, (s * 0.8 + noise).round() as i16]
        })
        .collect()
}

/// Render `script` through both paths and hold them to each other.
fn ab(name: &str, patch: &[(&str, String)], script: &[Block]) {
    let _clock = hold_the_clock();
    let paths = Paths::new();
    for (k, v) in patch {
        paths.set(k, v);
    }
    let input = input(script.len());
    let (mut gated, mut differ, mut first) = (0usize, 0usize, None);
    for (n, b) in script.iter().enumerate() {
        for (k, v) in &b.set {
            paths.set(k, v);
        }
        let x = &input[n * BLOCK * 2..(n + 1) * BLOCK * 2];
        let (plugin, mv) = paths.process(b, x, n % 2 == 0);
        for (i, ((p, m), x)) in plugin.iter().zip(&mv).zip(x).enumerate() {
            if p != m {
                differ += 1;
                first.get_or_insert((n * BLOCK + i / 2, i % 2, *x, *p, *m));
            }
            if (i32::from(*m) - i32::from(*x)).abs() > 2 {
                gated += 1;
            }
        }
    }
    if let Some((frame, ch, x, p, m)) = first {
        panic!(
            "{name}: the plugin and the Move differ in {differ} samples; the first is frame {frame} ({}), \
             input {x}: plugin {p}, Move {m}",
            ["left", "right"][ch]
        );
    }
    /* Two paths that both passed the input through would agree trivially. */
    assert!(gated > script.len() * BLOCK / 4, "{name}: the gate hardly gated ({gated} samples), so this proves nothing");
}

/// A running transport from `start`, `blocks` long, at `bpm`.
fn running(start: f64, bpm: f32, blocks: usize) -> Vec<Block> {
    (0..blocks)
        .map(|n| Block { beats: Some(start + (n * BLOCK) as f64 / SR * f64::from(bpm) / 60.0), bpm, set: vec![] })
        .collect()
}

fn stopped(bpm: f32, blocks: usize) -> Vec<Block> {
    (0..blocks).map(|_| Block { beats: None, bpm, set: vec![] }).collect()
}

fn s(k: &'static str, v: &str) -> (&'static str, String) {
    (k, v.to_string())
}

/// The patch render_ref.c and render_plugin.c render: gaps, a tie, per-step
/// amounts, a real envelope, a gate below 1.
fn golden_patch() -> Vec<(&'static str, String)> {
    let mut p = vec![
        s("rate", "1/16"), s("length", "15"), s("pattern", "BEEF"), s("ties", "0022"),
        s("attack", "3.8267"), s("decay", "43.7333"), s("sustain", "0.6"), s("release", "27.3333"),
        s("hold", "0.75"), s("amount", "0.9"),
    ];
    for step in 0..16 {
        p.push(s("cursor", &step.to_string()));
        p.push(s("step_amount", &format!("{:.3}", 0.35 + 0.04 * f64::from(step))));
    }
    p.push(s("cursor", "0"));
    p
}

#[test]
fn the_golden_patch_renders_the_same_through_the_plugin_and_the_move() {
    /* render_ref.c's render, 4 s at 123 BPM: a tempo that does not divide
     * the block, where phase errors show. */
    ab("golden", &golden_patch(), &running(0.0, 123.0, (4.0 * SR) as usize / BLOCK));
}

#[test]
fn a_set_played_live_renders_the_same_through_the_plugin_and_the_move() {
    /* What a performance does that the golden render does not: the transport
     * stopped, then started mid-bar, a tempo change, a jump back, a slot
     * switch to a second sound, edits while it plays, the fade, ms-timed
     * stages and every curve. */
    let mut patch = golden_patch();
    patch.extend([
        s("slot", "3"), s("length", "39"), s("rate", "1/32"), s("pattern", "F0F0A5A5C3"),
        s("time_mode", "1"), s("curve", "2"), s("attack", "4"), s("decay", "20"), s("sustain", "0.3"),
        s("release", "12"), s("hold", "0.5"), s("amount", "1.0"), s("legato", "1"),
        s("fade", "0.6"), s("fade_dir", "1"), s("fade_soft", "1"),
        s("slot", "0"),
    ]);
    let mut script = stopped(128.0, 20);
    script.extend(running(2.75, 128.0, 150));
    script[40].set = vec![s("attack", "12.5"), s("curve", "1")];
    script.extend(running(8.0, 96.5, 120));
    script[200].set = vec![s("slot", "3")];
    script[230].set = vec![s("fade", "0.25"), s("step_amount", "0.8")];
    script.extend(stopped(96.5, 10));
    script.extend(running(1.0, 140.0, 140));
    script[320].set = vec![s("slot", "0"), s("legato", "1")];
    ab("live", &patch, &script);
}
