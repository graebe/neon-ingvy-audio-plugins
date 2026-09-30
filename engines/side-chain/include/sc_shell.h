/*
 * sc_shell.h -- the Side-Chain engine as a plugin shell holds it.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * THE ENGINE BELONGS TO THE AUDIO THREAD; tg_shell.h states the rule and this
 * is the same arrangement. The audio thread calls sc_core_* only on the pointer
 * sc_shell_begin lends it, between sc_shell_begin and sc_shell_end. Every other
 * thread reads what the audio thread published, and never the engine:
 *
 *   audio thread                        any other thread
 *   ------------                        ----------------
 *   c = sc_shell_begin(s);              sc_shell_read(s, "ui", buf, n);
 *   sc_core_set_num(c, ...);
 *   sc_core_process_f32_split_tap(c,...)
 *   sc_shell_end(s, frames);
 *
 * Readouts are republished a hundred times a second. begin/end allocate
 * nothing, take no lock and never wait. begin may be called again before end
 * within one block -- for MIDI, which the host delivers ahead of the block.
 *
 * The Move module does not use this: Schwung calls its module on one thread.
 */
#ifndef SC_SHELL_H
#define SC_SHELL_H

#include "sc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScShell sc_shell_t;

sc_shell_t *sc_shell_create(double sample_rate);
void        sc_shell_destroy(sc_shell_t *s);

/* ---- any thread but the audio thread ---- */

/* Applied at the top of the next block. */
void sc_shell_post_sample_rate(sc_shell_t *s, double sample_rate);

/* "ui", "params" or "stage_ms", exactly as sc_core_get_param formats them.
 * Returns the length written, or -1 for any other key. Size `buf` with
 * SC_STATE_MAX. */
int  sc_shell_read(sc_shell_t *s, const char *key, char *buf, int buf_len);

/* ---- the audio thread ---- */

sc_core_t *sc_shell_begin(sc_shell_t *s);
void       sc_shell_end(sc_shell_t *s, int frames);

#ifdef __cplusplus
}
#endif
#endif /* SC_SHELL_H */
