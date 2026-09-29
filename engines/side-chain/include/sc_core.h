/*
 * NI Side-Chain -- the C ABI over the Rust ducker engine.
 *
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THIS HEADER IS WRITTEN BY HAND RATHER THAN GENERATED, AND IT IS THE
 * CONTRACT. If it and crates/sc-capi/src/lib.rs disagree, they disagree
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

#ifndef SC_CORE_H
#define SC_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The opaque instance. */
typedef struct sc_instance sc_core_t;

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
} sc_transport_t;

/*
 * The longest block the key buffer holds. A host handing over more than this
 * must be chunked by the shell -- which already chunks for its own dry-copy
 * buffers. Past this point a block gets a key signal for its first
 * SC_MAX_BLOCK frames and silence after, so do not rely on it.
 */
#define SC_MAX_BLOCK 8192

/* Big enough for every readout below, `params` included. */
#define SC_STATE_MAX 4096

/* ------------------------------------------------------------ lifecycle */

sc_core_t *sc_core_create(double sample_rate);
void         sc_core_destroy(sc_core_t *c);
void         sc_core_set_sample_rate(sc_core_t *c, double sample_rate);
double       sc_core_get_sample_rate(const sc_core_t *c);

/* Open the gate now and forget the trigger: a panic. */
void         sc_core_reset(sc_core_t *c);

/* --------------------------------------------------------------- inputs */

/*
 * The key signal for the block about to be rendered. Call BEFORE process.
 *
 * The engine clears it afterwards, so a block with no key is SILENCE rather
 * than the previous block held -- a shell that forgets to push does not get a
 * stale trigger out of it.
 */
