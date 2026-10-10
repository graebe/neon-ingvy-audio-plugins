// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's editor against a model with nothing behind it
 * (vocalize_fakes.h): the window and its grid, what each control asks of the
 * host, the vowel chart following Vowel and Size, the envelope following the
 * audio and naming the engine that drew it, every line the window writes,
 * and the window as a picture -- at its defaults, and Spectral between E and
 * I.
 *
 * What only the real model can show -- the formants and the picture being
 * the engine's -- is tests/vocalize/vz_processor.cpp's.
 */
#include "Editor.h"
#include "InfoLines.h"
#include "Pointer.h"
#include "checks.h"
#include "snapshot.h"
#include "vocalize_fakes.h"

#include <doctest.h>

#include <algorithm>
#include <vector>

using namespace ni::vz;
using ni::ui::gallery::Pointer;
using ni::vz::test::FakeModel;

namespace
{
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

/* The knobs, panel by panel: Editor.h's layout. */
const std::vector<int> knobs { param::vowel, param::size, param::resonance, param::vocal,
                               param::excite, param::mix, param::output };
} // namespace

TEST_CASE ("vocalize: the window is its design's size, on its grid, and every line fits the bar")
{
    Rig rig;
    auto& e = rig.editor;
    CHECK (e.getWidth() == Editor::designWidth);
    CHECK (e.getHeight() == Editor::designHeight);
    CHECK (e.getWidth() == 888);
    CHECK (e.getHeight() == 532);
    NI_CHECK_INFO_LIMIT (e);

    /* The wells over the panels, on the same three columns. */
    CHECK (e.vowelChart().getBounds() == juce::Rectangle<int> (0, 0, Editor::column, Editor::plotH));
    CHECK (e.envelopePlot().getX() == e.panel (1).getX());
    CHECK (e.envelopePlot().getRight() == e.panel (2).getRight());
    CHECK (e.envelopePlot().getHeight() == Editor::plotH);
    CHECK (e.panel (0).getTitleText() == "Vowel");
    CHECK (e.panel (1).getTitleText() == "Vocal");
    CHECK (e.panel (2).getTitleText() == "Level");
    for (int p = 0; p < 3; ++p)
        CHECK (e.panel (p).getBounds() == juce::Rectangle<int> (p * (Editor::column + Editor::gutter), Editor::panelsY,
                                                                 Editor::column, Editor::panelH));

    /* Every control inside its panel's padding, none overlapping another. */
    std::vector<juce::Component*> controls;
    for (const int i : knobs)
        controls.push_back (&e.knob (i));
    controls.push_back (&e.modeSelect());
    for (auto* c : controls)
    {
        const auto* panel = dynamic_cast<ni::ui::Panel*> (c->getParentComponent());
        REQUIRE (panel != nullptr);
        CHECK (panel->contentBounds().contains (c->getBounds()));
    }
    for (size_t a = 0; a < controls.size(); ++a)
        for (size_t b = a + 1; b < controls.size(); ++b)
            if (controls[a]->getParentComponent() == controls[b]->getParentComponent())
                CHECK_FALSE (controls[a]->getBounds().intersects (controls[b]->getBounds()));

    /* Vowel's knobs in the Vowel panel, Mode under Vocal's, the level's in
     * Level: and the dials on one line across the window. */
    CHECK (e.knob (param::vowel).getParentComponent() == &e.panel (0));
    CHECK (e.knob (param::resonance).getParentComponent() == &e.panel (0));
    CHECK (e.knob (param::vocal).getParentComponent() == &e.panel (1));
    CHECK (e.modeSelect().getParentComponent() == &e.panel (1));
    CHECK (e.modeSelect().getY() > e.knob (param::vocal).getBottom());
    CHECK (e.knob (param::output).getParentComponent() == &e.panel (2));
    for (const int i : knobs)
        CHECK (e.knob (i).getY() == e.knob (param::vowel).getY());
}

