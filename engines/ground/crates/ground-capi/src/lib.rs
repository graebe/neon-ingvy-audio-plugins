// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The C ABI: what every plugin in this repository links to ring its ground on
the beat -- and the source of ground.h, which build.rs generates from this
file (cbindgen's configuration is cbindgen/ground.toml), these doc comments
included.

Scalars cross the boundary. No callbacks, no shared structs, no ownership
passing either way except the one opaque handle.

THE SURFACE IS EIGHT FUNCTIONS because the consumer is four plugins that want
the same few lines of code each. The musical rule -- a ring a beat, the
downbeat strongest, nothing while stopped -- is ground-core's, and none of it
is configurable: a plugin that could tune it would be a plugin whose
background disagreed with the others.

THE BEAT CLOCK BEHIND THE ANIMATED BACKGROUND. The Ultraviolet design system
gives every plugin window a "ground" -- dot paper over noise grain -- and
that ground is a wave field that rings and is perfectly still otherwise
(design/scheme/project/README.md, section Motion). This library decides WHEN
it rings, from the host's transport:

  - one ring on every quarter note while the transport plays;
  - a strong ring on each bar's downbeat, the bar being num*4/den quarters
    (4/4 when the host reports no time signature);
  - nothing while it is stopped.

It reads no audio, so every plugin rings identically, on a silent track as
on a drum bus. crates/ground-core/src/beat.rs states the rule exactly,
including starts, loops, seeks and meters whose bars are not whole quarters.

Ultraviolet 1.0.0 drove the ground from the sound (a 20-80 Hz onset
detector); this followed the tempo instead by the owner's decision, and
Ultraviolet 1.1.0 made that the design's rule. docs/tech/ground.md says why.

WHY IT IS DOWN HERE AT ALL, since the field itself is drawn in the editor.
A plugin editor lives on the message thread: it cannot see the host's
transport, and its timers are neither sample-accurate nor running when the
host bounces. So
the beat is found on the audio thread, against the host's own clock, and
what crosses to the editor is a ring: a count and a strength.

THE RULE IS NOT A PARAMETER. Four plugins share one ground and a per-plugin
knob would be four backgrounds that disagreed.

THE THREAD RULES ARE PART OF THE ABI:

  gnd_new / gnd_free                        the main thread
  gnd_tick                                  the audio thread, one caller
                                            at a time
  gnd_set_sample_rate / gnd_reset /
  gnd_set_active                            any thread
  gnd_fires / gnd_strength                  any thread

THE AUDIO THREAD OWNS THE CLOCK. gnd_tick is the only function that ever
writes it. gnd_set_sample_rate, gnd_reset and gnd_set_active store a REQUEST
in an atomic, and the next gnd_tick applies it first. So a host that calls
OnReset on the audio thread, on the main thread, or on a third thread while
a block is in flight gets the same, race-free result.

gnd_tick allocates nothing, takes no lock and makes no system call.
gnd_new allocates, which is why it is not allowed near the audio thread.

A NEW GROUND IS INACTIVE. It only drives an editor, so it does nothing --
gnd_tick returns at once -- until gnd_set_active(g, 1), and a plugin turns it
off again when the editor closes. Turning it on asks for a reset as well.

HOW A PLUGIN USES IT -- the whole of it, and ni::GroundClock
(plugins/_shared/juce/GroundClock.h) does it for every product:

```c
// prepareToPlay
gnd_set_sample_rate(g, sampleRate);  gnd_reset(g);

// when an editor opens / closes
gnd_set_active(g, 1);   ...   gnd_set_active(g, 0);

// every audio block, bypassed ones too, before the audio, from the host
gnd_tick(g, ppq, bpm, numerator, denominator, playing, frames);

// the editor's frame, on the message thread
const uint32_t fires = gnd_fires(g);
if (fires != seen) {          // != and not >, so a wrap is fine
    seen = fires;
    // ring the field with gnd_strength(g)
}
```

THE COUNT IS COMPARED FOR INEQUALITY, NOT ORDER. It is monotonic but it
wraps, and `fires > mGroundFires` would go permanently false at the wrap.

A COUNT AND NOT A FLAG, because the editor reads it 20-50 times a second and
a flag can be missed entirely if the tick lands between the audio thread
setting and clearing it. A count cannot lose an event: the reader compares
it against what it saw last.

WHAT A READER MAY CONCLUDE, exactly: if the count moved, at least one ring
happened, and gnd_strength is the strength of the MOST RECENT one. It is not
a queue. Two rings inside one idle tick read as +2 and the later strength --
the field sums overlapping rings anyway.
*/

use ground_core::{Ground, Transport};

/// The opaque handle. One per plugin instance.
pub struct GndGround(Ground);

/// Create a ground for a sample rate. Never returns null; a nonsensical rate
/// still allocates and rings nothing until it is told the truth, because the
/// alternative is a plugin that has to branch on it in `ProcessBlock`.
///
/// Allocates. The main thread only.
#[no_mangle]
pub extern "C" fn gnd_new(sample_rate: f64) -> *mut GndGround {
    Box::into_raw(Box::new(GndGround(Ground::new(sample_rate))))
}

/// Release a ground. Null is a no-op.
///
/// # Safety
/// `g` must come from `gnd_new` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn gnd_free(g: *mut GndGround) {
    if !g.is_null() {
        drop(Box::from_raw(g));
    }
}

