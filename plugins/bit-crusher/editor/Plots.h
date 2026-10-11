// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's two wells, side by side over the panels:
 *
 *   StepsPlot   THE STAIRCASE: input level across, output level up, over
 *               the window around silence the engine picks so that its steps
 *               show -- at most 16 of them, so 8 bits is seen at 1/16 of full
 *               scale rather than as 256 steps narrower than a pixel. The
 *               untouched signal is the grey diagonal behind it (dryLine, the
 *               input's colour in both wells); the staircase the engine
 *               renders from the parameters now is the curve in front. Drive
 *               tilts it, Bits sets its steps, Shape bunches them near zero,
 *               Mid-Rise lifts it off zero. The caption repeats Bits in the
 *               engine's words and the zoom ("STEPS   8 BITS   ×16"), so
 *               the picture says what it is of.
 *
 *   SignalPlot  THE SOUND: the last ~11 ms of the left channel -- the input
 *               as a grey line (dryLine, as in the Steps well), the crushed
 *               output as the curve in front -- so the steps in both
 *               directions are seen where they land: the flat tops of a low
 *               Rate, the stairs of few Bits. It starts on
 *               an upward zero crossing, so a steady tone stands still. The
 *               level range follows the material (Plot.h), so a quiet track
 *               still fills the well; double-click holds it at full scale.
 *
 * Both draw what the model hands them; neither knows what a step is (Plot.h:
 * "draw what the engine renders"). update() is the editor's frame: it reads
 * the model and repaints only when the picture changed. Message thread.
 */
#pragma once

#include "Model.h"
#include "Plot.h"

#include <vector>

namespace ni::bc
{

class StepsPlot final : public ni::ui::PlotWell
{
public:
    /* Points across the input range: about one a pixel of the well. */
    static constexpr int points = 241;

    explicit StepsPlot (Model&);

    void update();

    /* What it drew, for the tests: the outputs, and the input range they
     * span either side of silence. */
    const std::vector<float>& outputs() const noexcept { return levels; }
    double inputSpan() const noexcept { return span; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void rebuild();

    Model& model;
    std::vector<float> levels, fresh;
    double span = 1.0;
    juce::Path curve, diagonal;
};

class SignalPlot final : public ni::ui::PlotWell
{
public:
    explicit SignalPlot (Model&);

    void update (double nowMs);

    /* Whether there is a picture yet: false before any audio. */
    bool hasSignal() const noexcept { return shown.count() > 0; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void levelChanged() override { rebuild(); }
    void rebuild();

    Model& model;
    Scope shown;
    juce::Path dryLine, wetLine;
};

} // namespace ni::bc
