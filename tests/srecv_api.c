/*
 * The receiver, through the C ABI the plugin actually links.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * spectro-recv's own cargo tests cover the behaviour. This covers the SEAM:
 * spectro_recv.h is hand-written, and a hand-written header can drift from its
 * implementation without either side failing to compile -- a wrong argument
 * order or a missing const reads perfectly well right up until it runs.
 *
 * It also exercises the two things a C caller gets wrong first: passing null
 * for everything, and asking for a source list before knowing how big it is.
 *
 * WHY THERE IS NO LIVE BUS HERE, when that is the feature's whole point.
 *
 * Two Rust staticlibs cannot go into one binary: libspectro_capi.a and
 * libbus_capi.a each embed the Rust runtime, and linking both is a duplicate
 * `rust_eh_personality`. No plugin hits this -- the Spectrogram links only
 * libspectro_capi.a, which already CONTAINS bus-core through spectro-recv --
 * but a C test that wanted to claim a bus would have to link both.
 *
 * So the end-to-end claim (a real sender, a real segment, and the two sources
 * staying in step) is made in Rust, where a `bus_core::Writer` needs no second
 * library: see crates/spectro-recv/tests/receiver.rs. What is left here is the
 * seam that Rust cannot check, which is this header.
 */
#include "spectro_recv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void ok(int cond, const char* what)
{
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) failures++;
}

/* The slot these tests use. High, where a person experimenting with Listen-In
 * is least likely to be, and released before returning. */
#define SLOT 10u
#define SR 48000.0f

static srecv_t* make(void)
{
    const int fft = spectro_pick_fft_size(SR);
    return srecv_new(SR, fft, spectro_pick_hop(SR, fft), SPECTRO_BANDS,
                     SPECTRO_F_MIN, SPECTRO_F_MAX, SPECTRO_DB_FLOOR, SPECTRO_DB_CEIL);
}

static void fill_mono(float* out, int n, float hz, float amp, double* phase)
{
    for (int i = 0; i < n; i++)
    {
        *phase += 2.0 * M_PI * (double) hz / (double) SR;
        out[i] = (float) sin(*phase) * amp;
    }
}

