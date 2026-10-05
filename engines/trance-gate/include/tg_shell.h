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

/*
 * The state blob a save writes: the engine's, queued edits included, with the
 * host's TG_P_COUNT `values` (as tg_shell_push takes them) applied the way the
 * next block will apply them -- so a project saved before any audio has run
 * holds the parameters the host shows, in the slot they belong to. Returns the
 * length written, or -1. Size `buf` with TG_STATE_MAX.
 */
int  tg_shell_save(tg_shell_t *s, const double *values, int n, char *buf, int buf_len);

/*
 * SLOT FILES: one slot (.nitgslot) or all eight (.nitgbank), as text the engine
 * writes and reads (tg-core's slotfile.rs). Size buffers with TG_SLOTFILE_MAX.
 *
 * tg_shell_export: the current slot (`all` 0) or every slot (`all` 1), as the
 * next block will hold them -- the host's `values` included, as for
 * tg_shell_save. Returns the length written, or -1.
 *
 * tg_shell_import: checks `text` whole and queues it only when good -- a slot
 * file replaces the current slot, a bank all eight, after which the host
 * follows (tg_shell_take_params). Returns 1 for a slot, 2 for a bank, or 0 with
 * the reason in words in `err` (may be NULL); nothing changes then.
 */
#define TG_SLOTFILE_MAX 16384
int  tg_shell_export(tg_shell_t *s, const double *values, int n, int all, char *buf, int buf_len);
int  tg_shell_import(tg_shell_t *s, const char *text, char *err, int err_len);

/* One cycle of the pattern in ms, as last published: the scope's axis. */
double tg_shell_cycle_ms(tg_shell_t *s);

/*
 * THE HOST'S PARAMETERS MIRROR THE CURRENT SLOT. Every parameter but Slot is
 * per slot in the engine, so on the block the Slot moves (or a paste lands)
 * the engine's values win and the host has to follow: once per published
 * switch this returns 1 with the current slot's TG_P_COUNT values, on the
 * numeric wire (tg_core_set_num's units), in `out`; the host moves each of its
 * parameters to them. Otherwise 0. The main thread.
 */
int  tg_shell_take_params(tg_shell_t *s, double *out, int n);

/*
 * A host's state load, as ONE edit: the blob (NULL or "" for none), then the
 * host's own TG_P_COUNT values -- the restored parameters, on the numeric wire
 * -- which are the current slot's and win over the blob's rounded copy of
 * them. A blob from before every slot had its own sound holds one sound for
 * all eight, and these values are it. Returns 1 when queued.
 */
int  tg_shell_load(tg_shell_t *s, const char *blob, const double *values, int n);
/* The editor's paste of a whole patch: the host then follows the engine's
 * current slot (tg_shell_take_params). Returns 1 when queued. */
int  tg_shell_paste(tg_shell_t *s, const char *blob);

/* ---- the audio thread ---- */

tg_core_t *tg_shell_begin(tg_shell_t *s);
/* The host's TG_P_COUNT parameters, in tg_param_t order and on the numeric
 * wire, into `core`, the engine begin lent. A value is written when the host
 * moved it, into the current slot; on the block the Slot moves nothing else is
 * written, and the host is told the new slot's values (tg_shell_take_params). */
void       tg_shell_push(tg_shell_t *s, tg_core_t *core, const double *values, int n);
/* Publish at this block's end whatever the cadence -- for a change a reader
 * acts on and must not see late, such as the slot moving. */
void       tg_shell_touch(tg_shell_t *s);
void       tg_shell_end(tg_shell_t *s, int frames);

#ifdef __cplusplus
}
#endif
#endif /* TG_SHELL_H */
