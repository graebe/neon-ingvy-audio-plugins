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
 * (bc_transfer_render, over bc_transfer_span's window): a mid-tread grid of
 * 2^(1-Bits) under Drive, zoomed until at most 16 steps show, which is what
 * the engine draws with Shape at 0 -- the test's, not the editor's, which
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
        params.addFloat ("post_octave", "Octave", linear (-3.0f, 3.0f), 0.0f, fixed (1, "oct", true), number);
        params.addFloat ("mix", "Mix", linear (0.0f, 100.0f), 100.0f, fixed (0, "%", false), number);
        params.addFloat ("output", "Output", linear (-24.0f, 24.0f), 0.0f, fixed (1, "dB", true), number);
        params.addChoice ("refresh", "Refresh", { "Live", "10/s", "4/s", "1/s" }, 3);
    }

    int numParameters() const override { return params.size(); }
    juce::RangedAudioParameter& parameter (int i) override { return params[i]; }
    int takeRings (float*, int) override { return 0; }
    bool motion() const override { return motionOn; }
    void setMotion (bool on) override { motionOn = on; }
    bool bipolar (int i) const override { return i == param::drive || i == param::postOctave || i == param::output; }

    float plain (int i) { return params[i].convertFrom0to1 (params[i].getValue()); }

    /* On the quantiser's own input, after Drive: no Drive in it. */
    double steps (float* out, int count) override
    {
        const double delta = std::exp2 (1.0 - (double) plain (param::bits));
        /* The window: halved from full scale while it would hold more than
         * 16 steps, as bc_transfer_span counts them. */
        double span = 1.0;
        while (span > 1.0 / 65536.0 && 2.0 * span / delta > 16.0)
            span *= 0.5;
        for (int i = 0; i < count; ++i)
        {
            const double x = span * (-1.0 + 2.0 * i / (count - 1));
            out[i] = (float) juce::jlimit (-1.0, 1.0, delta * std::round (x / delta));
        }
        return span;
    }

    const Scope& scope() override { return audio; }

    double setting (int i) const override
    {
        auto& p = params[i];
        return (double) p.convertFrom0to1 (p.getValue());
    }

    int levelCount() const override
    {
        const double n = std::exp2 (setting (param::bits) - 1.0);
        return setting (param::midRise) >= 0.5 ? (int) std::round (2.0 * n) : (int) std::round (2.0 * n + 1.0);
    }

    double refreshPeriodMs() const override
    {
        static constexpr double periods[] { 0.0, 100.0, 250.0, 1000.0 };
        return periods[juce::jlimit (0, 3, juce::roundToInt (params[param::refresh].convertFrom0to1 (params[param::refresh].getValue())))];
    }

    /* A tone of `hz` at `level` through `bits` and a hold of `hold` samples,
     * as the engine would publish it at Drive 0 dB: dry and crushed, 512
     * samples at 48 kHz starting on an upward crossing, the same samples in
     * the quantiser, and the hold's window of sixteen periods. */
    void play (double hz, float level, double bits, int hold)
    {
        audio.dry.resize (512);
        audio.wet.resize (512);
        audio.ampIn.resize (512);
        audio.ampOut.resize (512);
        audio.sampleRate = 48000.0;
        audio.triggered = true;
        ++audio.serial;
        const double delta = std::exp2 (1.0 - bits);
        const auto at = [&] (int n) { return level * std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / 48000.0); };
        const auto step = [&] (double x) { return (float) (delta * std::round (x / delta)); };
        /* In the quantiser, as the engine reports it: its input after Drive,
         * and the level steps() draws for it. */
        const double gridStep = std::exp2 (1.0 - (double) plain (param::bits));
        const double gain = std::pow (10.0, (double) plain (param::drive) / 20.0);
        const auto level_ = [&] (double x) { return (float) juce::jlimit (-1.0, 1.0, gridStep * std::round (x / gridStep)); };
        for (int i = 0; i < 512; ++i)
        {
            audio.dry[(size_t) i] = (float) at (i);
            audio.wet[(size_t) i] = step (at (i - i % hold));
            audio.ampIn[(size_t) i] = (float) (gain * at (i));
            audio.ampOut[(size_t) i] = level_ (gain * at (i));
        }
        const int len = 16 * hold + 1;
        audio.holdIn.resize ((size_t) len);
        audio.holdTook.resize ((size_t) len);
        audio.holdAt.assign ((size_t) len, 0.0f);
        for (int i = 0; i < len; ++i)
        {
            audio.holdIn[(size_t) i] = (float) at (i);
            audio.holdTook[(size_t) i] = i % hold == 0 ? 1 : 0;
        }
        audio.holdTakes = 17;
        /* Made with the knobs as they are, long enough ago. */
        audio.settled = Scope::settledWindow;
        for (int i = 0; i < param::count; ++i)
            audio.settings[(size_t) i] = setting (i);
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

    /* A well over each panel, on the same three columns. */
    const juce::Component* wells[] { &e.amplitudePlot(), &e.timePlot(), &e.signalPlot() };
    for (int p = 0; p < 3; ++p)
        CHECK (wells[p]->getBounds() == juce::Rectangle<int> (e.panel (p).getX(), 0, Editor::column, Editor::plotH));
    CHECK (e.panel (0).getTitleText() == "Amplitude Crush");
    CHECK (e.panel (1).getTitleText() == "Time Crush");
    CHECK (e.panel (2).getTitleText() == "Level");

    /* Refresh and Pause in the Level panel's free row, under its knobs. */
    for (juce::Component* c : { static_cast<juce::Component*> (&e.refreshSelect()), static_cast<juce::Component*> (&e.pauseButton()) })
    {
        REQUIRE (c->getParentComponent() == &e.panel (2));
        CHECK (e.panel (2).contentBounds().contains (c->getBounds()));
        for (const int k : { param::drive, param::mix, param::output })
            CHECK_FALSE (c->getBounds().intersects (e.knob (k).getBounds()));
    }
    CHECK_FALSE (e.refreshSelect().getBounds().intersects (e.pauseButton().getBounds()));
    CHECK (e.refreshSelect().getIndex() == 3);
    for (int p = 0; p < 3; ++p)
        CHECK (e.panel (p).getBounds() == juce::Rectangle<int> (p * (Editor::column + Editor::gutter), Editor::panelsY,
                                                                 Editor::column, Editor::panelH));

    /* Every control inside its panel, none overlapping another. */
    std::vector<juce::Component*> controls;
    for (int i = 0; i < param::refresh; ++i)
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
    auto& steps = rig.editor.amplitudePlot();
    const auto distinct = [] (const std::vector<float>& v)
    {
        std::vector<float> levels (v.begin(), v.end());
        std::sort (levels.begin(), levels.end());
        levels.erase (std::unique (levels.begin(), levels.end()), levels.end());
        return levels;
    };
    /* 8 bits is 256 steps: the well shows a sixteenth of full scale, says so,
     * and holds 16 steps there -- a staircase, not a diagonal. */
    CHECK (steps.getCaption() == juce::String::fromUTF8 ("AMPLITUDE CRUSH   257 LEVELS   \xc3\x97" "16"));
    CHECK (steps.inputSpan() == 1.0 / 16.0);
    CHECK (distinct (steps.outputs()).size() == 17);
    const auto before = steps.outputs();

    /* 2 bits fits whole: no zoom, and no zoom in the caption. */
    rig.model.set (param::bits, 2.0f);
    rig.frames (1);
    CHECK (steps.getCaption() == "AMPLITUDE CRUSH   5 LEVELS");
    CHECK (steps.inputSpan() == 1.0);
    rig.model.set (param::midRise, 1.0f);
    rig.frames (1);
    CHECK (steps.getCaption() == "AMPLITUDE CRUSH   4 LEVELS");
    rig.model.set (param::midRise, 0.0f);
    rig.frames (1);
    const auto& after = steps.outputs();
    CHECK (after != before);
    CHECK (distinct (after) == std::vector<float> { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f });
}

