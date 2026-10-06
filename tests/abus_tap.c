// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * abus_tap -- listen to a bus from the command line.
 *
 * NOT A TEST. This is the tool that answers "is Listen-In actually routing?"
 * against real audio in a real host, before any plugin exists that reads a bus
 * -- which is the whole point of shipping the transport ahead of the receiver.
 *
 * It is also the worked example of the receiving ABI: opening a slot, draining
 * it, and paying attention to `dropped`, in sixty lines. Whoever wires the
 * Spectrogram to a bus starts here.
 *
 *   abus_tap            what every slot is doing, once
 *   abus_tap 3          follow slot 3 until interrupted
 */
#include "audio_bus.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHUNK 4096

static void list_all(void)
{
  printf("slot  state   rate   name\n");
  for (uint32_t s = 1; s <= abus_max_slot(); s++)
  {
    int32_t live = 0;
    uint32_t sr = 0;
    char label[ABUS_LABEL_CAP];
    if (!abus_probe(s, &live, &sr, label, ABUS_LABEL_CAP)) continue;
    printf("%4u  %-6s  %5u  %s\n", s, live ? "live" : "idle", sr, label);
  }
}

int main(int argc, char** argv)
{
  if (argc < 2) { list_all(); return 0; }

  const uint32_t slot = (uint32_t) atoi(argv[1]);
  abus_reader_t* r = NULL;
  if (abus_reader_open(slot, &r) != ABUS_OK || r == NULL)
  {
    fprintf(stderr, "slot %u is not there -- nothing has claimed it.\n", slot);
    return 1;
  }

  int32_t live = 0;
  uint32_t sr = 0;
  char label[ABUS_LABEL_CAP];
  abus_probe(slot, &live, &sr, label, ABUS_LABEL_CAP);
  printf("slot %u  \"%s\"  %u Hz  %s\n", slot, label, sr, live ? "live" : "idle");

  float* buf = (float*) malloc(sizeof(float) * CHUNK * abus_channels());
  if (buf == NULL) return 1;

  uint64_t total = 0, dropped_total = 0;
  for (;;)
  {
    float peak = 0.f;
    uint32_t frames = 0;
    for (int i = 0; i < 16; i++)
    {
      uint64_t dropped = 0;
      int32_t resynced = 0;
      const uint32_t got = abus_reader_read(r, buf, CHUNK, &dropped, &resynced);
      dropped_total += dropped;
      if (resynced) printf("  -- the sender restarted\n");
      for (uint32_t k = 0; k < got * abus_channels(); k++)
      {
        const float a = fabsf(buf[k]);
        if (a > peak) peak = a;
      }
      frames += got;
      if (got == 0) break;
    }
    total += frames;

    const double db = peak > 0.f ? 20.0 * log10((double) peak) : -120.0;
    printf("\r%8llu frames  peak %6.1f dB  dropped %llu    ",
           (unsigned long long) total, db, (unsigned long long) dropped_total);
    fflush(stdout);
    usleep(100000);
  }
}
