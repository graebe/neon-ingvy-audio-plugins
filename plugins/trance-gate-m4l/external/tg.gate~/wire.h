/*
 * The M4L shell's wire arithmetic, on its own so it can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THIS IS NOT IN tg.gate~.c, and it is the same reason
 * plugins/trance-gate/Wire.h gives for the plugin: everything in that file is
 * either a method on a Max class or a perform routine, so none of it can be
 * reached without building an external and loading Max around it. A test that
 * needs a DAW is a test nobody runs.
 *
 * What moved is only the arithmetic. NO MAX TYPE APPEARS BELOW -- not t_atom,
 * not t_itm, not t_object -- which is what lets tests/render_m4l.c link it
 * against nothing but the engine.
 *
 * THESE TWO ARE WHERE THIS SHELL CAN BE SILENTLY WRONG:
 *
 *   tg_m4l_transport    Max counts in TICKS and the engine counts in BEATS.
 *                       Get the divisor wrong and the gate still gates -- at
 *                       the wrong tempo, which reads as "the pattern drifts"
 *                       rather than as a bug with a location.
 *
 *   tg_m4l_param_value  Three of the twelve are one-based to a user and
 *                       zero-based to the engine, three more are percentages
 *                       to a user and 0..1 to the engine, and three MORE are
 *                       percentages to both. A wrong one here is a knob that
 *                       works but means something else.
 */
#ifndef TG_M4L_WIRE_H
#define TG_M4L_WIRE_H

#include "trance_gate_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MAX'S FIXED INTERNAL RESOLUTION, 480 ticks to a quarter note. It is not a
 * preference and not derived from the Live Set's time signature, so it does
 * not move when either does.
 */
#define TG_TICKS_PER_BEAT 480.0

/*
 * The engine's "no transport". Not a stale position and not zero -- a stopped
 * transport is NO beat, and conflating the two makes the gate resume
 * mid-pattern on play instead of from the top. TranceGate.cpp says the same
 * thing at its own transport, and for the same reason.
 */
#define TG_NO_BEAT (-1.0)

/*
 * Max's transport, as the engine wants it. `running` is itm_getstate, `ticks`
 * is itm_getticks, `bpm` is itm_gettempo.
 *
 * A non-positive bpm becomes 120: Live reports one before the transport has
 * settled, and dividing a step length by zero puts an infinity into the
 * engine's phase, which does not come back out.
 */
tg_transport_t tg_m4l_transport(int running, double ticks, double bpm);

/*
 * A patcher-side value for parameter `idx`, in the units the engine wants.
 * Out-of-range indices return `v` untouched -- the caller drops them, and
 * this function has no way to say so.
 */
double tg_m4l_param_value(int idx, double v);

#ifdef __cplusplus
}
#endif
#endif /* TG_M4L_WIRE_H */
