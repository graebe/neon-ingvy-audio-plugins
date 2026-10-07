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
 *   the pattern    its caption, its playhead on the fractional phase
 *   the signal     its caption carries the cycle in ms
 *   the marks      a rule per step from 6px, bars and beats at any density
 */
#include "Plots.h"

#include "InfoLines.h"
#include "trance-gate_fakes.h"

#include "Info.h"

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

TEST_CASE ("trance-gate plots: the pattern's caption, and its playhead on the fractional phase")
{
    PatternPlot plot;
    plot.setSize (760, 92);
    CHECK (plot.getCaption() == "PATTERN   ONE CYCLE");
    CHECK (plot.getPhase() < 0.0);
    plot.setPhase (5.4);
    CHECK (plot.getPhase() == doctest::Approx (5.4));
    CHECK (ni::ui::infoOf (plot) == info::patternPlot.str());
}

TEST_CASE ("trance-gate plots: the signal's caption carries one cycle in ms")
{
    SignalPlot plot;
    plot.setSize (760, 92);
    FakeModel model;
    renderCurves (model);
    fillCapture (model, 256, 10, 2000.0);
    plot.update (model.signal, model.gateCurve, 16, 1.0f, true);
    CHECK (plot.getCaption() == "SIGNAL   ONE CYCLE, 2000 MS   DRY IN GREY, GATED IN FRONT");
    model.signal.cycleMs = 1000.0;
    ++model.signal.serial;
    plot.update (model.signal, model.gateCurve, 16, 1.0f, true);
    CHECK (plot.getCaption().contains ("1000 MS"));
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
