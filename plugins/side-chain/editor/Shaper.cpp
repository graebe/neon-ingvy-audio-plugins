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
#include "Keys.h"
#include "UvLight.h"
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
    setCaption (captionFor (! quiet, spanMs, total));
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

void Shaper::update()
{
    const auto state = model.state();
    sweep = playheadOf (state);
    spanMs = state.msPerCycle;
    stagesMs = model.stageMs().total();

    rebuildEnvelope();
    rebuildShape();
    refreshCaption();
    layoutHandles();
    repaint();
}

void Shaper::resized()
{
    rebuildShape();
    rebuildEnvelope();
    layoutHandles();
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
 * WHAT HAPPENED: the input and output as min/max bands, and the measured gain
 * as an outline -- the DEEPEST duck behind each pixel, the capture's own
 * minimum rule, because averaging would report a duck nobody heard. A column
 * the sweep has not reached is a gap, so a picture still filling reads as
 * unfinished.
 */
void Shaper::rebuildEnvelope()
{
    const auto& scope = model.scope();
    quiet = ! hasInput (scope);

    const ni::ui::plot::Capture capture { scope.data.data(), Scope::stride, scope.count };
    const ni::ui::plot::BandGeometry geometry { ni::ui::plot::inset, std::max (1, juce::roundToInt (plotWidth())),
                                                top(), bottom() };
    const auto seen = [&scope] (int column) { return scope.isSeen (column); };
    ni::ui::plot::band (dryBand, capture, Scope::dryLo, Scope::dryHi, geometry, seen);
    ni::ui::plot::band (wetBand, capture, Scope::wetLo, Scope::wetHi, geometry, seen);

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

    /* 1. What arrived; 2. what left; 3. the ceiling it was allowed. */
    plot::dry (g, dryBand);
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
    : shaper (s), kind (w), xIndex (xParam), yIndex (yParam)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    switch (w)
    {
        case Which::start:   setTitle ("Delay"); break;
        case Which::bottom:  setTitle ("Attack and depth"); break;
        case Which::holdEnd: setTitle ("Hold"); break;
        case Which::end:     setTitle ("Release"); break;
    }
    setSize (size, size);
}

juce::String Shaper::Handle::valueText() const
{
    auto text = shaper.textOfStage ? shaper.textOfStage (xIndex) : shaper.binding (xIndex).text();
    if (yIndex >= 0)
        text << ", " << shaper.binding (yIndex).text();
    return text;
}

void Shaper::Handle::typeValue (const juce::String& text)
{
    auto& x = shaper.binding (xIndex);
    if (! shaper.parseForStage)
        x.setText (text);
    else if (const auto v = shaper.parseForStage (xIndex, text.trim()))
        x.commit (*v);
}

bool Shaper::Handle::hitTest (int x, int y)
{
    return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= targetR;
}

void Shaper::Handle::paint (juce::Graphics& g)
{
    const auto dot = juce::Rectangle<float> (2.0f * handleR, 2.0f * handleR)
                         .withCentre (getLocalBounds().toFloat().getCentre());
    if (focus.isVisible())
        uv::light::glowFocus (g, dot, handleR);

    juce::Path disc;
    disc.addEllipse (dot);
    if (isLit())
        uv::light::glowArc (g, disc);
    g.setColour (isLit() ? c::uv : c::ink);
    g.fillPath (disc);
    g.setColour (c::bg000);
    g.strokePath (disc, juce::PathStrokeType (1.5f));
}

void Shaper::Handle::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void Shaper::Handle::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

/*
 * A DRAG, AND EVERY RULE IN IT IS A FIX FOR A REAL FAULT (Shaper.jsx):
 *
 *  - The sensitivity is chosen AT PRESS and never re-read: the delta is from
 *    the press, so changing the divisor half way would make the value jump.
 *  - Positions are the WELL's, not the handle's: the handle moves under the
 *    pointer as it drags, and the well does not.
 *  - THE GESTURE OPENS ONCE THE DEAD ZONE IS LEFT, not at the press: a press
 *    that moves nothing -- a click, either half of a double-click -- is no edit.
 *  - Both parameters of the bottom corner open and close together, so the
 *    host sees one gesture and undo is one step.
 *  - A HANDLE MOVES ITS OWN STAGE, NOT THE ONES AFTER IT: each boundary is the
 *    sum of the stages before it, so `end` changes Release alone.
 */
void Shaper::Handle::mouseDown (const juce::MouseEvent& e)
{
    focus.pointerUsed();
    if (isShowing())
        grabKeyboardFocus();

    const auto& x = shaper.binding (xIndex);
    Drag d;
    d.from = e.getEventRelativeTo (&shaper).position;
    d.plainX = x.parameter().convertFrom0to1 (x.value());
    if (yIndex >= 0)
    {
        const auto& y = shaper.binding (yIndex);
        d.plainY = y.parameter().convertFrom0to1 (y.value());
    }
    d.fine = e.mods.isShiftDown();
    drag = d;
    repaint();
}

void Shaper::Handle::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag.has_value())
        return;

    const auto at = e.getEventRelativeTo (&shaper).position;
    const auto moved = at - drag->from;
    if (! drag->moved)
    {
        if (std::abs (moved.x) < deadZone && std::abs (moved.y) < deadZone)
            return;
        drag->moved = true;
        shaper.binding (xIndex).begin();
        if (yIndex >= 0)
            shaper.binding (yIndex).begin();
    }

    const float fine = drag->fine ? fineRatio : 1.0f;

    /* Sideways: the cycle's width is 100 % of it, the unit the stages are in. */
    auto& x = shaper.binding (xIndex);
    const float percent = moved.x / shaper.plotWidth() * 100.0f / fine;
    x.input (x.parameter().convertTo0to1 (drag->plainX + percent));

    /* Up and down: against the HALF height, because the handle sits on the
     * upper edge of a mirrored envelope -- a pixel of travel is a pixel of
     * gain there. Down is deeper. */
    if (yIndex >= 0)
    {
        auto& y = shaper.binding (yIndex);
        const float depth = moved.y / shaper.half() * 100.0f / fine;
        y.input (y.parameter().convertTo0to1 (drag->plainY + depth));
    }
}

