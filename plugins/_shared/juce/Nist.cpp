// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The iPlug2 builds' state chunk. Nist.h and tests/fixtures/iplug2/FORMAT.md.
 */
#include "Nist.h"

#include <cmath>
#include <cstring>

namespace ni::nist
{

namespace
{
/* "NIst", then the tail that makes the eight bytes a quiet NaN when read as a
 * double -- which no parameter can be, so no headerless chunk looks like one
 * (the layout tests/fixtures/iplug2/FORMAT.md records). */
constexpr std::uint8_t magic[8] { 'N', 'I', 's', 't', 0x00, 0x00, 0xF8, 0x7F };
constexpr std::size_t headerBytes = 16;

/*
 * LITTLE-ENDIAN, SPELLED OUT. iPlug2 copied host memory into the chunk, and
 * every machine it shipped on is little-endian, so that is the format; reading
 * and writing byte by byte keeps it the format on any machine this builds for.
 */
std::int32_t getI32 (const std::uint8_t* p)
{
    return (std::int32_t) ((std::uint32_t) p[0] | (std::uint32_t) p[1] << 8 | (std::uint32_t) p[2] << 16
                           | (std::uint32_t) p[3] << 24);
}

double getF64 (const std::uint8_t* p)
{
    std::uint64_t bits = 0;
    for (int i = 7; i >= 0; --i)
        bits = bits << 8 | p[i];
    double v;
    std::memcpy (&v, &bits, sizeof v);
    return v;
}

void putI32 (std::vector<std::uint8_t>& out, std::int32_t v)
{
    const auto u = (std::uint32_t) v;
    for (int i = 0; i < 4; ++i)
        out.push_back ((std::uint8_t) (u >> (8 * i)));
}

void putF64 (std::vector<std::uint8_t>& out, double v)
{
    std::uint64_t bits;
    std::memcpy (&bits, &v, sizeof bits);
    for (int i = 0; i < 8; ++i)
        out.push_back ((std::uint8_t) (bits >> (8 * i)));
}

/*
 * A NUMBER OF THE RIGHT KIND, NOT A NUMBER IN RANGE -- iPlug2's CheckParams.
 * Finite always; a stepped parameter's a whole number, because iPlug2 rounds
 * those before it stores them. A range can narrow between versions, so a
 * value outside it is clamped when applied, never refused.
 */
bool plausible (const ParamSpec& spec, double v)
{
    if (! std::isfinite (v))
        return false;
    return ! spec.stepped() || (std::fabs (v) <= 2147483647.0 && v == std::floor (v));
}

/* `count` parameters from `at`, inside `end`; the rest take their defaults.
 * The position after them, or nothing. */
std::optional<std::size_t> getParams (const Layout& layout, const std::uint8_t* b, std::size_t at, std::size_t end,
                                      int count, State& s)
{
    if (count > layout.numParams || at + (std::size_t) count * 8 > end)
        return std::nullopt;
    s.params.resize ((std::size_t) layout.numParams);
    for (int i = 0; i < layout.numParams; ++i)
        s.params[(std::size_t) i] = layout.params[i].def;
    for (int i = 0; i < count; ++i)
    {
        const double v = getF64 (b + at + (std::size_t) i * 8);
        if (! plausible (layout.params[i], v))
            return std::nullopt;
        s.params[(std::size_t) i] = v;
    }
    s.carried = count;
    return at + (std::size_t) count * 8;
}

/* One string at `at`, inside `end`: its length (>= 0) and its bytes. */
std::optional<std::size_t> getString (const std::uint8_t* b, std::size_t at, std::size_t end, State& s)
{
    if (at + 4 > end)
        return std::nullopt;
    const std::int32_t len = getI32 (b + at);
    if (len < 0 || at + 4 + (std::size_t) len > end)
        return std::nullopt;
    s.strings.emplace_back (reinterpret_cast<const char*> (b + at + 4), (std::size_t) len);
    return at + 4 + (std::size_t) len;
}

std::optional<State> readHeaded (const Layout& layout, const std::uint8_t* b, std::size_t size)
{
    const std::int32_t body = getI32 (b + 12);
    if (body < 0 || headerBytes + (std::size_t) body > size)
        return std::nullopt;
    const std::size_t end = headerBytes + (std::size_t) body;
    State s;
    auto pos = getParams (layout, b, headerBytes, end, layout.numParams, s);
    if (! pos)
        return std::nullopt;
    for (int i = 0; i < layout.maxStrings; ++i)
    {
        const auto after = getString (b, *pos, end, s);
        if (! after)
        {
            /* A required string is the chunk; past them, what does not read
             * as a string belongs to a later version and is skipped. */
            if (i < layout.requiredStrings)
                return std::nullopt;
            break;
        }
        pos = after;
    }
    if (size >= end + 4)
        s.bypass = getI32 (b + end) != 0;
    return s;
}

/*
 * NO HEADER: A BUILD FROM BEFORE 2026-09-30. Its body starts at 0 and its
 * parameter count is not written down, because it was simply that build's.
 * The count, and how many optional strings it had, are the ones for which
 * the body ends exactly at the end of the stream, or four bytes before it,
 * where the wrapper's bypass is.
 */
std::optional<State> readLegacy (const Layout& layout, const std::uint8_t* b, std::size_t size)
{
    std::vector<int> counts = layout.legacyCounts;
    if (counts.empty())
        counts.push_back (layout.numParams);
    for (const int count : counts)
    {
        State s;
        s.legacy = true;
        auto pos = getParams (layout, b, 0, size, count, s);
        if (! pos)
            continue;
        for (int k = 0;; ++k)
        {
            if (k >= layout.requiredStrings)
            {
                /* The wrapper wrote 0 or 1 there, and nothing else: four
                 * bytes that are neither are not its bypass, which is what
                 * tells a body from bytes that only happen to end there. */
                const auto flag = *pos + 4 == size ? getI32 (b + *pos) : -1;
                if (flag == 0 || flag == 1)
                {
                    s.bypass = flag == 1;
                    return s;
                }
                if (*pos == size)
                    return s;
            }
            if (k == layout.maxStrings)
                break;
            pos = getString (b, *pos, size, s);
            if (! pos)
                break;
        }
    }
    return std::nullopt;
}
} // namespace

std::optional<State> read (const Layout& layout, const void* data, std::size_t size)
{
    const auto* b = static_cast<const std::uint8_t*> (data);
    if (b == nullptr || size == 0)
        return std::nullopt;
    if (size >= headerBytes && std::memcmp (b, magic, sizeof magic) == 0)
        return readHeaded (layout, b, size);
    return readLegacy (layout, b, size);
}

std::vector<std::uint8_t> write (const Layout& layout, const State& state)
{
    std::vector<std::uint8_t> out (magic, magic + sizeof magic);
    putI32 (out, version);
    const std::size_t sizeAt = out.size();
    putI32 (out, 0);
    for (int i = 0; i < layout.numParams; ++i)
    {
        const double v = (std::size_t) i < state.params.size() ? state.params[(std::size_t) i] : layout.params[i].def;
        putF64 (out, layout.params[i].stepped() ? std::round (v) : v);
    }
    for (int i = 0; i < layout.maxStrings; ++i)
    {
        /* Every string the layout knows, so a reader of this version finds
         * them all; one the state lacks is empty. */
        const std::string none;
        const auto& str = (std::size_t) i < state.strings.size() ? state.strings[(std::size_t) i] : none;
        putI32 (out, (std::int32_t) str.size());
        out.insert (out.end(), str.begin(), str.end());
    }
    const auto body = (std::uint32_t) (out.size() - headerBytes);
    for (int i = 0; i < 4; ++i)
        out[sizeAt + (std::size_t) i] = (std::uint8_t) (body >> (8 * i));
    /* After the chunk, as iPlug2's VST3 wrapper wrote it: its setState fails
     * when these four bytes are missing. */
    putI32 (out, state.bypass.value_or (false) ? 1 : 0);
    return out;
}

State defaults (const Layout& layout)
{
    State s;
    for (int i = 0; i < layout.numParams; ++i)
        s.params.push_back (layout.params[i].def);
    s.strings.resize ((std::size_t) layout.requiredStrings);
    s.carried = layout.numParams;
    return s;
}

} // namespace ni::nist