int main(void)
{
    printf("srecv, through the C ABI\n");

    /*
     * NULL IS A NO-OP EVERYWHERE, not a crash. ProcessBlock calls push
     * unconditionally and "there is no receiver yet" has to be silence rather
     * than a branch at every call site.
     */
    srecv_free(NULL);
    srecv_push_own(NULL, NULL, 0);
    srecv_set_sources(NULL, NULL, 0);
    srecv_set_range(NULL, 10.f, 20000.f);
    srecv_set_clash(NULL, -60.f, 12.f);
    ok(srecv_pump(NULL) == 0, "a null receiver pumps nothing");
    ok(srecv_channels(NULL) == 0, "a null receiver has no channels");
    ok(srecv_take_columns(NULL, 0, NULL, 4) == 0, "a null receiver draws nothing");
    ok(srecv_dropped(NULL, 0) == 0, "a null receiver dropped nothing");
    ok(srecv_max_sources() >= 2, "a receiver can hold more than its own channel");

    srecv_t* r = make();
    ok(r != NULL, "a receiver was allocated");
    ok(srecv_channels(r) == 1, "it starts with the own channel and nothing else");
    ok(srecv_slot_of(r, SRECV_OWN) == 0, "the own channel has no slot");
    ok(srecv_rate_mismatch(r, SRECV_OWN) == 0, "the own channel cannot mismatch itself");

    /* The own channel alone still makes a picture. */
    const int bands = SPECTRO_BANDS;
    unsigned char* cols = (unsigned char*) calloc((size_t) bands * 32, 1);
    float* block = (float*) malloc(sizeof(float) * 2048);
    double ph = 0.0;
    int got = 0;
    for (int i = 0; i < 40; i++)
    {
        fill_mono(block, 2048, 1000.f, 0.5f, &ph);
        srecv_push_own(r, block, 2048);
        srecv_pump(r);
        got += srecv_take_columns(r, SRECV_OWN, cols, 32);
    }
    ok(got > 0, "the own channel produced columns");
    ok(srecv_dropped(r, SRECV_OWN) == 0, "nothing was dropped while draining every tick");

    /* The clash, which needs no bus: it is a pure function of two columns and
     * the settings, and the shell calls it with columns it already holds. */
    unsigned char* a = (unsigned char*) malloc((size_t) bands);
    unsigned char* b = (unsigned char*) malloc((size_t) bands);
    unsigned char* out = (unsigned char*) malloc((size_t) bands);
    memset(a, 220, (size_t) bands);
    memset(b, 220, (size_t) bands);
    srecv_set_clash(r, -60.f, 12.f);
    srecv_clash(r, a, b, out, 1);
    int lit = 0;
    for (int i = 0; i < bands; i++) lit += out[i] > 0;
    ok(lit == bands, "two matched sources clash across the column");

    memset(b, 0, (size_t) bands);
    srecv_clash(r, a, b, out, 1);
    lit = 0;
    for (int i = 0; i < bands; i++) lit += out[i] > 0;
    ok(lit == 0, "a source against silence is not a clash");

    /*
     * SUMMING, WHICH CANNOT BE DONE IN BYTE SPACE. A byte is linear in dB, so
     * adding two bytes adds two decibels -- which multiplies amplitudes and
     * would put two equal sources QUIETER than either. Two of the same thing
     * must read +3 dB.
     */
    {
        const int half = 128;   /* whatever level; the claim is the +3, not the level */
        memset(a, (unsigned char) half, (size_t) bands);
        const unsigned char* srcs[2] = { a, a };
        srecv_sum(r, srcs, 2, out, 1);
        /* -96..0 over 255 steps is 0.376 dB a byte, so +3 dB is ~8 bytes. */
        const int lift = (int) out[0] - half;
        ok(lift >= 6 && lift <= 10, "two equal sources did not sum to about +3 dB");

        /* One source is itself, and none is silence. */
        const unsigned char* one[1] = { a };
        srecv_sum(r, one, 1, out, 1);
        ok(abs((int) out[0] - half) <= 1, "one source was changed by summing it");
        srecv_sum(r, NULL, 0, out, 1);
        ok(out[0] == 0, "summing nothing was not silence");

        /* And silence adds nothing to something. */
        memset(b, 0, (size_t) bands);
        const unsigned char* mix[2] = { a, b };
        srecv_sum(r, mix, 2, out, 1);
        ok(abs((int) out[0] - half) <= 1, "silence moved a source that was there");
    }

    ok(srecv_starved(r, SRECV_OWN) == 0, "the own channel cannot starve -- it sets the pace");

    /*
     * The source list sizes itself. With nothing sending it is legitimately
     * empty, and asking for the size of an empty list must be 0 rather than a
     * crash -- which is exactly what a receiver opened on a quiet machine does.
     */
    const int need = srecv_slots(NULL, 0);
    ok(need >= 0, "sizing the source list did not fail");
    char* text = (char*) calloc((size_t) need + 1, 1);
    const int wrote = srecv_slots((unsigned char*) text, need + 1);
    ok(wrote == need, "asking twice gave the same size");
    ok(text[need] == 0 || need == 0, "the list was not terminated");

    /* A slot nobody has claimed cannot be opened, and asking is not an error. */
    const unsigned int nothing = 9u;
    srecv_set_sources(r, &nothing, 1);
    ok(srecv_channels(r) >= 1, "asking for a dead slot broke the receiver");
    srecv_set_sources(r, NULL, 0);
    ok(srecv_channels(r) == 1, "clearing the selection left something open");

    free(text); free(a); free(b); free(out);

    free(block);
    free(cols);
    srecv_free(r);

    printf(failures ? "\nFAIL\n" : "\nok\n");
    return failures ? 1 : 0;
}