TEST_CASE ("vocalize: each knob is its parameter, in the engine's words; Size and Output from 12 o'clock")
{
    Rig rig;
    auto& e = rig.editor;
    const std::pair<int, const char*> labels[] { { param::vowel, "Vowel" }, { param::size, "Size" },
                                                 { param::resonance, "Resonance" }, { param::vocal, "Vocal" },
                                                 { param::excite, "Excite" }, { param::mix, "Mix" },
                                                 { param::output, "Output" } };
    for (const auto& entry : labels)
    {
        const int index = entry.first;
        const juce::String label (entry.second);
        CAPTURE (label);
        CHECK (e.knob (index).binding().parameter().getParameterIndex() == index);
        CHECK (e.knob (index).getLabel() == label);
        CHECK (e.knob (index).isBipolar() == (index == param::size || index == param::output));
    }
    CHECK (e.knob (param::vocal).getValueText() == "80 %");
    CHECK (e.knob (param::vowel).getValueText() == "A");
    CHECK (e.knob (param::size).getValueText() == "0 %");
    CHECK (e.knob (param::output).getValueText() == "0.0 dB");

    rig.model.set (param::vowel, 1.3);
    CHECK (e.knob (param::vowel).getValueText() == "E>I 30 %");
    rig.model.set (param::size, 30.0);
    CHECK (e.knob (param::size).getValueText() == "+30 %");
    rig.model.set (param::output, -3.0);
    CHECK (e.knob (param::output).getValueText() == "-3.0 dB");
}

TEST_CASE ("vocalize: the Vowel knob holds on each of the five vowels; no other knob holds anywhere")
{
    Rig rig;
    auto& vowel = rig.editor.knob (param::vowel);
    CHECK (vowel.getDetents() == std::vector<double> { 0.0, 0.25, 0.5, 0.75, 1.0 });
    for (const int i : knobs)
        if (i != param::vowel)
            CHECK_MESSAGE (rig.editor.knob (i).getDetents().empty(), i);

    /* From A, a drag up anywhere across E's hold lands on E exactly: a
     * quarter of the 200px travel, plus half A's hold and E's 14px. */
    for (const float dy : { 60.0f, 64.0f, 68.0f })
    {
        CAPTURE (dy);
        rig.model.set (param::vowel, 0.0);
        const auto centre = vowel.dial().getLocalBounds().getCentre().toFloat();
        Pointer p;
        p.down (vowel.dial(), centre);
        p.drag (centre.translated (0.0f, -dy));
        p.up();
        CHECK (rig.model.plain (param::vowel) == 1.0);
        CHECK (vowel.getValueText() == "E");
    }
}

TEST_CASE ("vocalize: Mode is a select of Time and Spectral, and shows what the host holds")
{
    Rig rig;
    auto& mode = rig.editor.modeSelect();
    CHECK (mode.binding().parameter().getParameterIndex() == param::mode);
    CHECK (mode.getOptions() == juce::StringArray { "Time", "Spectral" });
    CHECK (mode.getIndex() == 0);
    rig.model.set (param::mode, 1.0);
    CHECK (mode.getIndex() == 1);
    CHECK (mode.indexOfValue() == 1);
    rig.model.set (param::mode, 0.0);
    CHECK (mode.getIndex() == 0);
}

TEST_CASE ("vocalize: the vowel chart's dot follows Vowel, its caption in the engine's words")
{
    Rig rig;
    auto& chart = rig.editor.vowelChart();
    CHECK (chart.getCaption() == "VOWEL   A");
    CHECK (chart.dot() == chart.letter (0));

    for (int v = 1; v < 5; ++v)
    {
        rig.model.set (param::vowel, v);
        rig.frames (1);
        CHECK (chart.dot() == chart.letter (v));
    }

    rig.model.set (param::vowel, 1.3);
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   E>I 30 %");
    CHECK (chart.dot() != chart.letter (1));
    CHECK (chart.dot() != chart.letter (2));
    /* Between E and I on the chart: I is front and closed, above E and left
     * of it. */
    CHECK (chart.dot().x < chart.letter (1).x);
    CHECK (chart.dot().x > chart.letter (2).x);

    /* The vowel space as phoneticians draw it: I top left, A at the bottom,
     * U back at the top right. */
    CHECK (chart.letter (2).x < chart.letter (4).x);
    CHECK (chart.letter (0).y > chart.letter (2).y);
    CHECK (chart.letter (0).y > chart.letter (4).y);
}

TEST_CASE ("vocalize: Size slides the whole path along its diagonal; Resonance moves no formant")
{
    Rig rig;
    auto& chart = rig.editor.vowelChart();
    rig.model.set (param::vowel, 2.5);
    rig.frames (1);
    const auto at = [&chart]
    {
        std::vector<juce::Point<float>> p;
        for (int k = 0; k < 5; ++k)
            p.push_back (chart.letter (k));
        p.push_back (chart.dot());
        return p;
    };
    const auto middle = at();

    /* Bigger: every formant lower -- F2 to the right, F1 up. */
    rig.model.set (param::size, 100.0);
    rig.frames (1);
    const auto bigger = at();
    for (size_t k = 0; k < middle.size(); ++k)
    {
        CAPTURE (k);
        CHECK (bigger[k].x > middle[k].x);
        CHECK (bigger[k].y < middle[k].y);
    }

    /* Smaller: the other way. */
    rig.model.set (param::size, -100.0);
    rig.frames (1);
    const auto smaller = at();
    for (size_t k = 0; k < middle.size(); ++k)
    {
        CAPTURE (k);
        CHECK (smaller[k].x < middle[k].x);
        CHECK (smaller[k].y > middle[k].y);
    }

    rig.model.set (param::resonance, 95.0);
    rig.frames (1);
    CHECK (at() == smaller);
}

