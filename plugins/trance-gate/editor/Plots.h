// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's three plots, each a PlotWell drawing what the engine
 * rendered -- the web editor's EnvelopePlot.jsx, Plots.jsx and Band.jsx:
 *
 *   EnvelopePlot   under the ring: one gate on a millisecond axis
 *   PatternPlot    the gate across one cycle, as the engine applies it
 *   SignalPlot     the input and what the gate made of it, one cycle
 *   Band           the last two in one place, one at a time, with the tab
 *                  strip laid over the band's right edge
 *
 * THE CURVES ARE THE ENGINE'S (Model::envelope, gate, capture), and none of
 * this reasons about the sound. What is worked out here is where to draw it:
 * the envelope's millisecond span from the stage settings, and the one thing
 * the renders leave out on purpose, Amount -- m = 1 - amount (1 - g) is a
 * floor under the curve, affine in g, so a turn of Amount costs a repaint
 * and not a render, and everything under the floor is lit whatever the gate
 * does.
 *
 * ON ULTRAVIOLET 1.1.0, the PlotWell card's data colours replace the web's
 * hand-picked ones: the curve is the 2px uv line with its arc glow over
 * plot-fill, the envelope as dialled behind what the gate leaves of it is the
 * 1px plot-ghost line, the dry input is plot-dry at half opacity under the uv
 * trace, the gate's outline over the Signal is plot-ghost too, and every
 * position mark -- where the gate closes, the step's edge, the playhead, the
 * sweep -- is a line-200 rule, the step's edge amber when the release runs
 * past it. The envelope's stage dots are gone: the card draws no mark there.
 * Step numbers and the A/D/S/R letters are labels, in the hint style in
 * ink-dim, as the as-built artboard has them.
 *
 * Each plot rebuilds its paths only when what it draws from changes (a
 * render's serial, a setting, its size), and the moving marks repaint only
 * the strips they leave and enter. Message thread.
 */
#pragma once

#include "Model.h"

#include "Luminous.h"
#include "Plot.h"
#include "Tabs.h"

#include <cstdint>
#include <vector>

namespace ni::tg
{

/* ------------------------------------------------------------ envelope -- */

class EnvelopePlot final : public ni::ui::PlotWell
{
public:
    /* The settings the axis is worked out from: the stages in % of the
     * gate's open time, Width and Amount 0..1, one step in ms. */
    struct Setting
    {
        double msPerStep = 0.0;
        double width = 1.0;
        double attack = 0.0, decay = 0.0, release = 0.0;
        double amount = 1.0;

        bool operator== (const Setting& o) const
        {
            return juce::exactlyEqual (msPerStep, o.msPerStep) && juce::exactlyEqual (width, o.width)
                && juce::exactlyEqual (attack, o.attack) && juce::exactlyEqual (decay, o.decay)
                && juce::exactlyEqual (release, o.release) && juce::exactlyEqual (amount, o.amount);
        }
    };

    /* What the axis comes to, in ms. */
    struct Span
    {
        double gate = 0.0;          // where the gate closes: its open time
        double attackEnd = 0.0, decayEnd = 0.0, releaseEnd = 0.0;
        double span = 0.0;          // the axis: both curves, and 4 % of air
        bool truncated = false;     // the release runs past the step's edge
        bool early = false;         // the gate closed before the decay ended
    };
    static Span spanOf (const Setting&);

    /* The letters A, D, S and R are left out under this many pixels. */
    static constexpr float letterMin = 12.0f;

    EnvelopePlot();

    void update (const EnvelopeCurves&, const Setting&);
    const Span& span() const noexcept { return shape; }
    /* The letters drawn, in order: "ADSR" when all four fit. */
    juce::String letters() const;

    void resized() override;

protected:
    void paintPlot (juce::Graphics&) override;

private:
    void rebuild();
    float xOfMs (double ms) const;

    EnvelopeCurves curves;
    Setting setting;
    Span shape;
    bool drawn = false;

    juce::Path line, area, ghostLine;
    juce::Rectangle<float> floorBox;
    struct Letter
    {
        juce::String text;
        float x0, x1;
    };
    std::vector<Letter> letterMarks;
    ni::ui::plot::Axis axis;
};

/* ------------------------------------------------------------- pattern -- */

/* The step rules and numbers both plots draw: a rule between steps (bars and
 * beats at any density, every step from 6px), a number per step from 14px. */
struct StepMarks
{
    static constexpr float ruleMin = 6.0f;
    static constexpr float numberMin = 14.0f;

    static void rules (juce::Graphics&, int count, float width, float top, float bottom);
    static void numbers (juce::Graphics&, int count, float width, float top);
};

class PatternPlot final : public ni::ui::PlotWell
{
public:
    PatternPlot();

    /* The render, the pattern's length and Amount (0..1). */
    void update (const GateCurve&, int length, float amount);
    /* The playhead, in steps; negative for none. Repaints its strips. */
    void setPhase (double steps);
    double getPhase() const noexcept { return phase; }

    void resized() override;

protected:
    void paintPlot (juce::Graphics&) override;

private:
    void rebuild();
    float playheadX() const;

    GateCurve curve;
    int length = 16;
    float amount = 1.0f;
    double phase = -1.0;
    juce::Path line, area;
    juce::Rectangle<float> floorBox;
};

/* -------------------------------------------------------------- signal -- */

class SignalPlot final : public ni::ui::PlotWell
{
public:
    SignalPlot();

    /* The capture, the render it was gated by, the length and Amount, and
     * whether the sweep is moving (the transport runs). */
    void update (const Capture&, const GateCurve&, int length, float amount, bool moving);

    void resized() override;

protected:
    void paintPlot (juce::Graphics&) override;

private:
    void rebuild();
    void rebuildGate();
    float top() const;
    float bottom() const;

    std::uint32_t captureSerial = 0, gateSerial = 0;
    std::vector<float> data;
    int columns = 0, head = 0;
    double cycleMs = 0.0;
    GateCurve curve;
    int length = 16;
    float amount = 1.0f;
    bool moving = false;

    juce::Path dryBand, wetBand, gateUp, gateDown;
    ni::ui::plot::Axis axis;
};

/* ---------------------------------------------------------------- band -- */

class Band final : public juce::Component,
                   public ni::ui::Luminous
{
public:
    static constexpr int height = 92;

    Band();

    PatternPlot& pattern() noexcept { return patternPlot; }
    SignalPlot& signal() noexcept { return signalPlot; }
    ni::ui::Tabs& tabs() noexcept { return strip; }

    /* 0 the Pattern, 1 the Signal. */
    void show (int tab);
    int shown() const noexcept { return strip.getActive(); }

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

private:
    PatternPlot patternPlot;
    SignalPlot signalPlot;
    ni::ui::Tabs strip;
};

} // namespace ni::tg
