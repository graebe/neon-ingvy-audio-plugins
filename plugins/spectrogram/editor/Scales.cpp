// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#include "Scales.h"

#include <cmath>
#include <limits>

namespace ni::spectrogram
{

namespace
{
/* The JavaScript Math.round the web editor used: halves go up. */
double roundHalfUp (double v)
{
    return std::floor (v + 0.5);
}

/* "−": the minus sign, not a hyphen, wherever a reading is negative. */
const juce::String minus = juce::String::fromUTF8 ("\xe2\x88\x92");
} // namespace

int rangeIndex (float lo, float hi)
{
    for (int i = 0; i < numRanges; ++i)
        if (std::abs (ranges[i].lo - lo) < 0.5f && std::abs (ranges[i].hi - hi) < 0.5f)
            return i;
    return 0;
}

/*
 * Eight over 256 pixels is a mark every 32 or so: enough to read a height off,
 * few enough that the gutter stays a scale rather than a ruler.
 */
std::vector<FreqMark> freqMarks (const std::vector<float>& hz, float height)
{
    static constexpr struct { double f; const char* label; } marks[] {
        { 10, "10" }, { 20, "20" }, { 50, "50" }, { 100, "100" }, { 500, "500" },
        { 1000, "1k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" },
    };

    std::vector<FreqMark> out;
    if (hz.size() < 2)
        return out;

    double lo = std::log ((double) hz.front());
    double hi = std::log ((double) hz.back());
    /* With two centres the per-band ratio is unknown -- the "step" would be
     * the whole span -- so only three or more widen the axis. */
    if (hz.size() >= 3)
    {
        const double half = (hi - lo) / (double) (hz.size() - 1) / 2.0;
        lo -= half;
        hi += half;
    }

    for (const auto& m : marks)
    {
        const double t = (std::log (m.f) - lo) / (hi - lo);
        if (t < -1e-6 || t > 1.0 + 1e-6)
            continue;
        /* Band 0 is at the bottom: t runs up, y runs down. */
        out.push_back ({ m.label, (float) ((1.0 - juce::jlimit (0.0, 1.0, t)) * height) });
    }
    return out;
}

std::vector<TimeMark> secondMarks (int columns, double colsPerSecond, float width, double stepS)
{
    std::vector<TimeMark> out;
    if (columns <= 0 || colsPerSecond <= 0.0 || width <= 0.0f || stepS <= 0.0)
        return out;

    const double span = columns / colsPerSecond;
    for (int k = 0;; ++k)
    {
        const double a = k * stepS;
        if (a > span + 1e-9)
            break;
        const double x = width - (a / span) * width;
        if (x < -1e-9)
            break;
        TimeMark m;
        const bool whole = std::abs (a - std::round (a)) < 1e-9;
        m.label = k == 0 ? juce::String ("0s")
                         : "-" + (whole ? juce::String ((int) std::round (a)) : juce::String (a, 1)) + "s";
        m.x = (float) x;
        m.anchor = x > width - 12.0 ? TimeMark::Anchor::end
                 : x < 12.0         ? TimeMark::Anchor::start
                                    : TimeMark::Anchor::mid;
        out.push_back (m);
    }
    return out;
}

/*
 * At 16 bars a bar is 38 px and its beats would be 9 apart: a grey haze
 * rather than a grid, and the bar lines would stop reading as the strong ones.
 */
std::vector<TimeMark> barMarks (int bars, int numerator, int denominator, float width)
{
    static constexpr double minBeatPx = 14.0;

    std::vector<TimeMark> out;
    if (bars <= 0 || width <= 0.0f)
        return out;

    const double bpb = beatsPerBar (numerator, denominator);
    const double perBar = width / (double) bars;
    const double perBeat = perBar / bpb;
    const bool beats = perBeat >= minBeatPx;

    for (int b = 0; b < bars; ++b)
    {
        out.push_back ({ juce::String (b + 1), (float) (b * perBar), TimeMark::Anchor::mid, false });
        if (! beats)
            continue;
        /* From 1: beat 0 is the bar line, and both would stack two ticks. */
        for (int k = 1; k < (int) roundHalfUp (bpb); ++k)
            out.push_back ({ {}, (float) (b * perBar + k * perBeat), TimeMark::Anchor::mid, true });
    }
    return out;
}

double beatsPerBar (int numerator, int denominator)
{
    if (numerator <= 0 || denominator <= 0)
        return 4.0;
    return numerator * 4.0 / denominator;
}

/*
 * THE MODULO HAS TO SURVIVE A NEGATIVE ppq. Hosts count bars from 1 but PPQ
 * from 0, and a count-in reports a negative position; fmod keeps the sign of
 * its left operand, so the window is added back before the second fmod.
 */
int slotForPpq (double ppq, int bars, int numerator, int denominator, int columns)
{
    if (! std::isfinite (ppq) || bars <= 0 || columns <= 0)
        return 0;
    const double window = bars * beatsPerBar (numerator, denominator);
    if (! (window > 0.0))
        return 0;
    const double phase = std::fmod (std::fmod (ppq, window) + window, window) / window;
    /* Clamped as well as wrapped: phase can round up to 1 at the very top. */
    return juce::jlimit (0, columns - 1, (int) std::floor (phase * columns));
}

BarPosition posForSlot (int slot, int bars, int numerator, int denominator, int columns)
{
    if (columns <= 0 || bars <= 0)
        return {};
    const double bpb = beatsPerBar (numerator, denominator);
    const double beats = (juce::jlimit (0, columns - 1, slot) / (double) columns) * bars * bpb;
    const int bar = (int) std::floor (beats / bpb);
    return { bar + 1, beats - bar * bpb + 1.0 };
}

/* The inverse of the engine's amplitude_to_byte, and it has to stay that. */
double dbForLevel (int level)
{
    if (level <= 0)
        return -std::numeric_limits<double>::infinity();
    return dbFloor + (juce::jmin (255, level) / 255.0) * (dbCeil - dbFloor);
}

juce::String asHz (double hz)
{
    if (hz < 1000.0)
        return juce::String ((int) roundHalfUp (hz)) + " Hz";
    return juce::String (hz / 1000.0, 1) + " kHz";
}

juce::String readFrequency (const std::vector<float>& hz, int band)
{
    if (band < 0 || band >= (int) hz.size())
        return noReading();
    return asHz (hz[(size_t) band]);
}

juce::String readAge (int ageColumns)
{
    const double t = ageColumns >= 0 ? ageColumns / columnsPerSecond : 0.0;
    if (t < 0.005)
        return "now";
    return minus + juce::String (t, 2) + " s";
}

/* "8:4.7": a colon says bar and beat are different kinds of number. */
juce::String readPosition (int slot, int bars, int numerator, int denominator)
{
    const auto p = posForSlot (slot, bars, numerator, denominator, pictureWidth);
    return juce::String (p.bar) + ":" + juce::String (p.beat, 1);
}

/* Byte 0 is "at or below the floor", not "exactly the floor". */
juce::String readLevel (int level)
{
    const double db = dbForLevel (level);
    if (std::isinf (db))
        return "< " + minus + juce::String ((int) std::abs (dbFloor)) + " dB";
    return minus + juce::String (std::abs (db), 1) + " dB";
}

juce::String noReading()
{
    return juce::String::fromUTF8 ("\xe2\x80\x94");
}

} // namespace ni::spectrogram
