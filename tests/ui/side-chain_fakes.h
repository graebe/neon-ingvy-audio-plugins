// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Side-Chain's plugin, faked for its editor's tests: the fifteen host
 * parameters exactly as the iPlug2 build declares them, and a model whose
 * snapshots the test sets.
 *
 * THE PARAMETERS are Params.cpp's, in its order -- the VST3 IDs the spike's
 * tests/fixtures/iplug2/NISideChain/parameters.json lists (Source 0 ... Lockout
 * 14): the same names, ranges, steps, defaults and display text ("35.0 %",
 * "-inf dB", "C1", "Omni"). A fake in another order would test another plugin.
 *
 * THE SHAPE stands in for the engine's (Model::shapeGain, shapeMarks) with the
 * web editor's port of shape.rs (ui/src/lib/shape.js and the kit's curve.js),
 * which is pinned to the engine's own table -- so the pictures the goldens
 * hold are the duck the plugin draws. It is the test's stand-in, not the
 * editor's: the editor draws whatever the model hands it.
 */
#pragma once

#include "Model.h"

#include "fakes.h"

#include <algorithm>
#include <cmath>

namespace ni::sc::test
{

/* The rate labels: the engine's table (sc_core_rate_label). */
inline juce::StringArray rateLabels()
{
    return { "1/1", "1/1T", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" };
}

/* Live's octave numbering, where 36 is C1: Params.cpp's NoteName. */
inline juce::StringArray noteNames()
{
    const char* pitch[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::StringArray names;
    for (int n = 0; n < 128; ++n)
        names.add (juce::String (pitch[n % 12]) + juce::String (n / 12 - 2));
    return names;
}

/* The fifteen, as Params.cpp declares them. */
inline void addParameters (ni::ui::test::FakeParameters& p)
{
    /* "%.1f %%", which prints no "-0.0" for a value a float's snapping left a
     * hair below zero (the plugin's values are doubles, and exact). */
    const auto percent = [] (float v) { return juce::String (std::round (v * 10.0f) / 10.0f + 0.0f, 1) + " %"; };
    const auto number = [] (const juce::String& s) { return s.getFloatValue(); };
    const auto pct = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        p.addFloat (id, name, { lo, hi, 0.01f }, def, percent, number);
    };

    p.addChoice ("source", "Source", { "Cycle", "MIDI", "Sidechain" }, 0);
    p.addChoice ("rate", "Rate", rateLabels(), 4);
    p.addChoice ("timeMode", "Time", { "ms", "% of cycle" }, 0);
    pct ("delay", "Delay", -100.0f, 100.0f, 0.0f);
    pct ("attack", "Attack", 0.0f, 200.0f, 2.0f);
    pct ("hold", "Hold", 0.0f, 200.0f, 8.0f);
    pct ("release", "Release", 0.0f, 200.0f, 35.0f);
    pct ("depth", "Depth", 0.0f, 100.0f, 100.0f);
    p.addChoice ("curve", "Curve", { "Linear", "Exponential", "S-Curve" }, 1);

    juce::StringArray channels { "Omni" };
    for (int i = 1; i <= 16; ++i)
        channels.add (juce::String (i));
    p.addChoice ("channel", "Channel", channels, 1);
    p.addChoice ("note", "Trigger", noteNames(), 36);
    p.addChoice ("midiMode", "Mode", { "Trigger", "Gate" }, 0);
    pct ("velSens", "Vel", 0.0f, 100.0f, 0.0f);

    /* The bottom reads "-inf dB", which reads back as the bottom. */
    p.addFloat ("threshold", "Threshold", { -60.0f, 0.0f, 0.1f }, -24.0f,
                [] (float v) { return v <= -60.0f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; },
                [] (const juce::String& s) { return s.containsIgnoreCase ("inf") ? -60.0f : s.getFloatValue(); });
    p.addFloat ("lockout", "Lockout", { 0.0f, 200.0f, 1.0f }, 20.0f,
                [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }, number);
}

/* ------------------------------------------------- the shape, for tests -- */

/* The kit's curve.js, which is the engine's shape(). */
inline double curveShape (int curve, double t)
{
    if (! (t > 0.0))
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    const auto exp = [] (double x) { return (1.0 - std::exp (-3.0 * x)) / 0.95021293163213605; };
    switch (curve)
    {
        case 1: return exp (t);
        case 2: return t < 0.5 ? 0.5 * (1.0 - exp (1.0 - 2.0 * t)) : 0.5 + 0.5 * exp (2.0 * t - 1.0);
        default: return t;
    }
}

struct Shape
{
    int curve = 1;
    double delay = 0.0, attack = 2.0, hold = 8.0, release = 35.0, depth = 1.0;
    bool cycle = true;

    static double wrap (double v) { return std::fmod (std::fmod (v, 100.0) + 100.0, 100.0); }
    double start() const { return cycle ? wrap (delay) : std::max (0.0, delay); }

    /* shape.js's duckAt: the attenuation 0..1 at `t` % of the cycle. */
    double duckAt (double t) const
    {
        const double a = std::max (0.0, attack), h = std::max (0.0, hold), r = std::max (0.0, release);
        const double e = cycle ? wrap (t - start()) : t - start();
        if (! (e >= 0.0))
            return 0.0;
        if (e < a)
            return curveShape (curve, a > 0.0 ? e / a : 1.0);
        if (e < a + h)
            return 1.0;
        if (e < a + h + r)
            return 1.0 - curveShape (curve, (e - a - h) / r);
        return 0.0;
    }

    /* shape.js's bounds. */
    ShapeMarks marks() const
    {
        const double a = std::max (0.0, attack), h = std::max (0.0, hold), r = std::max (0.0, release);
        const double s = start();
        const auto at = [this] (double v) { return cycle ? wrap (v) : v; };
        return { at (s), at (s + a), at (s + a + h), at (s + a + h + r), a + h + r, 1.0 - depth };
    }
};

/* ------------------------------------------------------------- model -- */

class FakeModel final : public Model
{
public:
    FakeModel() { addParameters (common.params); }

    /* The shared half: parameters, rings, Motion. */
    ni::ui::test::FakeEditorModel common;
    ni::ui::test::FakeParameters& params = common.params;

    /* The snapshots, as the test sets them. */
    State engine;
    Buses bus;
    Scope capture;
    int scopeReads = 0;

    int numParameters() const override { return common.numParameters(); }
    juce::RangedAudioParameter& parameter (int index) override { return common.parameter (index); }
    int takeRings (float* s, int capacity) override { return common.takeRings (s, capacity); }
    bool motion() const override { return common.motion(); }
    void setMotion (bool on) override { common.setMotion (on); }

    State state() override { return engine; }
    /* The engine's stage_ms: each stage's percentage of the cycle now. */
    StageMs stageMs() override
    {
        const double k = engine.msPerCycle / 100.0;
        return { plain (param::delay) * k, plain (param::attack) * k, plain (param::hold) * k,
                 plain (param::release) * k };
    }
    Buses buses() override { return bus; }
    const Scope& scope() override
    {
        ++scopeReads;
        return capture;
    }

    /* The shape from the host parameters now, as the processor will ask the
     * engine for it. */
    Shape shapeNow()
    {
        Shape s;
        s.curve = juce::roundToInt (params[param::curve].getValue() * 2.0f);
        s.delay = plain (param::delay);
        s.attack = plain (param::attack);
        s.hold = plain (param::hold);
        s.release = plain (param::release);
        s.depth = plain (param::depth) / 100.0;
        s.cycle = juce::roundToInt (params[param::source].getValue() * 2.0f) == 0;
        return s;
    }

    void shapeGain (float* gain, int count) override
    {
        const auto s = shapeNow();
        for (int i = 0; i < count; ++i)
        {
            const double t = count > 1 ? 100.0 * i / (count - 1) : 0.0;
            gain[i] = (float) (1.0 - s.depth * s.duckAt (t));
        }
    }

    ShapeMarks shapeMarks() override { return shapeNow().marks(); }

    /* A parameter's value in its own units -- to the 0.0001 the engine's
     * doubles would hold, not the hair a float's snapping to 0.01 leaves, which
     * would wrap a Delay of "0" to the end of the cycle. */
    double plain (int index) const
    {
        auto& p = params[index];
        return std::round ((double) p.convertFrom0to1 (p.getValue()) * 1.0e4) / 1.0e4;
    }

    /* ---- captures */

    /* `count` columns, the first `seenUpTo` of them reached, a sine-ish input
     * of `level` ducked by the shape now: what the plugin would capture. */
    void fillScope (int count, int seenUpTo, float level)
    {
        const auto s = shapeNow();
        capture.count = count;
        capture.data.assign ((size_t) (count * Scope::stride), 0.0f);
        for (int i = 0; i < count; ++i)
        {
            const double t = 100.0 * i / count;
            const float in = level * (float) (0.55 + 0.45 * std::abs (std::sin (i * 0.37) * std::cos (i * 0.11)));
            const float g = (float) (1.0 - s.depth * s.duckAt (t));
            auto* col = capture.data.data() + i * Scope::stride;
            col[Scope::seen] = i < seenUpTo ? 1.0f : 0.0f;
            col[Scope::dryLo] = -in;
            col[Scope::dryHi] = in;
            col[Scope::wetLo] = -in * g;
            col[Scope::wetHi] = in * g;
            col[Scope::gain] = g;
        }
    }
};

} // namespace ni::sc::test
