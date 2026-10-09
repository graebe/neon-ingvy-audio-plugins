// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's plots, held to the manual (docs/live.md, The envelope
 * plot, Pattern and Signal) and to the web editor's arithmetic
 * (EnvelopePlot.jsx, Plots.jsx):
 *
 *   the envelope   its span holds both curves and 4 % of air; the step's edge
 *                  is amber when the release outlives the step; the letters
 *                  label what ran; the caption names the span and the step
 *   the pattern    its caption, its playhead in whole steps
 *   the signal     its caption carries the cycle in ms; its traces zoom with
 *                  the material, quickly up and slowly down, labelled clear
 *                  of the tabs, and a double-click holds them at full scale
 *   the marks      a rule per step from 6px, bars and beats at any density
 */
#include "Plots.h"

#include "InfoLines.h"
#include "trance-gate_fakes.h"

#include "Info.h"
#include "Pointer.h"
#include "UvTokens.h"
#include "snapshot.h"

#include <doctest.h>

using namespace ni::tg;
using namespace ni::tg::test;

namespace
{
EnvelopePlot::Setting setting (double msPerStep, double width, double attack, double decay, double release)
{
    EnvelopePlot::Setting s;
    s.msPerStep = msPerStep;
    s.width = width;
    s.attack = attack;
    s.decay = decay;
    s.release = release;
    return s;
}
} // namespace

TEST_CASE ("trance-gate plots: the envelope's span holds the gated curve and the one dialled, and 4 % of air")
{
    /* The defaults at 120 BPM, 1/16: a 125 ms gate, Release 16 % of it. */
    auto s = EnvelopePlot::spanOf (setting (125.0, 1.0, 1.6, 16.0, 16.0));
    CHECK (s.gate == doctest::Approx (125.0));
    CHECK (s.releaseEnd == doctest::Approx (145.0));
    CHECK (s.span == doctest::Approx (145.0 * 1.04));
    /* The release runs past the step: the next gate will cut it. */
    CHECK (s.truncated);
    CHECK_FALSE (s.early);

    /* A narrow Width with a long attack and decay: the gate closes first,
     * and the span is the envelope as dialled, not gate + release. */
    s = EnvelopePlot::spanOf (setting (100.0, 0.25, 200.0, 100.0, 10.0));
    CHECK (s.gate == doctest::Approx (25.0));
    CHECK (s.decayEnd == doctest::Approx (75.0));
    CHECK (s.early);
    CHECK_FALSE (s.truncated);
    CHECK (s.span == doctest::Approx (100.0 * 1.04));

    /* No tempo yet: nothing to draw. */
    CHECK (EnvelopePlot::spanOf (setting (0.0, 1.0, 1.6, 16.0, 16.0)).span == 0.0);
}

TEST_CASE ("trance-gate plots: the caption names the span and the step; no tempo, no caption")
{
    EnvelopePlot plot;
    plot.setSize (240, 104);
    FakeModel model;
    renderCurves (model);
    plot.update (model.envelopeCurves, setting (125.0, 1.0, 1.6, 16.0, 16.0));
    CHECK (plot.getCaption() == "ENVELOPE 151 MS   STEP 125.0 MS");
    plot.update (model.envelopeCurves, setting (0.0, 1.0, 1.6, 16.0, 16.0));
    CHECK (plot.getCaption().isEmpty());
    CHECK (ni::ui::infoOf (plot) == info::envelopePlot.str());
}

TEST_CASE ("trance-gate plots: the letters label the stages that ran, each where it has room")
{
    EnvelopePlot plot;
    plot.setSize (240, 104);
    FakeModel model;
    renderCurves (model);
    /* The defaults: the attack is 2 ms of 151, under a letter's width. */
    plot.update (model.envelopeCurves, setting (125.0, 1.0, 1.6, 16.0, 16.0));
    CHECK (plot.letters() == "DSR");
    /* A slow attack has room. */
    plot.update (model.envelopeCurves, setting (125.0, 1.0, 40.0, 16.0, 16.0));
    CHECK (plot.letters() == "ADSR");
    /* The gate shuts mid-attack: no decay, no sustain ran. */
    plot.update (model.envelopeCurves, setting (125.0, 0.3, 200.0, 50.0, 30.0));
    CHECK (plot.letters() == "AR");
}

TEST_CASE ("trance-gate plots: the pattern's caption, and its playhead in whole steps")
{
    PatternPlot plot;
    plot.setSize (760, 92);
    CHECK (plot.getCaption() == "PATTERN   ONE CYCLE");
    CHECK (plot.getPlayhead() < 0);
    plot.setPlayhead (5);
    CHECK (plot.getPlayhead() == 5);
    /* A step the pattern does not have is none. */
    plot.setPlayhead (16);
    CHECK (plot.getPlayhead() < 0);
    CHECK (ni::ui::infoOf (plot) == info::patternPlot.str());
}

