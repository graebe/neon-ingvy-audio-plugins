/*
 * The ground's kick detector through its C ABI.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * ground-core's own cargo tests cover the detector in Rust -- the band, the
 * relative threshold, the re-arm. THIS one compiles against the hand-written
 * engines/ground/include/ground_detect.h and links the real staticlib, which is
 * the only thing that catches the header drifting from the implementation: both
 * sides keep compiling while they disagree.
 *
 * The claims here are the ones the four plugins depend on, and every one of them
 * is a thing a wrong header would break silently:
 *
 *   - a kick moves the count and a hi-hat does not
 *   - the strength arrives inside the range the field was calibrated for
 *   - gnd_reset does NOT rewind the count (every plugin compares it against
 *     its own last value, so a rewind would draw a ring on every transport stop)
 *   - the NULL and empty-block paths are no-ops, because ProcessBlock is not
 *     going to check
 *   - a new detector is INACTIVE and ignores audio until gnd_set_active, and
 *     switching it off stops it -- the editor-closed path every plugin takes
 */
#include "ground_detect.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SR     48000.0
#define BLOCK  512

static int failures = 0;

static void check(int ok, const char* what)
{
  if (!ok) { printf("FAIL: %s\n", what); failures++; }
  else     { printf("ok:   %s\n", what); }
}

/* A decaying sine at `hz`, written into `buf`, continuing from sample `start`.
 * Doubles, as iPlug2's `sample` is -- see gnd_push in the header. */
static void fill_tone(double* buf, int n, int start, double hz, double decay)
{
  for (int i = 0; i < n; i++)
  {
    const double t = (double) (start + i) / SR;
    const double a = decay > 0.0 ? exp(-t / decay) : 1.0;
    buf[i] = a * sin(2.0 * M_PI * hz * t);
  }
}

/* Push `secs` of a tone through in BLOCK-sized chunks, as a host would. */
static void push_tone(gnd_t* g, double secs, double hz, double decay)
{
  double buf[BLOCK];
  const int total = (int) (secs * SR);
  for (int done = 0; done < total; done += BLOCK)
  {
    const int n = (total - done) < BLOCK ? (total - done) : BLOCK;
    fill_tone(buf, n, done, hz, decay);
    gnd_push(g, buf, buf, n);
  }
}

int main(void)
{
  /* The NULL paths first: if the header's types were wrong these would not even
   * link, and if the implementation's guards were missing they would crash. */
  gnd_free(NULL);
  gnd_reset(NULL);
  gnd_set_sample_rate(NULL, SR);
  gnd_set_active(NULL, 1);
  gnd_push(NULL, NULL, NULL, 0);
  check(gnd_fires(NULL) == 0, "gnd_fires(NULL) reads 0");
  check(gnd_strength(NULL) == 0.0f, "gnd_strength(NULL) reads 0");

  gnd_t* g = gnd_new(SR);
  check(g != NULL, "a detector can be created");
  if (g == NULL) return 1;

  check(gnd_fires(g) == 0, "a fresh detector has seen no kicks");
  check(gnd_strength(g) == 0.0f, "... and reports no strength");

  /* Inactive until an editor opens: a kick is ignored outright. */
  push_tone(g, 0.4, 60.0, 0.05);
  check(gnd_fires(g) == 0, "an inactive detector ignores a kick");
  gnd_set_active(g, 1);

  /* An empty block, which a host does hand out. */
  double one = 0.0;
  gnd_push(g, &one, &one, 0);
  gnd_push(g, &one, &one, -1);
  check(gnd_fires(g) == 0, "an empty block is a no-op");

  /* One kick. */
  push_tone(g, 0.4, 60.0, 0.05);
  const uint32_t after_kick = gnd_fires(g);
  check(after_kick == 1, "a 60 Hz kick is one onset");

  const float s = gnd_strength(g);
  check(s >= 0.3f && s <= 1.0f, "the strength is inside 0.3..1");

  /* A hi-hat must not move it -- the property that makes this detector
   * different from the side-chain's broadband follower. */
  push_tone(g, 2.0, 1000.0, 0.0);
  check(gnd_fires(g) == after_kick, "a sustained 1 kHz tone adds no onset");

  /* Reset forgets the envelope but must NOT rewind the count. */
  gnd_reset(g);
  check(gnd_fires(g) == after_kick, "gnd_reset leaves the count alone");
  gnd_set_sample_rate(g, 44100.0);
  check(gnd_fires(g) == after_kick, "gnd_set_sample_rate leaves the count alone");

  /* And it still works at the new rate. */
  push_tone(g, 0.4, 60.0, 0.05);
  check(gnd_fires(g) == after_kick + 1, "the detector fires again after a reset");

  /* The editor closes: the detector stops. It reopens: it runs again. */
  gnd_set_active(g, 0);
  push_tone(g, 0.4, 60.0, 0.05);
  check(gnd_fires(g) == after_kick + 1, "an inactive detector adds no onset");
  gnd_set_active(g, 1);
  push_tone(g, 0.4, 60.0, 0.05);
  check(gnd_fires(g) == after_kick + 2, "a reactivated detector fires again");

  gnd_free(g);

  printf(failures ? "\n%d failure(s)\n" : "\nall ok\n", failures);
  return failures ? 1 : 0;
}
