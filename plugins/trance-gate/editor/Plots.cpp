// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's plots. Plots.h has what each one draws and in what.
 */
#include "Plots.h"

#include "InfoLines.h"

#include "ChildLights.h"
#include "Info.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>
#include <cmath>

namespace ni::tg
{

namespace
{
namespace c = uv::tok::colour;
namespace plot = ni::ui::plot;

/* A ruler under a plot: its band, from the content's bottom. */
constexpr float axisH = 14.0f;
/* The envelope's letters, in the strip over the ruler. */
constexpr float letterH = 12.0f;
/* How many points the envelope is drawn with across its span. */
constexpr int envelopePoints = 240;

/* A rendered curve's level `x` steps after the gate opened, linear between
 * samples; past the end, the last sample (lib/capture.js levelAt). */
float levelAt (const std::vector<float>& values, int perStep, double x)
{
    if (values.empty() || perStep <= 0)
        return 0.0f;
    const double f = std::max (0.0, x * perStep);
    const auto i = (std::size_t) f;
    if (i + 1 >= values.size())
        return values.back();
    const auto t = (float) (f - (double) i);
    return values[i] + (values[i + 1] - values[i]) * t;
}

/*
 * A curve rising from an Amount floor: `line` along the values, `area` under
 * it down to the floor, and `floorBox` the lit band under the floor. Each
 * value is a gain 0..1; x runs from inset to the far inset.
 */
void floored (juce::Path& line, juce::Path& area, juce::Rectangle<float>& floorBox,
              const std::vector<float>& values, float width, float top, float bottom, float amount)
{
    line.clear();
    area.clear();
    floorBox = {};
    const auto n = values.size();
    if (n < 2)
        return;
    const float floor = 1.0f - juce::jlimit (0.0f, 1.0f, amount);
    const float h = (bottom - top) * (1.0f - floor);
    const float w = width - 2.0f * plot::inset;
    const auto x = [&] (std::size_t i) { return plot::inset + w * (float) i / (float) (n - 1); };
    const auto y = [&] (float g) { return top + h * (1.0f - juce::jlimit (0.0f, 1.0f, g)); };

    line.preallocateSpace ((int) n * 3);
    line.startNewSubPath (x (0), y (values[0]));
    for (std::size_t i = 1; i < n; ++i)
        line.lineTo (x (i), y (values[i]));
    area = line;
    area.lineTo (x (n - 1), top + h);
    area.lineTo (x (0), top + h);
    area.closeSubPath();
    if (floor > 0.0f)
        floorBox = { plot::inset, top + h, w, juce::jmax (0.0f, bottom - (top + h)) };
}

void hintText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> box,
               juce::Justification j = juce::Justification::centredLeft)
{
    uv::type::draw (g, text, box, uv::type::hint(), c::inkMuted, j);
}
} // namespace

/* ------------------------------------------------------------ envelope -- */

/*
 * THE AXIS HOLDS BOTH CURVES, and they end in different places: the gated one
 * at gate + release -- the gate shuts at `gate` whatever stage is running, so
 * the release starts there -- and the envelope as dialled at attack + decay.
 * At least a step, and 4 % of air so the release lands inside the well.
 */
EnvelopePlot::Span EnvelopePlot::spanOf (const Setting& s)
{
    Span out;
    if (! (s.msPerStep > 0.0))
        return out;
    const double widthMs = juce::jlimit (0.0, 1.0, s.width) * s.msPerStep;
    const double k = widthMs * 0.01;
    const double a = std::max (0.0, s.attack) * k, d = std::max (0.0, s.decay) * k,
                 r = std::max (0.0, s.release) * k;
    out.gate = widthMs;
    out.attackEnd = a;
    out.decayEnd = a + d;
    out.releaseEnd = widthMs + r;
    out.span = std::max ({ widthMs + r, a + d, s.msPerStep, 2.0 }) * 1.04;
    out.truncated = widthMs + r > s.msPerStep + 1.0e-6;
    out.early = widthMs < a + d - 1.0e-6;
    return out;
}

EnvelopePlot::EnvelopePlot()
{
    ni::ui::setInfo (*this, info::envelopePlot);
}

void EnvelopePlot::update (const EnvelopeCurves& e, const Setting& s)
{
    if (drawn && e.serial == curves.serial && s == setting)
        return;
    curves = e;
    setting = s;
    rebuild();
}

juce::String EnvelopePlot::letters() const
{
    juce::String out;
    for (const auto& l : letterMarks)
        out << l.text;
    return out;
}

