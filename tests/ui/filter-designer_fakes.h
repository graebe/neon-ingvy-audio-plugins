// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's model with nothing behind it, for the editor's tests:
 * the 174 parameters as the engine's table declares them (fd-core's
 * params.rs), and a picture made up of the parameters -- each handle where
 * the engine's design::handle puts it, each Bell's edges where design::edges
 * puts them, every curve flat -- so what the window does with a picture can
 * be tested apart from what the engine draws. The real model's pictures are
 * tests/filter-designer/fd_processor.cpp's.
 */
#pragma once

#include "Model.h"

#include "fakes.h"

#include <cmath>
#include <limits>

namespace ni::fd::test
{

inline juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::pow (b / a, n); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float, float, float v) { return v; } };
}

/* The table, in its order, with the engine's labels and ranges. */
inline void addParameters (ni::ui::test::FakeParameters& p)
{
    const auto number = [] (const juce::String& s) { return s.getFloatValue(); };
    const auto db = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; };
    const auto hz = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String ((int) v) + " Hz"; };
    const auto plainText = [] (float v) { return juce::String (v, 2); };
    const auto linear = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi); };
    const juce::StringArray offOn { "Off", "On" };

    p.addFloat ("output", "Output", linear (-24.0f, 24.0f), 0.0f, db, number);
    p.addFloat ("scale", "Scale", linear (-200.0f, 200.0f), 100.0f, plainText, number);
    p.addChoice ("adaptive_q", "Adaptive Q", offOn, 0);
    p.addChoice ("analyzer", "Analyzer", { "Off", "Pre", "Post", "Both" }, 2);
    p.addChoice ("iso_on", "Isolator", offOn, 0);
    p.addFloat ("iso_low", "Isolator Low", linear (-60.0f, 6.0f), 0.0f, db, number);
    p.addFloat ("iso_mid", "Isolator Mid", linear (-60.0f, 6.0f), 0.0f, db, number);
    p.addFloat ("iso_high", "Isolator High", linear (-60.0f, 6.0f), 0.0f, db, number);
    p.addChoice ("iso_kill_low", "Kill Low", offOn, 0);
    p.addChoice ("iso_kill_mid", "Kill Mid", offOn, 0);
    p.addChoice ("iso_kill_high", "Kill High", offOn, 0);
    p.addFloat ("iso_freq_lo", "Isolator Lower", logRange (30.0f, 2000.0f), 250.0f, hz, number);
    p.addFloat ("iso_freq_hi", "Isolator Upper", logRange (500.0f, 16000.0f), 2500.0f, hz, number);
    p.addChoice ("iso_slope", "Isolator Slope", { "24 dB", "48 dB" }, 0);
    const float freqs[] = { 30, 45, 70, 100, 150, 220, 330, 500, 750, 1100, 1600, 2400, 3600, 5400, 8000, 12000 };
    for (int n = 0; n < param::nodes; ++n)
    {
        const auto key = "n" + juce::String (n + 1).paddedLeft ('0', 2) + "_";
        const auto name = "Filter " + juce::String (n + 1) + " ";
        p.addChoice (key + "type", name + "Type",
                     { "Empty", "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Band Pass", "Notch", "All Pass", "Tilt" }, 0);
        p.addChoice (key + "on", name + "On", offOn, 1);
        p.addChoice (key + "family", name + "Family",
                     { "Butterworth", "Bessel", "Chebyshev I", "Chebyshev II", "Elliptic", "Linkwitz-Riley" }, 0);
        p.addChoice (key + "order", name + "Order", { "6 dB", "12 dB", "18 dB", "24 dB", "30 dB", "36 dB", "42 dB", "48 dB" }, 1);
        p.addFloat (key + "freq", name + "Freq", logRange (10.0f, 22000.0f), freqs[n], hz, number);
        p.addFloat (key + "gain", name + "Gain", linear (-30.0f, 30.0f), 0.0f, db, number);
        p.addFloat (key + "q", name + "Q", logRange (0.1f, 40.0f), 0.7071f, plainText, number);
        p.addFloat (key + "ripple", name + "Ripple", linear (0.01f, 3.0f), 1.0f, db, number);
        p.addFloat (key + "stopband", name + "Stopband", linear (20.0f, 120.0f), 60.0f, db, number);
        p.addChoice (key + "channel", name + "Channel", { "Stereo", "Left", "Right", "Mid", "Side" }, 0);
    }
}

