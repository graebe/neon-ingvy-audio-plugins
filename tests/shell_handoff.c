/*
 * The handoff through its C ABI, holding a real bus writer.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * shell-core's cargo tests prove the mechanism, two threads and all. This one
 * compiles against the hand-written engines/shell/include/shell_handoff.h and
 * links an archive the symbols ride in (libbus_capi.a), which is the only way
 * to notice the header drifting from the implementation. And it holds what
 * NI Listen-In holds: a writer whose release frees a slot, so "released
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

static void release_writer(void* w) { abus_writer_release((abus_writer_t*) w); }

static int slot_is_free(void)
{
  abus_writer_t* probe = NULL;
  if (abus_writer_claim(SLOT, 48000, &probe) != ABUS_OK) return 0;
  abus_writer_release(probe);
  return 1;
}

int main(void)
{
  check(shell_handoff_new(NULL) == NULL, "no release function, no handoff");

  shell_handoff_t* h = shell_handoff_new(release_writer);
  abus_writer_t* w = NULL;
  if (abus_writer_claim(SLOT, 48000, &w) != ABUS_OK)
  {
    printf("skip: slot %d is in use on this machine\n", SLOT);
    shell_handoff_free(h);
    return SKIP;
  }
  shell_handoff_set(h, w);
  check(shell_handoff_current(h) == w, "the main thread sees what it installed");

  /* A block in progress on the audio thread. */
  void* held = shell_handoff_acquire(h);
  check(held == w, "the audio thread is lent the writer");
  float block[2 * 64] = {0};
  abus_writer_push((abus_writer_t*) held, block, 64);

  /* The slot moves on the main thread mid-block. */
  shell_handoff_set(h, NULL);
  check(shell_handoff_collect(h) == 1, "a writer still held is not released");
  check(!slot_is_free(), "so its slot is still claimed");
  abus_writer_push((abus_writer_t*) held, block, 64);   /* still valid */

  shell_handoff_release(h);
  check(shell_handoff_collect(h) == 0, "released once the block lets go");
  check(slot_is_free(), "and the slot is free again");

  check(shell_handoff_acquire(h) == NULL, "the next block gets no writer");
  shell_handoff_release(h);

  /* The destructor's path: the live writer is released with the handoff. */
  abus_writer_t* w2 = NULL;
  check(abus_writer_claim(SLOT, 48000, &w2) == ABUS_OK, "claimed again");
  shell_handoff_set(h, w2);
  shell_handoff_free(h);
  check(slot_is_free(), "freeing the handoff released the live writer");

  printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
  return failures ? 1 : 0;
}
