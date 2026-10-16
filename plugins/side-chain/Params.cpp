// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's host parameters. Params.h says what they must stay.
 */
#include "Params.h"

#include "HostTable.h"
#include "ni/Wire.h"

namespace ni::sc
{

namespace
{
static_assert ((int) ParamSpec::Kind::integer == 0 && (int) ParamSpec::Kind::choice == 1
                   && (int) ParamSpec::Kind::toggle == 2 && (int) ParamSpec::Kind::continuous == 3,
               "NiHostParam's kinds are ParamSpec's, in order");

/*
 * THE ENGINE'S SCHEMA, ROW FOR ROW (sc_core_param_spec, HostTable.h): the
 * iPlug2 build's Declare, which this file used to copy. The schema's own
 * test and this product's (tests/side-chain/sc_processor.cpp) hold it to
 * that build's parameters.json; what each range means is said there
 * (sc-core/src/schema.rs).
 */
const HostTable<NiHostParam, kNumParams>& table()
{
    static const HostTable<NiHostParam, kNumParams> t (sc_core_param_spec, sc_core_param_option);
    return t;
}

/* The continuous values a host reads as percentages of the cycle or of the
 * whole: how their text is printed. */
bool isPercent (int index)
{
    return index == kDelay || index == kAttack || index == kHold || index == kRelease || index == kDepth
        || index == kVelSens;
}

} // namespace

const ParamSpec& specOf (int index)
{
    return table().specs()[index];
}

const ParamSpec* specs()
{
    return table().specs();
}

const nist::Layout& layout()
{
    /* The one optional string is the kick the plot shows (Kick.h). */
    static const nist::Layout l { specs(), kNumParams, { kNumParams }, 0, 1 };
    return l;
}

const nist::Layout& layoutWithoutKick()
{
    static const nist::Layout l { specs(), kNumParams, { kNumParams }, 0, 0 };
    return l;
}

/* The schema's own conversion: host = engine * scale + offset. */
double toEngine (int index, double plain)
{
    return table().toEngine (index, plain);
}

std::string rateLabel (int index)
{
    char label[32];
    if (sc_core_rate_label (index, label, (int) sizeof label) > 0)
        return label;
    std::string out;
    ni::wire::append_int (out, index);
    return out;
}

std::string noteName (int note)
{
    static const char* const pitch[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = note < 0 ? 0 : note > 127 ? 127 : note;
    std::string out (pitch[n % 12]);
    ni::wire::append_int (out, n / 12 - 2);
    return out;
}

std::string valueText (int index, double plain)
{
    std::string out;
    if (index == kThreshold)
    {
        if (plain <= -60.0)
            return "-inf dB";
        ni::wire::append_fixed (out, plain, 1);
        return out + " dB";
    }
    if (index == kLockout)
    {
        ni::wire::append_fixed (out, plain, 0);
        return out + " ms";
    }
    ni::wire::append_fixed (out, plain, isPercent (index) ? 1 : 2);
    return isPercent (index) ? out + " %" : out;
}

bool parseValue (int index, const std::string& text, double& plain)
{
    /* What the bottom of Threshold reads as reads back as that bottom; a
     * positive infinity is past the top, which the parameter clamps. */
    if (index == kThreshold && text.find ("inf") != std::string::npos)
    {
        plain = text.find ('-') != std::string::npos ? specOf (kThreshold).min : specOf (kThreshold).max;
        return true;
    }
    if (! ni::wire::starts_with_number (text))
        return false;
    plain = ni::wire::parse_number (text);
    return true;
}

} // namespace ni::sc
