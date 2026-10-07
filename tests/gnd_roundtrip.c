// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ground's beat clock through its C ABI.
 *
 * ground-core's own cargo tests cover the clock in Rust -- starts, loops,
 * seeks, tempo changes, meters. THIS one compiles against ground.h, which
 * ground-capi's build.rs generates, and links the real staticlib, which is the
 * only thing that catches the C side and the Rust disagreeing while both keep
 * compiling -- an argument swapped in gnd_tick, a double read as an int, and
 * the ground rings on the wrong beats or never.
 *
 * The claims here are the ones the four plugins depend on, and every one of
 * them is a thing a wrong header would break silently:
 *
 *   - a playing transport rings once a beat, the downbeat at 1 and the rest
 *     at 0.4, in 4/4 and in 7/8 -- so ppq, bpm, num and den each arrive where
 *     the header says
 *   - a stopped one rings nothing -- so `playing` does
 *   - gnd_reset does NOT rewind the count (every plugin compares it against
 *     its own last value, so a rewind would draw a ring on every reset)
 *   - the NULL and empty-block paths are no-ops, because ProcessBlock is not
 *     going to check
 *   - a new ground is INACTIVE until gnd_set_active, and switching it off
 *     stops it -- the editor-closed path every plugin takes
 */
#include "ground.h"

#include <math.h>
#include <stdio.h>

#define SR     48000.0
#define BLOCK  512

static int failures = 0;

static void check(int ok, const char* what)
{
  if (!ok) { printf("FAIL: %s\n", what); failures++; }
  else     { printf("ok:   %s\n", what); }
}

/* Play from `ppq` for `blocks` blocks at `bpm` in num/den, as a host would;
 * returns where the song got to. A stopped transport stays where it is. */
static double play(gnd_t* g, double ppq, int blocks, double bpm, int num, int den, int playing)
{
  for (int i = 0; i < blocks; i++)
  {
    gnd_tick(g, ppq, bpm, num, den, playing, BLOCK);
    if (playing) ppq += BLOCK * bpm / (60.0 * SR);
  }
  return ppq;
}

/* Blocks of BLOCK samples in `quarters` at `bpm`, rounded down. */
static int blocks_for(double quarters, double bpm)
{
  return (int) (quarters * 60.0 / bpm * SR / BLOCK);
}

int main(void)
{
  /* The NULL paths first: if the header's types were wrong these would not even
   * link, and if the implementation's guards were missing they would crash. */
  gnd_free(NULL);
  gnd_reset(NULL);
  gnd_set_sample_rate(NULL, SR);
  gnd_set_active(NULL, 1);
  gnd_tick(NULL, 0.0, 120.0, 4, 4, 1, BLOCK);
  check(gnd_fires(NULL) == 0, "gnd_fires(NULL) reads 0");
  check(gnd_strength(NULL) == 0.0f, "gnd_strength(NULL) reads 0");

  gnd_t* g = gnd_new(SR);
  check(g != NULL, "a ground can be created");
  if (g == NULL) return 1;

  check(gnd_fires(g) == 0, "a fresh ground has rung nothing");
  check(gnd_strength(g) == 0.0f, "... and reports no strength");

  /* Inactive until an editor opens: a playing transport is ignored. */
  play(g, 0.0, blocks_for(4.0, 120.0), 120.0, 4, 4, 1);
  check(gnd_fires(g) == 0, "an inactive ground ignores the transport");
  gnd_set_active(g, 1);

  /* Empty blocks, which hosts do hand out. */
  gnd_tick(g, 0.0, 120.0, 4, 4, 1, 0);
  gnd_tick(g, 0.0, 120.0, 4, 4, 1, -1);
  check(gnd_fires(g) == 0, "an empty block is a no-op");

  /* 4/4 at 120: one ring a beat, the downbeat strong. Beat by beat, so each
   * ring's strength can be read before the next replaces it. */
  int beats_ok = 1;
  double ppq = 0.0;
  for (int beat = 0; beat < 8; beat++)
  {
    /* From just before this beat to just before the next. */
    ppq = play(g, ppq, blocks_for(1.0, 120.0), 120.0, 4, 4, 1);
    const float want = beat % 4 == 0 ? 1.0f : 0.4f;
    if (gnd_fires(g) != (uint32_t) beat + 1 || gnd_strength(g) != want) beats_ok = 0;
  }
  check(beats_ok, "4/4 at 120 BPM: a ring a beat, 1.0 on the one, 0.4 on the rest");
  const uint32_t after_bars = gnd_fires(g);

  /* Stopped: nothing, however long. */
  play(g, ppq, blocks_for(8.0, 120.0), 120.0, 4, 4, 0);
  check(gnd_fires(g) == after_bars, "a stopped transport rings nothing");

  /* Reset forgets the last block but must NOT rewind the count. */
  gnd_reset(g);
  check(gnd_fires(g) == after_bars, "gnd_reset leaves the count alone");
  gnd_set_sample_rate(g, 44100.0);
  check(gnd_fires(g) == after_bars, "gnd_set_sample_rate leaves the count alone");

  /* 7/8 at 90, from the top, at the new rate: 0 1 2 3, the downbeat at 3.5,
   * 4 5 6 -- eight rings in seven quarters, the 1st and the 5th strong. One
   * block is BLOCK/44100 s; the meter and the tempo both have to arrive. */
  int seven_ok = 1, rang = 0, strong = 0;
  ppq = 0.0;
  const double span = BLOCK * 90.0 / (60.0 * 44100.0);
  while (ppq + span <= 6.9)
  {
    const uint32_t before = gnd_fires(g);
    gnd_tick(g, ppq, 90.0, 7, 8, 1, BLOCK);
    const uint32_t now = gnd_fires(g);
    if (now != before)
    {
      if (now - before != 1) seven_ok = 0;
      const int is_strong = gnd_strength(g) == 1.0f;
      if (is_strong != (rang == 0 || rang == 4)) seven_ok = 0;
      strong += is_strong;
      rang++;
    }
    ppq += span;
  }
  check(seven_ok && rang == 8 && strong == 2, "7/8 at 90 BPM, 44.1 kHz: 0 1 2 3 3.5 4 5 6");

  /* No time signature from the host is 4/4: 4 is a downbeat. Back at 48 kHz,
   * which play() assumes. */
  gnd_set_sample_rate(g, SR);
  gnd_tick(g, 4.0, 120.0, 0, 0, 1, BLOCK);
  check(gnd_strength(g) == 1.0f, "a host without a time signature is 4/4");

  /* The editor closes: the ground stops. It reopens: it rings again. */
  const uint32_t before_close = gnd_fires(g);
  gnd_set_active(g, 0);
  play(g, 8.0, blocks_for(4.0, 120.0), 120.0, 4, 4, 1);
  check(gnd_fires(g) == before_close, "an inactive ground adds no ring");
  gnd_set_active(g, 1);
  play(g, 16.0, blocks_for(1.0, 120.0), 120.0, 4, 4, 1);
  check(gnd_fires(g) == before_close + 1, "a reactivated ground rings again");

  gnd_free(g);

  printf(failures ? "\n%d failure(s)\n" : "\nall ok\n", failures);
  return failures ? 1 : 0;
}
