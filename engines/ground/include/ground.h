// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ground -- the C ABI.
 *
 * THE BEAT CLOCK BEHIND THE ANIMATED BACKGROUND. The Ultraviolet design system
 * gives every plugin window a "ground" -- dot paper over noise grain -- and
 * that ground is a wave field that rings and is perfectly still otherwise
 * (design/scheme/project/README.md, section Motion). This library decides WHEN
 * it rings, from the host's transport:
 *
 *   - one ring on every quarter note while the transport plays;
 *   - a strong ring on each bar's downbeat, the bar being num*4/den quarters
 *     (4/4 when the host reports no time signature);
 *   - nothing while it is stopped.
 *
 * It reads no audio, so every plugin rings identically, on a silent track as
 * on a drum bus. crates/ground-core/src/beat.rs states the rule exactly,
 * including starts, loops, seeks and meters whose bars are not whole quarters.
 *
 * A DEVIATION FROM ULTRAVIOLET 1.0.0, by the owner's decision: the design's
 * Motion spec drives the ground from the sound (a 20-80 Hz onset detector).
 * docs/tech/ground.md says why it follows the tempo instead.
 *
 * WHY IT IS DOWN HERE AT ALL, since the field itself is drawn in the editor.
 * A plugin editor is a WebView: it cannot see the host's transport, and its
 * timers are neither sample-accurate nor running when the host bounces. So
 * the beat is found on the audio thread, against the host's own clock, and
 * what crosses to the editor is a ring: a count and a strength.
 *
 * THE RULE IS NOT A PARAMETER. Four plugins share one ground and a per-plugin
 * knob would be four backgrounds that disagreed.
 *
 * THE THREAD RULES ARE PART OF THE ABI:
 *
 *   gnd_new / gnd_free                        the main thread
 *   gnd_tick                                  the audio thread, one caller
 *                                             at a time
 *   gnd_set_sample_rate / gnd_reset /
 *   gnd_set_active                            any thread
 *   gnd_fires / gnd_strength                  any thread
 *
 * THE AUDIO THREAD OWNS THE CLOCK. gnd_tick is the only function that ever
 * writes it. gnd_set_sample_rate, gnd_reset and gnd_set_active store a REQUEST
 * in an atomic, and the next gnd_tick applies it first. So a host that calls
 * OnReset on the audio thread, on the main thread, or on a third thread while
 * a block is in flight gets the same, race-free result.
 *
 * gnd_tick allocates nothing, takes no lock and makes no system call.
 * gnd_new allocates, which is why it is not allowed near the audio thread.
 *
 * A NEW GROUND IS INACTIVE. It only drives an editor, so it does nothing --
 * gnd_tick returns at once -- until gnd_set_active(g, 1), and a plugin turns it
 * off again when the editor closes. Turning it on asks for a reset as well.
 *
 * HOW A PLUGIN USES IT -- the whole of it, and ni::WebPlugin does it for all
 * four:
 *
 *     // OnReset
 *     gnd_set_sample_rate(mGround, GetSampleRate());
 *
 *     // OnUIOpen / when the editor window closes
 *     gnd_set_active(mGround, 1);   ...   gnd_set_active(mGround, 0);
 *
 *     // ProcessBlock, once a block, from the host's transport
 *     gnd_tick(mGround, GetPPQPos(), GetTempo(), num, den,
 *              GetTransportIsRunning(), nFrames);
 *
 *     // OnIdle -- FIRST, before anything in OnIdle can return early
 *     const uint32_t fires = gnd_fires(mGround);
 *     if (fires != mGroundFires) {          // != and not >, so a wrap is fine
 *         mGroundFires = fires;
 *         // send gnd_strength(mGround) to the editor
 *     }
 *
 * THE COUNT IS COMPARED FOR INEQUALITY, NOT ORDER. It is monotonic but it
 * wraps, and `fires > mGroundFires` would go permanently false at the wrap.
 *
 * A COUNT AND NOT A FLAG, because the editor reads it 20-50 times a second and
 * a flag can be missed entirely if the tick lands between the audio thread
 * setting and clearing it. A count cannot lose an event: the reader compares
 * it against what it saw last.
 *
 * WHAT A READER MAY CONCLUDE, exactly: if the count moved, at least one ring
 * happened, and gnd_strength is the strength of the MOST RECENT one. It is not
 * a queue. Two rings inside one idle tick read as +2 and the later strength --
 * the field sums overlapping rings anyway.
 *
 * This header is written by hand rather than generated, and it is the contract:
 * if it and crates/ground-capi/src/lib.rs disagree, they disagree silently.
 * tests/gnd_roundtrip.c compiles against THIS file and links the real library,
 * which is what keeps them honest.
 */
