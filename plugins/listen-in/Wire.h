// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Listen-In keeps of what it is given: a bus name and a bus number,
 * on their own so they can be tested. Plain C++.
 */
#pragma once

namespace ni::li::wire
{

/*
 * Keep a bus name into `out` (at most `cap` bytes, always NUL-terminated).
 * Returns the number of bytes written, excluding the NUL.
 *
 * TWO THINGS IT HAS TO GET RIGHT, neither visible in a name that looks fine:
 *
 *   Control characters, DEL and colons are dropped. A receiver lists the bus
 *   by this name on one line, and the iPlug2 builds sent it to their editor
 *   inside a colon-separated message, so no build has ever kept one; a set
 *   saved by such a build opens with exactly the name it showed.
 *
 *   TRUNCATION LANDS ON A UTF-8 BOUNDARY. The bus label is 32 bytes and a
 *   person typing "Bässe und Flächen" is producing two bytes per umlaut. Cut
 *   at byte 31, the last character would be half a sequence: a name no text
 *   decoder accepts, in the saved set and on the bus alike.
 */
int parse_label (const char* in, char* out, int cap);

/* Buses are 1-based and there are ABUS_MAX_SLOT of them -- the engine's
 * number, the one the Bus parameter's range comes from too. A value from a
 * future build, or one that arrived as 0, lands on a bus that exists. */
int clamp_slot (int slot);

} // namespace ni::li::wire