TEST_CASE ("bit-crusher: the diagonal stays the diagonal; Drive moves the signal up the stairs")
{
    Rig rig;
    auto& steps = rig.editor.amplitudePlot();
    rig.model.set (param::refresh, 0.0f);
    const auto at0 = steps.outputs();
    const double span0 = steps.inputSpan();
    rig.model.play (220.0, 0.1f, 8.0, 4);
    rig.frames (1);
    const auto quiet = steps.liveDots().size();

    /* Drive leaves the staircase, its zoom and its scale where they were... */
    for (const float drive : { 12.0f, -12.0f, 24.0f })
    {
        rig.model.set (param::drive, drive);
        rig.frames (1);
        CHECK (steps.outputs() == at0);
        CHECK (steps.inputSpan() == span0);
        CHECK (steps.outputSpan() == steps.inputSpan());
    }
    /* ...and pushes the signal's samples further up it: at +12 dB fewer of
     * them stay near silence, inside the window. */
    rig.model.set (param::drive, 12.0f);
    rig.model.play (220.0, 0.1f, 8.0, 4);
    rig.frames (1);
    CHECK (steps.liveDots().size() < quiet);
    /* Every dot is on the staircase, so on or within a step of the diagonal. */
    const auto area = steps.getLocalBounds().toFloat();
    for (const auto& d : steps.liveDots())
        CHECK (area.contains (d));
}

