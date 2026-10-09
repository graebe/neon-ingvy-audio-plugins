// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's editor against a model with nothing behind it: the window
 * and its grid, what each control asks of the host, the staircase following
 * the parameters, the scope following the audio, and the window as a picture,
 * at its defaults and crushed hard.
 *
 * THE PARAMETERS are the engine's table (bc_param_*), transcribed in its
 * order with its ranges, travel and words; a fake in another order would test
 * another plugin. THE STAIRCASE is a stand-in for the engine's
 * (bc_transfer_render): a mid-tread grid of 2^(1-Bits) under Drive, which is
 * what the engine draws with Shape at 0 -- the test's, not the editor's, which
 * draws whatever the model hands it.
 */
#include "Editor.h"
#include "InfoLines.h"
#include "Pointer.h"
#include "checks.h"
#include "fakes.h"
#include "snapshot.h"

#include <doctest.h>

#include <algorithm>
#include <cmath>

using namespace ni::bc;
using ni::ui::gallery::Pointer;

namespace
{
struct FakeModel final : public Model
{
    FakeModel()
    {
        const auto number = [] (const juce::String& s) { return s.getFloatValue(); };
        const auto fixed = [] (int decimals, const char* unit, bool sign)
        {
            return [=] (float v)
            {
                const float r = std::round (v * std::pow (10.0f, (float) decimals)) / std::pow (10.0f, (float) decimals);
                return (sign && r > 0.0f ? "+" : "") + juce::String (r + 0.0f, decimals) + " " + unit;
            };
        };
        const auto linear = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi); };
        /* Rate's travel: equal ratios, as the engine's table has it. */
        juce::NormalisableRange<float> ratio (
            50.0f, 96000.0f, [] (float lo, float hi, float n) { return lo * std::pow (hi / lo, n); },
            [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); });
        const auto hz = [] (float v)
        {
            return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                               : juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz";
        };

        params.addFloat ("drive", "Drive", linear (-24.0f, 24.0f), 0.0f, fixed (1, "dB", true), number);
        params.addFloat ("bits", "Bits", linear (1.0f, 16.0f), 8.0f, fixed (1, "bits", false), number);
        params.addFloat ("shape", "Shape", linear (0.0f, 100.0f), 0.0f, fixed (0, "%", false), number);
        params.addBool ("mid_rise", "Mid-Rise", false);
        params.addFloat ("dither", "Dither", linear (0.0f, 100.0f), 0.0f, fixed (0, "%", false), number);
        params.addBool ("anti_alias", "Anti-Alias", false);
        params.addFloat ("rate", "Rate", ratio, 12000.0f, hz, number);
        params.addFloat ("jitter", "Jitter", linear (0.0f, 100.0f), 0.0f, fixed (0, "%", false), number);
        params.addBool ("pre_filter", "Pre-Filter", false);
        params.addBool ("post_filter", "Post-Filter", false);
        params.addFloat ("post_octave", "Octave", linear (-2.0f, 4.0f), 0.0f, fixed (1, "oct", true), number);
        params.addFloat ("mix", "Mix", linear (0.0f, 100.0f), 100.0f, fixed (0, "%", false), number);
        params.addFloat ("output", "Output", linear (-24.0f, 12.0f), 0.0f, fixed (1, "dB", true), number);
    }

    int numParameters() const override { return params.size(); }
    juce::RangedAudioParameter& parameter (int i) override { return params[i]; }
    int takeRings (float*, int) override { return 0; }
    bool motion() const override { return motionOn; }
    void setMotion (bool on) override { motionOn = on; }
    bool bipolar (int i) const override { return i == param::drive || i == param::postOctave || i == param::output; }

    float plain (int i) { return params[i].convertFrom0to1 (params[i].getValue()); }

    void steps (float* out, int count) override
    {
        const double delta = std::exp2 (1.0 - (double) plain (param::bits));
        const double gain = std::pow (10.0, (double) plain (param::drive) / 20.0);
        for (int i = 0; i < count; ++i)
        {
            const double x = -1.0 + 2.0 * i / (count - 1) * 1.0;
            out[i] = (float) juce::jlimit (-1.0, 1.0, delta * std::round (x * gain / delta));
        }
    }

    const Scope& scope() override { return audio; }

    /* A tone of `hz` at `level` through `bits` and a hold of `hold` samples,
     * as the engine would publish it: dry and crushed, 512 samples at 48 kHz,
     * starting on an upward crossing. */
    void play (double hz, float level, double bits, int hold)
    {
        audio.dry.resize (512);
        audio.wet.resize (512);
        audio.sampleRate = 48000.0;
        audio.triggered = true;
        ++audio.serial;
        const double delta = std::exp2 (1.0 - bits);
        for (int i = 0; i < 512; ++i)
        {
            const auto at = [&] (int n) { return level * std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / 48000.0); };
            audio.dry[(size_t) i] = (float) at (i);
            audio.wet[(size_t) i] = (float) (delta * std::round (at (i - i % hold) / delta));
        }
    }

    void set (int i, float value)
    {
        auto& p = params[i];
        p.setValueNotifyingHost (p.convertTo0to1 (value));
    }

    ni::ui::test::FakeParameters params;
    Scope audio;
    bool motionOn = true;
};

struct Rig
{
    double ms = 0.0;
    FakeModel model;
    Editor editor { model, [this] { return ms; } };

    Rig()
    {
        editor.frame().ground().setReducedMotionQuery ([] { return false; });
        model.params.clear();
    }

