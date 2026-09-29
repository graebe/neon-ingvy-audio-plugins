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
 * THE NUMBERS ARE NOT PARAMETERS. Band (20-80 Hz), envelope times, the running
 * mean, the ratio, the re-arm, the floor and the refractory are all the design
 * system's, and none of them is settable here. Four plugins share one ground
 * and a per-plugin tuning knob would be four backgrounds that disagreed.
 * crates/ground-core/src/detect.rs records what each one is for.
 *
 * WHY NOT THE SIDE-CHAIN'S DETECTOR, which is forty lines away and already
 * finds onsets: it is broadband, so a snare or a loud vocal moves it. That is
 * right for a ducker and wrong for this, where the design asks specifically for
 * 20-80 Hz -- a background that answers a guitar is a background that never
 * sits still.
 *
 * THE THREAD RULES ARE PART OF THE ABI:
 *
 *   gnd_new / gnd_free                the main thread
 *   gnd_set_sample_rate / gnd_reset   the main thread
 *   gnd_push                          the audio thread, and only it
 *   gnd_fires / gnd_strength          any thread
 *
 * gnd_push allocates nothing, takes no lock and makes no system call.
 * gnd_new does all three, which is why it is not allowed near the audio thread.
 *
 * HOW A PLUGIN USES IT -- the whole of it, and it is the same in all four:
 *
 *     // OnReset
 *     gnd_set_sample_rate(mGround, GetSampleRate());
 *     gnd_reset(mGround);
 *
 *     // ProcessBlock, after the plugin has done its own work
 *     gnd_push(mGround, inputs[0], inputs[nChans > 1 ? 1 : 0], nFrames);
 *
 *     // OnIdle
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
 * Reconfigure for a new sample rate and forget all detector state.
 *
 * The onset count is deliberately NOT reset -- see gnd_fires. The main thread.
 */
void gnd_set_sample_rate(gnd_t *g, double sample_rate);

/*
 * Forget the detector's state, so that a transport stop does not fire an onset
 * on a stale envelope when playback resumes. Leaves the count alone. The main
 * thread.
 */
void gnd_reset(gnd_t *g);

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
 * THE AUDIO THREAD, AND ONLY IT. NULL pointers or a non-positive `frames` are
 * a no-op rather than undefined behaviour: an empty block is ordinary.
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
