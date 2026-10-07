// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's name and bus number. Wire.h says why each rule exists.
 *
 * audio_bus.h only for ABUS_MAX_SLOT, a macro: nothing is linked.
 */
#include "Wire.h"

#include "audio_bus.h"

namespace ni::li::wire
{

int parse_label (const char* in, char* out, int cap)
{
    if (out == nullptr || cap <= 0)
        return 0;
    out[0] = '\0';
    if (in == nullptr)
        return 0;

    const int limit = cap - 1; /* room for the NUL */
    int n = 0;
    for (const auto* p = reinterpret_cast<const unsigned char*> (in); *p != 0; ++p)
    {
        const unsigned char c = *p;
        if (c < 0x20 || c == 0x7f || c == ':')
            continue;
        if (n >= limit)
            break;
        out[n++] = (char) c;
    }

    /*
     * AND NOW DROP A TRAILING PARTIAL CHARACTER, IF THERE IS ONE. The loop
     * stops at `limit` bytes, which may be the middle of a sequence: find the
     * last sequence's lead byte, ask how many bytes it promises, and drop it
     * only if that many are not there -- a complete "Bä" (42 C3 A4) must
     * survive whole.
     */
    int i = n - 1;
    while (i >= 0 && (((unsigned char) out[i]) & 0xC0) == 0x80)
        --i;
    if (i >= 0)
    {
        const auto lead = (unsigned char) out[i];
        const int need = (lead & 0x80) == 0x00   ? 1
                         : (lead & 0xE0) == 0xC0 ? 2
                         : (lead & 0xF0) == 0xE0 ? 3
                         : (lead & 0xF8) == 0xF0 ? 4
                                                 : -1; /* a stray continuation */
        if (need < 0 || i + need > n)
            n = i;
    }

    out[n] = '\0';
    return n;
}

int clamp_slot (int slot)
{
    if (slot < 1)
        return 1;
    if (slot > ABUS_MAX_SLOT)
        return ABUS_MAX_SLOT;
    return slot;
}

} // namespace ni::li::wire
