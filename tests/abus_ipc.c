/*
 * The bus across a process boundary -- the claim the design was chosen for.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * EVERY OTHER TEST HERE WOULD PASS OVER A PROCESS-LOCAL RING.
 *
 * That is why this one exists. The whole reason the transport is POSIX shared
 * memory rather than a static table inside the shared library is that a host
 * may put the sender and the receiver in different processes -- an AU under a
 * sandbox, a bridged VST3, a Live set where one plugin is scanned out of
 * process. A `static BUSES[16]` would satisfy the rest of this suite and fail
 * silently the day that happened, and the symptom would be a spectrogram that
 * is simply always empty.
 *
 * So: fork, write in the child, read in the parent, and check the bytes.
 */
#include "audio_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Its own slot, though the namespace below already keeps it apart. */
#define SLOT 10
#define FRAMES 1024
#define ROUNDS 64

static int failures = 0;

static void check(int ok, const char* what)
{
  if (!ok) { printf("FAIL: %s\n", what); failures++; }
  else     { printf("ok:   %s\n", what); }
}

/* Frame n carries the value n in both channels' magnitude, so the parent can
 * check CONTINUITY and not merely plausibility. */
static void fill(float* buf, unsigned base, unsigned n)
{
  for (unsigned i = 0; i < n; i++)
  {
    buf[i * 2]     =  (float) (base + i);
    buf[i * 2 + 1] = -(float) (base + i);
  }
}

/*
 * A PRIVATE SET OF BUSES FOR THIS PROCESS. The shm names are global to the
 * user, so a ctest run in another checkout, or a Live session, would otherwise
 * share -- and unlink -- our slots. The engine hashes NIA_BUS_NS into every
 * name; set before the first bus call, and inherited across fork.
 */
static void private_namespace(const char* test)
{
  char ns[64];
  snprintf(ns, sizeof ns, "%s.%d", test, (int) getpid());
  setenv("NIA_BUS_NS", ns, 1);
}

int main(void)
{
  private_namespace("abus_ipc");
  const pid_t child = fork();
  if (child < 0) { printf("FAIL: fork\n"); return 1; }

  if (child == 0)
  {
    /* THE SENDER, in its own process. */
    abus_writer_t* w = NULL;
    abus_pusher_t* p = NULL;
    if (abus_writer_claim(SLOT, 48000, &w, &p) != ABUS_OK || w == NULL || p == NULL)
      _exit(2);
    abus_writer_set_label(w, "Child");

    float buf[FRAMES * 2];
    for (unsigned k = 0; k < ROUNDS; k++)
    {
      fill(buf, k * FRAMES, FRAMES);
      abus_pusher_push(p, buf, FRAMES);
      /* Slow enough that the parent is not simply reading an empty bus, and
       * far short of the ring's 131072 frames of slack. */
      usleep(2000);
    }
    usleep(200000); /* stay alive while the parent finishes reading */
    abus_pusher_release(p);
    abus_writer_release(w);
    _exit(0);
  }

  /* THE RECEIVER, in this one. The child has to get its claim in first. */
  abus_reader_t* r = NULL;
  for (int tries = 0; tries < 200 && r == NULL; tries++)
  {
    if (abus_reader_open(SLOT, &r) == ABUS_OK && r != NULL) break;
    r = NULL;
    usleep(5000);
  }
  check(r != NULL, "a reader in THIS process opens a bus created in ANOTHER");
  if (r == NULL) { waitpid(child, NULL, 0); return 1; }

  int32_t live = 0;
  uint32_t sr = 0;
  char label[ABUS_LABEL_CAP];
  abus_probe(SLOT, &live, &sr, label, ABUS_LABEL_CAP);
  check(sr == 48000, "the rate crosses the process boundary");
  check(strcmp(label, "Child") == 0, "so does the name");

  float out[FRAMES * 2];
  unsigned long total = 0;
  int discontinuities = 0;
  uint64_t dropped = 0;

  for (int tries = 0; tries < 1000 && total < FRAMES * 8; tries++)
  {
    int32_t resynced = 0;
    uint64_t d = 0;
    const uint32_t got = abus_reader_read(r, out, FRAMES, &d, &resynced);
    dropped += d;

    if (got > 1)
    {
      /* Every delivered block must be internally contiguous. A buffer spliced
       * from two moments is the failure this transport is most likely to have
       * and the one that looks most like working audio. */
      const float first = out[0];
      for (uint32_t i = 0; i < got; i++)
        if (out[i * 2] != first + (float) i || out[i * 2 + 1] != -(first + (float) i))
          { discontinuities++; break; }
      total += got;
    }
    usleep(1000);
  }

  check(total > 0, "audio written in another process arrives here");
  check(discontinuities == 0, "and no block is spliced from two moments");
  check(dropped == 0, "and nothing was lost at this pace");

  abus_reader_close(r);

  int status = 0;
  waitpid(child, &status, 0);
  check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "the sender exited cleanly");

  printf("%s (%lu frames across the boundary)\n",
         failures ? "FAILED" : "all good", total);
  return failures ? 1 : 0;
}
