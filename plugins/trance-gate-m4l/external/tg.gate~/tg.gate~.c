/*
 * tg.gate~ -- the Trance Gate engine as a Max/MSP object.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THIS EXISTS. Ableton Live draws a third-party plugin's editor in a
 * separate floating window and nothing else; the device row in the chain gets
 * Live's generic parameter strip. For a step sequencer that is backwards --
 * the pattern IS the instrument, and it is the one thing the chain cannot
 * show. A Max for Live device can draw whatever it likes inline, so there is
 * a fourth shell.
 *
 * IT IS A SHELL, NOT A PORT. This file holds no DSP. The engine is the same
 * libtg_capi.a the VST3/AU/CLAP links -- see cmake/TranceGateEngine.cmake,
 * which explains at length why there is exactly one copy of it -- and the
 * same one tg-move wraps for the Move. Three shells, one engine, and
 * tests/render_m4l.c is what keeps this one honest: it renders through the
 * code below and must produce the FNV-1a hash tests/render_plugin.c does.
 *
 * WHAT THIS FILE OWES THE OTHER SHELLS. The parameter wire is
 * plugins/trance-gate/TranceGate.cpp's PushParams, value for value, including
 * the conversions that are easy to get subtly wrong:
 *
 *   Slot and Length are ONE-BASED to a user and ZERO-BASED to the engine.
 *   Amount, Width and Sustain are PERCENTAGES to a user and 0..1 to the
 *     engine. Attack, Decay and Release are percentages to BOTH -- they are a
 *     proportion of the gate's Width, not of anything normalised.
 *
 * Three of those four rules have the same shape and the fourth does not,
 * which is exactly the kind of thing a second implementation gets wrong
 * quietly. The wire test walks all twelve rather than trusting this comment.
 */
#include "ext.h"
#include "ext_obex.h"
#include "ext_itm.h"
#include "z_dsp.h"

#include "trance_gate_core.h"
#include "wire.h"

typedef struct _tg_gate
{
    t_pxobject  ob;
    tg_core_t  *core;

    /*
     * MSP IS DOUBLE AND THE ENGINE IS FLOAT, so the block is converted in and
     * out through these. Sized at dsp64 to the vector size MSP announces --
     * which is where allocation belongs, because perform64 may not allocate
     * and dsp64 is not the audio thread.
     */
    float      *l, *r;
    long        cap;

    /*
     * THE TRANSPORT, REFERENCED ONCE. itm_getglobal is a lookup, and doing it
     * per block would be a lookup per block for an answer that cannot change.
     * Referenced at dsp64 and released at free, which is what itm_reference
     * is for.
     *
     * In a Max for Live device this is LIVE'S clock: plugin~ syncs Max's
     * global transport to the host's, so there is nothing else to ask.
     */
    t_itm      *itm;

    /*
     * THE TWELVE, CACHED. Messages arrive on the scheduler thread and the
     * engine is driven from the audio thread, so they are stored here and
     * pushed at the top of every block -- unconditionally, for the reason
     * TranceGate.cpp's OnParamChange gives: set_num owns all the clamping and
     * the side effects, and pushing twelve doubles is cheaper than tracking
     * which of them moved.
     *
     * These are ENGINE-SIDE values, already converted. The conversion happens
     * once, in tg_gate_num, so the audio thread never does it.
     */
    double      pval[TG_P_COUNT];

    /*
     * LENGTH IS PER SLOT, SO IT IS NOT ALWAYS OURS TO PUSH -- the same trap
     * TranceGate.cpp documents. pat[slot].length belongs to the slot just
     * switched to and the patcher's Length still holds the slot left behind,
     * so pushing it would destroy the new slot's length by the act of looking
     * at it. On the block the slot moves, the engine's length wins and the
     * patcher is told to catch up; until it has, Length is not pushed.
     */
    long        slot_pushed;
    long        slot_sync;

    void       *out_state;  /* the ui readout / state blob, on demand */
    void       *out_sync;   /* "the slot moved, here is its length" */
} t_tg_gate;

static t_class *tg_gate_class = NULL;

/* ------------------------------------------------------------------ */

static void tg_gate_push_params(t_tg_gate *x)
{
    int i;
    long slot = (long) x->pval[TG_P_SLOT];

    if (slot != x->slot_pushed) {
        x->slot_pushed = slot;
        x->slot_sync = 1;
    }

    for (i = 0; i < TG_P_COUNT; i++) {
        if (i == TG_P_LENGTH && x->slot_sync) continue;
        tg_core_set_num(x->core, (tg_param_t) i, x->pval[i]);
    }
}

