/*
 * The handoff through its C ABI, holding a real bus pusher.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * shell-core's cargo tests prove the mechanism, two threads and all. This one
 * compiles against the hand-written engines/shell/include/shell_handoff.h and
 * links an archive the symbols ride in (libbus_capi.a), which is the only way
 * to notice the header drifting from the implementation. And it holds what
 * NI Listen-In holds: the audio thread's half of a bus claim, whose release --
 * with the main thread's writer already gone -- frees a slot, so "released
 * later" is visible as "the slot is still taken until then".
 */
#include "audio_bus.h"
#include "shell_handoff.h"

#include <stdio.h>

/* Its own slot, clear of the other bus tests' (8..16). A Listen-In in a
 * running session may hold it, so a refusal of the FIRST claim skips. */
#define SLOT 7
#define SKIP 77

static int failures = 0;

static void check(int ok, const char* what)
{
  if (!ok) { printf("FAIL: %s\n", what); failures++; }
  else     { printf("ok:   %s\n", what); }
}

static void release_pusher(void* p) { abus_pusher_release((abus_pusher_t*) p); }

static int slot_is_free(void)
{
  abus_writer_t* w = NULL;
  abus_pusher_t* p = NULL;
  if (abus_writer_claim(SLOT, 48000, &w, &p) != ABUS_OK) return 0;
  abus_pusher_release(p);
  abus_writer_release(w);
  return 1;
}

int main(void)
{
  check(shell_handoff_new(NULL) == NULL, "no release function, no handoff");

  shell_handoff_t* h = shell_handoff_new(release_pusher);
  abus_writer_t* w = NULL;
  abus_pusher_t* p = NULL;
  if (abus_writer_claim(SLOT, 48000, &w, &p) != ABUS_OK)
  {
    printf("skip: slot %d is in use on this machine\n", SLOT);
    shell_handoff_free(h);
    return SKIP;
  }
  shell_handoff_set(h, p);
  check(shell_handoff_current(h) == p, "the main thread sees what it installed");

  /* A block in progress on the audio thread. */
  void* held = shell_handoff_acquire(h);
  check(held == p, "the audio thread is lent the pusher");
  float block[2 * 64] = {0};
  abus_pusher_push((abus_pusher_t*) held, block, 64);

  /* The slot moves on the main thread mid-block: the writer goes at once, the
   * pusher is retired. */
  abus_writer_release(w);
  shell_handoff_set(h, NULL);
  check(shell_handoff_collect(h) == 1, "a pusher still held is not released");
  check(!slot_is_free(), "so its slot is still claimed");
  abus_pusher_push((abus_pusher_t*) held, block, 64);   /* still valid */

  shell_handoff_release(h);
  check(shell_handoff_collect(h) == 0, "released once the block lets go");
  check(slot_is_free(), "and the slot is free again");

  check(shell_handoff_acquire(h) == NULL, "the next block gets no pusher");
  shell_handoff_release(h);

  /* The destructor's path: the live pusher is released with the handoff. */
  abus_writer_t* w2 = NULL;
  abus_pusher_t* p2 = NULL;
  check(abus_writer_claim(SLOT, 48000, &w2, &p2) == ABUS_OK, "claimed again");
  abus_writer_release(w2);
  shell_handoff_set(h, p2);
  shell_handoff_free(h);
  check(slot_is_free(), "freeing the handoff released the live pusher");

  printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
  return failures ? 1 : 0;
}