float EnvelopePlot::xOfMs (double ms) const
{
    if (! (shape.span > 0.0))
        return plot::inset;
    return xAt ((float) (ms / shape.span));
}

void EnvelopePlot::resized()
{
    rebuild();
}

void EnvelopePlot::rebuild()
{
    drawn = true;
    shape = spanOf (setting);
    line.clear();
    area.clear();
    ghostLine.clear();
    floorBox = {};
    letterMarks.clear();

    if (! (shape.span > 0.0))
    {
        setCaption ({});
        repaint();
        return;
    }
    setCaption ("Envelope " + juce::String (juce::roundToInt (shape.span)) + " ms   step "
                + juce::String (setting.msPerStep, 1) + " ms");
    axis.set ((float) getWidth(), shape.span, setting.msPerStep);

    const float top = plot::captionH;
    const float bottom = (float) getHeight() - plot::inset - letterH - axisH;

    /* Both curves sampled across the span: the gated one as it sounds, the
     * ghost as dialled, each from the engine's render. */
    if (curves.perStep > 0 && ! curves.gated.empty())
    {
        std::vector<float> gated ((std::size_t) envelopePoints), ghost ((std::size_t) envelopePoints);
        for (int i = 0; i < envelopePoints; ++i)
        {
            const double ms = shape.span * i / (envelopePoints - 1);
            gated[(std::size_t) i] = levelAt (curves.gated, curves.perStep, ms / setting.msPerStep);
            ghost[(std::size_t) i] = levelAt (curves.ghost, curves.perStep, ms / setting.msPerStep);
        }
        floored (line, area, floorBox, gated, (float) getWidth(), top, bottom, (float) setting.amount);
        if (shape.early)
        {
            juce::Path unusedArea;
            juce::Rectangle<float> unusedFloor;
            floored (ghostLine, unusedArea, unusedFloor, ghost, (float) getWidth(), top, bottom,
                     (float) setting.amount);
        }
    }

    /*
     * THE LETTERS LABEL WHAT RAN, NOT WHAT WAS DIALLED: each stage is
     * clamped to the gate, which shuts whatever stage is running, so a stage
     * the gate cut short collapses and is left out, as is one narrower than
     * its letter.
     */
    const auto clamp = [this] (double ms) { return std::min (ms, shape.gate); };
    const struct
    {
        const char* text;
        double from, to;
    } stages[] {
        { "A", 0.0, clamp (shape.attackEnd) },
        { "D", clamp (shape.attackEnd), clamp (shape.decayEnd) },
        { "S", clamp (shape.decayEnd), shape.gate },
        { "R", shape.gate, shape.releaseEnd },
    };
    for (const auto& s : stages)
    {
        const float x0 = xOfMs (s.from), x1 = xOfMs (s.to);
        if (s.to > s.from && x1 - x0 >= letterMin)
            letterMarks.push_back ({ s.text, x0, x1 });
    }
    repaint();
}

void EnvelopePlot::paintPlot (juce::Graphics& g)
{
    if (! (shape.span > 0.0))
        return;
    const float top = plot::captionH;
    const float bottom = (float) getHeight() - plot::inset - letterH - axisH;

    /* Where the gate really closes, when it closes before the step's end. */
    if (setting.width < 1.0)
        plot::rule (g, xOfMs (shape.gate), top, bottom);

    if (! ghostLine.isEmpty())
        plot::ghost (g, ghostLine);
    if (! floorBox.isEmpty())
    {
        juce::Path floorArea;
        floorArea.addRectangle (floorBox);
        plot::under (g, floorArea);
    }
    plot::under (g, area);
    plot::curve (g, line);

    for (const auto& l : letterMarks)
        hintText (g, l.text, { l.x0, bottom, l.x1 - l.x0, letterH }, juce::Justification::centred);

    /* The step's edge, amber when the envelope runs past it. */
    plot::rule (g, xOfMs (setting.msPerStep), top, bottom, shape.truncated);
    axis.paint (g, (float) getHeight() - plot::inset - axisH);
}

/* --------------------------------------------------------------- marks -- */

void StepMarks::rules (juce::Graphics& g, int count, float width, float top, float bottom)
{
    if (count < 1 || width <= 2.0f * plot::inset)
        return;
    const float w = (width - 2.0f * plot::inset) / (float) count;
    const bool every = w >= ruleMin;
    for (int i = 1; i < count; ++i)
    {
        const bool bar = i % 16 == 0, beat = i % 4 == 0;
        /* Bars and beats at any density: they keep a long pattern readable. */
        if (! every && ! bar && ! beat)
            continue;
        g.setColour (bar ? c::line200 : c::line100);
        const float x = plot::inset + w * (float) i;
        g.drawLine (x, top, x, bottom, uv::tok::stroke::strokeHair);
    }
}