    void frames (int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ms += 1000.0 / 60.0;
            editor.tick (ms);
        }
    }
};
} // namespace

TEST_CASE ("bit-crusher: the window is its design's size, on its grid, and every line fits the bar")
{
    Rig rig;
    auto& e = rig.editor;
    CHECK (e.getWidth() == Editor::designWidth);
    CHECK (e.getHeight() == Editor::designHeight);
    NI_CHECK_INFO_LIMIT (e);

    /* The wells over the panels, on the same three columns. */
    CHECK (e.stepsPlot().getBounds() == juce::Rectangle<int> (0, 0, Editor::column, Editor::plotH));
    CHECK (e.signalPlot().getX() == e.panel (1).getX());
    CHECK (e.signalPlot().getRight() == e.panel (2).getRight());
    CHECK (e.panel (0).getTitleText() == "Steps");
    CHECK (e.panel (1).getTitleText() == "Hold");
    CHECK (e.panel (2).getTitleText() == "Level");
    for (int p = 0; p < 3; ++p)
        CHECK (e.panel (p).getBounds() == juce::Rectangle<int> (p * (Editor::column + Editor::gutter), Editor::panelsY,
                                                                 Editor::column, Editor::panelH));

    /* Every control inside its panel, none overlapping another. */
    std::vector<juce::Component*> controls;
    for (int i = 0; i < param::count; ++i)
    {
        const bool isSwitch = i == param::midRise || i == param::antiAlias || i == param::preFilter || i == param::postFilter;
        juce::Component& c = isSwitch ? static_cast<juce::Component&> (e.toggle (i)) : e.knob (i);
        /* Inside the panel's padding, not only its hairline. */
        const auto* panel = dynamic_cast<ni::ui::Panel*> (c.getParentComponent());
        REQUIRE (panel != nullptr);
        CHECK_MESSAGE (panel->contentBounds().contains (c.getBounds()), "parameter " << i);
        controls.push_back (&c);
    }
    for (size_t a = 0; a < controls.size(); ++a)
        for (size_t b = a + 1; b < controls.size(); ++b)
            if (controls[a]->getParentComponent() == controls[b]->getParentComponent())
                CHECK_FALSE (controls[a]->getBounds().intersects (controls[b]->getBounds()));

    CHECK (e.knob (param::drive).isBipolar());
    CHECK (e.knob (param::output).isBipolar());
    CHECK_FALSE (e.knob (param::bits).isBipolar());
    CHECK (e.knob (param::rate).getValueText() == "12.0 kHz");
}

TEST_CASE ("bit-crusher: a switch asks the host, and shows what it holds")
{
    Rig rig;
    auto& t = rig.editor.toggle (param::antiAlias);
    CHECK_FALSE (t.isOn());
    Pointer p;
    p.click (t, t.housing().getCentre());
    CHECK (rig.model.plain (param::antiAlias) == 1.0f);
    CHECK (t.isOn());

    /* And from the host's side. */
    rig.model.set (param::midRise, 1.0f);
    CHECK (rig.editor.toggle (param::midRise).isOn());
}

TEST_CASE ("bit-crusher: a knob shows the host's value in the engine's words")
{
    Rig rig;
    rig.model.set (param::bits, 4.0f);
    CHECK (rig.editor.knob (param::bits).getValueText() == "4.0 bits");
    rig.model.set (param::rate, 4410.0f);
    CHECK (rig.editor.knob (param::rate).getValueText() == "4.41 kHz");
}

TEST_CASE ("bit-crusher: the staircase follows the parameters at once")
{
    Rig rig;
    auto& steps = rig.editor.stepsPlot();
    CHECK (steps.getCaption() == "STEPS   8.0 BITS");
    const auto before = steps.outputs();

    rig.model.set (param::bits, 2.0f);
    rig.frames (1);
    CHECK (steps.getCaption() == "STEPS   2.0 BITS");
    const auto& after = steps.outputs();
    CHECK (after != before);
    std::vector<float> levels (after.begin(), after.end());
    std::sort (levels.begin(), levels.end());
    levels.erase (std::unique (levels.begin(), levels.end()), levels.end());
    CHECK (levels == std::vector<float> { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f });
}

TEST_CASE ("bit-crusher: the scope waits for audio, then shows its window's length")
{
    Rig rig;
    auto& signal = rig.editor.signalPlot();
    CHECK_FALSE (signal.hasSignal());
    CHECK (signal.getCaption() == "SIGNAL");
    rig.model.play (220.0, 0.8f, 4.0, 8);
    rig.frames (1);
    CHECK (signal.hasSignal());
    CHECK (signal.getCaption() == "SIGNAL   11 MS   INPUT IN GREY");
}

NI_SNAPSHOT_TEST ("bit-crusher: the defaults, a tone playing")
{
    Rig rig;
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.frames (60);
    NI_CHECK_SNAPSHOT (rig.editor, "bit-crusher-default");
}

NI_SNAPSHOT_TEST ("bit-crusher: crushed hard, every switch on")
{
    Rig rig;
    rig.model.set (param::bits, 3.0f);
    rig.model.set (param::drive, 6.0f);
    rig.model.set (param::rate, 4000.0f);
    rig.model.set (param::jitter, 25.0f);
    rig.model.set (param::mix, 80.0f);
    for (const int s : { param::midRise, param::antiAlias, param::preFilter, param::postFilter })
        rig.model.set (s, 1.0f);
    rig.model.play (220.0, 0.7f, 3.0, 12);
    rig.frames (60);
    NI_CHECK_SNAPSHOT (rig.editor, "bit-crusher-crushed");
}
