/*
 * ground -- the C ABI.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * THE KICK DETECTOR BEHIND THE ANIMATED BACKGROUND. The Ultraviolet design
 * system gives every plugin window a "ground" -- dot paper over noise grain --
 * and that ground is a wave field which rings when a kick lands and is
 * perfectly still otherwise (design/files/project/README.md, section Motion).
 * This library is the part that sees the kick.
 *
 * WHY IT IS DOWN HERE AT ALL, since the field itself is drawn in the editor.
 * The design system's reference implementation detects the kick in the browser,
 * off a Web Audio graph. A plugin editor is a WebView with no AudioContext fed
 * by the host's audio -- there is no way to hand JavaScript the signal. So the
 * detection happens on the audio thread, and what crosses to the editor is an
 * onset: a count and a strength, which the editor turns into one ring.
 *
 * THE NUMBERS ARE NOT PARAMETERS. Band (20-80 Hz), envelope times, refractory
 * and strength range are the design system's; none of them, nor the onset
 * rule, is settable here. Four plugins share one ground and a per-plugin tuning
 * knob would be four backgrounds that disagreed.
 *
 * THE ONSET RULE DEVIATES FROM ULTRAVIOLET 1.0.0, pending a design update: the
 * Motion spec asks for the envelope above 1.8x its 300 ms mean, and this fires
 * on the envelope's excess over a slow bed follower instead, because the
 * specified rule almost never fires on a real mix.
 * crates/ground-core/src/detect.rs states the rule exactly and why.
 *
 * WHY NOT THE SIDE-CHAIN'S DETECTOR, which is forty lines away and already
 * finds onsets: it is broadband, so a snare or a loud vocal moves it. That is
 * right for a ducker and wrong for this, where the design asks specifically for
 * 20-80 Hz -- a background that answers a guitar is a background that never
 * sits still.
 *
 * THE THREAD RULES ARE PART OF THE ABI:
 *
 *   gnd_new / gnd_free                        the main thread
 *   gnd_push                                  the audio thread, one caller
 *                                             at a time
 *   gnd_set_sample_rate / gnd_reset /
 *   gnd_set_active                            any thread
 *   gnd_fires / gnd_strength                  any thread
 *
 * THE AUDIO THREAD OWNS THE DETECTOR. gnd_push is the only function that ever
 * writes it. gnd_set_sample_rate, gnd_reset and gnd_set_active store a REQUEST
 * in an atomic, and the next gnd_push applies it before it reads a sample. So
 * a host that calls OnReset on the audio thread, on the main thread, or on a
 * third thread while a block is in flight gets the same, race-free result.
 *
 * gnd_push allocates nothing, takes no lock and makes no system call.
 * gnd_new does all three, which is why it is not allowed near the audio thread.
 *
 * A NEW DETECTOR IS INACTIVE. It only drives an editor, so it does nothing --
 * gnd_push returns at once -- until gnd_set_active(g, 1), and a plugin turns it
 * off again when the editor closes. Turning it on asks for a reset as well.
 *
 * HOW A PLUGIN USES IT -- the whole of it, and it is the same in all four:
 *
 *     // OnReset
 *     gnd_set_sample_rate(mGround, GetSampleRate());
 *
 *     // OnUIOpen / when the editor window closes
 *     gnd_set_active(mGround, 1);   ...   gnd_set_active(mGround, 0);
 *
 *     // ProcessBlock, after the plugin has done its own work
 *     gnd_push(mGround, inputs[0], inputs[nChans > 1 ? 1 : 0], nFrames);
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
 * (At a physically impossible ten kicks a second that is thirteen years, so
 * this is a correctness habit rather than a live concern -- but it costs a
 * character.)
 *
 * A COUNT AND NOT A FLAG, because the editor reads it 20-50 times a second and
 * kicks can be closer together than that. A flag loses the second of two kicks
 * inside one tick and can be missed entirely if the tick lands between the
 * audio thread setting and clearing it. A count cannot lose an event: the
 * reader compares it against what it saw last. NI Side-Chain's sc_core_fires is
 * the same shape for the same reason.
 *
 * WHAT A READER MAY CONCLUDE, exactly: if the count moved, at least one onset
 * happened, and gnd_strength is the strength of the MOST RECENT one. It is not
 * a queue. Two onsets inside one tick read as +2 and the later strength, which
 * is the honest answer here -- the field sums overlapping kicks anyway, so a
 * ring lost inside 20 ms is one that would have merged with the ring it was
 * lost inside.
 *
 * This header is written by hand rather than generated, and it is the contract:
 * if it and crates/ground-capi/src/lib.rs disagree, they disagree silently.
 * tests/gnd_roundtrip.c compiles against THIS file and links the real library,
 * which is what keeps them honest.
 */