void StepMarks::numbers (juce::Graphics& g, int count, float width, float top)
{
    if (count < 1 || width <= 2.0f * plot::inset)
        return;
    const float w = (width - 2.0f * plot::inset) / (float) count;
    if (w < numberMin)
        return;
    for (int i = 0; i < count; ++i)
        hintText (g, juce::String (i + 1), { plot::inset + w * (float) i + 2.0f, top, w, uv::tok::type::hint.lineHeight });
}

/* ------------------------------------------------------------- pattern -- */

PatternPlot::PatternPlot()
{
    setCaption ("Pattern   one cycle");
    ni::ui::setInfo (*this, info::patternPlot);
}

void PatternPlot::update (const GateCurve& g, int steps, float level)
{
    steps = juce::jlimit (1, maxSteps, steps);
    if (built && g.serial == curve.serial && steps == length && juce::exactlyEqual (level, amount))
        return;
    built = true;
    curve = g;
    length = steps;
    amount = level;
    rebuild();
}

float PatternPlot::playheadX() const
{
    return xAt ((float) playStep / (float) length);
}

void PatternPlot::setPlayhead (int step)
{
    step = step >= 0 && step < length ? step : -1;
    if (step == playStep)
        return;
    const auto strip = [this]
    {
        return playStep < 0 ? juce::Rectangle<int>() : juce::Rectangle<int> (juce::roundToInt (playheadX()) - 2, 0, 5, getHeight());
    };
    repaint (strip());
    playStep = step;
    repaint (strip());
}

void PatternPlot::resized()
{
    rebuild();
}

void PatternPlot::rebuild()
{
    floored (line, area, floorBox, curve.values, (float) getWidth(), plot::captionH,
             (float) getHeight() - plot::inset, amount);
    repaint();
}

void PatternPlot::paintPlot (juce::Graphics& g)
{
    const float top = plot::captionH, bottom = (float) getHeight() - plot::inset;
    /* Rules under the curve, so nothing is hidden by a rule. */
    StepMarks::rules (g, length, (float) getWidth(), top, bottom);
    if (! floorBox.isEmpty())
    {
        juce::Path floorArea;
        floorArea.addRectangle (floorBox);
        plot::under (g, floorArea);
    }
    plot::under (g, area);
    plot::curve (g, line);
    /* The playhead in hard steps, as the ring and the pads have it. */
    if (playStep >= 0)
        plot::rule (g, playheadX(), top, bottom);
    /* Numbers last, so a number is never swallowed by what it labels. */
    StepMarks::numbers (g, length, (float) getWidth(), top);
}

/* -------------------------------------------------------------- signal -- */

SignalPlot::SignalPlot()
{
    ni::ui::setInfo (*this, info::signalPlot);
    enableLevelRange ((float) ni::ui::Tabs::width);
}

float SignalPlot::top() const { return plot::captionH; }
float SignalPlot::bottom() const { return (float) getHeight() - plot::inset - axisH; }

void SignalPlot::update (const Capture& cap, const GateCurve& g, int steps, float level, bool isMoving,
                         double nowMs)
{
    steps = juce::jlimit (1, maxSteps, steps);
    const bool gateChanged = g.serial != gateSerial || steps != length || ! juce::exactlyEqual (level, amount)
                          || curve.values.size() != g.values.size();
    const bool captureChanged = cap.serial != captureSerial || cap.columns != columns || cap.head != head
                             || ! juce::exactlyEqual (cap.cycleMs, cycleMs) || data.empty() != cap.data.empty();
    const bool movingChanged = isMoving != moving;

    /* Every frame, changed or not: a range gliding in goes on gliding while
     * the transport is stopped and the capture holds still. */
    if (captureChanged)
    {
        const int usable = std::min (cap.columns, (int) (cap.data.size() / (std::size_t) Capture::stride));
        captured = plot::peak ({ cap.data.data(), Capture::stride, usable }, { 0, 1, 2, 3 });
    }
    const bool rangeMoved = followLevel (captured, nowMs);
    if (! gateChanged && ! captureChanged && ! movingChanged && ! rangeMoved)
        return;

    length = steps;
    amount = level;
    moving = isMoving;
    if (gateChanged)
    {
        curve = g;
        gateSerial = g.serial;
        rebuildGate();
    }
    if (captureChanged)
    {
        captureSerial = cap.serial;
        columns = cap.columns;
        head = cap.head;
        cycleMs = cap.cycleMs;
        data.assign (cap.data.begin(), cap.data.end());
    }
    if (captureChanged || rangeMoved)
        rebuild();
    repaint();
}

