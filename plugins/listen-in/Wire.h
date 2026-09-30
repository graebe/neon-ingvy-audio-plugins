/*
 * Listen-In's wire format, on its own so it can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THIS IS NOT IN ListenIn.cpp, which is the Spectrogram's reason and worth
 * restating because it is structural rather than stylistic: iplug::Plugin is a
 * typedef to IPlugVST3 or IPlugAU depending on which define is set, nothing can
 * construct one outside a plugin bundle, and IPlug_include_in_plug_hdr.h
 * #errors outside one. So NOTHING in the plugin class can be reached by a test.
 *
 * Anything that can be quietly wrong therefore lives here instead, in free
 * functions over plain data. This file includes <string> and <cstdio> and
 * nothing else, which is what lets tests/cpp link it on its own.
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
 * Peak is fixed to four decimals rather than %g, because %g emits "1e-05" for
 * a quiet signal and the editor parses this with parseFloat on a split(':').
 * Four decimals is 0.0001, which is -80 dB: below anything a meter draws.
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
