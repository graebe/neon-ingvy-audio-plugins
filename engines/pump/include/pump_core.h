/*
 * NI Pump -- the C ABI over the Rust ducker engine.
 *
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THIS HEADER IS WRITTEN BY HAND RATHER THAN GENERATED, AND IT IS THE
 * CONTRACT. If it and crates/pump-capi/src/lib.rs disagree, they disagree
 * silently: a mismatched signature links fine and corrupts the stack at the
 * call. What keeps them honest is that tests/test_core.c compiles against THIS
 * file and links the real library.
 *
 * THREADING. Every function here may be called from the audio callback and
 * none of them allocate, lock or unwind. The exceptions are create/destroy,
 * which allocate and must not be called from the callback.
 *
 * WHAT THE ENGINE DOES NOT KNOW: buses, plugin formats, webviews, or which
 * host it is in. It is handed audio, optionally a key signal, optionally a
 * transport, and MIDI with sample offsets.
 */

#ifndef PUMP_CORE_H
#define PUMP_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The opaque instance. */
typedef struct pump_instance pump_core_t;

/*
 * What the host says about the transport.
 *
 * `beats` IS QUARTER NOTES SINCE THE START OF THE TIMELINE, and a NEGATIVE
 * value means "no transport" -- which is not the same as beat zero. A stopped
 * host must never report a stale position: pass running = 0 and beats = -1.
 *
 * Passing NULL for the whole struct means the host told us nothing at all. The
 * engine then keeps the last tempo it saw, which is what lets a MIDI- or
 * sidechain-triggered duck work in a session with the timeline parked.
 */
typedef struct {
    int    running;
    double beats;
    float  bpm;
} pump_transport_t;

/*
 * The longest block the key buffer holds. A host handing over more than this
 * must be chunked by the shell -- which already chunks for its own dry-copy
 * buffers. Past this point a block gets a key signal for its first
 * PUMP_MAX_BLOCK frames and silence after, so do not rely on it.
 */
#define PUMP_MAX_BLOCK 8192

/* Big enough for every readout below, `params` included. */
#define PUMP_STATE_MAX 4096

/* ------------------------------------------------------------ lifecycle */

pump_core_t *pump_core_create(double sample_rate);
void         pump_core_destroy(pump_core_t *c);
void         pump_core_set_sample_rate(pump_core_t *c, double sample_rate);
double       pump_core_get_sample_rate(const pump_core_t *c);

/* Open the gate now and forget the trigger: a panic. */
void         pump_core_reset(pump_core_t *c);

/* --------------------------------------------------------------- inputs */

/*
 * The key signal for the block about to be rendered. Call BEFORE process.
 *
 * The engine clears it afterwards, so a block with no key is SILENCE rather
 * than the previous block held -- a shell that forgets to push does not get a
 * stale trigger out of it.
 */
void pump_core_push_key_f32(pump_core_t *c, const float *l, const float *r,
                            int frames);

/*
 * Whether an aux bus is actually patched.
 *
 * The engine does not infer this, and must not: an unconnected bus and a
 * silent one are the same block of zeroes, and the difference is precisely
 * what the UI has to report. "No key" is a different message from "nothing is
 * playing", and only the shell can tell them apart --
 * IsChannelConnected(ERoute::kInput, 2).
 */
void pump_core_set_key_connected(pump_core_t *c, int connected);

/*
 * Queue a MIDI message at `at`, a sample offset within the NEXT block.
 *
 * `at` IS NOT OPTIONAL TO GET RIGHT. Passing 0 for everything costs up to a
 * full buffer of jitter on the one event whose timing is the entire effect --
 * and it is jitter, not latency, so it cannot be compensated. iPlug2 has it in
 * IMidiMsg::mOffset. The Schwung audio_fx v2 on_midi has no such field and
 * passes 0, which is what makes the Live/Move render A/B comparable.
 *
 * An offset past the end of the block is CLAMPED into it rather than dropped:
 * the queue is cleared per block, so an event the sample walk never reaches is
 * an event that never happens, and a host reporting an offset against a
 * different buffer size is a real thing.
 *
 * CC 120 (All Sound Off) and CC 123 (All Notes Off) open the gate, whatever
 * the note filter says. That is the only channel a host panic can reach a
 * Schwung module through -- the v2 vtable has no reset hook.
 */