class FakeModel final : public Model
{
public:
    FakeModel()
    {
        addParameters (common.params);
        for (int i = 0; i < Picture::points; ++i)
            shown.hz[(size_t) i] = (float) (10.0 * std::pow (2200.0, i / (double) (Picture::points - 1)));
        shown.sampleRate = 48000.0;
    }

    ni::ui::test::FakeEditorModel common;
    ni::ui::test::FakeParameters& params = common.params;
    Picture shown;
    int lanesAsked = -1;

    int numParameters() const override { return common.numParameters(); }
    juce::RangedAudioParameter& parameter (int i) override { return common.parameter (i); }
    int takeRings (float* s, int n) override { return common.takeRings (s, n); }
    bool motion() const override { return common.motion(); }
    void setMotion (bool on) override { common.setMotion (on); }

    bool bipolar (int i) const override
    {
        return i == param::output || i == param::scale
               || (i >= param::nodeBase && (i - param::nodeBase) % param::fields == param::gain);
    }

    double plain (int i) const
    {
        auto& p = const_cast<FakeModel*> (this)->parameter (i);
        return (double) p.convertFrom0to1 (p.getValue());
    }

    /* Where design::handle and design::edges put them. */
    const Picture& picture (int lane) override
    {
        lanesAsked = lane;
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        for (int s = 0; s < param::nodes; ++s)
        {
            const int t = (int) plain (param::node (s, param::type));
            const double f = plain (param::node (s, param::freq));
            const double g = plain (param::node (s, param::gain)) * plain (param::scale) / 100.0;
            shown.handleHz[(size_t) s] = t == type::empty ? nan : f;
            shown.handleDb[(size_t) s] = t == type::empty                                         ? nan
                                         : (t == type::bell || t == type::lowShelf || t == type::highShelf) ? g
                                         : (t == type::lowCut || t == type::highCut)              ? -3.0103
                                                                                                  : 0.0;
            if (t == type::bell || t == type::bandPass || t == type::notch)
            {
                const double half = std::asinh (1.0 / (2.0 * plain (param::node (s, param::q)))) / std::log (2.0);
                shown.edgeHz[(size_t) s] = { f * std::pow (2.0, -half), f * std::pow (2.0, half) };
            }
            else
                shown.edgeHz[(size_t) s] = { nan, nan };
        }
        ++shown.serial;
        return shown;
    }

    unsigned typeUses (int t, int order) const override
    {
        unsigned u = 0;
        if (t >= type::lowCut && t <= type::notch)
            u |= uses::family;
        if (t == type::lowShelf || t == type::highShelf || (t >= type::lowCut && t <= type::allPass))
            u |= uses::order;
        if (t == type::bell || t == type::lowShelf || t == type::highShelf || t == type::tilt)
            u |= uses::gain;
        if (t == type::bell || t == type::bandPass || t == type::notch
            || ((t == type::lowCut || t == type::highCut || t == type::lowShelf || t == type::highShelf || t == type::allPass)
                && order >= 1))
            u |= uses::q;
        return u;
    }
    int typeMaxOrder (int t) const override
    {
        return t == type::lowShelf || t == type::highShelf ? 3 : (t >= type::lowCut && t <= type::allPass) ? 7 : -1;
    }
    bool familyUsesRipple (int family) const override { return family == 2 || family == 4; }
    bool familyUsesStopband (int family) const override { return family == 3 || family == 4; }
    double qForEdge (double freq, double edgeHz) const override
    {
        const double octaves = std::max (0.05, 2.0 * std::abs (std::log2 (edgeHz / freq)));
        return juce::jlimit (0.1, 40.0, 1.0 / (2.0 * std::sinh (octaves * std::log (2.0) / 2.0)));
    }
    int typeForNew (double hz) const override { return hz < 30.0 ? type::lowCut : hz > 15000.0 ? type::highCut : type::bell; }
};

} // namespace ni::fd::test