static void tg_gate_perform64(t_tg_gate *x, t_object *dsp64, double **ins,
                              long numins, double **outs, long numouts,
                              long n, long flags, void *up)
{
    tg_transport_t t;
    long i;
    const int stereo = (numins > 1 && numouts > 1);

    if (!x->core || n <= 0 || n > x->cap || numouts < 1) {
        for (i = 0; i < numouts; i++)
            if (outs[i]) memset(outs[i], 0, sizeof(double) * (size_t) n);
        return;
    }

    tg_gate_push_params(x);

    /*
     * THE TRANSPORT, SAMPLED ONCE PER BLOCK. itm_getstate is nonzero while
     * the host is rolling; itm_getticks is the position. A block is short
     * enough that the engine's own per-sample phase carries the rest, which
     * is why there is no sub-block advance here and there is one in
     * TranceGate.cpp: that shell chunks a block it was handed longer than it
     * reserved, and MSP never hands over more than the vector size.
     */
    t = x->itm ? tg_m4l_transport(itm_getstate(x->itm),
                                  itm_getticks(x->itm),
                                  itm_gettempo(x->itm))
               : tg_m4l_transport(0, 0.0, 0.0);

    for (i = 0; i < n; i++) {
        x->l[i] = (float) ins[0][i];
        x->r[i] = (float) (stereo ? ins[1][i] : ins[0][i]);
    }

    tg_core_process_f32_split(x->core, x->l, x->r, (int) n, &t);

    for (i = 0; i < n; i++) {
        outs[0][i] = (double) x->l[i];
        if (stereo) outs[1][i] = (double) x->r[i];
    }
}

static void tg_gate_dsp64(t_tg_gate *x, t_object *dsp64, short *count,
                          double sr, long maxvectorsize, long flags)
{
    if (!x->core) return;

    tg_core_set_sample_rate(x->core, sr);

    /*
     * GROWN, NEVER SHRUNK, and reallocated only when it is actually too
     * small. dsp64 runs on every chain rebuild -- adding a device to the
     * track is one -- and freeing a buffer perform64 might still be inside is
     * the kind of crash that reads as "Live quit while I was playing".
     */
    if (maxvectorsize > x->cap) {
        float *nl = (float *) sysmem_newptr(sizeof(float) * (size_t) maxvectorsize);
        float *nr = (float *) sysmem_newptr(sizeof(float) * (size_t) maxvectorsize);
        if (!nl || !nr) {
            if (nl) sysmem_freeptr(nl);
            if (nr) sysmem_freeptr(nr);
            object_error((t_object *) x, "out of memory for a %ld-frame vector",
                         maxvectorsize);
            return;
        }
        if (x->l) sysmem_freeptr(x->l);
        if (x->r) sysmem_freeptr(x->r);
        x->l = nl;
        x->r = nr;
        x->cap = maxvectorsize;
    }

    if (!x->itm) {
        x->itm = (t_itm *) itm_getglobal();
        if (x->itm) itm_reference(x->itm);
    }

    object_method(dsp64, gensym("dsp_add64"), x, tg_gate_perform64, 0, NULL);
}

/* ------------------------------------------------------------------ */

/*
 * `num <index> <value>`, the parameter door.
 *
 * THE INDEX IS tg_param_t's AND SO IS THE PATCHER'S ORDER. plugins/
 * trance-gate/TranceGate.h says why it refuses to carry a mapping table: the
 * JUCE build had one, called wire[], because its declaration order had
 * drifted from the engine's, and starting again was the chance not to. This
 * shell inherits that -- an index out of range is dropped rather than
 * clamped onto a neighbour, because clamping would silently edit the wrong
 * parameter.
 *
 * THE UNIT CONVERSIONS ARE IN wire.c AND NOT IN THE PATCHER, so a live.dial
 * can be labelled in the units a user thinks in without every one of them
 * having to restate what the engine wants. They are in wire.c rather than
 * here so a test can reach them -- three of the twelve are asymmetric with
 * their neighbours, which is the kind of thing a comment cannot enforce.
 */
static void tg_gate_num(t_tg_gate *x, t_symbol *s, long argc, t_atom *argv)
{
    long idx;
    double v;

    if (argc < 2) {
        object_error((t_object *) x, "num: expected <index> <value>");
        return;
    }
    idx = atom_getlong(argv);
    v   = atom_getfloat(argv + 1);

    if (idx < 0 || idx >= TG_P_COUNT) {
        object_error((t_object *) x, "num: parameter %ld is out of range", idx);
        return;
    }

    x->pval[idx] = tg_m4l_param_value((int) idx, v);
}