TEST_CASE ("bit-crusher: the live dots sit where the look's samples are on the staircase")
{
    Rig rig;
    auto& steps = rig.editor.amplitudePlot();
    CHECK (steps.liveDots().empty());
    /* Two bits: the whole range is in view, so every sample is a dot. */
    rig.model.set (param::bits, 2.0f);
    rig.model.play (220.0, 0.7f, 2.0, 4);
    rig.frames (1);
    CHECK (steps.liveDots().size() == 512);
    const auto area = steps.getLocalBounds().toFloat();
    for (const auto& d : steps.liveDots())
        CHECK (area.contains (d));
    /* Eight bits, a sixteenth of full scale: only the samples near silence. */
    rig.model.set (param::bits, 8.0f);
    rig.model.set (param::refresh, 0.0f);
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.frames (1);
    CHECK (steps.liveDots().size() > 0);
    CHECK (steps.liveDots().size() < 100);
}

TEST_CASE ("bit-crusher: the time plot marks every take, sixteen holds of them")
{
    Rig rig;
    auto& time = rig.editor.timePlot();
    CHECK_FALSE (time.hasSignal());
    CHECK (time.getCaption() == "TIME CRUSH   12.0 KHZ");
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.frames (1);
    CHECK (time.hasSignal());
    CHECK (time.takeDots().size() == 17);
    CHECK (time.getCaption() == "TIME CRUSH   12.0 KHZ   16 HOLDS");
    /* The takes are evenly spaced across the well. */
    const auto& d = time.takeDots();
    for (size_t i = 2; i < d.size(); ++i)
        CHECK (std::abs ((d[i].x - d[i - 1].x) - (d[1].x - d[0].x)) < 0.01f);
}

TEST_CASE ("bit-crusher: the pictures take a new look at Refresh's pace, all three the same")
{
    Rig rig;
    auto& signal = rig.editor.signalPlot();
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.frames (1);
    const auto first = signal.shownSerial();
    CHECK (first == rig.model.audio.serial);

    /* 1/s, the default: a newer look a tenth of a second later waits. */
    rig.model.play (330.0, 0.5f, 8.0, 4);
    rig.frames (6);
    CHECK (signal.shownSerial() == first);
    rig.frames (60);
    CHECK (signal.shownSerial() == rig.model.audio.serial);

    /* Live: every new look, at the next frame. */
    rig.model.set (param::refresh, 0.0f);
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.frames (1);
    CHECK (signal.shownSerial() == rig.model.audio.serial);
}

