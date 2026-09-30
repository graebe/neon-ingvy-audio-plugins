/*
 * tg_shell.h -- the Trance Gate engine as a plugin shell holds it.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * THE ENGINE BELONGS TO THE AUDIO THREAD. A plugin has at least three threads
 * that want it -- the audio callback, the editor's messages, the host's state
 * calls -- and tg_core_* has no lock in it, by design. So a shell never calls
 * tg_core_* on anything but the pointer tg_shell_begin lends it, and only
 * between tg_shell_begin and tg_shell_end:
 *
 *   audio thread                      any other thread
 *   ------------                      ----------------
 *   c = tg_shell_begin(s);            tg_shell_post(s, kv, n);    edits in
 *   tg_core_set_num(c, ...);          tg_shell_read(s, "ui", ...) readouts out
 *   tg_core_process_f32_split(c,...)
 *   tg_shell_end(s, frames);
 *
 * An edit is applied at the top of the next block. A readout is the latest one
 * the audio thread published -- or, while an edit is still waiting to be
 * applied, what the engine WILL read once it has been, so a save straight
 * after an edit writes that edit even with the host's audio engine off.
 *
 * begin/end allocate nothing, take no lock and never wait. post/read may
 * allocate and serialise with each other, never with the audio thread.
 *
 * The Move module does not use this: Schwung calls its module on one thread.
 */
#ifndef TG_SHELL_H
#define TG_SHELL_H

#include "trance_gate_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TgShell tg_shell_t;

tg_shell_t *tg_shell_create(double sample_rate);
void        tg_shell_destroy(tg_shell_t *s);

/* ---- any thread but the audio thread ---- */

/*
 * One edit: `n_pairs` key/value pairs for tg_core_set_param, applied together
 * and in order -- "cursor","3","step","2" moves the cursor and edits the step
 * it lands on in the same block. Returns 1 when queued, 0 when refused (a null
 * or an edit longer than any state blob). A full queue is not a refusal: the
 * edit waits on this side and is sent in order.
 *
 * "randomize" with no seed is given one here, so the roll that plays is the
 * roll a save writes.
 */
int  tg_shell_post(tg_shell_t *s, const char *const *pairs, int n_pairs);
void tg_shell_post_sample_rate(tg_shell_t *s, double sample_rate);

/* "ui", "params", "state" or "length", exactly as tg_core_get_param formats
 * them. Returns the length written, or -1 for any other key. Size `buf` with
 * TG_STATE_MAX. */
int  tg_shell_read(tg_shell_t *s, const char *key, char *buf, int buf_len);

/* ---- the audio thread ---- */

tg_core_t *tg_shell_begin(tg_shell_t *s);
/* Publish at this block's end whatever the cadence -- for a change a reader
 * acts on and must not see late, such as the slot moving. */
void       tg_shell_touch(tg_shell_t *s);
void       tg_shell_end(tg_shell_t *s, int frames);

#ifdef __cplusplus
}
#endif
#endif /* TG_SHELL_H */