/*
 * `param <key> <value>` -- the engine's string door, for everything that is
 * not one of the twelve: step edits, per-step depths, ties, the cursor.
 *
 * APPLIED HERE RATHER THAN DEFERRED TO THE BLOCK, which is what
 * TranceGate.cpp does with the same edits for the same reason: a drag sends
 * one of these per mouse move and routing each through a pending-state buffer
 * would cost a full patch serialise per sample of the gesture. The whole-
 * patch paste below IS deferred, because that one is worth it.
 */
static void tg_gate_param(t_tg_gate *x, t_symbol *s, long argc, t_atom *argv)
{
    char val[64];
    const char *key, *v;

    if (!x->core || argc < 2) {
        object_error((t_object *) x, "param: expected <key> <value>");
        return;
    }
    if (atom_gettype(argv) != A_SYM) {
        object_error((t_object *) x, "param: the key must be a symbol");
        return;
    }
    key = atom_getsym(argv)->s_name;

    /*
     * THE ENGINE'S DOOR TAKES STRINGS, so a number has to become one. It is
     * deliberately strings -- the header says why: it is what Schwung's chain
     * already speaks and it is what makes a patch portable. The cost is this
     * conversion, and it is paid on the MESSAGE thread, never in a block.
     */
    switch (atom_gettype(argv + 1)) {
    case A_SYM:
        v = atom_getsym(argv + 1)->s_name;
        break;
    case A_LONG:
        snprintf_zero(val, sizeof val, "%ld", atom_getlong(argv + 1));
        v = val;
        break;
    case A_FLOAT:
        snprintf_zero(val, sizeof val, "%g", atom_getfloat(argv + 1));
        v = val;
        break;
    default:
        object_error((t_object *) x, "param: %s got a value it cannot read", key);
        return;
    }

    tg_core_set_param(x->core, key, v);
}

/* A step edit: `step <index> <mode>` where mode is 0 off, 1 on, 2 tie.
 * Cursor first, then the edit -- the engine's own two-call shape, which
 * TranceGate.cpp uses identically. */
static void tg_gate_step(t_tg_gate *x, t_symbol *s, long argc, t_atom *argv)
{
    char idx[32], mode[32];

    if (!x->core || argc < 2) {
        object_error((t_object *) x, "step: expected <index> <mode>");
        return;
    }
    snprintf_zero(idx, sizeof idx, "%ld", atom_getlong(argv));
    snprintf_zero(mode, sizeof mode, "%ld", atom_getlong(argv + 1));
    tg_core_set_param(x->core, "cursor", idx);
    tg_core_set_param(x->core, "step", mode);
}

/* A per-step depth: `depth <index> <0..255>`. */
static void tg_gate_depth(t_tg_gate *x, t_symbol *s, long argc, t_atom *argv)
{
    char idx[32], amt[32];

    if (!x->core || argc < 2) {
        object_error((t_object *) x, "depth: expected <index> <amount>");
        return;
    }
    snprintf_zero(idx, sizeof idx, "%ld", atom_getlong(argv));
    snprintf_zero(amt, sizeof amt, "%ld", atom_getlong(argv + 1));
    tg_core_set_param(x->core, "cursor", idx);
    tg_core_set_param(x->core, "step_amount", amt);
}

/*
 * `bang` -- the ui readout, out the left outlet as a single symbol.
 *
 * PULLED, NEVER PUSHED. The readout is wanted at frame rate and the only
 * thread that knows the playhead moved is the audio thread, which may not
 * touch an outlet. So the patcher asks -- a qmetro into here -- and the
 * answer is produced on the thread that asked. That is also why this is the
 * same string the WebView editor polls: one readout, three shells.
 */
static void tg_gate_bang(t_tg_gate *x)
{
    char buf[TG_STATE_MAX];
    t_atom a;

    if (!x->core) return;

    /*
     * THE OTHER HALF OF THE SLOT HANDSHAKE, and it is here because this is
     * the thread allowed to have it. The block noticed the slot moved and
     * stopped pushing Length; the engine's length for the new slot goes out
     * now, the patcher's Length catches up, and pushing resumes. Until then
     * Length is the SLOT's, not the dial's -- which is the whole point.
     */
    if (x->slot_sync) {
        if (tg_core_get_param(x->core, "length", buf, (int) sizeof buf) >= 0) {
            /* The engine counts from zero and a user counts from one. */
            atom_setlong(&a, atol(buf) + 1);
            outlet_anything(x->out_sync, gensym("length"), 1, &a);
            x->slot_sync = 0;
        }
    }

    if (tg_core_get_param(x->core, "ui", buf, (int) sizeof buf) < 0) return;
    atom_setsym(&a, gensym(buf));
    outlet_anything(x->out_state, gensym("ui"), 1, &a);
}