void sc_core_push_key_f32(sc_core_t *c, const float *l, const float *r,
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
void sc_core_set_key_connected(sc_core_t *c, int connected);

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
void sc_core_on_midi(sc_core_t *c, const uint8_t *msg, int len, int at);

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
void sc_core_process_f32_split(sc_core_t *c, float *l, float *r, int frames,
                                 const sc_transport_t *t);
/*
 * The split path, tapping two per-sample values the editor needs. Either
 * out-pointer may be NULL; both NULL is the plain split path above.
 *
 * `gain` is the MULTIPLIER APPLIED, 0..1, with Depth already in it -- so a trace
 * drawn from it is the effect the listener heard, not the envelope behind it.
 *
 * WHY THE ENGINE HANDS THAT OUT rather than the shell deriving it: the editor
 * draws what the ducker actually DID beside what it was asked to do, and the two
 * differ whenever a trigger interrupts a recovery. The shell has the dry and the
 * wet and could divide one by the other, but that answer is meaningless wherever
 * the input is near silence -- which is exactly where a duck is most visible.
 *
 * `sweep` is where each sample sits on the display axis, 0..1 -- see
 * sc_core_sweep01. The shell bins its capture columns by it, and CANNOT
 * compute it itself without becoming a second copy of the phase logic: the
 * phase-locked loop's per-block correction, the saturation past one cycle, and
 * the difference between the three sources all feed it. The first time any of
 * those changed, a shell-side copy would stop matching the sound in a way that
 * looks like a drawing bug.
 */
void sc_core_process_f32_split_tap(sc_core_t *c, float *l, float *r,
                                     float *gain, float *sweep, int frames,
                                     const sc_transport_t *t);
void sc_core_process_f32(sc_core_t *c, float *lr, int frames,
                           const sc_transport_t *t);
void sc_core_process_i16(sc_core_t *c, int16_t *lr, int frames,
                           const sc_transport_t *t);

/* ----------------------------------------------------------- parameters */

/*
 * The parameters, in the order the shell declares them so a HOST INDEX IS AN
 * ENGINE INDEX.
 *
 * APPEND ONLY. The numeric form is what a host stores in a session, so
 * inserting a parameter re-points every project saved by the build before it.
 */
typedef enum {
    SC_P_SOURCE = 0,   /* 0 Cycle, 1 MIDI, 2 Sidechain                  */
    SC_P_RATE,         /* index into the rate table, see sc_core_get_param("rate_label") */
    SC_P_TIME_MODE,    /* 0 ms, 1 % of cycle -- A DISPLAY CHOICE, see below */
    SC_P_DELAY,        /* -100..100 percent of the cycle; NEGATIVE IS EARLY */
    SC_P_ATTACK,       /* 0..200  percent of the cycle                   */
    SC_P_HOLD,         /* 0..200  percent of the cycle                   */
    SC_P_RELEASE,      /* 0..200  percent of the cycle                   */
    SC_P_DEPTH,        /* 0..1                                           */
    SC_P_CURVE,        /* 0 Linear, 1 Exponential, 2 S-Curve             */
    SC_P_CHANNEL,      /* 0 Omni, 1..16                                  */
    SC_P_NOTE,         /* 0..127                                         */
    SC_P_MIDI_MODE,    /* 0 Trigger, 1 Gate                              */
    SC_P_VEL_SENS,     /* 0..1                                           */
    SC_P_THRESHOLD,    /* dB, -60..0. -60 means "anything triggers"      */
    SC_P_LOCKOUT,      /* ms, 0..200                                     */
    SC_P_COUNT
} sc_param_t;

/*
 * DELAY IS TWO MECHANISMS, AND THE SOURCE DECIDES WHICH.
 *
 * On CYCLE it is a phase offset on the trigger rather than a wait -- the cycle
 * is PERIODIC, so firing at 80% of it is the same event as firing 20% before
 * the next beat. That is what makes a NEGATIVE delay possible without
 * anticipating anything: "early" is a position already passed. A positive delay
 * lands on exactly the same samples either way, so it goes through the phase
 * too and there is one mechanism rather than two.
 *
 * On MIDI and SIDECHAIN it is a wait after the trigger, because a note that has
 * not arrived cannot be ducked ahead of. A negative delay is CLAMPED TO ZERO
 * there rather than refused: an automation lane is entitled to sweep through
 * it, and the editor says which sources can use it.
 *
 * THE TIME UNIT IS A PERCENTAGE OF THE CYCLE, ALWAYS, AND SC_P_TIME_MODE IS
 * A DISPLAY CHOICE.
 *
 * Stated here because a reader coming from a compressor will expect otherwise.
 * The four stage lengths are percentages of the current cycle; ms and % are
 * two readings of one number, and sc_core_get_param("stage_ms") gives the
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
void   sc_core_set_num(sc_core_t *c, sc_param_t param, double value);
double sc_core_get_num(const sc_core_t *c, sc_param_t param);

/* Returns nonzero if the key was recognised, so a shell can chain its own. */
int    sc_core_set_param(sc_core_t *c, const char *key, const char *val);

/*
 * Returns the length WRITTEN, excluding the terminator, or -1 for a key this
 * engine does not own. The buffer is always terminated.
 *
 * NOT SNPRINTF'S RETURN. snprintf reports what would have fit; this reports
 * what did, so the result is always <= buf_len - 1 and TRUNCATION IS SILENT --
 * a short buffer looks exactly like a short value. Sizing a buffer from the
 * return value therefore does not work; pass SC_STATE_MAX, which is sized
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
 *                   source:rate:ms_cycle:sweep:advancing:fires:duck:key:connected:stage:phase
 *                   ELEVEN fields, positional -- App.jsx parses it by position,
 *                   so this is a contract and not a debug dump.
 *   "sweep"         the shared display axis, 0..1 -- see sc_core_sweep01
 *   "params"        every automatable value, in sc_param_t order, colon
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
int    sc_core_get_param(const sc_core_t *c, const char *key, char *buf,
                           int buf_len);

/* ------------------------------------------------- audio-thread reads */

double sc_core_phase01(const sc_core_t *c);

/*
 * Where the display window has got to, 0..1. THE WINDOW IS ONE CYCLE LONG, and
 * this is the axis both the shape editor and the signal scope are drawn on --
 * which is what makes them one picture rather than two stacked ones.
 *
 * One definition for all three sources: on Cycle it is the transport's phase; on
 * MIDI and Sidechain, which have no transport phase, it is the time since the
 * last trigger over one cycle. It SATURATES at 1.0 rather than wrapping, so a
 * source that has not fired again parks at the right-hand edge instead of
 * drawing a dip that never happened.
 *
 * The shell indexes its capture columns by this. A column is therefore written
 * once per cycle -- twice a second at 1/4 and 120 bpm, which reads as a live
 * waveform; once every four seconds at 1/1 and 60 bpm, where the picture really
 * is that old. The alternative is a rolling window that does not line up with
 * the editor, which is a worse picture that merely looks fresher.
 */
double sc_core_sweep01(const sc_core_t *c);
float  sc_core_duck(const sc_core_t *c);
uint32_t sc_core_fires(const sc_core_t *c);

/* ------------------------------------------------------- test hooks */

/*
 * The shape, and its inverse.
 *
 * NOT FOR THE PLUGIN. These exist so tests/shape_table.c can generate the
 * fixture that the C and the JavaScript copies of the curve maths are each
 * pinned to -- generated BY THE ENGINE rather than transcribed from it, which
 * is the only version of that test worth having.
 *
 * THEY TOOK A DIRECTION AND NO LONGER DO. A fourth curve, `Pump`, was
 * asymmetric -- linear going down, a cubic ease-out coming back -- so these
 * took a `dir` and the other three ignored it. That curve is gone and the
 * argument went with it, rather than staying as one every caller passes and no
 * curve reads.
 */
double sc_test_shape(int curve, double t);
double sc_test_shape_inv(int curve, double w);

#ifdef __cplusplus
}
#endif

#endif /* SC_CORE_H */
