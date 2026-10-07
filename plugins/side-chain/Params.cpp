// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's host parameters. Params.h says what they must stay.
 */
#include "Params.h"

#include "ni/Wire.h"

namespace ni::sc
{

namespace
{
using Kind = ParamSpec::Kind;

const char* const sources[] { "Cycle", "MIDI", "Sidechain" };
/*
 * TIME IS A READING AND CHANGES NO SOUND. The four stages are percentages of
 * the cycle, always (sc-core's lib.rs has the long note): a unit switch that
 * changed their meaning would give the host four parameters that do something
 * different depending on a fifth, and a host caches parameter displays. The
 * editor shows the engine's stage_ms when this says ms.
 */
const char* const timeModes[] { "ms", "% of cycle" };
/* Three, not four: the asymmetric Pump curve is gone. */
const char* const curves[] { "Linear", "Exponential", "S-Curve" };
/* Omni and the sixteen channels, as a choice, so the host's menu reads Omni. */
const char* const channels[] { "Omni", "1", "2",  "3",  "4",  "5",  "6",  "7", "8",
                               "9",    "10", "11", "12", "13", "14", "15", "16" };
const char* const midiModes[] { "Trigger", "Gate" };

/*
 * The iPlug2 build's Params.cpp Declare, row for row.
 *
 * DELAY RUNS BOTH WAYS, -100..+100, AND THE NEGATIVE HALF IS THE POINT: on
 * Cycle "20 % early" is "80 % into the previous cycle", a position already
 * passed. MIDI and Sidechain have nothing periodic to anticipate, so the
 * engine clamps a negative Delay to no wait there; the parameter still
 * travels the whole range, because an automation lane may sweep through it.
 * Attack, Hold and Release run to twice the cycle, as far as a stage can go
 * and still finish before the trigger after next.
 *
 * A NOTE IS AN ADDRESS, NOT A POSITION ON A RANGE: Trigger is a choice of the
 * 128 notes by name, so a host draws "C1" and steps a semitone at a time.
 */
const ParamSpec table[kNumParams] {
    { "source", "Source", Kind::choice, 0, 2, 0, "", sources, 3 },
    { "rate", "Rate", Kind::choice, 0, numRates - 1, defaultRate, "" },
    { "timeMode", "Time", Kind::choice, 0, 1, 0, "", timeModes, 2 },
    { "delay", "Delay", Kind::continuous, -100, 100, 0, "" },
    { "attack", "Attack", Kind::continuous, 0, 200, 2, "" },
    { "hold", "Hold", Kind::continuous, 0, 200, 8, "" },
    { "release", "Release", Kind::continuous, 0, 200, 35, "" },
    { "depth", "Depth", Kind::continuous, 0, 100, 100, "" },
    { "curve", "Curve", Kind::choice, 0, 2, 1, "", curves, 3 },
    { "channel", "Channel", Kind::choice, 0, 16, 1, "", channels, 17 },
    { "note", "Trigger", Kind::choice, 0, 127, 36, "" },
    { "midiMode", "Mode", Kind::choice, 0, 1, 0, "", midiModes, 2 },
    { "velSens", "Vel", Kind::continuous, 0, 100, 0, "" },
    { "threshold", "Threshold", Kind::continuous, -60, 0, -24, "" },
    { "lockout", "Lockout", Kind::continuous, 0, 200, 20, "" },
};

bool isPercent (int index)
{
    return index == kDelay || index == kAttack || index == kHold || index == kRelease || index == kDepth
        || index == kVelSens;
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
    static const nist::Layout l { table, kNumParams, { kNumParams }, 0, 0 };
    return l;
}

double toEngine (int index, double plain)
{
    return index == kDepth || index == kVelSens ? plain / 100.0 : plain;
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
    if (! startsWithNumber (text))
        return false;
    plain = ni::wire::parse_number (text);
    return true;
}

} // namespace ni::sc