TEST_CASE ("bit-crusher: Pause holds all three pictures; the staircase still follows the knobs")
{
    Rig rig;
    auto& e = rig.editor;
    rig.model.set (param::refresh, 0.0f);
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.frames (1);
    const auto held = e.signalPlot().shownSerial();
    const auto dots = e.amplitudePlot().liveDots().size();

    Pointer p;
    p.click (e.pauseButton(), e.pauseButton().getLocalBounds().toFloat().getCentre());
    CHECK (e.isPaused());
    CHECK (e.pauseButton().isOn());
    rig.model.play (330.0, 0.9f, 8.0, 8);
    rig.frames (120);
    CHECK (e.signalPlot().shownSerial() == held);
    CHECK (e.amplitudePlot().liveDots().size() == dots);
    CHECK (e.timePlot().takeDots().size() == 17);
    const auto heldDots = e.timePlot().takeDots();

    /* The knobs still move the staircase; the held look was made at 8 bits,
     * so its samples are not shown on a 2-bit staircase -- nor the hold. */
    rig.model.set (param::bits, 2.0f);
    rig.frames (1);
    CHECK (e.amplitudePlot().getCaption() == "AMPLITUDE CRUSH   5 LEVELS");
    CHECK (e.amplitudePlot().liveDots().empty());
    CHECK_FALSE (e.timePlot().hasSignal());
    CHECK (e.signalPlot().shownSerial() == held);
    /* Back to the settings that made it: it is theirs again. */
    rig.model.set (param::bits, 8.0f);
    rig.frames (1);
    CHECK (e.amplitudePlot().liveDots().size() == dots);
    CHECK (e.timePlot().takeDots() == heldDots);
    rig.model.set (param::bits, 2.0f);

    /* Let go: the latest look at once. */
    p.click (e.pauseButton(), e.pauseButton().getLocalBounds().toFloat().getCentre());
    CHECK_FALSE (e.isPaused());
    rig.frames (1);
    CHECK (e.signalPlot().shownSerial() == rig.model.audio.serial);
}

TEST_CASE ("bit-crusher: a knob turned between refreshes hides the dots until a look made with it")
{
    Rig rig;
    auto& amp = rig.editor.amplitudePlot();
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.frames (1);
    REQUIRE_FALSE (amp.liveDots().empty());
    const auto shown = rig.editor.signalPlot().shownSerial();

    /* At 1/s: Bits moves, the look is a second from due -- the dots go now. */
    rig.model.set (param::bits, 4.0f);
    rig.frames (1);
    CHECK (amp.liveDots().empty());
    /* The engine's next look is still the old settings' (not yet settled)... */
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.model.audio.settled = 100;
    rig.frames (1);
    CHECK (amp.liveDots().empty());
    /* ...then one made with them throughout: taken at once, not a second on. */
    rig.model.play (220.0, 0.7f, 4.0, 4);
    rig.frames (1);
    CHECK (rig.editor.signalPlot().shownSerial() != shown);
    CHECK_FALSE (amp.liveDots().empty());
}

