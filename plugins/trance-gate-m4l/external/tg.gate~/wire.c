/*
 * The M4L shell's wire arithmetic. See wire.h for why it is not in tg.gate~.c.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "wire.h"

tg_transport_t tg_m4l_transport(int running, double ticks, double bpm)
{
    tg_transport_t t;

    t.running = running ? 1 : 0;
    t.bpm     = (float) (bpm > 0.0 ? bpm : 120.0);
    t.beats   = t.running ? ticks / TG_TICKS_PER_BEAT : TG_NO_BEAT;

    /* A rolling transport at a negative position is not a position. Live can
     * report one during count-in; the engine must see "stopped" rather than a
     * beat before the start of the song. */
    if (t.running && t.beats < 0.0) {
        t.running = 0;
        t.beats   = TG_NO_BEAT;
    }
    return t;
}

double tg_m4l_param_value(int idx, double v)
{
    switch (idx) {
    /*
     * ONE-BASED TO A USER, ZERO-BASED TO THE ENGINE. The patcher's Slot reads
     * 1..8 and its Length reads 1..32 because that is what a musician counts;
     * pat[] and the length field are indices.
     */
    case TG_P_SLOT:
    case TG_P_LENGTH:
        return v - 1.0;

    /*
     * A PERCENTAGE TO A USER, 0..1 TO THE ENGINE.
     *
     * Attack, Decay and Release are deliberately NOT here, and that asymmetry
     * is the trap this whole file exists to pin down: they are a proportion
     * of the gate's WIDTH, not of anything normalised, and the engine takes
     * them as the percentage they already are. Scaling them by 1/100 here
     * would leave the gate audibly working with an envelope a hundred times
     * too fast -- which sounds like a click, not like a unit error.
     */
    case TG_P_AMOUNT:
    case TG_P_HOLD:
    case TG_P_SUSTAIN:
        return v / 100.0;

    default:
        return v;
    }
}
