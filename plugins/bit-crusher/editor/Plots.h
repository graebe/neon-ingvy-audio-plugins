// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's three wells, one over each panel -- the two
 * discretizations, then the sound:
 *
 *   AmplitudePlot  THE AMPLITUDE CRUSH: input level across, output level up,
 *                  over the window around silence the engine picks so that
 *                  its steps show -- at most 16 of them, so 8 bits is seen at
 *                  1/16 of full scale rather than as 256 steps narrower than
 *                  a pixel. The untouched signal is the grey diagonal (out =
 *                  in); the staircase the engine renders from the parameters
 *                  now is the curve in front: Drive tilts it, Bits sets its
 *                  steps, Shape bunches them near zero, Mid-Rise lifts it off
 *                  zero. The output axis fits what the window's inputs come
 *                  out as, so Drive's tilt is drawn rather than cut off at
 *                  the well's edge as a clip that does not happen. THE LIVE
 *                  VIEW: a dot for each sample of the look the quantiser saw,
 *                  where its input and its level put it -- on the staircase,
 *                  or beside it by Dither's noise. While there are dots the
 *                  staircase is drawn unlit, a map, and the dots light the
 *                  stairs the signal is on (Plot.h: uv is what is lit).
 *                  Zoomed in, the dots are the part of the signal near
 *                  silence; at few bits, all of it.
 *                  Caption: "AMPLITUDE CRUSH   8.0 BITS   ×16".
 *
 *   TimePlot       THE TIME CRUSH: the hold's own window, the last sixteen
 *                  periods of it at any Rate (or what the ring holds at a slow
 *                  one): the quantiser's input in grey, the level the hold
 *                  keeps as the map -- flat between takes, a riser at each,
 *                  unlit -- and each take lit as a dot, as the amplitude
 *                  well lights its samples. Jitter shows as uneven treads.
 *                  It shows the hold alone: the levels it takes before the
 *                  steps. Caption: "TIME CRUSH   12.0 KHZ   16 HOLDS". Its
 *                  level range follows the material, as the Signal's does.
 *
 *   SignalPlot     THE SOUND: the last ~11 ms of the left channel -- the
 *                  input as a grey line, the crushed output as the curve in
 *                  front -- the two discretizations together, with the
 *                  filters, Mix and Output. It starts on an upward zero
 *                  crossing, so a steady tone stands still. The level range
 *                  follows the material (Plot.h), so a quiet track still fills
 *                  the well; double-click holds it at full scale.
 *
 * THE LIVE VIEWS SHARE ONE LOOK. The editor hands all three the same Scope --
 * the one it took, at Refresh's pace and not while paused (Editor.h) -- so
 * they always show the same moment. The staircase itself follows the knobs
 * every frame, paused or not: it is the settings, not the sound.
 *
 * All draw what the model hands them; none knows what a step is (Plot.h:
 * "draw what the engine renders"). update() is the editor's frame: it repaints
 * only when the picture changed. Message thread.
 */
#pragma once

#include "Model.h"
#include "Plot.h"

#include <cstdint>
#include <vector>

namespace ni::bc
{

class AmplitudePlot final : public ni::ui::PlotWell
{
public:
    /* Points across the input range: about one a pixel of the well. */
    static constexpr int points = 241;

    explicit AmplitudePlot (Model&);

    /* The staircase from the knobs, every frame; the live dots from `look`
     * when it is `fresh`. */
    void update (const Scope& look, bool fresh);

    /* What it drew, for the tests: the curve's outputs, the input range they
     * span either side of silence, the output range the well shows either
     * side of it, and the live dots inside the window. */
    const std::vector<float>& outputs() const noexcept { return levels; }
    double inputSpan() const noexcept { return span; }
    double outputSpan() const noexcept { return outSpan; }
    const std::vector<juce::Point<float>>& liveDots() const noexcept { return dotsAt; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void rebuild();

    Model& model;
    std::vector<float> levels, freshLevels;
    double span = 1.0, outSpan = 1.0;
    std::vector<float> liveIn, liveOut;
    juce::Path curve, diagonal;
    std::vector<juce::Point<float>> dotsAt;
};

class TimePlot final : public ni::ui::PlotWell
{
public:
    explicit TimePlot (Model&);

    void update (const Scope& look, bool fresh, double nowMs);

    /* Whether there is a picture yet: false before any audio. */
    bool hasSignal() const noexcept { return ! in.empty(); }
    /* The takes it marks, for the tests. */
    const std::vector<juce::Point<float>>& takeDots() const noexcept { return dotsAt; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void levelChanged() override { rebuild(); }
    void rebuild();

    Model& model;
    std::vector<float> in;
    std::vector<std::uint8_t> took;
    int takes = 0;
    juce::Path inputLine, heldLine;
    std::vector<juce::Point<float>> dotsAt;
};

class SignalPlot final : public ni::ui::PlotWell
{
public:
    SignalPlot();

    /* `look` drawn when it is `fresh`; the level range follows every frame. */
    void update (const Scope& look, bool fresh, double nowMs);

    /* Whether there is a picture yet: false before any audio. */
    bool hasSignal() const noexcept { return shown.count() > 0; }
    /* The serial of the look it shows, for the tests. */
    std::uint32_t shownSerial() const noexcept { return shown.serial; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void levelChanged() override { rebuild(); }
    void rebuild();

    Scope shown;
    juce::Path dryLine, wetLine;
};

} // namespace ni::bc
