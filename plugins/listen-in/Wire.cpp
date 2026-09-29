/*
 * Listen-In's wire format. See Wire.h for why it is not in ListenIn.cpp.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * <string> and <cstdio>, and deliberately nothing else: tests/cpp links this
 * translation unit on its own, and an include of anything iPlug2 would put it
 * back out of reach.
 */
#include "Wire.h"

#include <cstdio>

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

  char buf[64];
  std::snprintf(buf, sizeof(buf), "%d:%d:%.4f", slot, status, double(peak));
  return std::string(buf);
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
   * THE FIRST VERSION OF THIS WALKED BACK OVER CONTINUATION BYTES AND THEN
   * DROPPED THE LEAD BYTE UNCONDITIONALLY, which is right for a cut sequence
   * and WRONG FOR EVERY COMPLETE ONE: "Bä" is 42 C3 A4, and it came back as
   * "B". A name that fits perfectly is the common case, so the common case was
   * the broken one.
   *
   * So: find the last sequence's lead byte, ask how many bytes it promises,
   * and drop it only if that many are not actually there.
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
  if (slot > 16) return 16;
  return slot;
}

} /* namespace wire */
} /* namespace listenin */