/// Ask for a new sample rate and a fresh clock. The ring count is
/// deliberately NOT reset -- see `gnd_fires`.
///
/// Any thread: this stores a request, and the audio thread applies it at the
/// top of its next `gnd_tick`. It never touches the clock itself, so it
/// cannot race a block in progress -- which matters because hosts do not
/// promise to call `OnReset` off the audio thread.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_set_sample_rate(g: *mut GndGround, sample_rate: f64) {
    if let Some(d) = g.as_ref() {
        d.0.set_sample_rate(sample_rate);
    }
}

/// Ask for the clock to forget the last block, without touching the count:
/// the next playing block is then a fresh start, which rings only what it
/// holds.
///
/// Any thread; applied by the next `gnd_tick`, as `gnd_set_sample_rate` is.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_reset(g: *mut GndGround) {
    if let Some(d) = g.as_ref() {
        d.0.reset();
    }
}

/// Switch the ground on (nonzero) or off (zero). A new ground is OFF.
///
/// The ground only drives an editor, so a plugin switches it on when its
/// editor opens and off when it closes; while off, `gnd_tick` returns at once.
/// Switching on also asks for a reset, so a reopened editor's first block is a
/// fresh start and rings no backlog.
///
/// Any thread.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_set_active(g: *mut GndGround, active: i32) {
    if let Some(d) = g.as_ref() {
        d.0.set_active(active != 0);
    }
}

/// One block of the host's transport: its position at the block's first
/// sample in quarter notes, its tempo, its time signature, whether it is
/// playing, and how many samples the block holds. See `Ground::tick` and
/// ground-core's beat.rs for what rings.
///
/// Every value is taken as the host reported it: a time signature that is not
/// positive is 4/4, a tempo that is not is 120, and a position that is not
/// finite is a stopped transport. A non-positive `frames` is a no-op.
///
/// THE AUDIO THREAD, AND ONLY IT -- one caller at a time, which is what makes
/// the clock it owns safe to mutate through a shared handle. Allocates
/// nothing, takes no lock, makes no system call.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null, and no other `gnd_tick`
/// on the same handle may be running.
#[no_mangle]
pub unsafe extern "C" fn gnd_tick(
    g: *mut GndGround,
    ppq: f64,
    bpm: f64,
    num: i32,
    den: i32,
    playing: i32,
    frames: i32,
) {
    if frames <= 0 {
        return;
    }
    let Some(d) = g.as_ref() else { return };
    d.0.tick(&Transport { playing: playing != 0, ppq, bpm, num, den }, frames as usize);
}

/// The monotonic ring count. WATCH IT CHANGE -- its absolute value means
/// nothing, and it is not reset by `gnd_reset` or `gnd_set_sample_rate`
/// precisely so that a reader comparing it against what it last saw cannot be
/// tricked into seeing a ring that never happened.
///
/// Any thread. Null reads 0.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_fires(g: *const GndGround) -> u32 {
    g.as_ref().map_or(0, |d| d.0.fires())
}

/// The most recent ring's strength -- 1 for a downbeat, 0.4 for any other
/// beat -- which the editor passes to the field. Meaningless until `gnd_fires`
/// has moved, where it reads 0.
///
/// Any thread. Null reads 0.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_strength(g: *const GndGround) -> f32 {
    g.as_ref().map_or(0.0, |d| d.0.strength())
}

#[cfg(test)]
mod tests;
