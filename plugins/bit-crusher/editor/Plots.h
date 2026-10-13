// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's three wells, one over each panel -- the two
 * discretizations, then the sound:
 *
 *   AmplitudePlot  THE AMPLITUDE CRUSH: input level across, output level up,
 *                  over the whole range, -1 to 1, never zoomed: at a few bits
 *                  the steps are plain to see, at many they are finer than
 *                  the pixels and the staircase is the diagonal -- as it is
 *                  to the ear. The untouched signal is the grey diagonal (out
 *                  = in), corner to corner: both axes are the
 *                  quantiser's own level, after Drive, on one scale. The
 *                  staircase the engine renders from the parameters now is
 *                  the curve in front, always along the diagonal: Bits sets
 *                  its steps, Shape bunches them near zero, Mid-Rise lifts it
 *                  off zero. Drive moves the signal up it -- the dots spread
 *                  further -- and past full scale into the clip. THE LIVE
 *                  VIEW: a dot for each sample of the look the quantiser saw,
 *                  where its input and its level put it -- on the staircase,
 *                  or beside it by Dither's noise. While there are dots the
 *                  staircase is drawn unlit, a map, and the dots light the
 *                  stairs the signal is on (Plot.h: uv is what is lit).
 *                  Caption: "AMPLITUDE CRUSH   257 LEVELS".
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
 * THE RULERS (plot::ruler) sit along the bottom and the left of each
 * picture's own area, labels outside it: level in full scale, the ends and
 * silence named, time in milliseconds. The two discretizations put their
 * grid on the axes too, as minor ticks: every step of the staircase, both
 * ways, while the steps stand apart, and every take of the hold.
 *
 * All draw what the model hands them; none knows what a step is (Plot.h:
 * "draw what the engine renders"). update() is the editor's frame: it repaints
 * only when the picture changed. Message thread.
 */
#pragma once

#include "Model.h"
#include "ParamBinding.h"
#include "Plot.h"
#include "PlotHandle.h"

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
     * when it is `fresh`, shown while it is `live` (made with the knobs'
     * settings: Editor::current). */
    void update (const Scope& look, bool fresh, bool live);

    /* What it drew, for the tests: the curve's outputs over -1..1, and the
     * live dots. */
    const std::vector<float>& outputs() const noexcept { return levels; }
    const std::vector<juce::Point<float>>& liveDots() const noexcept { return dotsAt; }
    /* Its rulers' marks, labelled and minor: level in, level out. */
    const std::vector<ni::ui::plot::Mark>& inputMarks() const noexcept { return xMarks; }
    const std::vector<ni::ui::plot::Mark>& outputMarks() const noexcept { return yMarks; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void rebuild();
    /* The plot inside the well, clear of its rulers' labels. */
    juce::Rectangle<float> plotArea() const;

    Model& model;
    std::vector<ni::ui::plot::Mark> xMarks, yMarks;
    std::vector<float> levels, freshLevels;
    std::vector<float> liveIn, liveOut;
    bool shownLive = false;
    juce::Path curve, diagonal;
    std::vector<juce::Point<float>> dotsAt;
};

class TimePlot final : public ni::ui::PlotWell
{
public:
    explicit TimePlot (Model&);

    /* `look`'s hold window when it is `fresh`, shown while it is `live`. */
    void update (const Scope& look, bool fresh, bool live, double nowMs);

    /* Whether there is a picture: false before any audio, and while the
     * look is not the knobs'. */
    bool hasSignal() const noexcept { return shownLive && ! in.empty(); }
    /* The takes it marks, and its rulers' marks, for the tests. */
    const std::vector<juce::Point<float>>& takeDots() const noexcept { return dotsAt; }
    const std::vector<ni::ui::plot::Mark>& timeMarks() const noexcept { return xMarks; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void levelChanged() override { rebuild(); }
    void rebuild();
    juce::Rectangle<float> plotArea() const;

    Model& model;
    std::vector<ni::ui::plot::Mark> xMarks, yMarks;
    std::vector<float> in;
    std::vector<std::uint8_t> took;
    std::vector<float> at;
    int takes = 0;
    double sampleRate = 0.0;
    bool shownLive = false;
    juce::Path inputLine, heldLine;
    std::vector<juce::Point<float>> dotsAt;
};

/*
 *   FilterPlot     THE FILTER: its response, frequency across (20 Hz to 20
 *                  kHz, in octaves) and gain up (+24 to -48 dB), from the
 *                  engine's own sections (bc_filter_response) at Cutoff, Peak
 *                  and Slope now. The grey line is 0 dB, the untouched
 *                  signal; the curve is lit while the Filter is on and unlit
 *                  -- what it would do -- while it is off. Caption: "FILTER
 *                  2.00 KHZ   24 DB/OCT   +6.0 DB", or "FILTER   OFF".
 *                  THE HANDLE (PlotHandle.h): a dot on the curve at the
 *                  cutoff. Sideways it is Cutoff, up and down Peak -- the
 *                  same host parameters as the knobs, one gesture a drag --
 *                  and it shapes the curve whether the Filter is on or not.
 */
class FilterPlot final : public ni::ui::PlotWell
{
public:
    static constexpr int points = 220;
    static constexpr double loHz = 20.0, hiHz = 20000.0;
    static constexpr float topDb = 24.0f, bottomDb = -48.0f;

    explicit FilterPlot (Model&);

    /* The response from the knobs, every frame. */
    void update();

    /* What it drew, for the tests. */
    const std::vector<float>& responseDb() const noexcept { return db; }
    bool isOn() const noexcept { return on; }
    const std::vector<ni::ui::plot::Mark>& frequencyMarks() const noexcept { return xMarks; }
    ni::ui::PlotHandle& cutoffHandle() noexcept { return handle; }
    /* The plot inside the well, clear of its rulers' labels. */
    juce::Rectangle<float> plotArea() const;

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void rebuild();

    Model& model;
    std::vector<float> db, freshDb;
    bool on = false;
    juce::Path curve, flat;
    std::vector<ni::ui::plot::Mark> xMarks, yMarks;
    ni::ui::ParamBinding cutoff, peak;
    ni::ui::PlotHandle handle;
};

class SignalPlot final : public ni::ui::PlotWell
{
public:
    SignalPlot();

    /* `look` drawn when it is `fresh`; the level range follows every frame. */
    void update (const Scope& look, bool fresh, double nowMs);

    /* Whether there is a picture yet: false before any audio. */
    bool hasSignal() const noexcept { return shown.count() > 0; }
    /* The serial of the look it shows, and its time ruler, for the tests. */
    std::uint32_t shownSerial() const noexcept { return shown.serial; }
    const std::vector<ni::ui::plot::Mark>& timeMarks() const noexcept { return xMarks; }
    const std::vector<ni::ui::plot::Mark>& levelMarks() const noexcept { return yMarks; }

    void resized() override { rebuild(); }

private:
    void paintPlot (juce::Graphics&) override;
    void levelChanged() override { rebuild(); }
    void rebuild();
    juce::Rectangle<float> plotArea() const;

    Scope shown;
    std::vector<ni::ui::plot::Mark> xMarks, yMarks;
    juce::Path dryLine, wetLine;
};

} // namespace ni::bc