void pump_core_on_midi(pump_core_t *c, const uint8_t *msg, int len, int at);

/* -------------------------------------------------------------- process */

/*
 * In place, and THE DRY COPY IS THE CALLER'S JOB: the plugin keeps one so it
 * can draw the input behind the output.
 *
 * The float paths do not clamp, deliberately. A ducker only ever ATTENUATES --
 * the gain is in 0..1 -- so it cannot push a signal out of range, and a host
 * is entitled to headroom above 1.0 that we must not steal. The i16 path
 * clamps because i16 has no headroom, and asymmetrically because i16 is:
 * -32768 is representable and +32768 is not.
 *
 * ONE GAIN LAW, THREE BUFFER FORMATS. Move hands over int16 interleaved; VST3,
 * AU and CLAP hand over float, usually as separate channel pointers.
 */
void pump_core_process_f32_split(pump_core_t *c, float *l, float *r, int frames,
                                 const pump_transport_t *t);
void pump_core_process_f32(pump_core_t *c, float *lr, int frames,
                           const pump_transport_t *t);
void pump_core_process_i16(pump_core_t *c, int16_t *lr, int frames,
                           const pump_transport_t *t);

/* ----------------------------------------------------------- parameters */

/*
 * The parameters, in the order the shell declares them so a HOST INDEX IS AN
 * ENGINE INDEX.
 *
 * APPEND ONLY. The numeric form is what a host stores in a session, so
 * inserting a parameter re-points every project saved by the build before it.
 */
typedef enum {
    PUMP_P_SOURCE = 0,   /* 0 Cycle, 1 MIDI, 2 Sidechain                  */
    PUMP_P_RATE,         /* index into the rate table, see pump_core_get_param("rate_label") */
    PUMP_P_TIME_MODE,    /* 0 ms, 1 % of cycle -- A DISPLAY CHOICE, see below */
    PUMP_P_DELAY,        /* 0..100  percent of the cycle                   */
    PUMP_P_ATTACK,       /* 0..200  percent of the cycle                   */
    PUMP_P_HOLD,         /* 0..200  percent of the cycle                   */
    PUMP_P_RELEASE,      /* 0..200  percent of the cycle                   */
    PUMP_P_DEPTH,        /* 0..1                                           */
    PUMP_P_CURVE,        /* 0 Linear, 1 Exponential, 2 S-Curve, 3 Pump     */
    PUMP_P_CHANNEL,      /* 0 Omni, 1..16                                  */
    PUMP_P_NOTE,         /* 0..127                                         */
    PUMP_P_MIDI_MODE,    /* 0 Trigger, 1 Gate                              */
    PUMP_P_VEL_SENS,     /* 0..1                                           */
    PUMP_P_THRESHOLD,    /* dB, -60..0. -60 means "anything triggers"      */
    PUMP_P_LOCKOUT,      /* ms, 0..200                                     */
    PUMP_P_COUNT
} pump_param_t;

/*
 * THE TIME UNIT IS A PERCENTAGE OF THE CYCLE, ALWAYS, AND PUMP_P_TIME_MODE IS
 * A DISPLAY CHOICE.
 *
 * Stated here because a reader coming from a compressor will expect otherwise.
 * The four stage lengths are percentages of the current cycle; ms and % are
 * two readings of one number, and pump_core_get_param("stage_ms") gives the
 * millisecond reading.
 *
 * A parameter whose MEANING depended on another parameter would be one whose
 * automation lane changes what it does when something else moves. And the
 * musical default for a ducker is the relative one anyway: a shape
 * proportional to the cycle keeps its proportions when the tempo or the rate
 * changes, which is what "in time with the music" means.
 *
 * TWO DOORS, AND WHY BOTH.
 *
 * set_num takes an enum and a number and is for host automation on the audio
 * thread. set_param takes strings and is for a Schwung chain_params shell, a
 * typed value, or a saved state.
 *
 * The second door is not a convenience: atof honours LC_NUMERIC, so in a host
 * running under a comma-decimal locale "0.750" parses as 0. set_param is
 * implemented VIA set_num, so every clamp exists exactly once and the string
 * door's only extra job is deciding which number a word means -- it accepts
 * either a label ("1/8", "S-Curve", "Omni") or an index.
 *
 * Out-of-range values are CLAMPED, never rejected: a host is allowed to send a
 * normalised value that rounds outside the range, and refusing it would freeze
 * the parameter rather than move it. A NaN is DROPPED, because clamping one
 * propagates it and a NaN gain silences a track permanently.
 */