TEST_CASE ("vocalize: the envelope waits for audio, then names its engine -- Time, or Spectral and how late")
{
    Rig rig;
    auto& plot = rig.editor.envelopePlot();
    CHECK_FALSE (plot.hasPicture());
    CHECK (plot.getCaption() == "ENVELOPE");

    rig.model.play (false, 0, 44100.0);
    rig.frames (1);
    CHECK (plot.hasPicture());
    CHECK (plot.getCaption() == "ENVELOPE   TIME");

    /* 1024 samples at 44.1 kHz: 23.2 ms. */
    rig.model.play (true, 1024, 44100.0);
    rig.frames (1);
    CHECK (plot.getCaption() == "ENVELOPE   SPECTRAL   23 MS LATE");

    /* 2048 at 96 kHz: 21.3 ms. */
    rig.model.play (true, 2048, 96000.0);
    rig.frames (1);
    CHECK (plot.getCaption() == "ENVELOPE   SPECTRAL   21 MS LATE");

    rig.model.play (false, 0, 96000.0);
    rig.frames (1);
    CHECK (plot.getCaption() == "ENVELOPE   TIME");

    /* Its frequencies on the plot's scale: 50 Hz at the left, 10 kHz at the
     * right, 1 kHz between. */
    CHECK (plot.xOf (50.0) < plot.xOf (1000.0));
    CHECK (plot.xOf (1000.0) < plot.xOf (10000.0));
    CHECK (plot.xOf (20.0) == plot.xOf (50.0));
}

TEST_CASE ("vocalize: every line the window writes is in it, each where it belongs")
{
    Rig rig;
    auto& e = rig.editor;
    const auto lines = ni::ui::collectInfo (e);
    const auto has = [&lines] (const juce::String& line) { return std::find (lines.begin(), lines.end(), line) != lines.end(); };
    for (const auto& line : { info::vowelPanel, info::vocalPanel, info::levelPanel, info::vowelChart, info::envelopePlot,
                              info::vowel, info::size, info::resonance, info::vocal, info::excite, info::mode, info::mix,
                              info::output, info::vowelValue, info::percentValue, info::dbValue, info::motion,
                              info::signature })
        CHECK_MESSAGE (has (line), line.str());

    CHECK (ni::ui::infoOf (e.knob (param::vowel)) == info::vowel.str());
    CHECK (ni::ui::infoOf (e.knob (param::size)) == info::size.str());
    CHECK (ni::ui::infoOf (e.knob (param::output)) == info::output.str());
    CHECK (ni::ui::infoOf (e.modeSelect()) == info::mode.str());
    CHECK (ni::ui::infoOf (e.vowelChart()) == info::vowelChart.str());
    CHECK (ni::ui::infoOf (e.envelopePlot()) == info::envelopePlot.str());

    /* And held to the bar in the other states too. */
    rig.model.set (param::mode, 1.0);
    rig.model.play (true, 1024, 44100.0);
    rig.frames (2);
    NI_CHECK_INFO_LIMIT (e);
}

NI_SNAPSHOT_TEST ("vocalize: the defaults, a sound playing through Time")
{
    Rig rig;
    rig.model.play (false, 0, 44100.0);
    rig.frames (60);
    NI_CHECK_SNAPSHOT (rig.editor, "vocalize-default");
}

NI_SNAPSHOT_TEST ("vocalize: Spectral, between E and I, a bigger voice")
{
    Rig rig;
    rig.model.set (param::mode, 1.0);
    rig.model.set (param::vocal, 85.0);
    rig.model.set (param::vowel, 1.3);
    rig.model.set (param::size, 30.0);
    rig.model.set (param::resonance, 65.0);
    rig.model.set (param::excite, 40.0);
    rig.model.set (param::mix, 90.0);
    rig.model.set (param::output, -3.0);
    rig.model.play (true, 1024, 44100.0);
    rig.frames (60);
    NI_CHECK_SNAPSHOT (rig.editor, "vocalize-spectral");
}
