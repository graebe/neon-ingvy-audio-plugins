// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Listen-In's wire format. See Wire.h.
 *
 * audio_bus.h only for ABUS_MAX_SLOT, a macro: nothing is linked, and
 * tests/cpp links this translation unit on its own.
 */
#include "Wire.h"
#include "ni/Wire.h"

#include "audio_bus.h"

namespace listenin {
namespace wire {

std::string encode_state(int slot, int status, float peak)
{
  if (peak < 0.f) peak = 0.f;
  /* A meter is drawn from this, and a sample above full scale is a real thing
   * a host can hand us -- but a bar past the end of its well is a drawing bug,
   * not information. Clamp here rather than in the editor, so every reader of
   * this format gets the same answer. */
  if (peak > 1.f) peak = 1.f;

  std::string out;
  ni::wire::append_int(out, slot);
  out += ':';
  ni::wire::append_int(out, status);
  out += ':';
  ni::wire::append_fixed(out, double(peak), 4);
  return out;
}

int parse_label(const char* in, char* out, int cap)
{
  if (out == nullptr || cap <= 0) return 0;
  out[0] = '\0';
  if (in == nullptr) return 0;

  const int limit = cap - 1; /* room for the NUL */
  int n = 0;

  for (const unsigned char* p = (const unsigned char*) in; *p != 0; p++)
  {
    const unsigned char c = *p;
    /* Control characters and DEL, dropped. A colon or a newline typed into the
     * name field would split a message that is parsed by position. */
    if (c < 0x20 || c == 0x7f || c == ':') continue;
    if (n >= limit) break;
    out[n++] = char(c);
  }

  /*
   * AND NOW DROP A TRAILING PARTIAL CHARACTER, IF THERE IS ONE.
   *
   * The loop above stops at `limit` bytes, which may be the middle of a
   * multi-byte sequence. A string cut there is not a rendering glitch -- it is
   * one the editor's TextDecoder refuses outright, so the whole message is
   * lost and the editor silently stops updating.
   *
   * Find the last sequence's lead byte, ask how many bytes it promises, and
   * drop it only if that many are not there -- a complete "Bä" (42 C3 A4)
   * must survive whole.
   */
  {
    int i = n - 1;
    while (i >= 0 && (((unsigned char) out[i]) & 0xC0) == 0x80)
      i--;

    if (i >= 0)
    {
      const unsigned char lead = (unsigned char) out[i];
      int need;
      if ((lead & 0x80) == 0x00)      need = 1;  /* ASCII                  */
      else if ((lead & 0xE0) == 0xC0) need = 2;
      else if ((lead & 0xF0) == 0xE0) need = 3;
      else if ((lead & 0xF8) == 0xF0) need = 4;
      else                            need = -1; /* a stray continuation   */

      if (need < 0 || i + need > n)
        n = i;
    }
  }

  out[n] = '\0';
  return n;
}

int clamp_slot(int slot)
{
  if (slot < 1) return 1;
  if (slot > ABUS_MAX_SLOT) return ABUS_MAX_SLOT;
  return slot;
}

} /* namespace wire */
} /* namespace listenin */