#ifndef GROUND_H
#define GROUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One ground per plugin instance. Opaque: its size and layout are Rust's. */
typedef struct GndGround gnd_t;

/*
 * Create a ground for a sample rate.
 *
 * Never returns NULL. A nonsensical rate (zero, negative) still allocates and
 * rings nothing until told the truth, because the alternative is every caller
 * branching on it inside ProcessBlock -- and a host that reports its sample
 * rate late is ordinary.
 *
 * Allocates. The main thread.
 */
gnd_t *gnd_new(double sample_rate);

/* Release a ground. NULL is a no-op. The main thread. */
void gnd_free(gnd_t *g);

/*
 * Ask for a new sample rate and a fresh clock. Any thread: the request is
 * applied by the next gnd_tick, on the audio thread, before anything else.
 *
 * The ring count is deliberately NOT reset -- see gnd_fires.
 */
void gnd_set_sample_rate(gnd_t *g, double sample_rate);

/*
 * Ask for the clock to forget the last block, so the next playing block is a
 * fresh start that rings only what it holds. Leaves the count alone. Any
 * thread; applied by the next gnd_tick.
 */
void gnd_reset(gnd_t *g);

/*
 * Switch the ground on (nonzero) or off (zero). A new ground is OFF.
 *
 * The ground exists to drive an editor, so a plugin switches it on when the
 * editor opens and off when it closes; while it is off, gnd_tick returns at
 * once and costs the audio thread nothing. Switching on also asks for a reset,
 * so a reopened editor rings no backlog. Any thread.
 */
void gnd_set_active(gnd_t *g, int32_t active);

/*
 * One block of the host's transport.
 *
 *   ppq       the position at the block's first sample, in quarter notes
 *   bpm       quarter notes per minute; not positive (or not finite) is 120
 *   num, den  the time signature; either not positive is 4/4
 *   playing   nonzero while the transport plays; zero rings nothing
 *   frames    the block's length in samples; not positive is a no-op
 *
 * The values are the host's as iPlug2 reports them -- GetPPQPos, GetTempo,
 * GetTimeSig, GetTransportIsRunning -- and are passed through unjudged: the
 * rules for a missing or nonsensical one are here, once, rather than in four
 * plugins.
 *
 * THE AUDIO THREAD, AND ONLY IT -- never two calls at once on one handle,
 * which is what lets it own the clock without a lock. Once a block, before or
 * after the plugin's own audio: it reads no samples. A pending reset or rate
 * change is applied first; an inactive ground returns at once.
 */
void gnd_tick(gnd_t *g, double ppq, double bpm, int32_t num, int32_t den, int32_t playing,
              int32_t frames);

/*
 * The monotonic ring count. WATCH IT CHANGE -- its absolute value means
 * nothing, and neither gnd_reset nor gnd_set_sample_rate moves it, precisely so
 * that a reader comparing it against its last value cannot be shown a ring
 * that never happened.
 *
 * Any thread. NULL reads 0.
 */
uint32_t gnd_fires(const gnd_t *g);

/*
 * The most recent ring's strength -- what the editor hands the field:
 *
 *   1.0   a bar's downbeat
 *   0.4   every other beat
 *
 * Meaningless until gnd_fires has moved at least once, where it reads 0. The
 * field is linear in strength, so a beat is a ring two fifths the height of a
 * downbeat (docs/tech/ground.md: why 0.4).
 *
 * Any thread. NULL reads 0.
 */
float gnd_strength(const gnd_t *g);

#ifdef __cplusplus
}
#endif

#endif /* GROUND_H */