void   pump_core_set_num(pump_core_t *c, pump_param_t param, double value);
double pump_core_get_num(const pump_core_t *c, pump_param_t param);

/* Returns nonzero if the key was recognised, so a shell can chain its own. */
int    pump_core_set_param(pump_core_t *c, const char *key, const char *val);

/*
 * Returns the length WRITTEN, excluding the terminator, or -1 for a key this
 * engine does not own. The buffer is always terminated.
 *
 * NOT SNPRINTF'S RETURN. snprintf reports what would have fit; this reports
 * what did, so the result is always <= buf_len - 1 and TRUNCATION IS SILENT --
 * a short buffer looks exactly like a short value. Sizing a buffer from the
 * return value therefore does not work; pass PUMP_STATE_MAX, which is sized
 * for the longest readout here, and treat a result of buf_len - 1 as a bug in
 * the caller rather than a value.
 *
 * (`Buf` in fmt.rs does track the would-have-fit length. It is deliberately
 * not returned, matching tg_core_get_param, so that both engines' ABIs answer
 * this question the same way. Do not "fix" one of them alone.)
 *
 * Keys:
 *
 *   "ui"            ONE READ FOR THE WHOLE ANIMATED PICTURE, pushed per frame:
 *                   source:rate:ms_cycle:phase:advancing:fires:duck:key:connected:stage
 *                   Ten fields, positional -- App.jsx parses it by position, so
 *                   this is a contract and not a debug dump.
 *   "params"        every automatable value, in pump_param_t order, colon
 *                   separated, at full round-trip float precision
 *   "stage_ms"      delay:attack:hold:release, in milliseconds
 *   "phase"         cycle phase 0..1
 *   "ms_per_cycle"  the cycle length in ms
 *   "fires"         monotonic trigger count -- watch it CHANGE
 *   "duck"          current attenuation 0..1
 *   "key_level"     the detector's smoothed level
 *   "advancing"     1 while the transport is running
 *   "dropped"       MIDI events lost to queue overflow, a diagnostic
 *   "rate_label"    "1/8" -- so no shell re-spells a table the engine owns
 *   "curve_label"   "S-Curve"
 *   "source_label"  "Sidechain"
 *   <any param key> "attack", "depth", "trigger_note", ... the numeric value
 *
 * WHAT get_param MUST NOT DO: it runs on the audio callback too, so it does
 * not allocate and it does not compute. Everything it reports that costs
 * arithmetic is published once per block by the process call.
 */
int    pump_core_get_param(const pump_core_t *c, const char *key, char *buf,
                           int buf_len);

/* ------------------------------------------------- audio-thread reads */

double pump_core_phase01(const pump_core_t *c);
float  pump_core_duck(const pump_core_t *c);
uint32_t pump_core_fires(const pump_core_t *c);

/* ------------------------------------------------------- test hooks */

/*
 * The shape, and its inverse. `dir` is 0 for the duck going down and 1 for the
 * recovery; three of the four curves ignore it and Pump does not.
 *
 * NOT FOR THE PLUGIN. These exist so tests/shape_table.c can generate the
 * fixture that the C and the JavaScript copies of the curve maths are each
 * pinned to -- generated BY THE ENGINE rather than transcribed from it, which
 * is the only version of that test worth having.
 */
double pump_test_shape(int curve, double t, int dir);
double pump_test_shape_inv(int curve, double w, int dir);

#ifdef __cplusplus
}
#endif

#endif /* PUMP_CORE_H */
