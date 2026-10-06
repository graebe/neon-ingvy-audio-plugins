/*
 * The Trance Gate's saved state, in the layout every iPlug2 VST3 build wrote.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 *
 * tests/fixtures/iplug2/FORMAT.md is the contract; this reads all of it and
 * writes its current form, so that a set saved by either build opens in the
 * other:
 *
 *   "NIst" header, version 1   15 plain doubles, the engine's blob as an
 *                              IByteChunk string, and the int32 bypass that
 *                              iPlug2's VST3 wrapper appends -- and refuses to
 *                              load without, so this writes it too
 *   no header (legacy)         the same body from offset 0, with the 15, 14
 *                              or 12 parameters that build had
 *
 * Plain C++ and no engine, so test/ChunkTest.cpp drives it alone.
 */
#pragma once

#include "Params.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ni::tg::chunk {

/* The header's version. A later one may append fields; its size says where
 * the body ends, so this build skips what it does not know. */
constexpr int32_t kVersion = 1;

struct State
{
  /* Plain values, in the parameters' own units, by index. */
  double params[kNumParams] = {};
  /* The engine's state blob (tg_shell_save); empty in a chunk from a build
   * that saved none, which restores no pattern. */
  std::string blob;
  /* The VST3 wrapper's flag after the chunk, when the stream carries it. */
  std::optional<bool> bypass;
  /* How many parameters the chunk carried: 15, or 14 or 12 for a legacy
   * chunk from before the fade parameters, whose missing values are the
   * defaults those builds behaved as. */
  int carried = kNumParams;
  bool legacy = false;
};

/* What a VST3 stream holds, or nothing when no build wrote it -- in which
 * case the caller changes nothing, as iPlug2 did. Every check FORMAT.md
 * lists is made before anything is returned. */
std::optional<State> Read(const void* data, size_t size);

/* Version 1 with the bypass after it: what the iPlug2 build would write for
 * the same state, so it can read this build's sets too. */
std::vector<uint8_t> Write(const State& state);

} // namespace ni::tg::chunk