TEST_CASE ("trance-gate plots: the signal's caption carries one cycle in ms")
{
    SignalPlot plot;
    plot.setSize (760, 92);
    /* Before the first capture: the second its ruler is laid out over. */
    CHECK (plot.getCaption() == "SIGNAL   ONE CYCLE, 1000 MS   DRY IN GREY, GATED IN FRONT");
    FakeModel model;
    renderCurves (model);
    fillCapture (model, 256, 10, 2000.0);
    plot.update (model.signal, model.gateCurve, 16, 1.0f, true, 0.0);
    CHECK (plot.getCaption() == "SIGNAL   ONE CYCLE, 2000 MS   DRY IN GREY, GATED IN FRONT");
    model.signal.cycleMs = 1000.0;
    ++model.signal.serial;
    plot.update (model.signal, model.gateCurve, 16, 1.0f, true, 0.0);
    CHECK (plot.getCaption().contains ("1000 MS"));
}

namespace
{
/* Where the range is headed for the capture the model holds now: 3 dB over
 * its peak, dry or gated. */
float targetNow (const FakeModel& model)
{
    const auto& c = model.signal;
    return ni::ui::plot::LevelRange::targetFor (
        ni::ui::plot::peak ({ c.data.data(), Capture::stride, c.columns }, { 0, 1, 2, 3 }));
}
} // namespace

TEST_CASE ("trance-gate plots: the signal zooms with the material, quickly when it is louder, slowly when quieter")
{
    SignalPlot plot;
    plot.setSize (760, 92);
    CHECK (plot.hasLevelRange());
    FakeModel model;
    renderCurves (model);

    /* A quiet track, -24 dBFS: the first frame lands on its target. */
    fillCapture (model, 256, 10, 2000.0, 0.063f);
    double ms = 0.0;
    plot.update (model.signal, model.gateCurve, 16, 1.0f, true, ms);
    CHECK (plot.levelRange().db() == doctest::Approx (targetNow (model)));

    /* A loud one: up in a glide, past the peak inside 100 ms. */
    fillCapture (model, 256, 11, 2000.0, 0.9f);
    plot.update (model.signal, model.gateCurve, 16, 1.0f, true, ms += 1000.0 / 60.0);
    const float peakDb = targetNow (model) - ni::ui::plot::LevelRange::headroomDb;
    CHECK (plot.levelRange().db() < peakDb);
    for (int i = 0; i < 5; ++i)
        plot.update (model.signal, model.gateCurve, 16, 1.0f, true, ms += 1000.0 / 60.0);
    CHECK (plot.levelRange().db() >= peakDb);

    /* Quiet again, and the transport stopped -- the capture holds still, and
     * the range still eases down once the window has passed. */
    fillCapture (model, 256, 12, 2000.0, 0.1f);
    for (int i = 0; i < 900; ++i)
        plot.update (model.signal, model.gateCurve, 16, 1.0f, false, ms += 1000.0 / 60.0);
    CHECK (plot.levelRange().db() == doctest::Approx (targetNow (model)));
}

TEST_CASE ("trance-gate plots: the signal's range is labelled over its top edge, clear of the tabs")
{
    SignalPlot plot;
    plot.setSize (760, 92);
    FakeModel model;
    renderCurves (model);
    fillCapture (model, 256, 10, 2000.0, 0.063f);
    plot.update (model.signal, model.gateCurve, 16, 1.0f, true, 0.0);
    CHECK (plot.levelRange().label() == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "21 dB")));

    const auto img = ni::ui::test::render (plot);
    const auto inked = [&img] (int x0, int x1)
    {
        for (int y = 2; y < 13; ++y)
            for (int x = x0; x < x1; ++x)
                if (img.getPixelAt (x, y).getBrightness() > uv::tok::colour::bg000.getBrightness() + 0.2f)
                    return true;
        return false;
    };
    /* The tabs' 24px and the inset are left alone; the label ends just short. */
    CHECK (inked (690, 730));
    CHECK_FALSE (inked (731, 760));
}

TEST_CASE ("trance-gate plots: a double-click on the signal holds it at full scale, and another lets it go")
{
    SignalPlot plot;
    plot.setSize (760, 92);
    FakeModel model;
    renderCurves (model);
    fillCapture (model, 256, 10, 2000.0, 0.063f);
    plot.update (model.signal, model.gateCurve, 16, 1.0f, true, 0.0);
    const float following = plot.levelRange().db();

    ni::ui::gallery::Pointer p;
    p.doubleClick (plot, { 300.0f, 50.0f });
    CHECK (plot.levelRange().isFixed());
    CHECK (plot.levelRange().fullScale() == 1.0f);
    CHECK (plot.levelRange().label() == "0 dB fixed");
    p.doubleClick (plot, { 300.0f, 50.0f });
    CHECK_FALSE (plot.levelRange().isFixed());
    CHECK (plot.levelRange().db() == following);
}

TEST_CASE ("trance-gate plots: the band's tabs say what each shows")
{
    Band band;
    band.setSize (760, Band::height);
    CHECK (band.tabs().size() == 2);
    CHECK (ni::ui::infoOf (band.tabs().getTab (0)) == info::patternTab.str());
    CHECK (ni::ui::infoOf (band.tabs().getTab (1)) == info::signalTab.str());
    band.show (1);
    CHECK (band.shown() == 1);
    CHECK (band.signal().isVisible());
}
