// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's plugin, faked for its editor's tests: the ten host
 * parameters exactly as the engine's table declares them, and a model whose
 * pictures and captured voice the test sets.
 *
 * THE PARAMETERS are the engine's table (vz_param_*, vz-core's params.rs),
 * transcribed in its order with its ranges, defaults and words: "80 %",
 * "+30 %", "-3.0 dB", "E>I 30 %", Mode a choice of Time and Spectral and
 * Ceiling one of Off and On. A
 * fake in another order would test another plugin.
 *
 * THE FORMANTS stand in for the engine's (vz_formants, model.rs): the same
 * five vowels' measurements, the same walk between two of them on the Bark
 * scale, the same Size scaling every formant by 1.25^(-Size/100). THE
 * PICTURE stands in for the engine's envelopes: a tilted input and the vowel
 * put on it, as far as Vocal says. THE VOICE stands in for a capture: the
 * test says how it went (land()), and a captured voice is the textbook's
 * scaled, as far as Voice says. All are the test's stand-ins, not the
 * editor's: the editor draws whatever the model hands it, and asks it only to
 * capture a file or forget.
 */
#pragma once

#include "Model.h"

#include "fakes.h"

#include <array>
#include <cmath>
#include <vector>

namespace ni::vz::test
{

/* The vowels' formants in Hz, A E I O U (model.rs, VOWELS). */
inline constexpr double vowels[5][5] = {
    { 768.0, 1333.0, 2522.0, 3500.0, 4500.0 },
    { 476.0, 2089.0, 2691.0, 3500.0, 4500.0 },
    { 342.0, 2322.0, 3000.0, 3500.0, 4500.0 },
    { 497.0, 910.0, 2459.0, 3500.0, 4500.0 },
    { 378.0, 997.0, 2343.0, 3500.0, 4500.0 },
};

/* The engine's text for a vowel (Param::write_value): a letter on a vowel,
 * "E>I 30 %" between two. */
inline juce::String vowelText (float v)
{
    const char* letters[] { "A", "E", "I", "O", "U" };
    const double value = juce::jlimit (0.0, 4.0, (double) v);
    const double pct = std::round ((value - std::floor (value)) * 100.0);
    const int at = (int) std::floor (value) + (pct >= 100.0 ? 1 : 0);
    if (pct <= 0.0 || pct >= 100.0)
        return letters[at];
    return juce::String (letters[at]) + ">" + letters[at + 1] + " " + juce::String ((int) pct) + " %";
}

/* The ten, as the engine's table declares them. */
inline void addParameters (ni::ui::test::FakeParameters& p)
{
    const auto number = [] (const juce::String& s) { return s.getFloatValue(); };
    const auto percent = [] (bool bipolar)
    {
        return [bipolar] (float v)
        {
            const float r = std::round (v);
            return (bipolar && r > 0.0f ? "+" : "") + juce::String ((int) r) + " %";
        };
    };
    const auto db = [] (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return (r > 0.0f ? "+" : "") + juce::String (r + 0.0f, 1) + " dB";
    };
    /* A vowel's letter goes to it; a number to that place on the path. */
    const auto vowelParse = [] (const juce::String& s)
    {
        const auto at = juce::String ("AEIOU").indexOfIgnoreCase (s.trim().substring (0, 1));
        return at >= 0 ? (float) at : s.getFloatValue();
    };
    const auto linear = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi); };

    p.addChoice ("mode", "Mode", { "Time", "Spectral" }, 0);
    p.addFloat ("vocal", "Vocal", linear (0.0f, 100.0f), 80.0f, percent (false), number);
    p.addFloat ("vowel", "Vowel", linear (0.0f, 4.0f), 0.0f, vowelText, vowelParse);
    p.addFloat ("size", "Size", linear (-100.0f, 100.0f), 0.0f, percent (true), number);
    p.addFloat ("resonance", "Resonance", linear (0.0f, 100.0f), 50.0f, percent (false), number);
    p.addFloat ("excite", "Excite", linear (0.0f, 100.0f), 0.0f, percent (false), number);
    p.addFloat ("mix", "Mix", linear (0.0f, 100.0f), 100.0f, percent (false), number);
    p.addFloat ("output", "Output", linear (-24.0f, 24.0f), 0.0f, db, number);
    p.addFloat ("voice", "Voice", linear (0.0f, 100.0f), 100.0f, percent (false), number);
    p.addChoice ("ceiling", "Ceiling", { "Off", "On" }, 1);
}

/* -------------------------------------------- the formants, for tests -- */

/* Hz to Bark and back (Traunmüller 1990), as model.rs. */
inline double bark (double hz) { return 26.81 * hz / (1960.0 + hz) - 0.53; }
inline double hzOf (double b) { return 1960.0 * (b + 0.53) / (26.28 - b); }

/* model.rs's Formants::at, the frequencies only: Resonance moves none. */
inline Formants formantsAt (double vowel, double size)
{
    vowel = juce::jlimit (0.0, 4.0, vowel);
    const int from = juce::jmin (3, (int) std::floor (vowel));
    const double t = vowel - from;
    const double scale = std::pow (1.25, -juce::jlimit (-100.0, 100.0, size) / 100.0);
    Formants f {};
    for (size_t k = 0; k < f.size(); ++k)
    {
        const double a = vowels[from][k], b = vowels[from + 1][k];
        const double on = t == 0.0 ? a : t == 1.0 ? b : hzOf (bark (a) + (bark (b) - bark (a)) * t);
        f[k] = on * scale;
    }
    return f;
}

