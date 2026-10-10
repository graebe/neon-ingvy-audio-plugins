// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's host parameters. Params.h says what they must stay.
 */
#include "Params.h"

#include "ni/Wire.h"

#include <array>
#include <cstring>

namespace ni::tg
{

namespace
{
static_assert ((int) ParamSpec::Kind::integer == 0 && (int) ParamSpec::Kind::choice == 1
                   && (int) ParamSpec::Kind::toggle == 2 && (int) ParamSpec::Kind::continuous == 3,
               "tg_param_spec_t's kinds are ParamSpec's, in order");

/*
 * THE ENGINE'S SCHEMA, ROW FOR ROW (tg_core_param_spec). This file used to
 * hold its own copy of the fifteen rows -- the iPlug2 build's Declare -- and
 * the engine and the Move held theirs; now all three are tg_core::schema's.
 * The rows are still the iPlug2 build's, held to its parameters.json by the
 * schema's own test and by this product's (tests/tg_processor.cpp).
 */
const ParamSpec* table()
{
    static const auto rows = []
    {
        std::array<ParamSpec, kNumParams> t {};
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& r = *tg_core_param_spec (i);
            t[(std::size_t) i] = ParamSpec { r.id, r.name, (ParamSpec::Kind) r.kind, r.min, r.max, r.def,
                                             r.units, r.texts, r.num_texts };
        }
        return t;
    }();
    return rows.data();
}

/* Whether `s` starts with a number, as parse_number would read one. */
bool startsWithNumber (const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
        ++i;
    if (i < s.size() && s[i] == '.')
        ++i;
    return i < s.size() && s[i] >= '0' && s[i] <= '9';
}
} // namespace

const ParamSpec& specOf (int index)
{
    return table()[index];
}

const ParamSpec* specs()
{
    return table();
}

const nist::Layout& layout()
{
    static const nist::Layout l { table(), kNumParams, { 15, 14, 12 }, 1, 1 };
    return l;
}

/* The schema's own conversion: host = engine * scale + offset. */
double toEngine (int index, double plain)
{
    const auto& r = *tg_core_param_spec (index);
    return (plain - r.offset) / r.scale;
}

double fromEngine (int index, double engine)
{
    const auto& r = *tg_core_param_spec (index);
    return engine * r.scale + r.offset;
}

/*
 * EXACTLY, AS FLOATS, and on purpose: the engine holds a float, and a switch
 * moves a host parameter only when the float it would hold differs. A
 * tolerance would leave a real difference unfollowed.
 */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
bool sameInEngine (int index, double plain, double engine)
{
    return (float) toEngine (index, plain) == (float) engine;
}
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

std::string percentText (double plain)
{
    std::string out;
    ni::wire::append_fixed (out, plain, 2);
    return out + " %";
}

bool parsePercent (const std::string& text, double& plain)
{
    if (! startsWithNumber (text))
        return false;
    plain = ni::wire::parse_number (text);
    return true;
}

bool isStage (int index)
{
    return index == kAttack || index == kDecay || index == kRelease;
}

std::string formatStage (double pct, bool ms, double widthMs)
{
    std::string out;
    if (ms && widthMs > 0.0)
    {
        ni::wire::append_fixed (out, pct / 100.0 * widthMs, 1);
        return out + " ms";
    }
    ni::wire::append_fixed (out, pct, 2);
    return out + " %";
}

bool parseStage (const std::string& text, bool ms, double widthMs, double& pct)
{
    if (! startsWithNumber (text))
        return false;
    const double v = ni::wire::parse_number (text);
    /* The unit typed wins over the mode: "40 ms" means milliseconds even
     * while the readout shows percent, and "25 %" the other way round. */
    const bool saysMs = text.find ("ms") != std::string::npos;
    const bool saysPct = text.find ('%') != std::string::npos;
    const bool asMs = saysMs || (ms && ! saysPct);
    double p = v;
    if (asMs)
        p = widthMs > 0.0 ? v / widthMs * 100.0 : 0.0;
    pct = p < 0.0 ? 0.0 : p > TG_STAGE_MAX_PCT ? (double) TG_STAGE_MAX_PCT : p;
    return true;
}

} // namespace ni::tg
