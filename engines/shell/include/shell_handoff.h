/*
 * shell_handoff.h -- an object the main thread owns, lent to the audio thread.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * A bus pusher or an analyzer receiver maps memory and allocates, so it is
 * built and freed on the main thread; the audio thread uses it in between.
 * Swapping a plain pointer frees the old object while a block may still be
 * using it. This defers the free until the audio thread has let go:
 *
 *   audio thread, every block        main thread (OnIdle)
 *   ------------------------         --------------------
 *   p = shell_handoff_acquire(h);    shell_handoff_set(h, fresh);  // retires old
 *   ... use p (may be NULL) ...      shell_handoff_collect(h);     // frees old
 *   shell_handoff_release(h);                                      // once unheld
 *
 * THE RULES. One audio thread. acquire/release are wait-free and allocate
 * nothing; everything else is the main thread's. An object, once replaced, is
 * never installed again. shell_handoff_free releases everything and must only
 * run once no block can start -- in the plugin's destructor.
 *
 * The mechanism is a hazard pointer; engines/shell/crates/shell-core/src/
 * handoff.rs has the argument for why it is sound.
 */
#ifndef SHELL_HANDOFF_H
#define SHELL_HANDOFF_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ShellHandoff shell_handoff_t;
typedef void (*shell_release_fn)(void *object);

/* NULL if `release` is NULL. */
shell_handoff_t *shell_handoff_new(shell_release_fn release);
void             shell_handoff_free(shell_handoff_t *h);

/* The audio thread. */
void *shell_handoff_acquire(const shell_handoff_t *h);
void  shell_handoff_release(const shell_handoff_t *h);

/* The main thread. */
void *shell_handoff_current(const shell_handoff_t *h);
void  shell_handoff_set(const shell_handoff_t *h, void *next);
/* Returns how many retired objects are still waiting for the audio thread. */
int   shell_handoff_collect(const shell_handoff_t *h);

#ifdef __cplusplus
}
#endif
#endif /* SHELL_HANDOFF_H */