/*
 * `getstate` -- the whole patch as one string, which is the SAME text the
 * Move module writes and the VST3 pastes. That is the entire reason the
 * pattern travels as a blob rather than as parameters: a patch made on the
 * hardware opens in the DAW and the other way round.
 */
static void tg_gate_getstate(t_tg_gate *x)
{
    char buf[TG_STATE_MAX];
    t_atom a;

    if (!x->core) return;
    if (tg_core_get_param(x->core, "state", buf, (int) sizeof buf) < 0) return;
    atom_setsym(&a, gensym(buf));
    outlet_anything(x->out_state, gensym("state"), 1, &a);
}

static void tg_gate_setstate(t_tg_gate *x, t_symbol *s, long argc, t_atom *argv)
{
    if (!x->core || argc < 1) return;
    tg_core_set_param(x->core, "state", atom_getsym(argv)->s_name);
}

static void tg_gate_assist(t_tg_gate *x, void *b, long m, long a, char *d)
{
    if (m == ASSIST_INLET)
        snprintf_zero(d, 512, "(signal) left in / messages: num, param, step, "
                              "depth, bang, getstate, setstate");
    else if (a == 0)
        snprintf_zero(d, 512, "(signal) left out");
    else if (a == 1)
        snprintf_zero(d, 512, "(signal) right out");
    else if (a == 2)
        snprintf_zero(d, 512, "ui readout / state blob");
    else
        snprintf_zero(d, 512, "slot moved: its length, for Length to catch up");
}

/* ------------------------------------------------------------------ */

static void *tg_gate_new(t_symbol *s, long argc, t_atom *argv)
{
    t_tg_gate *x = (t_tg_gate *) object_alloc(tg_gate_class);
    int i;

    if (!x) return NULL;

    dsp_setup((t_pxobject *) x, 2);

    /* Outlets are created right to left. */
    x->out_sync  = outlet_new((t_object *) x, NULL);
    x->out_state = outlet_new((t_object *) x, NULL);
    outlet_new((t_object *) x, "signal");
    outlet_new((t_object *) x, "signal");

    x->core = tg_core_create(sys_getsr() > 0 ? sys_getsr() : 44100.0);
    if (!x->core) {
        object_error((t_object *) x, "the engine would not start");
        return x;
    }

    x->l = x->r = NULL;
    x->cap = 0;
    x->itm = NULL;
    x->slot_pushed = 0;
    x->slot_sync = 0;

    /* The engine's own defaults, read back rather than restated -- a second
     * list of defaults here would be a second thing to keep in step. */
    for (i = 0; i < TG_P_COUNT; i++) x->pval[i] = 0.0;
    x->pval[TG_P_LENGTH]  = 15.0;   /* 16 steps, zero-based */
    x->pval[TG_P_RATE]    = TG_RATE_DEFAULT;
    x->pval[TG_P_AMOUNT]  = 1.0;
    x->pval[TG_P_HOLD]    = 1.0;
    x->pval[TG_P_SUSTAIN] = 1.0;
    x->pval[TG_P_ATTACK]  = 1.6;
    x->pval[TG_P_DECAY]   = 16.0;
    x->pval[TG_P_RELEASE] = 16.0;

    return x;
}

static void tg_gate_free(t_tg_gate *x)
{
    dsp_free((t_pxobject *) x);
    if (x->itm) itm_dereference(x->itm);
    if (x->core) tg_core_destroy(x->core);
    if (x->l) sysmem_freeptr(x->l);
    if (x->r) sysmem_freeptr(x->r);
}

void ext_main(void *r)
{
    t_class *c = class_new("tg.gate~", (method) tg_gate_new,
                           (method) tg_gate_free, sizeof(t_tg_gate), NULL,
                           A_GIMME, 0);

    class_addmethod(c, (method) tg_gate_dsp64,    "dsp64",    A_CANT, 0);
    class_addmethod(c, (method) tg_gate_assist,   "assist",   A_CANT, 0);
    class_addmethod(c, (method) tg_gate_bang,     "bang",             0);
    class_addmethod(c, (method) tg_gate_num,      "num",      A_GIMME, 0);
    class_addmethod(c, (method) tg_gate_param,    "param",    A_GIMME, 0);
    class_addmethod(c, (method) tg_gate_step,     "step",     A_GIMME, 0);
    class_addmethod(c, (method) tg_gate_depth,    "depth",    A_GIMME, 0);
    class_addmethod(c, (method) tg_gate_getstate, "getstate",         0);
    class_addmethod(c, (method) tg_gate_setstate, "setstate", A_GIMME, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    tg_gate_class = c;
}
