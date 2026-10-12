// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Side-Chain's plot and its handles. Shaper.h has the picture and the
 * handles' rules.
 */
#include "Shaper.h"

#include "InfoLines.h"
#include "Status.h"

#include "Info.h"
#include "UvTokens.h"

#include <cmath>

namespace ni::sc
{

namespace
{
namespace c = uv::tok::colour;

/* The shape's line, and the casing under it that separates it from the uv
 * audio: the web editor's 1.5px on 4px at 0.65. */
constexpr float lineWidth = 1.5f;
constexpr float casingWidth = 4.0f;
constexpr float casingAlpha = 0.65f;

/* Below this share of its own range, the kick is silence and not drawn. */
constexpr float kickFloor = 0.03f;

juce::PathStrokeType roundStroke (float width)
{
    return juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
}
} // namespace

/* ------------------------------------------------------------- Shaper -- */

Shaper::Shaper (Model& m)
    : model (m)
{
    /* The parameters the picture is drawn from: a change to any, from anywhere
     * -- a handle, a knob, the host, automation -- redraws it at once. */
    for (const int index : { param::source, param::delay, param::attack, param::hold,
                             param::release, param::depth, param::curve })
        bindings[(size_t) index] = std::make_unique<ni::ui::ParamBinding> (
            model.parameter (index), [this] { rebuildShape(); layoutHandles(); repaint(); });
    /* Time changes only how the stages read: the caption's total. */
    bindings[(size_t) param::timeMode] = std::make_unique<ni::ui::ParamBinding> (
        model.parameter (param::timeMode), [this] { refreshCaption(); });

    handles[0] = std::make_unique<Handle> (*this, Which::start, param::delay, -1);
    handles[1] = std::make_unique<Handle> (*this, Which::bottom, param::attack, param::depth);
    handles[2] = std::make_unique<Handle> (*this, Which::holdEnd, param::hold, -1);
    handles[3] = std::make_unique<Handle> (*this, Which::end, param::release, -1);

    ni::ui::setInfo (*this, info::plot);
    ni::ui::setInfo (*handles[0], info::handleStart);
    ni::ui::setInfo (*handles[1], info::handleBottom);
    ni::ui::setInfo (*handles[2], info::handleHoldEnd);
    ni::ui::setInfo (*handles[3], info::handleEnd);

    for (auto& h : handles)
        addAndMakeVisible (*h);

    enableLevelRange();
    refreshCaption();
}

Shaper::~Shaper() = default;

void Shaper::setStageText (StageText text, StageParse parse)
{
    textOfStage = std::move (text);
    parseForStage = std::move (parse);
}

void Shaper::refreshCaption()
{
    const auto total = readsInMs (binding (param::timeMode).value(), spanMs) ? std::optional<double> (stagesMs)
                                                                            : std::nullopt;
    setCaption (captionFor (! quiet, spanMs, total, kickClause (kickView)));
}

ni::ui::ParamBinding& Shaper::binding (int index)
{
    jassert (bindings[(size_t) index] != nullptr);
    return *bindings[(size_t) index];
}

Shaper::Handle& Shaper::handle (Which which)
{
    return *handles[(size_t) which];
}

float Shaper::top() const { return ni::ui::plot::captionH + 4.0f; }
float Shaper::bottom() const { return (float) getHeight() - ni::ui::plot::inset - axisH; }
float Shaper::mid() const { return (top() + bottom()) * 0.5f; }
float Shaper::half() const { return (bottom() - top()) * 0.5f; }
float Shaper::plotWidth() const { return (float) getWidth() - 2.0f * ni::ui::plot::inset; }

float Shaper::xOf (double percent) const
{
    return ni::ui::plot::inset + plotWidth() * (float) (juce::jlimit (0.0, 100.0, percent) / 100.0);
}

float Shaper::yOf (double gain) const
{
    return mid() - half() * (float) juce::jlimit (0.0, 1.0, gain);
}

juce::Point<float> Shaper::handlePoint (Which which) const
{
    switch (which)
    {
        case Which::start:   return { xOf (marks.start), yOf (1.0) };
        case Which::bottom:  return { xOf (marks.bottom), yOf (marks.floor) };
        case Which::holdEnd: return { xOf (marks.holdEnd), yOf (marks.floor) };
        case Which::end:     return { xOf (marks.end), yOf (1.0) };
    }
    return {};
}

void Shaper::update (double nowMs)
{
    const auto state = model.state();
    sweep = playheadOf (state);
    spanMs = state.msPerCycle;
    stagesMs = model.stageMs().total();

    /* The range first, so the audio is built on the one it will be drawn on. */
    const auto& scope = model.scope();
    const auto seen = [&scope] (int column) { return scope.isSeen (column); };
    followLevel (ni::ui::plot::peak ({ scope.data.data(), Scope::stride, scope.count },
                                     { Scope::dryLo, Scope::dryHi, Scope::wetLo, Scope::wetHi }, seen),
                 nowMs);
    /* The kick on its own range: it says when, not how loud. */
    kickView = model.kick();
    const auto kickSeen = [&scope] (int column) { return scope.isKickSeen (column); };
    kickRange.follow (ni::ui::plot::peak ({ scope.data.data(), Scope::stride, scope.count },
                                          { Scope::kickLo, Scope::kickHi }, kickSeen),
                      nowMs);
    rebuildEnvelope (scope);
    rebuildShape();
    refreshCaption();
    layoutHandles();
    repaint();
}

void Shaper::resized()
{
    rebuildShape();
    rebuildEnvelope (model.scope());
    layoutHandles();
}

void Shaper::levelChanged()
{
    rebuildEnvelope (model.scope());
}

void Shaper::layoutHandles()
{
    for (auto& h : handles)
        h->setBounds (juce::Rectangle<int> (Handle::size, Handle::size)
                          .withCentre (handlePoint (h->which()).roundToInt()));
}

/*
 * WHAT WAS ASKED FOR: the engine's gain at one phase per pixel, as TWO open
 * subpaths mirrored about the centre -- not a closed region, which would draw
 * verticals at both ends where the gain is 1 and the edges a well apart.
 */
void Shaper::rebuildShape()
{
    marks = model.shapeMarks();
    ruler.set ((float) getWidth(), spanMs, landmarkMs (marks, spanMs));

    intended.clear();
    const int n = std::max (2, juce::roundToInt (plotWidth()));
    gains.resize ((size_t) n + 1);
    model.shapeGain (gains.data(), n + 1);

    for (int mirror = 0; mirror < 2; ++mirror)
        for (int i = 0; i <= n; ++i)
        {
            const float x = ni::ui::plot::inset + plotWidth() * (float) i / (float) n;
            const float up = yOf (gains[(size_t) i]);
            const juce::Point<float> p { x, mirror == 0 ? up : 2.0f * mid() - up };
            if (i == 0)
                intended.startNewSubPath (p);
            else
                intended.lineTo (p);
        }
}

/*
 * WHAT HAPPENED: the input and output as min/max bands on the level range,
 * and the measured gain, on its own scale, as an outline -- the DEEPEST duck
 * behind each pixel, the capture's own minimum rule, because averaging would
 * report a duck nobody heard. A column the sweep has not reached is a gap, so
 * a picture still filling reads as unfinished.
 */
void Shaper::rebuildEnvelope (const Scope& scope)
{
    quiet = ! hasInput (scope);

    const ni::ui::plot::Capture capture { scope.data.data(), Scope::stride, scope.count };
    const ni::ui::plot::BandGeometry geometry { ni::ui::plot::inset, std::max (1, juce::roundToInt (plotWidth())),
                                                top(), bottom(), levelRange().fullScale() };
    const auto seen = [&scope] (int column) { return scope.isSeen (column); };
    ni::ui::plot::band (dryBand, capture, Scope::dryLo, Scope::dryHi, geometry, seen);
    ni::ui::plot::band (wetBand, capture, Scope::wetLo, Scope::wetHi, geometry, seen);

    /* The kick, while one is chosen and drawn: a bus at another rate files
     * nothing, and a stale picture of a kick no longer chosen is none. */
    kickBand.clear();
    if (kickView.status == KickStatus::aligned || kickView.status == KickStatus::byArrival
        || kickView.status == KickStatus::silent)
    {
        auto onRange = geometry;
        onRange.fullScale = kickRange.fullScale();
        /* Where the kick has died away it is not drawn at all: a hairline of
         * amber along the zero line, across the cycle, would be a mark that
         * says nothing -- and amber marks are for saying something. */
        const float floor = kickFloor * onRange.fullScale;
        const auto kickSeen = [&scope, floor] (int column)
        {
            return scope.isKickSeen (column)
                && std::max (std::abs (scope.at (column, Scope::kickLo)), std::abs (scope.at (column, Scope::kickHi)))
                       >= floor;
        };
        ni::ui::plot::band (kickBand, capture, Scope::kickLo, Scope::kickHi, onRange, kickSeen);
    }

    envelope.clear();
    const int n = scope.count;
    if (n < 2)
        return;

    /* The lowest gain behind each pixel; above 1 where no column was seen. */
    const int width = geometry.width;
    lowest.resize ((size_t) width);
    for (int px = 0; px < width; ++px)
    {
        const int a = (px * n) / width;
        const int b = std::max (a + 1, ((px + 1) * n) / width);
        float low = 2.0f;
        for (int i = a; i < b && i < n; ++i)
            if (scope.isSeen (i))
                low = std::min (low, scope.at (i, Scope::gain));
        lowest[(size_t) px] = low;
    }

    /* Each run of measured pixels as its upper and lower edges; a run of one
     * pixel is not drawn. */
    int runStart = -1;
    for (int px = 0; px <= width; ++px)
    {
        const bool has = px < width && lowest[(size_t) px] <= 1.0f;
        if (has && runStart < 0)
            runStart = px;
        if (! has && runStart >= 0)
        {
            if (px - runStart > 1)
                for (int mirror = 0; mirror < 2; ++mirror)
                    for (int q = runStart; q < px; ++q)
                    {
                        const float up = yOf (lowest[(size_t) q]);
                        const juce::Point<float> p { ni::ui::plot::inset + (float) q,
                                                     mirror == 0 ? up : 2.0f * mid() - up };
                        if (q == runStart)
                            envelope.startNewSubPath (p);
                        else
                            envelope.lineTo (p);
                    }
            runStart = -1;
        }
    }
}

void Shaper::paintPlot (juce::Graphics& g)
{
    namespace plot = ni::ui::plot;
    const float w = (float) getWidth();

    /* The zero line, so a silent stretch reads as silence rather than as a
     * gap in the drawing. */
    g.setColour (c::line100);
    g.drawLine (plot::inset, mid(), w - plot::inset, mid(), uv::tok::stroke::strokeHair);

    /* 0. What it is timed against; 1. what arrived; 2. what left; 3. the
     * ceiling it was allowed. The kick's edge goes over the grey and under
     * the violet: through the input, behind what the plugin did. */
    plot::key (g, kickBand);
    plot::dry (g, dryBand);
    plot::keyEdge (g, kickBand);
    plot::wet (g, wetBand);
    plot::ghost (g, envelope);

    /* 4. What was asked for, on its casing. */
    g.setColour (c::bg000.withAlpha (casingAlpha));
    g.strokePath (intended, roundStroke (casingWidth));
    g.setColour (c::ink);
    g.strokePath (intended, roundStroke (lineWidth));

    /* The playhead: a position mark, so the card's line-200 rule. */
    plot::rule (g, xOf (sweep * 100.0), top(), bottom());

    /* A shape that cannot finish inside one cycle: the window's one amber
     * mark, at the edge it runs past. */
    if (overruns())
        plot::rule (g, w - plot::inset, top(), bottom(), true);

    ruler.paint (g, (float) getHeight() - plot::inset - axisH);
}

/* ------------------------------------------------------------- Handle -- */

Shaper::Handle::Handle (Shaper& s, Which w, int xParam, int yParam)
    : PlotHandle (
          s, s.binding (xParam), [&s] (float plain, float px) { return plain + px / s.plotWidth() * 100.0f; },
          yParam >= 0 ? &s.binding (yParam) : nullptr,
          [&s] (float plain, float px) { return plain + px / s.half() * 100.0f; }),
      shaper (s), kind (w), xIndex (xParam), yIndex (yParam)
{
    switch (w)
    {
        case Which::start:   setTitle ("Delay"); break;
        case Which::bottom:  setTitle ("Attack and depth"); break;
        case Which::holdEnd: setTitle ("Hold"); break;
        case Which::end:     setTitle ("Release"); break;
    }
}

juce::String Shaper::Handle::valueText() const
{
    if (! shaper.textOfStage)
        return PlotHandle::valueText();
    auto text = shaper.textOfStage (xIndex);
    if (yIndex >= 0)
        text << ", " << shaper.binding (yIndex).text();
    return text;
}

void Shaper::Handle::typeValue (const juce::String& text)
{
    if (! shaper.parseForStage)
        PlotHandle::typeValue (text);
    else if (const auto v = shaper.parseForStage (xIndex, text.trim()))
        shaper.binding (xIndex).commit (*v);
}

} // namespace ni::sc
