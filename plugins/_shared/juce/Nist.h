// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The state every iPlug2 VST3 build of ours saved, read and written: the
 * "NIst" chunk.
 *
 * tests/fixtures/iplug2/FORMAT.md is the contract, byte by byte; this is it
 * for every product, a product saying only what its chunk holds (Layout):
 *
 *   "NIst" header        magic, version, body size -- then the N parameters
 *                        as plain doubles, the product's strings (int32
 *                        length and bytes), and, after the body, the int32
 *                        bypass iPlug2's VST3 wrapper appends and refuses to
 *                        load without
 *   no header (legacy)   the same body from offset 0, from a build before
 *                        2026-09-30, with as many parameters as that build
 *                        had (`legacyCounts`): the count for which the body
 *                        ends at the stream's end, or four bytes before it
 *                        where the wrapper's bypass is -- 0 or 1, as it only
 *                        ever wrote
 *
 * Read makes every check FORMAT.md lists before it returns anything, so a
 * stream no build wrote changes nothing; Write writes the current form, which
 * the iPlug2 build reads as its own. A state read and written again is the
 * same bytes (the products' fixture tests hold that).
 *
 * Plain C++ and no JUCE, so a test drives it with nothing else.
 */
#pragma once

#include "ParamSpec.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ni::nist
{

/* The header's version: 1 for every product so far. A later one may append
 * fields; the body size says where they end, so they are skipped. */
constexpr int32_t version = 1;

struct Layout
{
    /* The product's parameters as it declares them now, in index order. */
    const ParamSpec* params = nullptr;
    int numParams = 0;
    /* The parameter counts a headerless chunk may carry, most recent first:
     * a count below numParams leaves the rest at their defaults, which is
     * what those builds behaved as. Empty: numParams only. */
    std::vector<int> legacyCounts;
    /* The strings after the parameters: the first `requiredStrings` must be
     * there; up to `maxStrings` are read, each later one optional. */
    int requiredStrings = 0;
    int maxStrings = 0;
};

struct State
{
    /* Plain values, in the parameters' own units, by index. */
    std::vector<double> params;
    std::vector<std::string> strings;
    /* The wrapper's flag after the chunk, when the stream carries it. */
    std::optional<bool> bypass;
    /* How many parameters the stream carried, and whether it had no header. */
    int carried = 0;
    bool legacy = false;
};

/* What a stream holds, or nothing when no build of this layout wrote it. */
std::optional<State> read (const Layout&, const void* data, std::size_t size);

/* The current form: header, parameters (a stepped one written whole, as iPlug2
 * stored it), strings, and the bypass after the body -- off when unknown. */
std::vector<std::uint8_t> write (const Layout&, const State&);

/* A state at the layout's defaults, with the required strings empty. */
State defaults (const Layout&);

} // namespace ni::nist
