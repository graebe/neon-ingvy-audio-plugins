// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's C ABI from C: compiled against both headers cbindgen
 * writes (fd-capi/build.rs) and linked to the plugin's real archive. What the
 * shell and the Move module reach: the table, an instance, the panic and the
 * picture.
 */
#include "fd_core.h"
#include "fd_shell.h"

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

static int node (int n, int field) { return FD_NODE_BASE + FD_NODE_FIELDS * n + field; }

int main (void)
{
    char buf[64];
    NiParamInfo info;

    /* The table. */
    CHECK (fd_param_count() == FD_PARAM_COUNT);
    CHECK (fd_param_count() == 174);
    CHECK (fd_param_key (node (0, 4), buf, sizeof buf) > 0 && strcmp (buf, "n01_freq") == 0);
    CHECK (fd_param_name (node (15, 9), buf, sizeof buf) > 0 && strcmp (buf, "Filter 16 Channel") == 0);
    CHECK (fd_param_info (node (0, 0), &info) && info.kind == NiParamKind_Choice && info.max == 9.0);
    CHECK (fd_param_info (3, &info) && ! info.automatable);
    CHECK (fd_param_label (node (0, 2), 4, buf, sizeof buf) > 0 && strcmp (buf, "Elliptic") == 0);
    CHECK (fd_param_text (node (0, 4), 2500.0, buf, sizeof buf) > 0 && strcmp (buf, "2.50 kHz") == 0);
    double v = 0.0;
    CHECK (fd_param_parse (node (0, 4), "1.2k", &v) && v == 1200.0);
    CHECK (! fd_param_parse (node (0, 4), "loud", &v));
    CHECK (! fd_param_info (FD_PARAM_COUNT, &info));
    CHECK (fd_type_uses (1, 1) == (4 | 8));
    CHECK (fd_type_for_new (20000.0) == 5);

    /* An empty instance is the input, bit for bit. */
    FdCore* core = fd_core_new (48000.0);
    float l[512], r[512], l0[512], r0[512];
    for (int i = 0; i < 512; ++i)
    {
        l[i] = l0[i] = (float) sin (0.05 * i) * 0.5f;
        r[i] = r0[i] = (float) cos (0.03 * i) * 0.5f;
    }
    fd_core_process_f32 (core, l, r, 512);
    CHECK (memcmp (l, l0, sizeof l) == 0 && memcmp (r, r0, sizeof r) == 0);

    /* A bell is heard; the panic empties it. */
    fd_core_set_num (core, node (0, 0), 1.0);
    fd_core_set_num (core, node (0, 5), 9.0);
    for (int k = 0; k < 8; ++k)
        fd_core_process_f32 (core, l, r, 512);
    CHECK (memcmp (l, l0, sizeof l) != 0);
    CHECK (! fd_core_is_silent (core));
    const uint8_t panic[3] = { 0xB2, 120, 0 };
    fd_core_on_midi (core, panic, 3);
    CHECK (fd_core_is_silent (core));
    int16_t move[1024];
    for (int i = 0; i < 1024; ++i)
        move[i] = (int16_t) (i * 13);
    fd_core_process_i16 (core, move, 512);
    fd_core_free (core);

    /* The picture: a bell of +9 dB at its default frequency. */
    FdShell* shell = fd_shell_create (48000.0);
    double values[FD_PARAM_COUNT];
    for (int i = 0; i < FD_PARAM_COUNT; ++i)
    {
        CHECK (fd_param_info (i, &info));
        values[i] = info.default_value;
    }
    values[node (0, 0)] = 1.0;
    values[node (0, 4)] = 1000.0;
    values[node (0, 5)] = 9.0;
    FdPicture* p = malloc (sizeof *p);
    fd_picture_init (p);
    CHECK (fd_shell_read (shell, values, FD_PARAM_COUNT, FD_LANE_LEFT, p));
    CHECK (p->handle_hz[0] == 1000.0 && p->handle_db[0] == 9.0);
    float peak = -1000.0f;
    for (int i = 0; i < FD_POINTS; ++i)
        if (p->total_db[i] > peak)
            peak = p->total_db[i];
    CHECK (fabsf (peak - 9.0f) < 0.05f);
    CHECK (! fd_shell_read (shell, values, 3, FD_LANE_LEFT, p));
    free (p);
    fd_shell_destroy (shell);

    printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
