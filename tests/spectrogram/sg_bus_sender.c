// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A Listen-In's sending half, as a process of its own: what puts a real bus
 * in front of NI Spectrogram's processor in sg_processor.
 *
 *   sg_bus_sender <slot> <hz> <seconds> <label>
 *
 * It claims <slot> at 48 kHz in the bus namespace it inherits (NIA_BUS_NS),
 * names it <label>, and publishes a sine at <hz> in both channels at the pace
 * a host's audio thread would, for <seconds> or until it is stopped.
 *
 * WHY A PROCESS. The bus's C ABI is libbus_capi.a, the Spectrogram's receiver
 * is libspectro_capi.a, and two Rust archives cannot go into one binary (each
 * carries the Rust runtime: tests/CMakeLists.txt, srecv_api). A Listen-In and
 * a Spectrogram are two plugins anyway, and the bus is shared memory between
 * processes as much as between plugins in one (tests/abus_ipc.c).
 */
#include "audio_bus.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define RATE 48000
#define FRAMES 512

static volatile sig_atomic_t stop = 0;

static void on_term(int sig)
{
  (void) sig;
  stop = 1;
}

int main(int argc, char** argv)
{
  if (argc != 5)
  {
    fprintf(stderr, "usage: sg_bus_sender <slot> <hz> <seconds> <label>\n");
    return 2;
  }
  const unsigned slot = (unsigned) atoi(argv[1]);
  const double hz = atof(argv[2]);
  const double seconds = atof(argv[3]);
  signal(SIGTERM, on_term);

  abus_writer_t* w = NULL;
  abus_pusher_t* p = NULL;
  if (abus_writer_claim(slot, RATE, &w, &p) != ABUS_OK || w == NULL || p == NULL)
  {
    fprintf(stderr, "sg_bus_sender: cannot claim slot %u\n", slot);
    return 1;
  }
  abus_writer_set_label(w, argv[4]);

  float buf[FRAMES * 2];
  double phase = 0.0;
  const double step = 2.0 * M_PI * hz / RATE;
  const long blocks = (long) (seconds * RATE / FRAMES);
  for (long b = 0; b < blocks && !stop; b++)
  {
    for (int i = 0; i < FRAMES; i++)
    {
      const float v = (float) (0.5 * sin(phase));
      phase += step;
      buf[i * 2] = v;
      buf[i * 2 + 1] = v;
    }
    abus_pusher_push(p, buf, FRAMES);
    usleep((useconds_t) (1000000.0 * FRAMES / RATE));
  }
  abus_pusher_release(p);
  abus_writer_release(w);
  return 0;
}