#ifndef GROUND_DETECT_H
#define GROUND_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One detector per plugin instance. Opaque: its size and layout are Rust's. */
typedef struct GndDetector gnd_t;

/*
 * Create a detector for a sample rate.
 *
 * Never returns NULL for a sane rate. A nonsensical one (zero, negative)
 * still allocates and behaves as a pass-through until told the truth, because
 * the alternative is every caller branching on it inside ProcessBlock -- and a
 * host that reports its sample rate late is ordinary.
 *
 * Allocates. The main thread.
 */
gnd_t *gnd_new(double sample_rate);

/* Release a detector. NULL is a no-op. The main thread. */
void gnd_free(gnd_t *g);

/*
 * Ask for a new sample rate and a clean detector. Any thread: the request is
 * applied by the next gnd_push, on the audio thread, before it reads a sample.
 *
 * The onset count is deliberately NOT reset -- see gnd_fires.
 */
void gnd_set_sample_rate(gnd_t *g, double sample_rate);

/*
 * Ask for the detector's state to be forgotten, so that a transport stop does
 * not fire an onset on a stale envelope when playback resumes. Leaves the count
 * alone. Any thread; applied by the next gnd_push.
 */
void gnd_reset(gnd_t *g);

/*
 * Switch the detector on (nonzero) or off (zero). A new detector is OFF.
 *
 * The detector exists to drive an editor, so a plugin switches it on when the
 * editor opens and off when it closes; while it is off, gnd_push returns at
 * once and costs the audio thread nothing. Switching on also asks for a reset,
 * so a reopened editor starts from silence rather than from the hump the
 * detector held when it was closed. Any thread.
 */
void gnd_set_active(gnd_t *g, int32_t active);

/*
 * Feed one block of stereo. A mono plugin passes the same pointer twice.
 *
 * DOUBLES, which is the opposite of what this repository's other engines take,
 * so it is worth saying why. They PROCESS -- a ducker writes its result back
 * into the buffer the host handed over, so the shell converts into a pre-sized
 * float scratch first. This only READS, and iPlug2's `sample` is a double. So a
 * plugin passes `inputs[0]` and `inputs[1]` straight through with no scratch
 * buffer, no chunking loop and no conversion, and the detector's arithmetic was
 * f64 all along.
 *
 * THE AUDIO THREAD, AND ONLY IT -- never two calls at once on one handle, which
 * is what lets it own the detector without a lock. A pending reset or rate
 * change is applied first; an inactive detector returns at once. NULL pointers
 * or a non-positive `frames` are a no-op rather than undefined behaviour: an
 * empty block is ordinary.
 *
 * The detector is the maximum of the two channels AFTER the band, not before,
 * so a kick panned hard left reads the same as a centred one. (Before the band
 * would be a rectifier in front of a band-pass, which silently destroys the
 * frequency information the band selects -- crates/ground-core/src/detect.rs
 * says more.)
 */
void gnd_push(gnd_t *g, const double *left, const double *right, int32_t frames);

/*
 * The monotonic onset count. WATCH IT CHANGE -- its absolute value means
 * nothing, and neither gnd_reset nor gnd_set_sample_rate moves it, precisely so
 * that a reader comparing it against its last value cannot be shown an onset
 * that never happened.
 *
 * Any thread. NULL reads 0.
 */
uint32_t gnd_fires(const gnd_t *g);

/*
 * The most recent onset's strength, 0.3 to 1 -- what the editor hands the
 * field. Meaningless until gnd_fires has moved at least once, where it reads 0.
 *
 * The floor is 0.3 and not 0 because a kick that only just crossed the
 * threshold still has to move the ground visibly; the ceiling is 1 because the
 * field's source gain is calibrated for that range.
 *
 * Any thread. NULL reads 0.
 */
float gnd_strength(const gnd_t *g);

#ifdef __cplusplus
}
#endif

#endif /* GROUND_DETECT_H */
