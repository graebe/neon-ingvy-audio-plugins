// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Listen-In's wire format, on its own so it can be tested.
 *
 * A plugin class cannot be constructed outside a plugin bundle, so anything
 * that can be quietly wrong lives here, in free functions over plain data, and
 * tests/cpp links it alone.
 */
#pragma once

#include <string>

namespace listenin {
namespace wire {

/* What the editor is told about the bus. Mirrored by STATUS in
 * ui/src/lib/msg.js -- the tag table and this enum are per-plugin, and the
 * table in ui/test/wire_table.txt is what keeps the two honest. */
enum Status
{
  kIdle        = 0, /* no slot claimed yet                        */
  kLive        = 1, /* publishing                                 */
  kTaken       = 2, /* another live sender holds this slot        */
  kUnavailable = 3, /* the segment could not be made or mapped    */
};

/*
 * "<slot>:<status>:<peak>" -- e.g. "3:1:0.4271".
 *
 * Peak has four fixed decimals, '.' whatever the locale: never an exponent
 * ("1e-05" for a quiet signal), because the editor splits on ':' and
 * parseFloats. 0.0001 is -80 dB, below anything a meter draws.
 */
std::string encode_state(int slot, int status, float peak);

/*
 * Sanitise a display name into `out` (at most `cap` bytes, always
 * NUL-terminated). Returns the number of bytes written, excluding the NUL.
 *
 * TWO THINGS IT HAS TO GET RIGHT, neither visible in a name that looks fine:
 *
 *   Control characters are dropped. The name crosses into the editor inside a
 *   colon-separated string; a stray newline or a colon typed into the field
 *   would split a field that is parsed by position.
 *
 *   TRUNCATION LANDS ON A UTF-8 BOUNDARY. The bus label is 32 bytes and a
 *   person typing "Bässe und Flächen" is producing two bytes per umlaut. Cut
 *   that at byte 31 and the last character is half of a sequence -- which is
 *   not a rendering glitch, it is a string the WebView's TextDecoder rejects
 *   outright, so the whole message is lost and the editor silently stops
 *   updating. Backing off to the boundary costs three comparisons.
 */
int parse_label(const char* in, char* out, int cap);

/* Slots are 1-based and there are ABUS_MAX_SLOT of them -- the engine's
 * number, the one the Bus parameter's range comes from too. A host restoring a
 * project written by a future version, or a parameter that arrived as 0, lands
 * on a valid slot rather than on an assertion. */
int clamp_slot(int slot);

} /* namespace wire */
} /* namespace listenin */
