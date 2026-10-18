// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The single filters' C ABI from C: compiled against both headers cbindgen
 * writes (sf-capi/build.rs) and linked to the plugins' real archive. What the
 * shell and the Move modules reach, for every product: the table, an
 * instance, the panic and the picture.
 */
#include "sf_core.h"
#include "sf_shell.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static const uint32_t shapes[9] = { SF_SHAPE_HIGH_PASS, SF_SHAPE_LOW_PASS, SF_SHAPE_BAND_PASS, SF_SHAPE_NOTCH, SF_SHAPE_BELL,
                                    SF_SHAPE_LOW_SHELF, SF_SHAPE_HIGH_SHELF, SF_SHAPE_TILT, SF_SHAPE_ALL_PASS };
static const int counts[9] = { 10, 10, 9, 9, 7, 7, 7, 5, 6 };

int main (void)
{
    char buf[64];
    NiParamInfo info;

    /* Every product's table. */
    for (int k = 0; k < 9; ++k)
    {
        const uint32_t s = shapes[k];
        CHECK (sf_param_count (s) == counts[k]);
        CHECK (sf_field_index (s, SF_FIELD_FREQ) == 0);
        CHECK (sf_param_key (s, 0, buf, sizeof buf) > 0 && strcmp (buf, "freq") == 0);
        const int out = sf_field_index (s, SF_FIELD_OUTPUT);
        CHECK (out > 0 && sf_param_name (s, out, buf, sizeof buf) > 0 && strcmp (buf, "Output") == 0);
        const int an = sf_field_index (s, SF_FIELD_ANALYZER);
        CHECK (an == counts[k] - 1 && sf_param_info (s, an, &info) && ! info.automatable);
        CHECK (! sf_param_info (s, counts[k], &info));
    }
    CHECK (sf_param_count (0) == 0);
    CHECK (sf_field_index (SF_SHAPE_HIGH_PASS, SF_FIELD_GAIN) == -1);
    CHECK (sf_param_label (SF_SHAPE_LOW_PASS, sf_field_index (SF_SHAPE_LOW_PASS, SF_FIELD_FAMILY), 4, buf, sizeof buf) > 0
           && strcmp (buf, "Elliptic") == 0);
    CHECK (sf_param_text (SF_SHAPE_BELL, 0, 2500.0, buf, sizeof buf) > 0 && strcmp (buf, "2.50 kHz") == 0);
    double v = 0.0;
    CHECK (sf_param_parse (SF_SHAPE_BELL, 0, "1.2k", &v) && v == 1200.0);
    CHECK (! sf_param_parse (SF_SHAPE_BELL, 0, "loud", &v));
    CHECK (sf_uses (SF_SHAPE_BELL, 0, 0) == SF_USES_Q);
    CHECK (sf_order_played (SF_SHAPE_HIGH_PASS, 5, 0) == 1);
    CHECK (sf_core_new (0, 48000.0) == NULL);
    CHECK (sf_shell_create (10, 48000.0) == NULL);

    for (int k = 0; k < 9; ++k)
    {
        const uint32_t s = shapes[k];
        /* An instance filters, and the panic empties it. */
        SfCore* core = sf_core_new (s, 48000.0);
        CHECK (core != NULL);
        float l[512], r[512];
        for (int i = 0; i < 512; ++i)
        {
            l[i] = (float) sin (0.05 * i) * 0.5f;
            r[i] = (float) cos (0.03 * i) * 0.5f;
        }
        sf_core_set_num (core, 0, 3000.0);
        CHECK (sf_core_num (core, 0) == 3000.0);
        /* A gain type at 0 dB is a wire, with no state to empty: give it a
         * gain. */
        if (sf_field_index (s, SF_FIELD_GAIN) >= 0)
            sf_core_set_num (core, sf_field_index (s, SF_FIELD_GAIN), 6.0);
        sf_core_process_f32 (core, l, r, 512);
        CHECK (! sf_core_is_silent (core));
        const uint8_t cc120[3] = { 0xB2, 120, 0 };
        sf_core_on_midi (core, cc120, 3);
        CHECK (sf_core_is_silent (core));
        int16_t move[1024];
        for (int i = 0; i < 1024; ++i)
            move[i] = (int16_t) (i * 31 % 2000 - 1000);
        sf_core_process_i16 (core, move, 512);
        sf_core_reset (core);
        CHECK (sf_core_is_silent (core));
        sf_core_free (core);

        /* The picture: the curve at its frequencies, the handle at Freq. */
        SfShell* shell = sf_shell_create (s, 48000.0);
        SfPicture* p = (SfPicture*) malloc (sizeof (SfPicture));
        sf_picture_init (p);
        double values[SF_PARAM_MAX];
        for (int i = 0; i < counts[k]; ++i)
        {
            sf_param_info (s, i, &info);
            values[i] = info.default_value;
        }
        CHECK (sf_shell_read (shell, values, (size_t) counts[k], p));
        CHECK (p->hz[0] == 10.0f && fabsf (p->hz[SF_POINTS - 1] - 22000.0f) < 1.0f);
        CHECK (fabs (p->handle_hz - values[0]) < 1e-9);
        CHECK (! sf_shell_read (shell, values, 2, p));
        free (p);
        sf_shell_destroy (shell);
    }

    printf (failures ? "\n%d FAILED\n" : "\nPASS\n", failures);
    return failures ? 1 : 0;
}