TEST_CASE ("bit-crusher: the hold is drawn at its takes' exact instants, and not without takes")
{
    Rig rig;
    auto& time = rig.editor.timePlot();
    rig.model.set (param::refresh, 0.0f);
    /* Anti-Alias at 12 kHz in 44.1 kHz: takes 3.675 samples apart. */
    rig.model.play (220.0, 0.7f, 8.0, 4);
    auto& a = rig.model.audio;
    const int len = 60;
    a.holdIn.assign ((size_t) len, 0.25f);
    a.holdTook.assign ((size_t) len, 0);
    a.holdAt.assign ((size_t) len, 0.0f);
    int takes = 0;
    for (double t = 0.0; t < len - 1; t += 3.675, ++takes)
    {
        a.holdTook[(size_t) t] = 1;
        a.holdAt[(size_t) t] = (float) (t - std::floor (t));
    }
    a.holdTakes = takes;
    rig.frames (1);
    const auto& d = time.takeDots();
    REQUIRE ((int) d.size() == takes);
    const float gap = d[1].x - d[0].x;
    for (size_t i = 2; i < d.size(); ++i)
        CHECK (std::abs ((d[i].x - d[i - 1].x) - gap) < 0.01f);

    /* No take in the window -- bypassed, say: nothing drawn as held. */
    rig.model.play (220.0, 0.7f, 8.0, 4);
    rig.model.audio.holdTook.assign (rig.model.audio.holdTook.size(), 0);
    rig.model.audio.holdTakes = 0;
    rig.frames (1);
    CHECK (time.takeDots().empty());
    CHECK (time.getCaption() == "TIME CRUSH   12.0 KHZ");

    /* At the host's own rate or above, nothing is held, and it says so. */
    rig.model.set (param::rate, 96000.0f);
    rig.model.play (220.0, 0.7f, 8.0, 1);
    rig.frames (1);
    CHECK (time.getCaption() == "TIME CRUSH   96.0 KHZ   NO HOLD");
}

TEST_CASE ("bit-crusher: the axes are ruled, and the discretizations' grids are on them")
{
    Rig rig;
    auto& amp = rig.editor.amplitudePlot();
    const auto labelled = [] (const std::vector<ni::ui::plot::Mark>& marks)
    {
        juce::StringArray s;
        for (const auto& m : marks)
            if (m.text.isNotEmpty())
                s.add (m.text);
        return s;
    };
    const auto minor = [] (const std::vector<ni::ui::plot::Mark>& marks)
    { return (int) std::count_if (marks.begin(), marks.end(), [] (const auto& m) { return m.text.isEmpty(); }); };

    /* 8 bits, a sixteenth of full scale: its ends and silence named, and a
     * minor tick at each of the 16 steps across and the 17 levels up. */
    CHECK (labelled (amp.inputMarks()) == juce::StringArray { "-1/16", "0", "1/16" });
    CHECK (labelled (amp.outputMarks()) == juce::StringArray { "-1/16", "0", "1/16" });
    CHECK (minor (amp.inputMarks()) == 16);
    CHECK (minor (amp.outputMarks()) == 17);
    /* 2 bits, the whole range: -1 to 1, four steps, five levels. */
    rig.model.set (param::bits, 2.0f);
    rig.frames (1);
    CHECK (labelled (amp.inputMarks()) == juce::StringArray { "-1", "0", "1" });
    CHECK (minor (amp.inputMarks()) == 4);
    CHECK (minor (amp.outputMarks()) == 5);
    /* The diagonal still corner to corner of the area the rulers frame. */
    const auto& xs = amp.inputMarks();
    CHECK (xs.front().at < xs[2].at);

    /* Time: milliseconds across, a minor tick at each of the 17 takes; the
     * Signal: its 11 ms. */
    rig.model.play (220.0, 0.7f, 2.0, 4);
    rig.frames (1);
    const auto& t = rig.editor.timePlot().timeMarks();
    CHECK (minor (t) == 17);
    CHECK (labelled (t).contains ("0"));
    CHECK (labelled (t).contains ("1"));
    const auto s = labelled (rig.editor.signalPlot().timeMarks());
    CHECK (s.contains ("0"));
    CHECK (s.contains ("10"));
    /* Level up in round numbers, whatever the range that follows the
     * material ends on. */
    for (const auto& m : rig.editor.signalPlot().levelMarks())
        CHECK_MESSAGE ((m.text == "0" || m.text == "-0.5" || m.text == "0.5" || m.text == "-1" || m.text == "1"), m.text);
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