/* ------------------------------------------------------------- model -- */

class FakeModel final : public Model
{
public:
    FakeModel() { addParameters (common.params); }

    /* The shared half: the parameters. */
    ni::ui::test::FakeEditorModel common;
    ni::ui::test::FakeParameters& params = common.params;
    Picture shown;

    int numParameters() const override { return common.numParameters(); }
    juce::RangedAudioParameter& parameter (int i) override { return common.parameter (i); }

    bool bipolar (int i) const override { return i == param::size || i == param::output; }
    const Picture& picture() override { return shown; }

    /* A captured voice: the textbook's times `capturedScale`, as far as
     * Voice says. */
    Formants formants (double vowel) override
    {
        auto f = formantsAt (vowel, plain (param::size));
        if (held.sung)
            for (auto& x : f)
                x *= std::pow (capturedScale, plain (param::voice) / 100.0);
        return f;
    }

    /* ---- the voice: what the editor asked, and what the test says came of
     * it */
    Voice held;
    double capturedScale = 1.0;
    std::vector<juce::File> captures;
    int forgotten = 0, restored = 0;
    /* The voice Forget let go, for one Restore. */
    Voice kept;
    double keptScale = 1.0;

    Voice voice() const override { return held; }

    void capture (const juce::File& recording) override
    {
        captures.push_back (recording);
        if (held.status == Voice::Status::capturing)
            return;
        held.status = Voice::Status::capturing;
        held.name = recording.getFileName();
        ++held.serial;
    }

    /* As the processor does: Forget keeps what was sung for one Restore,
     * which a voice sung since takes away. */
    void forgetVoice() override
    {
        ++forgotten;
        const bool had = held.sung;
        if (had)
        {
            kept = held;
            keptScale = capturedScale;
        }
        land (Voice::Status::none, {}, {});
        held.restorable = had;
    }

    void restoreVoice() override
    {
        ++restored;
        if (held.sung || ! held.restorable)
            return;
        land (Voice::Status::captured, kept.sungName, kept.found, keptScale, kept.confidence);
    }

    juce::String recordingWildcard() const override { return "*.wav;*.aif;*.aiff;*.m4a"; }

    /* How a capture went: its status, the recording's name, the vowels it
     * measured and how sure it was of each (all of them, unless said); a
     * captured voice `scale` times the textbook's. As the processor does, a
     * capture that failed keeps the voice sung before it, forgetting (none)
     * lets it go, and a voice sung leaves nothing to restore. */
    void land (Voice::Status status, const juce::String& name, std::array<bool, 5> found, double scale = 1.15,
               std::array<double, 5> confidence = { 1.0, 1.0, 1.0, 1.0, 1.0 })
    {
        held.status = status;
        held.name = name;
        if (status == Voice::Status::captured || status == Voice::Status::none)
        {
            held.sung = status == Voice::Status::captured;
            held.sungName = held.sung ? name : juce::String();
            held.found = held.sung ? found : std::array<bool, 5> {};
            for (size_t k = 0; k < 5; ++k)
                held.confidence[k] = held.found[k] ? confidence[k] : 0.0;
            capturedScale = held.sung ? scale : 1.0;
            held.restorable = false;
        }
        ++held.serial;
    }

    double plain (int i) { return (double) params[i].convertFrom0to1 (params[i].getValue()); }

    void set (int i, double value)
    {
        auto& p = params[i];
        p.setValueNotifyingHost (p.convertTo0to1 ((float) value));
    }

    /*
     * A picture as the engine would publish it, from the parameters now: the
     * input a slope of -4.5 dB an octave with a bump of its own at 300 Hz;
     * the output that slope traded for the vowel's resonances, as far as
     * Vocal goes. Each curve with its peak at 0 dB; 96 frequencies equally
     * spaced in pitch from 50 Hz to 10 kHz.
     */
    void play (bool spectral, int latency, double sampleRate)
    {
        constexpr int points = 96;
        shown.hz.resize (points);
        shown.input.resize (points);
        shown.output.resize (points);
        const auto f = formants (plain (param::vowel));
        const double v = plain (param::vocal) / 100.0;
        const double sharp = 0.08 + 0.25 * (1.0 - plain (param::resonance) / 100.0);
        for (int i = 0; i < points; ++i)
        {
            const double hz = 50.0 * std::pow (200.0, i / (points - 1.0));
            const double octaves = std::log2 (hz / 50.0);
            const double in = -4.5 * octaves + 6.0 * std::exp (-std::pow (std::log2 (hz / 300.0) / 0.6, 2.0));
            double vowel = -3.0 * octaves;
            for (size_t k = 0; k < 3; ++k)
                vowel += (18.0 - 4.0 * (double) k) * std::exp (-std::pow (std::log2 (hz / f[k]) / sharp, 2.0));
            shown.hz[(size_t) i] = hz;
            shown.input[(size_t) i] = in;
            shown.output[(size_t) i] = in + v * (vowel - in);
        }
        for (auto* curve : { &shown.input, &shown.output })
        {
            double peak = -1.0e9;
            for (const auto x : *curve)
                peak = juce::jmax (peak, x);
            for (auto& x : *curve)
                x -= peak;
        }
        shown.spectral = spectral;
        shown.latency = latency;
        shown.sampleRate = sampleRate;
        ++shown.serial;
    }
};

} // namespace ni::vz::test
