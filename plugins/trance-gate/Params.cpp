// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's host parameters. Params.h says what they must stay.
 */
#include "Params.h"

#include "ni/Wire.h"

#include <cstring>

namespace ni::tg
{

namespace
{
using Kind = ParamSpec::Kind;

const char* const offOn[] { "Off", "On" };
/* "%", not "% Step": what the percentage is OF is said once, by the name. */
const char* const envTime[] { "ms", "%" };
const char* const curves[] { "Linear", "Exponential", "S-Curve" };
/* The switch does not turn the fade on; it chooses whether an arriving step
 * ramps or jumps. */
const char* const hardSoft[] { "Hard", "Soft" };
/* Which end the pattern is built up from; In is what the gate did before the
 * direction existed. */
const char* const inOut[] { "In", "Out" };

/* The iPlug2 build's Params.cpp Declare, row for row. */
const ParamSpec table[kNumParams] {
    { "slot", "Slot", Kind::integer, 1, TG_SLOTS, 1, "" },
    { "length", "Length", Kind::integer, 1, TG_MAX_STEPS, 16, "steps" },
    { "rate", "Rate", Kind::choice, 0, TG_NUM_RATES - 1, TG_RATE_DEFAULT, "" },
    { "legato", "Join Neighbors", Kind::toggle, 0, 1, 0, "", offOn, 2 },
    { "timeMode", "Env Time", Kind::choice, 0, 1, 0, "", envTime, 2 },
    { "curve", "Env Curve", Kind::choice, 0, 2, 0, "", curves, 3 },
    { "amount", "Amount", Kind::continuous, 0, 100, 100, "" },
    { "width", "Width", Kind::continuous, 5, 100, 100, "" },
    { "attack", "Attack", Kind::continuous, 0, TG_STAGE_MAX_PCT, 1.6, "" },
    { "decay", "Decay", Kind::continuous, 0, TG_STAGE_MAX_PCT, 16, "" },
    { "sustain", "Sustain", Kind::continuous, 0, 100, 100, "" },
    { "release", "Release", Kind::continuous, 0, TG_STAGE_MAX_PCT, 16, "" },
    /* 100 IS THE DEFAULT AND HAS TO BE: Fade is how much of the pattern has
     * arrived, so zero is silence -- which a fresh instance must not be. */
    { "fade", "Fade", Kind::continuous, 0, 100, 100, "" },
    { "fadeSoft", "Fade Shape", Kind::toggle, 0, 1, 0, "", hardSoft, 2 },
    { "fadeDir", "Fade Dir", Kind::choice, 0, 1, 0, "", inOut, 2 },
};

bool isPercent (int index)
{
    return index == kAmount || index == kWidth || index == kSustain || index == kFade;
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
    return table[index];
}

const ParamSpec* specs()
{
    return table;
}

const nist::Layout& layout()
{
    static const nist::Layout l { table, kNumParams, { 15, 14, 12 }, 1, 1 };
    return l;
}

double toEngine (int index, double plain)
{
    if (index == kSlot || index == kLength)
        return plain - 1.0;
    return isPercent (index) ? plain / 100.0 : plain;
}

double fromEngine (int index, double engine)
{
    if (index == kSlot || index == kLength)
        return engine + 1.0;
    return isPercent (index) ? engine * 100.0 : engine;
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