void SignalPlot::levelChanged()
{
    rebuild();
}

void SignalPlot::resized()
{
    rebuildGate();
    rebuild();
}

void SignalPlot::rebuild()
{
    /* Before the first capture there is no cycle yet: the caption and the
     * ruler both read the web editor's first window, a second. */
    const double ms = cycleMs > 0.0 ? cycleMs : 1000.0;
    setCaption ("Signal   one cycle, " + juce::String (juce::roundToInt (ms)) + " ms   dry in grey, gated in front");
    axis.set ((float) getWidth(), ms, 0.0);

    const int usable = std::min (columns, (int) (data.size() / (std::size_t) Capture::stride));
    const plot::Capture capture { data.data(), Capture::stride, usable };
    const plot::BandGeometry geometry { plot::inset,
                                        std::max (1, juce::roundToInt ((float) getWidth() - 2.0f * plot::inset)),
                                        top(), bottom(), levelRange().fullScale() };
    plot::band (dryBand, capture, 0, 1, geometry);
    plot::band (wetBand, capture, 2, 3, geometry);
}

/*
 * THE GATE OVER THE TRACE, as the outline it cuts: the render mirrored about
 * the zero line, which is the outline a symmetrical signal at full level
 * would fill. The gap between it and the trace is what the audio did.
 */
void SignalPlot::rebuildGate()
{
    gateUp.clear();
    gateDown.clear();
    const auto n = curve.values.size();
    if (n < 2)
        return;
    const float floor = 1.0f - juce::jlimit (0.0f, 1.0f, amount);
    const float mid = (top() + bottom()) * 0.5f, half = (bottom() - top()) * 0.5f;
    for (std::size_t i = 0; i < n; ++i)
    {
        const float v = floor + (1.0f - floor) * juce::jlimit (0.0f, 1.0f, curve.values[i]);
        const float x = xAt ((float) i / (float) n);
        if (i == 0)
        {
            gateUp.startNewSubPath (x, mid - half * v);
            gateDown.startNewSubPath (x, mid + half * v);
        }
        else
        {
            gateUp.lineTo (x, mid - half * v);
            gateDown.lineTo (x, mid + half * v);
        }
    }
}

void SignalPlot::paintPlot (juce::Graphics& g)
{
    const float t = top(), b = bottom(), mid = (t + b) * 0.5f;
    StepMarks::rules (g, length, (float) getWidth(), t, b);
    /* The zero line, so silence reads as silence and not as a gap. */
    g.setColour (c::line100);
    g.drawLine (plot::inset, mid, (float) getWidth() - plot::inset, mid, uv::tok::stroke::strokeHair);
    plot::dry (g, dryBand);
    plot::wet (g, wetBand);
    plot::ghost (g, gateUp);
    plot::ghost (g, gateDown);
    /* Where the sweep is writing: the picture is current up to here. */
    if (moving && columns > 1)
        plot::rule (g, xAt ((float) head / (float) columns), t, b);
    axis.paint (g, b);
    StepMarks::numbers (g, length, (float) getWidth(), t);
}

/* ---------------------------------------------------------------- band -- */

Band::Band()
{
    setTitle ("Pattern and Signal");
    addAndMakeVisible (patternPlot);
    addChildComponent (signalPlot);
    strip.setTabs ({ "Pattern", "Signal" }, { info::patternTab.str(), info::signalTab.str() });
    strip.onSelect = [this] (int i) { show (i); };
    addAndMakeVisible (strip);
}

void Band::show (int tab)
{
    tab = juce::jlimit (0, 1, tab);
    strip.setActive (tab);
    patternPlot.setVisible (tab == 0);
    signalPlot.setVisible (tab == 1);
}

void Band::resized()
{
    const auto b = getLocalBounds();
    patternPlot.setBounds (b);
    signalPlot.setBounds (b);
    strip.setBounds (b.withLeft (b.getRight() - ni::ui::Tabs::width));
}

void Band::paint (juce::Graphics& g)
{
    ni::ui::paintChildLights (g, *this);
}

void Band::paintLight (juce::Graphics& g)
{
    ni::ui::forwardChildLights (g, *this);
}

} // namespace ni::tg
