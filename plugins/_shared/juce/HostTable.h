// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A product's host table, read from its engine's schema: the C++ half of
 * ni_params::host (engines/shared/crates/ni-params/src/host.rs), once, for
 * the products that took over an iPlug2 build (Trance Gate, Side-Chain).
 *
 * The engine declares each parameter as the iPlug2 build did -- ID, name,
 * kind, range, default and units, in the HOST's plain values -- and its
 * words one at a time. This reads every row once into the ni::ParamSpec
 * table the processor builds its parameters from and the state codec
 * (Nist.h) reads, and keeps each row's conversion,
 * host = engine * scale + offset.
 *
 * A product hands over its two entry points:
 *
 *   bool spec (int index, NiHostParam* out);       the row
 *   const char* option (int index, int option);    a word, or null
 *
 * Plain C++, no JUCE, so a test links it without the plugin. Read once, at
 * first use, and kept for the program: the rows point into the engine's
 * static strings and into this table's own word lists.
 */
#pragma once

#include "ParamSpec.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

namespace ni
{

template <typename Row, std::size_t N>
class HostTable
{
public:
    template <typename Spec, typename Option>
    HostTable (Spec spec, Option option)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            Row r {};
            const bool known = spec ((int) i, &r);
            assert (known);
            (void) known;
            auto& w = words[i];
            for (int k = 0; k < r.num_texts; ++k)
                w.push_back (option ((int) i, k));
            rows[i] = ParamSpec { r.id,    r.name,
                                  (ParamSpec::Kind) r.kind,
                                  r.min,   r.max,
                                  r.def,   r.units,
                                  w.empty() ? nullptr : w.data(),
                                  (int) w.size() };
            scales[i] = { r.scale, r.offset };
        }
    }

    HostTable (const HostTable&) = delete;
    HostTable& operator= (const HostTable&) = delete;

    const ParamSpec* specs() const noexcept { return rows.data(); }

    /* A host's plain value as the engine's number, and back. */
    double toEngine (int index, double plain) const noexcept
    {
        const auto& s = scales[(std::size_t) index];
        return (plain - s.offset) / s.scale;
    }
    double fromEngine (int index, double engine) const noexcept
    {
        const auto& s = scales[(std::size_t) index];
        return engine * s.scale + s.offset;
    }

private:
    struct Scale
    {
        double scale = 1.0, offset = 0.0;
    };

    std::array<ParamSpec, N> rows {};
    std::array<std::vector<const char*>, N> words;
    std::array<Scale, N> scales {};
};

} // namespace ni
