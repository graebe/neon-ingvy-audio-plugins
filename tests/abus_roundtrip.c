/*
 * The audio bus through its C ABI, in one process.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * bus-core's own cargo tests cover the ring in Rust. THIS one compiles against
 * the hand-written engines/audio-bus/include/audio_bus.h and links the real
 * staticlib, which is the only thing that catches the header drifting from the
 * implementation -- both sides keep compiling while they disagree.
 *
 * The claims here are the ones a receiver depends on: samples come back
 * unchanged, falling behind is REPORTED rather than papered over, and a second
 * sender on one slot is refused.
 */
#include "audio_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Its own slots, clear of the ones bus-core's cargo tests use (11..16) and of
 * abus_ipc.c's -- ctest may run these concurrently. */
#define SLOT      9
#define SLOT_TAKEN 8

static int failures = 0;

static void check(int ok, const char* what)
{
  if (!ok) { printf("FAIL: %s\n", what); failures++; }
  else     { printf("ok:   %s\n", what); }
}

int main(void)
{
  const uint32_t ch = abus_channels();
  check(ch == 2, "the bus is stereo");
  check(abus_max_slot() == 16, "there are sixteen slots");

  abus_writer_t* w = NULL;
  check(abus_writer_claim(SLOT, 48000, &w) == ABUS_OK && w != NULL,
        "a sender can claim a free slot");
  if (w == NULL) return 1;

  abus_writer_set_label(w, "Bass");

  abus_reader_t* r = NULL;
  check(abus_reader_open(SLOT, &r) == ABUS_OK && r != NULL,
        "a receiver can open a live slot");
  if (r == NULL) return 1;

  /* A ramp, so a wrong answer is a wrong NUMBER rather than a plausible one. */
  enum { N = 480 };
  float in[N * 2], out[N * 2];
  for (int i = 0; i < N; i++) { in[i * 2] = (float) i; in[i * 2 + 1] = (float) -i; }

  abus_writer_push(w, in, N);

  uint64_t dropped = 0;
  int32_t resynced = 0;
  uint32_t got = abus_reader_read(r, out, N, &dropped, &resynced);
  check(got == N, "every frame pushed comes back");
  check(dropped == 0 && resynced == 0, "and nothing is reported missing");
  check(memcmp(in, out, sizeof(in)) == 0, "the samples are unchanged");

  /* A SECOND SENDER IS REFUSED, not quietly allowed to overwrite. */
  abus_writer_t* w2 = NULL;
  check(abus_writer_claim(SLOT, 48000, &w2) == ABUS_ERR_TAKEN && w2 == NULL,
        "a second sender on a held slot is refused");

  /* Out of range is a mistake and is reported as one. */
  abus_writer_t* w3 = NULL;
  check(abus_writer_claim(0, 48000, &w3) == ABUS_ERR_BAD_SLOT,
        "slot 0 is not a bus");
  check(abus_writer_claim(abus_max_slot() + 1, 48000, &w3) == ABUS_ERR_BAD_SLOT,
        "nor is one past the end");

  /*
   * FALLING BEHIND IS REPORTED. A receiver that stalls and then reads must be
   * able to tell a gap from silence -- otherwise a spectrogram draws the
   * missing seconds as a quiet passage that never happened.
   */
  float* big = (float*) malloc(sizeof(float) * 8192 * 2);
  if (big == NULL) return 1;
  for (int i = 0; i < 8192 * 2; i++) big[i] = 0.5f;
  for (int k = 0; k < 64; k++) abus_writer_push(w, big, 8192);  /* 512k frames */

  dropped = 0;
  got = abus_reader_read(r, out, N, &dropped, &resynced);
  check(dropped > 0, "a receiver that fell behind is told how much it lost");
  check(got > 0, "and still receives the audio that survived");
  free(big);

  /* A probe describes the slot without opening it. */
  int32_t live = 0;
  uint32_t sr = 0;
  char label[ABUS_LABEL_CAP];
  check(abus_probe(SLOT, &live, &sr, label, ABUS_LABEL_CAP) == 1,
        "a claimed slot is visible to a probe");
  check(live == 1, "and reads as live");
  check(sr == 48000, "and reports its sample rate");
  check(strcmp(label, "Bass") == 0, "and carries its name");

  /* A SENDER THAT COMES BACK makes a new segment; the open reader follows it
   * only when asked, and says so with a resync. */
  check(abus_reader_reattach(r) == 0, "a current reader does not move");
  abus_writer_release(w);
  w = NULL;
  check(abus_writer_claim(SLOT, 48000, &w) == ABUS_OK && w != NULL,
        "the slot can be claimed again");
  check(abus_reader_reattach(r) == 1, "the reader moves to the new segment");
  got = abus_reader_read(r, out, N, &dropped, &resynced);
  check(got == 0 && resynced == 1, "and reports the move as a restart");
  abus_writer_push(w, in, N);
  got = abus_reader_read(r, out, N, &dropped, &resynced);
  check(got == N && resynced == 0, "then hears the new sender");

  abus_reader_close(r);
  abus_writer_release(w);

  /* A released slot is gone, not merely idle. */
  check(abus_probe(SLOT, &live, &sr, label, ABUS_LABEL_CAP) == 0,
        "a released slot no longer exists");

  /* And a slot nobody has touched is not brought into being by asking. */
  check(abus_probe(SLOT_TAKEN, &live, &sr, label, ABUS_LABEL_CAP) == 0,
        "probing an unused slot does not create it");
  check(abus_probe(SLOT_TAKEN, &live, &sr, label, ABUS_LABEL_CAP) == 0,
        "and asking twice still does not");

  printf("%s\n", failures ? "FAILED" : "all good");
  return failures ? 1 : 0;
}