void Shaper::Handle::mouseUp (const juce::MouseEvent&)
{
    const bool moved = drag.has_value() && drag->moved;
    drag.reset();
    if (! moved)
        return;
    shaper.binding (xIndex).end();
    if (yIndex >= 0)
        shaper.binding (yIndex).end();
}

/* Back to the plugin's own default, for each parameter the handle moves. */
void Shaper::Handle::mouseDoubleClick (const juce::MouseEvent&)
{
    shaper.binding (xIndex).reset();
    if (yIndex >= 0)
        shaper.binding (yIndex).reset();
}

void Shaper::Handle::commitKey (int index, float delta, std::optional<float> to)
{
    auto& b = shaper.binding (index);
    b.commit (to.has_value() ? *to : juce::jlimit (0.0f, 1.0f, b.value() + delta));
}

/*
 * THE KEYBOARD: each handle is a slider on the parameter it drags. Left and
 * Right move it along the cycle, Page by a tenth, Home and End to the ends of
 * its range; on the bottom corner Up and Down move Depth, Up shallower as
 * dragging up is. Each key is one committed edit.
 */
bool Shaper::Handle::keyPressed (const juce::KeyPress& k)
{
    namespace keys = ni::ui::keys;
    const auto key = keys::keyOf (k);
    const bool vertical = key == keys::Key::up || key == keys::Key::down;

    const auto action = keys::sliderKey (k, vertical && yIndex >= 0 ? keys::Axis::y : keys::Axis::x);
    if (action.kind == keys::SliderAction::Kind::none)
        return false;

    focus.keyUsed();
    if (vertical)
        commitKey (yIndex, -action.amount, std::nullopt);
    else if (action.kind == keys::SliderAction::Kind::to)
        commitKey (xIndex, 0.0f, action.amount);
    else
        commitKey (xIndex, action.amount, std::nullopt);
    repaint();
    return true;
}

void Shaper::Handle::focusGained (FocusChangeType cause)
{
    focus.focusGained (cause);
    repaint();
}

void Shaper::Handle::focusLost (FocusChangeType)
{
    focus.focusLost();
    repaint();
}

namespace
{
/* A slider to a screen reader: the stage's value to move, the parameters'
 * text to read. */
class HandleValue final : public juce::AccessibilityValueInterface
{
public:
    HandleValue (Shaper::Handle& h, ni::ui::ParamBinding& b) : handle (h), x (b) {}

    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return x.value(); }
    juce::String getCurrentValueAsString() const override { return handle.valueText(); }
    void setValue (double v) override { x.commit ((float) juce::jlimit (0.0, 1.0, v)); }
    void setValueAsString (const juce::String& text) override { handle.typeValue (text); }
    AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, (double) ni::ui::keys::step }; }

private:
    Shaper::Handle& handle;
    ni::ui::ParamBinding& x;
};
} // namespace

std::unique_ptr<juce::AccessibilityHandler> Shaper::Handle::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<HandleValue> (*this, shaper.binding (xIndex)) });
}

} // namespace ni::sc
